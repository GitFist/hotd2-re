#include "game/world_typing_drill_result_visuals.h"
#include "game/world_internal.h"
#include "game/shoot_camera.h"
#include "game/mstack.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game {
namespace {
using DrillPiece = std::array<uint32_t,9>;
#include "game/typing_drill_result_layout.inc"
#include "game/typing_drill_book_layout.inc"
constexpr std::array<int,13> kSourceExercise = {0,1,2,6,7,8,3,4,5,9,10,11,12};
constexpr std::array<int,13> kNativeExercise = {0,1,2,6,7,8,3,4,5,9,10,11,12};
constexpr std::array<int,13> kBookUnits = {5,2,6,0,5,2,1,0,5,3,4,0,5};
// Original authored instruction text at0x589298, in its menu order. Each row
// is five displayed limits followed by five comparison/NO CLEAR selectors.
// These labels deliberately retain authored display values even where the
// supervisor's grade boundary differs (Speed3 D and the Final D wording).
constexpr std::array<std::array<int,10>,13> kInstructionCriteria = {{
    {{60,90,130,200,201, 0,0,0,0,1}},
    {{35,15,6,3,2, 1,1,1,1,2}},
    {{12,18,24,30,42, 0,0,0,1,3}},
    {{40,25,15,10,9, 1,1,1,1,2}},
    {{45,90,120,180,181, 0,0,0,0,1}},
    {{45,30,15,10,9, 1,1,1,1,2}},
    {{100,60,40,20,19, 1,1,1,1,2}},
    {{45,30,20,10,9, 1,1,1,1,2}},
    {{120,60,45,30,29, 1,1,1,1,2}},
    {{128,64,32,17,16, 1,1,1,1,2}},
    {{70,50,30,18,17, 1,1,1,1,2}},
    {{36,24,16,8,7, 1,1,1,1,2}},
    {{280,320,360,361,480, 0,0,0,1,3}},
}};
// Source0x5894C8: accuracy, average reaction, time per letter availability.
constexpr std::array<unsigned,13> kBookStatsMask = {7,7,5,6,7,6,2,7,7,0,3,7,7};
// Source0x5D9BEC uses this same dash glyph at565/577/589 for each missing statistic.
constexpr DrillPiece kUnavailableStat = {0x0000021Du,0x000000A2u,0x3F780000u,0x3F3F0004u,
    0x3F7E8006u,0x3F487FFEu,0x3CD013A9u,0x3D17F62Bu,0x00010000u};
float Float(uint32_t value) { return std::bit_cast<float>(value); }

void BeginBookEnvironment(World::Impl& world) {
    // PC4A75B6/4A75C0 resets both environment blocks through4AE3A0
    // before the result book. Its fog limits are65535/65536; retaining the
    // completed arena's fog can black out the paper at its authored depth17.
    world.env.ResetBlocks();
    world.env.InitInterp();
}

int Model(World::Impl& world,int raw) {
    const int model=world.typingReference.SourceModel(raw);
    if (model<=0) return 0;
    if (model>=int(world.modelLoaded.size()) || !world.modelLoaded[model]) {
        int file=world.typingReference.ModelFile(model);
        if (file<0 && model<0x4000) file=world.exe.S16(0x004E83B4+uint32_t(model)*2);
        if (file>=0) world.ProcessRequest({0x52,uint32_t(file)});
    }
    return model<int(world.modelLoaded.size()) && world.modelLoaded[model] ? model : 0;
}
void Page(World::Impl& world,int model,int angle,float brightness) {
    Mat4 matrix=Mat4::Identity();
    mx::Translate(matrix,0,0,-17);
    mx::RotY(matrix,angle);
    if (auto* draw=world.Draw(model,matrix,8,-1,false,-1,true)) {
        draw->fn=0x00497820;
        draw->light.table=false;
        draw->light.intensity=1;
        for (float& color:draw->light.color) color=brightness;
        DirToAngles(world.cam.inverse.m[8],world.cam.inverse.m[9],world.cam.inverse.m[10],
                    &draw->light.pitch,&draw->light.yaw);
    }
}
bool Book(World::Impl& world,bool records) {
    if (!world.cfg.portTyping || !world.typingReferenceActive) return false;
    // Source0x4978F0 book data: two spreads, their front/back model pairs,
    // and the five staggered leaves used when opening and closing.
    const std::array<int,4> pages=records?std::array<int,4>{0x1566,0x1567,0x1566,0x1567}:
        std::array<int,4>{0x156E,0x1571,0x156E,0x1567};
    const int coverLeft=Model(world,0x2A5),coverRight=Model(world,0x2A6);
    std::array<int,4> models;
    for(int i=0;i<4;++i)models[i]=Model(world,pages[i]);
    if (!coverLeft || !coverRight || std::any_of(models.begin(),models.end(),[](int m){return !m;})) return false;
    world.hud.menuPanels.clear();
    world.hud.menuText.clear();
    auto& ui=world.typingDrill.bookUi;
    float position=ui.kind<0?1.f:ui.position;
    position=std::clamp(position,0.f,3.f);
    const int spread=std::min(2,int(position));
    const float fraction=position-spread;
    const float brightness=position<.5f?fraction*2:position>2.5f?(1-fraction)*2:1;
    auto page=[&](int model,int angle){Page(world,model,angle,brightness);};
    auto leaf=[&](float progress,int front,int back,bool reverse=false) {
        if(progress<0 || progress>1)return;
        const float split=reverse?.64f:.36f;
        const int model=progress<=split?front:back;
        // PC4978A7/4978C3 changes both the mesh and its facing. An open
        // front leaf is at0x8000; its back continues from the opposite side.
        // The reverse flag changes the mesh-switch threshold only on PC.
        const int angle=progress<=split?int((1-progress)*32768):int(progress*-32768);
        page(model,angle);
    };
    page(coverLeft,position<.5f?int(-65536*fraction):-32768);
    page(coverRight,position>2.5f?int(32768-(fraction-.5f)*65536):32768);
    if(position<1) {
        for(int i=1;i<=5;++i)leaf((fraction-i*.1f-.02f)*2,models[0],models[1]);
        page(models[1],32768);
        ui.alpha=std::clamp((fraction-.85f)*6.6666665f,0.f,1.f);
    } else if(position>2) {
        for(int i=0;i<5;++i)leaf((fraction-i*.1f)*2,models[2],models[3]);
        page(models[2],32768);
        ui.alpha=std::clamp(1-fraction*6.6666665f,0.f,1.f);
    } else {
        if(fraction<.9f)page(models[spread*2-2],32768);
        if(fraction>.1f && spread<2)page(models[spread*2+1],32768);
        leaf(fraction,models[spread*2-1],models[std::min(3,spread*2)],records&&ui.recordTurn<0);
        ui.alpha=fraction<.15f?std::clamp(1-fraction*6.6666665f,0.f,1.f):
            fraction>.85f?std::clamp((fraction-.85f)*6.6666665f,0.f,1.f):0;
    }
    return true;
}
void SubmitAt(World::Impl& world,const DrillPiece& piece,int index,float x,float y,float scale=1) {
    const size_t before=world.hudSprites.size();
    const float alpha=world.typingDrill.bookUi.kind<0?1.f:world.typingDrill.bookUi.alpha;
    world.TypingSprite(0xE29,x,y,alpha*((piece[8]&0xffffu)==1?.5f:1.f),
                       30.2f-float(index)*.00001f,5,0x004A8AE0);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back();
    draw.sx=Float(piece[6])*scale;
    draw.sy=Float(piece[7])*scale;
    draw.atlas=true;
    draw.uv[0]=Float(piece[2]); draw.uv[1]=1-Float(piece[5]);
    draw.uv[2]=Float(piece[4]); draw.uv[3]=1-Float(piece[3]);
}
void Submit(World::Impl& world,const DrillPiece& piece,int index) {
    SubmitAt(world,piece,index,float(int32_t(piece[0])),float(int32_t(piece[1])));
}
void ModeCaption(World::Impl& world) {
    // Drill book flags select source 0x5E37AC entry0 (DC mode). The AC-mode
    // record in the count layout is a dormant alternative, not its heading.
    DrillPiece mode=kDrillResult[1];
    mode[3]=0x3B7FFBCEu;
    mode[5]=0x3D07FFDEu;
    Submit(world,mode,1);
}
template<size_t N> struct Layout {
    std::array<DrillPiece,N> pieces;
    std::array<bool,N> visible{};
    explicit Layout(const std::array<DrillPiece,N>& source):pieces(source) {}
    void show(int index) { visible[size_t(index)]=true; }
    template<size_t F> void link(int index,const std::array<DrillPiece,F>& font,unsigned glyph) {
        if (index<0 || index>=int(N) || glyph>=F) return;
        std::copy(font[glyph].begin()+2,font[glyph].end(),pieces[index].begin()+2);
        show(index);
    }
    template<size_t F> void number(int at,int digits,unsigned value,const std::array<DrillPiece,F>& font,bool zeroes=false) {
        for (int i=digits-1;i>=0;--i) {
            link(at+i,font,value%10);
            visible[at+i]=zeroes || i==digits-1 || value!=0;
            value/=10;
        }
    }
    void draw(World::Impl& world) const {
        for (size_t i=0;i<N;++i) if (visible[i]) Submit(world,pieces[i],int(i));
    }
};
int Glyph(unsigned char character) {
    if (character>='A' && character<='Z') return character-'A';
    if (character>='a' && character<='z') return character-'a'+26;
    for (size_t i=0;i<kDrillBookSymbols.size();++i)
        if (character==kDrillBookSymbols[i]) return int(i)+52;
    return -1;
}
template<size_t N> void LayoutText(Layout<N>& layout,int at,int count,std::string_view text) {
    for(int i=0;i<count && i<int(text.size());++i) {
        const int glyph=Glyph(static_cast<unsigned char>(text[i]));
        if(glyph>=0 && text[i]!=' ')layout.link(at+i,kDrillBookFont,unsigned(glyph));
    }
}
template<size_t N,size_t F> void Metric(Layout<N>& layout,int at,int unit,unsigned value,
                                       const std::array<DrillPiece,F>& font) {
    if(unit==5) {
        layout.number(at,2,value/3600,font);
        layout.show(at+2);layout.show(at+5);
        layout.number(at+3,2,value/60%60,font,true);
        layout.number(at+6,2,value%60*100/60,font,true);
    } else if(unit==6) {
        const float frames=float(value*.0010000000474974513);
        const unsigned seconds=unsigned(frames*.01666666753590107);
        const unsigned fraction=unsigned((frames-seconds)*16.66666603088379+.5);
        layout.number(at,2,seconds,font);
        layout.show(at+2);
        layout.number(at+3,3,fraction,font,true);
    }
}
void Art(World::Impl& world,int sprite,float x,float y,float scale=1,uint32_t flags=5) {
    const float alpha=world.typingDrill.bookUi.kind<0?1:world.typingDrill.bookUi.alpha;
    const size_t before=world.hudSprites.size();
    world.TypingSprite(sprite,x,y,alpha,30.2f,flags,0x004A8AE0);
    if(world.hudSprites.size()!=before)world.hudSprites.back().sx=world.hudSprites.back().sy=scale;
}
void InstructionGlyph(World::Impl& world,int sprite,float x,float y) {
    world.TypingSprite(sprite,x,y,1,6,5,0x004A8240);
}
void InstructionNumber(World::Impl& world,unsigned value,int digits,float x,float y) {
    unsigned divisor=digits==3?100:digits==2?10:1;
    for (int i=0;i<digits;++i) {
        InstructionGlyph(world,0x28B+int(value/divisor%10),x+12*i,y);
        divisor=std::max(1u,divisor/10);
    }
}
std::pair<unsigned,unsigned> LetterTimeParts(const TypingDrillResult& result,int unit) {
    constexpr long double oneOver60=0.01666666753590106964111328125L;
    constexpr long double hundredthsPerFrame=1.66666662693023681640625L;
    const long double mean=unit==6?static_cast<long double>(result.metric)*0.0010000000474974513L:
        result.speedCharacters?static_cast<long double>(result.speedFrames)/result.speedCharacters:0;
    const float storedFrames=float(mean);
    const long double seconds=mean*oneOver60;
    const unsigned whole=unsigned(std::clamp(seconds,0.L,9999.L));
    // Source4A90E7's special metric path rounds;4A9161's ordinary path uses
    // its stored mean-seconds value in the subtraction and then truncates.
    // Keep that authored display arithmetic rather than using course time.
    const long double remainder=unit==6?static_cast<long double>(storedFrames)-whole:
        static_cast<long double>(storedFrames)-float(seconds);
    const unsigned fraction=unsigned(std::clamp(remainder*hundredthsPerFrame+(unit==6?.5L:0.L),0.L,9999.L));
    return {whole,fraction};
}
void Title(World::Impl& world,int source,int side) {
    const float offset=side?301.f:0.f;
    const size_t before=world.hudSprites.size();
    const float alpha=world.typingDrill.bookUi.kind<0?1:world.typingDrill.bookUi.alpha;
    world.TypingSprite(kDrillHeadings[source],62+offset,34,alpha,30.2f,5,0x004A2050);
    world.TypingSprite(kDrillSubheadings[source],212+offset,31,alpha,30.2f,5,0x004A2050);
    if (world.hudSprites.size()>before+1)
        world.hudSprites.back().sx=world.hudSprites.back().sy=.75f;
}
void Graph(World::Impl& world,const std::vector<int>& history,int side) {
    // Original chart box/point textures and 226-pixel span (0x4A2B60).
    const float start=64+301.f*side;
    float previousX=0,previousY=0;
    for (size_t i=0;i<history.size();++i) {
        const float x=start+226+(history.size()>8?4:8)-float((history.size()-i-1)*226/history.size());
        constexpr float gradeY[]={186,205,223,241,260};
        const float y=gradeY[std::clamp(history[history.size()-i-1],0,4)];
        if (i) {
            const float dx=x-previousX,dy=y-previousY;
            const size_t before=world.hudSprites.size();
            world.TypingSprite(0x4AD,previousX,previousY,world.typingDrill.bookUi.alpha,30.21f,0x2009,0x004A2CB0);
            if (world.hudSprites.size()!=before) {
                auto& line=world.hudSprites.back();
                line.sx=std::hypot(dx,dy)/8; line.sy=.125f;
                line.rot=int(-std::atan2(dy,dx)*10430.37835047045);
                line.tinted=true;
                line.tint[0]=.251f; line.tint[1]=.5804f; line.tint[2]=.2118f;
            }
        }
        world.TypingSprite(history.size()>8?0x4B1:0x4B0,x,y,world.typingDrill.bookUi.alpha,30.2f,0xA,0x004A2B60);
        previousX=x; previousY=y;
    }
}
template<size_t N> void Attempts(Layout<N>& layout,int at,unsigned attempts) {
    layout.number(at,2,attempts,kDrillSmallDigits);
    unsigned suffix=16;
    if(attempts<=3 || (attempts>20 && attempts%10>=1 && attempts%10<=3))suffix=12+attempts%10;
    layout.link(at+2,kDrillSmallDigits,suffix);layout.show(at+3);
}
void Criteria(World::Impl& world,Layout<100>& layout,int source,int grade) {
    const int unit=kBookUnits[source];
    Art(world,0xB12,39,327);
    Art(world,0xE2D,111,328+22.f*grade);
    for(int i=0;i<10;++i)Submit(world,kDrillResultTime[14+i],14+i);
    for(int row=0;row<5;++row) {
        if(row==4 && (source==2 || source==12)) {
            // The authored E criterion is NO CLEAR for these two courses.
            Submit(world,kDrillNoClear[0],300);
        } else if(unit==6) {
            const unsigned tenths=unsigned(kInstructionCriteria[source][row])/6;
            layout.number(80+4*row,1,tenths/10,kDrillSmallDigits);
            layout.show(81+4*row);
            layout.number(82+4*row,1,tenths%10,kDrillSmallDigits);
            layout.show(83+4*row);
        } else {
            const unsigned value=unsigned(kInstructionCriteria[source][row]);
            layout.number(60+4*row,3,value,kDrillSmallDigits);
            layout.link(63+4*row,kDrillResultUnits,unsigned(unit*2+(unit!=5 && value>1)));
        }
    }
}
void Ranking(World::Impl& world,int source) {
    const auto& drill=world.typingDrill.controller;
    const int exercise=kNativeExercise[source],unit=kBookUnits[source];
    const auto& rows=drill.records(exercise);
    Layout layout(kDrillRanking);
    Layout counts(kDrillCountRanking);
    layout.show(3);layout.show(5);
    const auto& name=world.typingDrill.nameUi;
    if(drill.recordRank()>=0)LayoutText(layout,7,5,name.active?name.name:std::string_view(drill.result().name.data()));
    if(name.active && !name.confirmation) {
        const int t=world.frame%32;
        const float pulse=float((world.frame&16)?t:31-t)/32;
        const size_t before=world.hudSprites.size();
        Art(world,0x288,40,352);
        if(world.hudSprites.size()!=before)world.hudSprites.back().alpha*=pulse;
    }
    for(int row=0;row<5;++row) {
        layout.show(17+row*2);layout.show(18+row*2);
        const unsigned value=row<int(rows.size())?unsigned(std::max(0,rows[row].metric)):0;
        if(row<int(rows.size()))LayoutText(layout,27+row*5,5,std::string_view(rows[row].name.data()));
        if(unit==5)Metric(layout,60+row*8,unit,value,kDrillSmallDigits);
        else if(unit==6)Metric(layout,104+row*6,unit,value,kDrillSmallDigits);
        else {
            counts.number(58+row*4,3,value,kDrillSmallDigits);
            counts.link(61+row*4,kDrillResultUnits,unsigned(unit*2+(value>1)));
        }
    }
    if(drill.attempts()[exercise])Attempts(layout,100,drill.attempts()[exercise]);
    else {counts.show(52);counts.show(53);}
    layout.draw(world);counts.draw(world);
    Graph(world,drill.history(exercise),1);
}
} // namespace

