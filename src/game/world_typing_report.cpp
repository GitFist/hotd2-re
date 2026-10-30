#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/typing_profile.h"
#include "game/typing_report.h"
#include "game/typing_score_ui.h"
#include "game/world_nightshift.h"
#include "game/world_internal.h"

namespace game {
namespace {
using Piece=std::array<uint32_t,9>;
#include "game/typing_report_layout.inc"
float F(uint32_t bits) { return std::bit_cast<float>(bits); }
struct ReportTask : WTask {
    TypingFinalEvaluation evaluation;
    int phase=0, age=0, wait=0;
    int entry=0, nameState=0, entryFrames=0;
    std::string name, text;
    std::unique_ptr<TypingScoreUi> scoreUi;
};
void IdleReport(Task&) {}
void BookLight(World::Impl& world,WorldDraw& draw,float brightness) {
    // Source4C1A80/4C1BB0's white headlight acts in model-view space. The
    // port draws view-space models through inverse(camera), so rotate that
    // direction with them instead of inheriting the last stage's world light.
    draw.light.table=false; draw.light.intensity=1;
    for (float& color:draw.light.color) color=brightness;
    DirToAngles(world.cam.inverse.m[8],world.cam.inverse.m[9],world.cam.inverse.m[10],
                &draw.light.pitch,&draw.light.yaw);
}
ReportTask* Report(World::Impl& world) {
    for (Task* task:world.ts.Live())
        if (task->alive) if (auto* report=dynamic_cast<ReportTask*>(task->data.get())) return report;
    return nullptr;
}
void Submit(World::Impl& world,const Piece& piece,int index,float alpha) {
    const size_t before=world.hudSprites.size();
    world.TypingSprite(0xE29,float(int32_t(piece[0])),float(int32_t(piece[1])),alpha,
                      30.200000762939453f-float(index)*.000009999999747378752f,5,0x004A19F0);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back();
    draw.sx=F(piece[6]); draw.sy=F(piece[7]); draw.atlas=true;
    draw.uv[0]=F(piece[2]); draw.uv[1]=1-F(piece[5]);
    draw.uv[2]=F(piece[4]); draw.uv[3]=1-F(piece[3]);
}
struct Layout {
    std::array<Piece,kReport.size()> pieces=kReport;
    std::array<bool,kReport.size()> visible{};
    template<size_t N> void link(int at,const std::array<Piece,N>& font,unsigned glyph) {
        if (at<0 || at>=int(pieces.size()) || glyph>=font.size()) return;
        std::copy(font[glyph].begin()+2,font[glyph].end(),pieces[at].begin()+2);
        visible[at]=true;
    }
    template<size_t N> void number(int at,int digits,uint64_t value,const std::array<Piece,N>& font,bool zeroes=false) {
        for (int i=digits-1;i>=0;--i) {
            link(at+i,font,unsigned(value%10));
            visible[at+i]=zeroes || i==digits-1 || value!=0;
            value/=10;
        }
    }
};
int Model(World::Impl& world,int raw) {
    const int model=world.typingReference.SourceModel(raw);
    if (model<=0) return 0;
    if (model>=int(world.modelLoaded.size()) || !world.modelLoaded[model]) {
        int file=world.typingReference.ModelFile(model);
        if (file<0 && model<0x4000) file=world.exe.S16(0x004E83B4+uint32_t(model)*2);
        if (file>=0) world.ProcessRequest({0x52,uint32_t(file)});
    }
    return model;
}
void Page(World::Impl& world,int raw,int angle,float brightness) {
    const int model=Model(world,raw);
    if (model<=0) return;
    Mat4 matrix=Mat4::Identity();
    mx::Translate(matrix,0,0,-17);
    mx::RotY(matrix,angle);
    if (auto* draw=world.Draw(model,matrix,8,-1,false,-1,true)) {
        draw->fn=0x00497820;
        BookLight(world,*draw,brightness);
    }
}
void Graph(World::Impl& world,const TypingFinalEvaluation& evaluation,float alpha) {
    const int model=Model(world,0x156C);
    if (model<=0) return;
    // Source4A2A00's inline vertices are mapped through5E490C/5E491C.
    // References9/12/14 share earlier vertices and are not independent edits.
    constexpr int points[] = {1,0,2,0,1,3,1,4,3,5,4,2};
    constexpr int grade[] = {4,0,5,3,1,2};
    constexpr int xy[][2] = {{5400,-7440},{0,0},{-5400,-7440},{8640,2760},{0,9120},{-8640,2760}};
    auto edit=std::make_shared<ModelEdit>();
    for (int i=0;i<12;++i) {
        const int point=points[i];
        const float span=grade[point]==0 ? 1.f : float(5-std::min(4,int(evaluation.grades[grade[point]-1])))*.2f;
        ModelEdit::Vertex vertex;
        vertex.index=i;
        vertex.pos[0]=float(xy[point][0])*span*.00009999999747378752f;
        vertex.pos[0]=std::bit_cast<float>(std::bit_cast<uint32_t>(vertex.pos[0])|1u);
        vertex.pos[1]=float(xy[point][1])*span*.00009999999747378752f;
        edit->vertices.push_back(vertex);
    }
    Mat4 matrix=Mat4::Identity();
    mx::Translate(matrix,-2.304f,.048f,-10);
    mx::Scale(matrix,alpha,alpha,alpha);
    if (auto* draw=world.Draw(model,matrix,8,alpha,false,-1,true)) {
        draw->edit=std::move(edit); draw->fn=0x004A2A00;
        BookLight(world,*draw,1);
    }
}
void Contents(World::Impl& world,const TypingFinalEvaluation& evaluation,float alpha) {
    if (alpha<=0) return;
    constexpr Piece dcMode={0x27,0x50,0x3f440000,0x3b7ffbce,0x3f697ffa,0x3d07ffde,0x3e160419,0x3cf0068e,0x10000};
    constexpr Piece acMode={0x27,0x50,0x3f440000,0x3d200086,0x3f697ffa,0x3d8bffce,0x3e160419,0x3cf0068e,0x10000};
    // Source497CC5 and497D3B: mode caption and AGENT1 RESULT REPORT heading.
    Submit(world,evaluation.originalMode ? dcMode : acMode,0,alpha);
    world.TypingSprite(evaluation.player ? 0xAC5 : 0xAC4,106,34,alpha,.2f,5,0x00497D3B);
    Layout layout;
    for (int i=0;i<5;++i) layout.link(84+i,kGrades,std::min(4u,unsigned(evaluation.grades[i])));
    uint64_t reward=uint64_t(evaluation.skillPoints)*100;
    if (reward>=1000000) {
        layout.number(90,3,reward/1000000,kRewardDigits);
        layout.visible[93]=true;
        layout.number(94,3,(reward/1000)%1000,kRewardDigits,true);
        layout.visible[97]=true;
        layout.number(98,3,reward%1000,kRewardDigits,true);
    } else if (reward>=1000) {
        layout.number(94,3,reward/1000,kRewardDigits);
        layout.visible[97]=true;
        layout.number(98,3,reward%1000,kRewardDigits,true);
    } else layout.number(98,3,reward,kRewardDigits);
    int first=90;
    while (first<101 && !layout.visible[first]) ++first;
    layout.pieces[89][0]=uint32_t(int32_t(layout.pieces[first][0])-19);
    layout.visible[89]=layout.visible[101]=layout.visible[102]=layout.visible[105]=true;
    const unsigned packed=TypingPackedProficiency(evaluation.proficiency);
    layout.link(103,kProficiency,(packed>>4)+1);
    if (packed&15) layout.link(104,kProficiency,(packed&15)+6);
    layout.number(106,3,evaluation.proficiency,kSpeedDigits);
    for (int chapter=0;chapter<6;++chapter) {
        const auto& record=evaluation.chapters[chapter];
        if (record.status!=1) {
            const auto& piece=record.status==2 ? kGameOver[17+chapter] : kNoClear[17+chapter];
            Submit(world,piece,17+chapter,alpha);
            continue;
        }
        const int at=chapter*14;
        // Source4A2920 right-aligns the four score digits against the ordinal.
        const int startX=int32_t(layout.pieces[at+5][0]);
        for (int i=0;i<5;++i) layout.pieces[at+i][0]=uint32_t(startX+i*12);
        layout.number(at,4,std::min<unsigned>(9999,record.points),kChapterDigits);
        layout.visible[at+4]=true;
        layout.number(at+10,3,record.ordinal,kChapterDigits);
        layout.link(at+13,kChapterDigits,record.ordinal<=3 ? 12+record.ordinal : 16);
    }
    for (size_t i=0;i<layout.pieces.size();++i) if (layout.visible[i]) Submit(world,layout.pieces[i],int(i),alpha);
    const size_t before=world.hudSprites.size();
    world.TypingSprite(2,268,76,alpha,.2f,0xA,0x004A25E0);
    if (world.hudSprites.size()!=before) { world.hudSprites.back().sx=.288f; world.hudSprites.back().sy=.36f; }
    const size_t label=world.hudSprites.size();
    world.TypingSprite(0xE2A,244,40,alpha,.1f,0xA,0x004A25E0);
    if (world.hudSprites.size()!=label) world.hudSprites.back().sx=world.hudSprites.back().sy=.3f;
    Graph(world,evaluation,alpha);
}
void Draw(World::Impl& world,const ReportTask& report,float turn) {
    const float brightness=turn<.5f ? turn*2 : turn>2.5f ? (3-turn)*2 : 1;
    Page(world,0x2A5,turn<.5f ? int(turn*-65536.f) : -0x8000,std::max(0.f,brightness));
    Page(world,0x2A6,turn>2.5f ? int((3-turn)*65536.f) : 0x8000,std::max(0.f,brightness));
    if (turn<1) {
        for (int i=1;i<=5;++i) {
            const float t=turn-float(i)*.1f-.02f;
            if (t>=0 && t<=.5f) Page(world,t<.25f ? 0x156B : 0x156F,int(t*-65536.f),brightness);
        }
        Page(world,0x156B,0x8000,brightness);
    } else if (turn<2.5f) {
        Page(world,0x156F,0x8000,brightness);
        Page(world,0x156B,0x8000,brightness);
    } else {
        Page(world,0x156F,0x8000,brightness);
        for (int i=0;i<5;++i) {
            const float t=turn-2-float(i)*.1f;
            if (t>=0 && t<=.5f) Page(world,t<.25f ? 0x156B : 0x156F,int(t*-65536.f),brightness);
        }
    }
    const float alpha=turn<1 ? std::clamp((turn-.85f)*6.666666507720947f,0.f,1.f) :
                      turn>=2 ? std::clamp(1-(turn-2)*6.666666507720947f,0.f,1.f) : 1;
    Contents(world,report.evaluation,alpha);
}

bool NameCharacter(unsigned char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
           std::string_view("\\.,?!_[]-% '$#@\"&/():*").find(char(c))!=std::string_view::npos;
}
void PopupSprite(World::Impl& world,int id,float x,float y,float depth,float sx=1,float sy=1,
                 float alpha=1,uint32_t flags=5) {
    const size_t before=world.hudSprites.size();
    world.TypingSprite(id,x,y,alpha,depth,flags,0x00498250);
    if (world.hudSprites.size()!=before) { world.hudSprites.back().sx=sx; world.hudSprites.back().sy=sy; }
}
void NameGlyph(World::Impl& world,unsigned char character,int index,float alpha) {
    if (character<32 || character>127) return;
    const int slot=character-32;
    const float x=320+world.focal*(float(index)*.11599999666213989f-.2200000137090683f)/3;
    const float y=240-world.focal*.04399999976158142f/3;
    const size_t before=world.hudSprites.size();
    const float a=8000.f/(8000.f-.5f),z=a-.5f*a/3;
    world.TypingSprite(0xF74+slot,x,y,alpha,.9f/(1-z),0x200Au,0x00498578);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back();
    const float pixels=world.focal*.15f/3;
    draw.sx=pixels/512; draw.sy=pixels/256;
    draw.atlas=true; draw.tinted=true;
    draw.tint[0]=.20392157137393951f; draw.tint[1]=.19607843458652496f; draw.tint[2]=.35294118523597717f;
    draw.uv[0]=float((slot%16)*32)/512;
    draw.uv[1]=float((slot/16+1)*32)/256;
    draw.uv[2]=draw.uv[0]+32.f/512;
    draw.uv[3]=draw.uv[1]-32.f/256;
}
void DrawName(World::Impl& world,const ReportTask& report) {
    // Source498250: original modal backdrop, panel and editing/confirmation art.
    PopupSprite(world,0xE36,0,0,30.185001f,80,60,.5f);
    PopupSprite(world,0xBC6,320,240,10.185001f,1,1,1,0xA);
    PopupSprite(world,0xBC7,192,237,10.183f);
    if (report.nameState) {
        PopupSprite(world,0xBCA,192,187,10.183f);
        PopupSprite(world,0xBCD,289,242,10.183f);
        PopupSprite(world,0xBC9,288,268,10.183f);
        PopupSprite(world,0xBCC,258,report.nameState==1 ? 243.f : 270.f,10.184f);
    } else {
        PopupSprite(world,0xBC8,192,178,10.183f);
        PopupSprite(world,0xBCB,192,277,10.183f);
        PopupSprite(world,0xBBC,256,254,10.183f);
        PopupSprite(world,0xBBD,357,261,10.183f);
    }
    for (size_t i=0;i<report.name.size();++i) NameGlyph(world,static_cast<unsigned char>(report.name[i]),int(i),1);
    if (!report.nameState && report.name.size()<5) {
        const int pulse=std::abs(20-int(world.frameCounter2BAC%40));
        NameGlyph(world,'_',int(report.name.size()),.1899999976158142f+float(pulse)*.04f);
    }
}
void NameFrame(World::Impl& world,ReportTask& report) {
    const bool enter=(world.pressed&0x80008u)!=0 || report.text.find('\r')!=std::string::npos;
    const bool escape=report.text.find('\x1b')!=std::string::npos;
    if (report.nameState) {
        if (world.pressed&0x00500050u) { report.nameState=1; world.TypingSound(0x003E16A9u); }
        if (world.pressed&0x00A000A0u) { report.nameState=2; world.TypingSound(0x003E16A9u); }
        if (escape || (enter && report.nameState==2)) {
            report.nameState=0;
            world.TypingSound(0x000500A9u);
        } else if (enter) {
            world.typing.storeFinal(report.evaluation,report.name);
            report.evaluation.name={};
            std::copy(report.name.begin(),report.name.end(),report.evaluation.name.begin());
            report.entry=2; report.entryFrames=0;
            world.TypingSound(0x003016A9u);
        }
    } else {
        for (unsigned char c:report.text) {
            if (c=='\b') {
                if (!report.name.empty()) report.name.pop_back();
                world.TypingSound(0x000936A9u);
            } else if (NameCharacter(c) && report.name.size()<5) {
                report.name.push_back(char(c));
                world.TypingSound(0x000836A9u);
            }
        }
        if (enter) { report.nameState=2; world.TypingSound(0x003E16A9u); }
    }
    report.text.clear();
    DrawName(world,report);
}
}

bool TypingReportActive(World::Impl& world) { return Report(world)!=nullptr; }
bool TypingReportNameActive(World::Impl& world) {
    const auto* report=Report(world);
    return report && (report->entry==1 ||
        (report->scoreUi && report->scoreUi->phase==TypingScoreUi::Profile));
}
bool QueueTypingReportText(World::Impl& world,std::string_view text) {
    auto* report=Report(world);
    if (!report) return false;
    if (report->scoreUi) return QueueTypingScoreUiText(*report->scoreUi,text);
    if (report->entry!=1) return false;
    report->text.append(text.substr(0,std::min<size_t>(32,text.size())));
    return true;
}

void StartTypingReport(World::Impl& world,bool cleared) {
    PortLeaderboardComplete(world,13,0,std::clamp(world.cfg.eventMode,0,5));
    const auto evaluation=world.typing.finalEvaluation(unsigned(std::max(0,int(world.civTotal))),
        unsigned(std::max(0,int(world.rescueTotal))),cleared);
    world.ts.Reset(); world.resQueue.clear();
    world.targets.clear(); world.targetCopy.clear();
    world.draws.clear(); world.hudSprites.clear();
    world.hud.menuText.clear(); world.hud.menuPanels.clear();
    world.typing.syncTargets({}); world.typingCharacters.clear();
    world.typingResultsActive=true;
    world.cfg.freePlay=false; world.cfg.credits=0;
    world.letterbox=world.letterboxCounter=0;
    world.projCenter[0]=320; world.projCenter[1]=240;
    world.SoundStopAll();
    auto* report=world.NewTask<ReportTask>(IdleReport,0x0047DFC0);
    report->evaluation=evaluation;
    world.subState=10;
    TypingReportFrame(world); // Source47DFB0 enters the first presentation tick immediately.
}

void TypingReportFrame(World::Impl& world) {
    auto* report=Report(world);
    if (!report) return;
    if (report->scoreUi) {
        TypingScoreUiFrame(world,*report->scoreUi);
        report->fn=report->scoreUi->phase==TypingScoreUi::Profile ? 0x004888F0 : 0x004A14C0;
        if (report->scoreUi->phase==TypingScoreUi::Done) {
            world.typingResultsActive=false;
            if (world.frontEnd) world.front.modeRequest=3;
            else world.SetGameMode(3);
        }
        return;
    }
    if (report->entry) {
        Draw(world,*report,1);
        if (report->entry==1) NameFrame(world,*report);
        else if (++report->entryFrames==3) report->entry=0;
        return;
    }
    float turn=1;
    if (report->wait>0) {
        --report->wait;
        if (world.pressed&0xE000Eu) report->wait=0;
        if (report->wait==0 && world.cfg.gameType==0 && world.typing.finalRank(report->evaluation)>=0) {
            report->entry=1;
            report->name.clear(); report->text.clear(); report->nameState=0;
        }
    } else {
        if (report->age==0) world.TypingSound(report->phase==0 ? 0x000036A9u : 0x000236A9u);
        turn=float(report->phase)+float(report->age)*.01666666753590107f;
        if (++report->age>60) {
            if (report->phase==0) { report->phase=2; report->age=0; report->wait=36000; }
            else {
                world.TypingSound(0x000336A9u);
                report->scoreUi=std::make_unique<TypingScoreUi>();
                StartTypingScoreUi(world,*report->scoreUi,report->evaluation);
                report->fn=report->scoreUi->phase==TypingScoreUi::Profile ? 0x004888F0 : 0x004A14C0;
                return;
            }
        }
    }
    Draw(world,*report,turn);
}
}
