#include <algorithm>
#include <bit>
#include <cmath>
#include <ctime>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/typing_score_ui.h"
#include "game/world_internal.h"

namespace game {
namespace {
using Piece=std::array<uint32_t,9>;
#include "game/typing_score_layout.inc"
float F(uint32_t bits) { return std::bit_cast<float>(bits); }
int Model(World::Impl& world,int raw) {
    const int id=world.typingReference.SourceModel(raw);
    if (id<=0) return 0;
    if (id>=int(world.modelLoaded.size()) || !world.modelLoaded[id]) {
        const int file=world.typingReference.ModelFile(id);
        if (file>=0) world.ProcessRequest({0x52,uint32_t(file)});
    }
    return id;
}
void Light(World::Impl& world,WorldDraw& draw,float brightness=1) {
    draw.light.table=false; draw.light.intensity=1;
    for (float& color:draw.light.color) color=brightness;
    DirToAngles(world.cam.inverse.m[8],world.cam.inverse.m[9],world.cam.inverse.m[10],
                &draw.light.pitch,&draw.light.yaw);
}
void Sprite(World::Impl& world,int id,float x,float y,float depth,float sx=1,float sy=1,
            float alpha=1,uint32_t flags=5) {
    const size_t before=world.hudSprites.size();
    world.TypingSprite(id,x,y,alpha,depth,flags,0x004888F0);
    if (world.hudSprites.size()!=before) { world.hudSprites.back().sx=sx; world.hudSprites.back().sy=sy; }
}
void Glyph(World::Impl& world,unsigned char c,float x,float y,float distance,float scale,float alpha=1) {
    if (c<32 || c>127) return;
    // Match the source font model's projected depth under its .5 /8000 lens.
    const float a=8000.f/(8000.f-.5f), z=a-.5f*a/distance;
    const float depth=.9f/(1-z);
    const float pixels=world.focal*scale/distance;
    const size_t before=world.hudSprites.size();
    world.TypingSprite(0xF74+c-32,320+world.focal*x/distance,240-world.focal*y/distance,
                       alpha,depth,0x200A,0x00488CD9);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back();
    draw.sx=pixels/512; draw.sy=pixels/256; draw.atlas=draw.tinted=true;
    draw.tint[0]=.2039215714f; draw.tint[1]=.1960784346f; draw.tint[2]=.3529411852f;
    const int slot=c-32;
    draw.uv[0]=float(slot%16*32)/512; draw.uv[1]=float((slot/16+1)*32)/256;
    draw.uv[2]=draw.uv[0]+32.f/512; draw.uv[3]=draw.uv[1]-32.f/256;
}
void Counter(World::Impl& world,int value) {
    for (int slot=0;slot<2;++slot) {
        const int raw=slot ? 0x1CB6+value%10 : 0x1CAC+value/10;
        const int model=Model(world,raw);
        if (model<=0) continue;
        Mat4 matrix=Mat4::Identity();
        mx::Translate(matrix,5400/world.focal*.2f,4920/world.focal*.2f,-6);
        mx::Scale(matrix,.08f,.08f,.08f);
        if (auto* draw=world.Draw(model,matrix,8,1,false,-1,true)) {
            draw->fn=0x004AC040; Light(world,*draw);
        }
    }
}
std::array<float,2> FieldPoint(int field,int index) {
    constexpr float xy[][2]={{-3.1600000858f,2.7000000477f},{-.25f,1.1799999475f},
                            {-3.1600000858f,-.3499999940f},{-4.0300002098f,-1.9099999666f}};
    return {xy[field][0]+float(field==3 ? index%15 : index)*.5799999833f,
            xy[field][1]-(field==3 ? float(index/15)*.6200000048f : 0)};
}
void ProfileDraw(World::Impl& world,const TypingScoreUi& ui) {
    // Source488AE1's five original background tiles and488B2A's title/P1 label.
    for (int i=0;i<5;++i) Sprite(world,0x976+i,float(i*128),-16,100.5f);
    Sprite(world,0x62B,486,38,100.4f,1,1,1,0);
    Sprite(world,0x975,68,39,100.4f,1,1,1,0);
    constexpr int label[][3]={{0x7B8,110,95},{0x5D,224,160},{0x720,111,224},
                             {0x7A2,86,291},{0x39D,257,419}};
    for (int i=0;i<5;++i) Sprite(world,label[i][0],float(label[i][1]),float(label[i][2]-16),100.1f,1,1,1,0);
    const int pulse=std::abs(128-((ui.age*6)&255));
    Sprite(world,label[ui.field][0]+1,float(label[ui.field][1]),float(label[ui.field][2]-16),50.05f,
           1,1,.2f+float(pulse)*.0078125f*.8f,0);
    for (int field=0;field<4;++field) {
        int cell=0;
        for (unsigned char c:ui.fields[field]) {
            if (c=='\n') { cell+=15-cell%15; continue; }
            const auto xy=FieldPoint(field,cell++);
            Glyph(world,c,xy[0],xy[1],15,.75f,.99f);
        }
        if (field==ui.field && cell<(field==1 ? 2 : field==3 ? 45 : 12)) {
            const auto xy=FieldPoint(field,cell);
            const int blink=std::abs(64-((ui.age*6)&127));
            Glyph(world,'_',xy[0],xy[1],15,.75f,.19f+float(blink)*.015625f*.8f);
        }
    }
    Counter(world,std::clamp(60-ui.age/60,0,60));
}
void Submit(World::Impl& world,const Piece& piece,int index,float alpha) {
    const size_t before=world.hudSprites.size();
    world.TypingSprite(0xE29,float(int32_t(piece[0])),float(int32_t(piece[1])),alpha,
                       30.2000007629f-float(index)*.00001f,5,0x004A1720);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back(); draw.sx=F(piece[6]); draw.sy=F(piece[7]); draw.atlas=true;
    draw.uv[0]=F(piece[2]); draw.uv[1]=1-F(piece[5]); draw.uv[2]=F(piece[4]); draw.uv[3]=1-F(piece[3]);
}
struct Layout {
    std::array<Piece,176> pieces=kScores;
    std::array<bool,176> visible;
    Layout() { visible.fill(true); }
    template<size_t N> void link(int at,const std::array<Piece,N>& font,unsigned glyph) {
        if (at<0 || at>=176 || glyph>=N) return;
        std::copy(font[glyph].begin()+2,font[glyph].end(),pieces[at].begin()+2); visible[at]=true;
    }
    template<size_t N> void number(int at,int count,unsigned value,const std::array<Piece,N>& font,bool zeros=false) {
        for (int i=count-1;i>=0;--i) {
            link(at+i,font,value%10); visible[at+i]=zeros || i==count-1 || value!=0; value/=10;
        }
    }
    void text(int at,int count,std::string_view value) {
        for (int i=0;i<count;++i) visible[at+i]=false;
        int cell=0;
        for (unsigned char c:value) {
            if (c=='\n') { cell+=15-cell%15; continue; }
            if (cell>=count) break;
            int glyph=-1;
            if (c>='A' && c<='Z') glyph=c-'A';
            else if (c>='a' && c<='z') glyph=c-'a'+26;
            else if (c!=' ') {
                constexpr std::string_view symbols="$.,?!\"@/#%0123456789'-()&*:";
                const auto at=symbols.find(char(c)); if (at!=std::string_view::npos) glyph=int(at)+52;
            }
            if (glyph>=0) link(at+cell,kLetters,unsigned(glyph));
            ++cell;
        }
    }
};
void Page(World::Impl& world,int raw,int angle,float brightness) {
    const int model=Model(world,raw); if (model<=0) return;
    Mat4 matrix=Mat4::Identity(); mx::Translate(matrix,0,0,-17); mx::RotY(matrix,angle);
    if (auto* draw=world.Draw(model,matrix,8,-1,false,-1,true)) {
        draw->fn=0x00497820; Light(world,*draw,brightness);
    }
}
void ScoreBook(World::Impl& world,const TypingScoreUi& ui,float turn) {
    const float brightness=turn<.5f ? turn*2 : turn>2.5f ? (3-turn)*2 : 1;
    Page(world,0x2A5,turn<.5f ? int(-turn*65536) : -0x8000,brightness);
    Page(world,0x2A6,turn>2.5f ? int((3-turn)*65536) : 0x8000,brightness);
    Page(world,0x1564,0x8000,brightness); Page(world,0x1565,0x8000,brightness);
    const float fraction=turn<1 ? turn : turn<2 ? turn-1 : turn-2;
    if (ui.phase!=TypingScoreUi::Reading || ui.turn) for (int i=0;i<5;++i) {
        const float t=fraction-float(i)*.1f-(turn<1 ? .12f : 0);
        if (t>=0 && t<=.5f) Page(world,t<.25f ? 0x1565 : 0x1564,int(-t*65536),brightness);
    }
    float alpha=1;
    if (turn<1) alpha=std::clamp((turn-.85f)*6.6666665f,0.f,1.f);
    else if (turn>=2) alpha=std::clamp(1-(turn-2)*6.6666665f,0.f,1.f);
    else if (ui.turn) alpha=fraction<.5f ? std::clamp(1-fraction*6.6666665f,0.f,1.f) :
                                         std::clamp((fraction-.85f)*6.6666665f,0.f,1.f);
    if (alpha<=0) return;
    constexpr Piece dcMode={0x27,0x50,0x3f440000,0x3b7ffbce,0x3f697ffa,0x3d07ffde,0x3e160419,0x3cf0068e,0x10000};
    constexpr Piece acMode={0x27,0x50,0x3f440000,0x3d200086,0x3f697ffa,0x3d8bffce,0x3e160419,0x3cf0068e,0x10000};
    Submit(world,ui.original ? dcMode : acMode,0,alpha);
    Layout layout;
    const auto& records=world.typing.scoreRecords(ui.original);
    for (int side=0;side<2;++side) {
        const int ordinal=(ui.page+side)%8+1,at=side*88;
        const auto& record=records[ordinal-1];
        layout.number(at,1,unsigned(ordinal),kOrdinal);
        layout.link(at+1,kOrdinal,ordinal<=3 ? unsigned(ordinal+9) : 13);
        layout.number(at+2,4,record.year,kDateDigits,true); layout.number(at+6,2,record.month,kDateDigits,true);
        layout.number(at+8,2,record.day,kDateDigits,true);
        layout.text(at+12,12,record.name); layout.text(at+24,12,record.occupation); layout.text(at+36,45,record.comment);
        for (int digit=0;digit<2;++digit) {
            layout.visible[at+81+digit]=false;
            if (digit<int(record.age.size()) && record.age[digit]>='0' && record.age[digit]<='9')
                layout.link(at+81+digit,kRewardDigits,unsigned(record.age[digit]-'0'));
        }
        layout.number(at+83,4,std::min(9999u,unsigned(record.points)),kRewardDigits);
        Sprite(world,0xAC3,float(106+side*300),34,.2f,1,1,alpha);
        Sprite(world,record.player ? 3 : 2,float(268+side*300),76,.2f,.288f,.36f,alpha,0xA);
        Sprite(world,0xE2A,float(244+side*300),40,.1f,.3f,.3f,alpha,0xA);
    }
    for (int i=0;i<176;++i) if (layout.visible[i]) Submit(world,layout.pieces[i],i,alpha);
}
void FinishProfile(World::Impl& world,TypingScoreUi& ui) {
    ui.record.name=ui.fields[0]; ui.record.age=ui.fields[1];
    ui.record.occupation=ui.fields[2]; ui.record.comment=ui.fields[3];
    world.typing.storeScore(ui.record,ui.original);
    ui.phase=TypingScoreUi::Opening; ui.age=0;
}
void ProfileFrame(World::Impl& world,TypingScoreUi& ui) {
    bool enter=(world.pressed&0x80008u)!=0;
    constexpr int limits[]={12,2,12,45};
    for (unsigned char c:ui.text) {
        if (c=='\t') { ui.field=std::min(4,ui.field+1); continue; }
        if (c=='\v') { ui.field=std::max(0,ui.field-1); continue; }
        if (c=='\r') { enter=true; continue; }
        if (ui.field==4) continue;
        auto& field=ui.fields[ui.field];
        if (c=='\b') {
            if (!field.empty()) { field.pop_back(); world.TypingSound(0x000936A9); }
        } else if (c>=32 && c<=126 && (ui.field!=1 || (c>='0' && c<='9')) && int(field.size())<limits[ui.field]) {
            field.push_back(char(c)); world.TypingSound(0x000836A9);
        }
    }
    ui.text.clear();
    if (enter) {
        if (ui.field==4) { FinishProfile(world,ui); return; }
        if (ui.field==3) {
            int cells=0; for (char c:ui.fields[3]) cells+=c=='\n' ? 15-cells%15 : 1;
            if (cells<30) ui.fields[3].push_back('\n');
        } else ++ui.field;
    }
    ProfileDraw(world,ui);
    if (++ui.age>=3600) FinishProfile(world,ui);
}
}

void StartTypingScoreUi(World::Impl& world,TypingScoreUi& ui,const TypingFinalEvaluation& evaluation) {
    ui={}; ui.original=world.cfg.typingOriginal;
    ui.record.points=uint16_t(world.typing.currentChapterPoints(unsigned(std::max(0,int(world.rescueTotal)))));
    ui.record.player=evaluation.player; ui.record.proficiency=TypingPackedProficiency(evaluation.proficiency);
    const bool network=world.cfg.portNetworkRoom || world.cfg.networkStartedAt!=0;
    const std::time_t now=network ?
        std::time_t(world.cfg.networkStartedAt ? world.cfg.networkStartedAt : 946684800u) : std::time(nullptr);
    std::tm date{};
#ifdef _WIN32
    if (network) gmtime_s(&date,&now); else localtime_s(&date,&now);
#else
    if (network) gmtime_r(&now,&date); else localtime_r(&now,&date);
#endif
    ui.record.year=uint16_t(date.tm_year+1900); ui.record.month=uint8_t(date.tm_mon+1); ui.record.day=uint8_t(date.tm_mday);
    if (world.typing.scoreRank(ui.record,ui.original)>=0) ui.phase=TypingScoreUi::Profile;
    world.TypingSound(0x10000006u);
}
bool QueueTypingScoreUiText(TypingScoreUi& ui,std::string_view text) {
    if (ui.phase==TypingScoreUi::Profile) ui.text.append(text.substr(0,64));
    else if (text.find('\x1b')!=std::string_view::npos) ui.back=true;
    return ui.phase!=TypingScoreUi::Done;
}
void TypingScoreUiFrame(World::Impl& world,TypingScoreUi& ui) {
    if (ui.phase==TypingScoreUi::Profile) { ProfileFrame(world,ui); return; }
    if (ui.phase==TypingScoreUi::Opening || ui.phase==TypingScoreUi::Closing) {
        if (ui.age==0) world.TypingSound(ui.phase==TypingScoreUi::Opening ? 0x000036A9u : 0x000236A9u);
        ScoreBook(world,ui,(ui.phase==TypingScoreUi::Opening ? 0.f : 2.f)+float(ui.age)/60);
        if (++ui.age>60) {
            ui.phase=ui.phase==TypingScoreUi::Opening ? TypingScoreUi::Reading : TypingScoreUi::Done;
            ui.age=0; ui.back=false;
            if (ui.phase==TypingScoreUi::Done) world.TypingSound(0x000336A9u);
        }
        return;
    }
    if (ui.phase!=TypingScoreUi::Reading) return;
    if (!ui.turn) {
        if (ui.back || (world.pressed&0x20002u)) { ui.phase=TypingScoreUi::Closing; ui.age=0; ui.back=false; }
        else if (world.pressed&0x400040u) { ui.turn=-1; ui.age=0; world.TypingSound(0x000136A9u); }
        else if (world.pressed&0x880088u) { ui.turn=1; ui.age=0; world.TypingSound(0x000136A9u); }
    }
    if (ui.turn) {
        ++ui.age;
        if (ui.age==30) ui.page=(ui.page+ui.turn*2+8)%8;
        if (ui.age>=60) { ui.turn=0; ui.age=0; }
    }
    ScoreBook(world,ui,ui.turn ? 1+float(ui.age)/60 : 1);
}
}