void DrawTypingDrillInstructionCriteria(World::Impl& world,int exercise) {
    if (exercise<0 || exercise>=13 || !world.cfg.portTyping || !world.typingReferenceActive) return;
    const auto& row=kInstructionCriteria[kSourceExercise[exercise]];
    const int unit=kBookUnits[kSourceExercise[exercise]];
    // Source0x58953C and0x589546: comparison labels and the instruction
    // font's plural units. These 16-pixel glyphs are distinct from book text.
    constexpr int comparison[]={0x29E,0x29C,0x29D};
    constexpr int units[]={0x2A4,0x298,0x2A2,0x296,0x29B,0x2A0,0x2A0};
    for (int grade=0;grade<5;++grade) {
        const float y=299+22.f*grade;
        const int kind=row[5+grade];
        if (kind==3) {
            InstructionGlyph(world,0x289,387,y);
            continue;
        }
        InstructionGlyph(world,comparison[kind],347,y);
        if (unit==6) {
            // Source0x493310 shows one decimal place. Its authored limits are
            // frame counts (12,18,24,30), displayed as0.2,0.3,0.4,0.5 seconds.
            const unsigned tenths=unsigned(row[grade])/6;
            InstructionNumber(world,tenths/10,1,399,y);
            InstructionGlyph(world,0x299,409,y);
            InstructionNumber(world,tenths%10,1,419,y);
        } else {
            const unsigned value=unsigned(row[grade]);
            const int digits=value<10?1:value<100?2:3;
            InstructionNumber(world,value,digits,435-12.f*digits,y);
        }
        InstructionGlyph(world,units[unit],unit==6?434.f:442.f,y);
    }
}

bool TypingDrillResultsVisuals(World::Impl& world) {
    if (!world.typingDrill.controller.result().original) return false;
    if (!Book(world,false)) return false;
    const auto& ui=world.typingDrill;
    const auto& drill=ui.controller;
    const auto& result=drill.result();
    Art(world,0xAC3,106,34);
    const int source=kSourceExercise[std::clamp(drill.exercise(),0,12)];
    Title(world,source,1);
    Art(world,0xE2E,49,157);Art(world,0xE30,49,208);Art(world,0xE2F,49,261);
    Layout layout(kDrillResult);
    layout.show(0); layout.show(2);
    ModeCaption(world);
    layout.link(3,kDrillGrades,unsigned(2+std::clamp(result.grade,0,4)));
    if (!drill.supplementary() && drill.recordRank()>=0) {
        const unsigned place=unsigned(drill.recordRank())+1;
        layout.number(5,1,place,kDrillLargeDigits);
        layout.link(4,kDrillLargeDigits,place<=3?9+place:13);
    } else Submit(world,kDrillNoRank[0],0);
    const unsigned metric=unsigned(std::max(0,result.metric));
    const int unit=kBookUnits[source];
    if (unit==5 || unit==6) {
        Layout time(kDrillResultTime);
        Metric(time,unit==5?6:104,unit,metric,kDrillLargeDigits);
        time.draw(world);
    } else {
        layout.number(56,3,metric,kDrillLargeDigits);
        layout.link(59,kDrillResultUnits,unsigned(11+unit*2+(metric>1)));
    }
    const auto& book=ui.bookUi;
    if(book.kind>=0 && book.position>=1.5f) {
        layout.draw(world);
        Ranking(world,source);
        return true;
    }
    Criteria(world,layout,source,std::clamp(result.grade,0,4));
    layout.number(16,3,unsigned(result.assignedWords),kDrillSmallDigits);
    layout.number(19,3,unsigned(result.completedWords),kDrillSmallDigits);
    const unsigned statistics=kBookStatsMask[kSourceExercise[drill.exercise()]];
    if (statistics&1) {
        const uint64_t attempted=uint64_t(result.correct)+result.errors;
        layout.number(22,3,attempted?unsigned(uint64_t(result.correct)*100/attempted):0,kDrillSmallDigits);
        layout.show(25);
    }
    const uint64_t reaction=result.reactions?result.reactionFrames/result.reactions:0;
    const auto letter=LetterTimeParts(result,unit);
    for (const auto [at,enabled,whole,fraction]:std::array<std::array<unsigned,4>,2>{{
            {{26,statistics&2,unsigned(std::min<uint64_t>(9999,reaction/60)),unsigned(reaction%60*100/60)}},
            {{31,statistics&4,letter.first,letter.second}}}}) {
        if (!enabled) continue;
        layout.number(int(at),2,whole,kDrillSmallDigits);
        layout.show(at+2);
        layout.number(int(at)+3,2,fraction,kDrillSmallDigits,true);
    }
    layout.draw(world);
    for (int row=0;row<3;++row) if (!(statistics&(1u<<row)))
        for (int digit=0;digit<3;++digit) SubmitAt(world,kUnavailableStat,300,565+12.f*digit,206+22.f*row);
    Layout weakness(kDrillResult);
    const auto weak=drill.weakKeyStats();
    for (size_t i=0;i<weak.size();++i) {
        const char key=char(weak[i].key);
        LayoutText(weakness,51+int(i),1,std::string_view(&key,1));
        weakness.number(36+3*int(i),3,weak[i].percent,kDrillSmallDigits);
    }
    weakness.draw(world);
    for(size_t i=weak.size();i<5;++i)
        for(int digit=0;digit<3;++digit)SubmitAt(world,kDrillDashes[25+i*3+digit],300,
            float(int32_t(kDrillDashes[25+i*3+digit][0])),float(int32_t(kDrillDashes[25+i*3+digit][1])));
    return true;
}

bool TypingDrillRecordsVisuals(World::Impl& world) {
    if (!Book(world,true)) return false;
    const auto& ui=world.typingDrill;
    const int source=kSourceExercise[std::clamp(ui.record,0,12)];
    Layout layout(kDrillRecordFull);
    Layout counts(kDrillRecords);
    ModeCaption(world);
    for (int side=0;side<2;++side) {
        const int exerciseSource=(source+side)%13;
        const int exercise=kNativeExercise[exerciseSource];
        const auto& records=ui.controller.records(exercise);
        Title(world,exerciseSource,side);
        Graph(world,ui.controller.history(exercise),side);
        for (int row=0;row<5;++row) {
            const int rank=(side?1:80)+row*2;
            layout.show(rank); layout.show(rank+1);
            const unsigned value=row<int(records.size())?unsigned(std::max(0,records[row].metric)):0;
            if(row<int(records.size()))LayoutText(layout,(side?11:90)+row*5,5,std::string_view(records[row].name.data()));
            const int unit=kBookUnits[exerciseSource];
            if(unit==5)Metric(layout,(side?36:115)+row*8,unit,value,kDrillSmallDigits);
            else if(unit==6)Metric(layout,(side?189:159)+row*6,unit,value,kDrillSmallDigits);
            else {
                const int at=(side?37:94)+row*4;
                counts.number(at,3,value,kDrillSmallDigits);
                counts.link(at+3,kDrillResultUnits,unsigned(unit*2+(value>1)));
            }
        }
        if(ui.controller.attempts()[exercise])Attempts(layout,side?76:155,ui.controller.attempts()[exercise]);
        else {counts.show(side?35:92);counts.show(side?36:93);}
    }
    layout.draw(world);counts.draw(world);
    return true;
}

void BeginTypingDrillResults(World::Impl& world) {
    BeginBookEnvironment(world);
    auto& ui=world.typingDrill.bookUi;
    ui={};ui.kind=0;ui.ticks=1;ui.position=1.f/60;
    ui.nameDone=world.typingDrill.controller.recordRank()<0;
    world.TypingSound(0x000036A9u);
}
void BeginTypingDrillRecords(World::Impl& world) {
    BeginBookEnvironment(world);
    auto& ui=world.typingDrill.bookUi;
    ui={};ui.kind=1;ui.ticks=0;ui.position=0;
    world.typingDrill.record=0;
    world.TypingSound(0x000036A9u);
}
TypingDrillBookAction TypingDrillResultsFrame(World::Impl& world,uint32_t buttons) {
    auto& ui=world.typingDrill.bookUi;
    if(ui.kind!=0)BeginTypingDrillResults(world);
    if(ui.phase<=2) {
        if(ui.ticks>0) {
            if(++ui.ticks>60){++ui.phase;ui.ticks=0;}
        } else if(ui.phase==2 && !ui.nameDone) {
            TypingDrillResultsVisuals(world);
            return TypingDrillBookAction::Name;
        } else if(buttons&0xEu) {
            ui.ticks=1;
            world.TypingSound(ui.phase<2?0x000136A9u:0x000236A9u);
        }
        ui.position=ui.phase+ui.ticks/60.f;
        TypingDrillResultsVisuals(world);
        if(ui.phase==3) {
            world.TypingSound(0x000336A9u);
            ui.phase=4;ui.confirmation=0;
        }
        return TypingDrillBookAction::None;
    }
    if(ui.phase==5) {
        if(--ui.ticks<=0)return TypingDrillBookAction::Supplementary;
        return TypingDrillBookAction::None;
    }
    // Source4A7AB0 asks whether to repeat this course using its five weak keys.
    // This is a separate question, after the ranking spread closes.
    if(!ui.confirmation){ui.confirmation=2;world.TypingSound(0x000536A9u);}
    if(buttons&0x50u){ui.confirmation=1;world.TypingSound(0x003E16A9u);}
    if(buttons&0xA0u){ui.confirmation=2;world.TypingSound(0x003E16A9u);}
    if(buttons&0xEu) {
        world.TypingSound(0x003016A9u);
        if((buttons&2u) || ui.confirmation==2)return TypingDrillBookAction::Select;
        ui.phase=5;ui.ticks=30;
    }
    auto popup=[&](int sprite,float x,float y,float depth,float sx=1,float sy=1,float alpha=1,uint32_t flags=5) {
        const size_t before=world.hudSprites.size();
        world.TypingSprite(sprite,x,y,alpha,depth,flags,0x00498250);
        if(world.hudSprites.size()!=before){world.hudSprites.back().sx=sx;world.hudSprites.back().sy=sy;}
    };
    popup(0xE36,0,0,30.185001f,80,60,.5f);
    popup(0xBC6,320,240,10.185001f,1,1,1,0xA);
    popup(0xBC7,192,237,10.183f);
    popup(0xBC5,192,179,10.183f);
    popup(0xBCD,289,242,10.183f);popup(0xBC9,288,268,10.183f);
    popup(0xBCC,258,ui.confirmation==1?243.f:270.f,10.184f);
    return TypingDrillBookAction::None;
}
TypingDrillBookAction TypingDrillRecordsFrame(World::Impl& world,uint32_t buttons) {
    auto& ui=world.typingDrill.bookUi;
    if(ui.kind!=1)BeginTypingDrillRecords(world);
    if(ui.phase==0) {
        if(++ui.ticks>60){ui.phase=1;ui.ticks=0;}
        ui.position=ui.phase==0?ui.ticks/60.f:1;
    } else if(ui.phase==2) {
        if(++ui.ticks>60)return TypingDrillBookAction::Select;
        ui.position=2+ui.ticks/60.f;
    } else if(ui.recordTurn) {
        ui.ticks+=ui.recordTurn;
        if(std::abs(ui.ticks)==30)ui.recordPage+=2*ui.recordTurn;
        ui.position=(ui.recordTurn>0?1.f:2.f)+ui.ticks/60.f;
        if(std::abs(ui.ticks)>=60){ui.recordTurn=0;ui.ticks=0;ui.position=1;}
    } else {
        ui.position=1;
        if(buttons&2u){ui.phase=2;ui.ticks=0;world.TypingSound(0x000236A9u);}
        else if(buttons&(0x40u|0x88u)) {
            ui.recordTurn=buttons&0x40u?-1:1;
            ui.ticks=ui.recordTurn;
            world.TypingSound(0x000136A9u);
        }
    }
    const int page=(ui.recordPage%13+13)%13;
    world.typingDrill.record=kNativeExercise[page];
    TypingDrillRecordsVisuals(world);
    return TypingDrillBookAction::None;
}
} // namespace game
