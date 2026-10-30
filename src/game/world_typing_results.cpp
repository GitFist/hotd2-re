#include <algorithm>
#include <array>
#include <bit>

#include "game/typing_results.h"
#include "game/world_internal.h"

namespace game {
namespace {
using Piece = std::array<uint32_t,9>;
#include "game/typing_result_layout.inc"
float F(uint32_t bits) { return std::bit_cast<float>(bits); }

struct Layout {
    std::array<Piece,kResult.size()> pieces = kResult;
    std::array<bool,kResult.size()> visible;
    Layout() {
        visible.fill(true);
        // Source one-player initialization491F70. These are mutable visibility
        // flags in the original layout; leave the atlas records themselves intact.
        for (int i=60;i<64;++i)
            for (int offset : {0,10,20,30,40,88,95,104,109}) visible[i+offset] = false;
        visible[142] = false;
        for (int i=159;i<162;++i) { visible[i] = false; visible[i+25] = false; }
        visible[187] = false;
        for (int i=75;i<77;++i)
            for (int offset : {-10,0,10,20,30,-8,2,12,22,32}) visible[i+offset] = false;
        visible[175] = false;
        for (int i=113;i<136;++i) visible[i] = false;
        visible[153] = visible[154] = false;
        for (int i=3;i<25;++i) visible[i] = false;
        for (int i=43;i<49;++i) visible[i] = false;
        for (int i=176;i<180;++i) visible[i] = false;
        for (int i : {136,137,138,139,140,141,143,144,145,146,147,152}) visible[i] = false;
    }
    template<size_t N> void link(int at, const std::array<Piece,N>& font, unsigned glyph) {
        if (at<0 || at>=int(pieces.size()) || glyph>=font.size()) return;
        // Source495270 keeps the destination anchor but takes UV, size and
        // material from the linked font entry.
        std::copy(font[glyph].begin()+2,font[glyph].end(),pieces[at].begin()+2);
        visible[at] = true;
    }
    template<size_t N> void number(int at, int digits, uint64_t value, const std::array<Piece,N>& font) {
        for (int index=digits-1;index>=0;--index) {
            const unsigned digit = unsigned(value%10);
            link(at+index,font,digit);
            visible[at+index] = index==digits-1 || value!=0;
            value/=10;
        }
    }
};

void Submit(World::Impl& world, const Piece& piece, int index, float alpha) {
    const size_t before = world.hudSprites.size();
    // Source495010: atlas240, source table coordinates, per-piece depth,
    // group0 opaque-white/group1 half-alpha, and top-left anchor flags5.
    world.TypingSprite(0x240,float(int32_t(piece[0])),float(int32_t(piece[1])),
                       alpha * ((piece[8]&0xffffu) == 1 ? .5f : 1.f),
                       30.200000762939453f-float(index)*.000009999999747378752f,5,0x00495010);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back();
    draw.sx=F(piece[6]); draw.sy=F(piece[7]);
    draw.atlas=true;
    draw.uv[0]=F(piece[2]); draw.uv[1]=1-F(piece[5]);
    draw.uv[2]=F(piece[4]); draw.uv[3]=1-F(piece[3]);
}
void ScoreGraph(World::Impl& world,Layout& layout,float alpha,bool showCurrent) {
    // Source490950: original0..best-thousand axes, personal-best and mean
    // markers, followed by the authored segmented score thermometer.
    const unsigned best=world.typing.chapterBest();
    const int levels=int(best/1000+1);
    const int spacing=400/std::max(1,levels);
    for (int i=0;i<std::min(10,levels);++i) {
        layout.visible[5+i]=true;
        layout.pieces[5+i][1]=uint32_t(440-spacing*(i+1)-10);
    }
    const float denominator=float(levels*1000);
    const int bestY=int(440-float(best)/denominator*400);
    layout.visible[45]=layout.visible[47]=true;
    layout.pieces[45][1]=uint32_t(bestY-8); layout.pieces[47][1]=uint32_t(bestY-16);
    const int meanY=int(440-world.typing.chapterMean()/denominator*400);
    layout.visible[176]=layout.visible[178]=true;
    layout.pieces[176][1]=uint32_t(meanY); layout.pieces[178][1]=uint32_t(meanY+5);
    world.TypingSprite(0xDB5,61,240,alpha,200,0xA,0x00490950);
    if (!showCurrent) return;
    const float height=56+float(std::min<uint64_t>(9999,world.typing.chapterScore().total()))/denominator*400;
    for (int at=0;float(at)<height;at+=32) {
        const float span=std::min(32.f,height-float(at));
        const size_t before=world.hudSprites.size();
        world.TypingSprite(0xDB6,61,496.f-float(at),alpha,.401f,0xE,0x0048FE60);
        if (world.hudSprites.size()==before) continue;
        auto& draw=world.hudSprites.back();
        draw.sx=1; draw.sy=span/512; draw.atlas=true;
        draw.uv[0]=0; draw.uv[1]=float(at)/512;
        draw.uv[2]=1; draw.uv[3]=(float(at)+span)/512;
    }
}
}

void DrawTypingChapterResults(World::Impl& world, int phase, int remaining, float alpha) {
    if (!world.cfg.portTyping || !world.typingReferenceActive) return;
    world.hud.menuPanels.clear();
    world.hud.menuText.clear();
    Layout layout;
    layout.number(2,1,unsigned(std::clamp(world.cfg.eventMode+1,1,6)),kChapterDigits);
    layout.link(180,kAgentLabels,0);
    const auto& score=world.typing.chapterScore();
    const auto& stats=world.typing.chapterStats();
    const auto& ranks=world.typing.scoreBuckets().rankCounts;
    const int age=phase==1 ? 247-remaining : phase>1 ? 247 : -1;
    // Source48FF30/490160: reveal base, rescue, accuracy, then E,D,C,B,A.
    // Category delays are35,35,50,30,30,35,0,0 and are independent of value.
    if (age>=0) layout.number(169,4,score.basePoints,kOrdinaryDigits);
    if (age>=35) {
        layout.number(164,4,score.civilianBonus,kOrdinaryDigits);
        layout.number(175,1,score.civilianBonus/150,kOrdinaryDigits);
    }
    if (age>=70) {
        layout.number(155,4,score.accuracyBonus,kOrdinaryDigits);
        layout.number(159,3,unsigned(stats.accuracy()),kOrdinaryDigits);
    }
    constexpr int reveal[] = {215,215,180,150,120}; // A..E
    for (int rank=0;rank<5;++rank) if (age>=reveal[rank]) {
        const int start=60+rank*10;
        layout.number(start,4,uint64_t(ranks[rank])*TypingRankBonus(TypingRank(rank)),kSmallDigits);
        layout.number(start+5,2,ranks[rank],kSmallDigits);
        layout.number(start+7,2,TypingRankBonus(TypingRank(rank)),kSmallDigits);
    }
    const int totalAge=phase==2 ? 86-remaining : phase>2 ? 86 : -1;
    constexpr int decoration[] = {2,4,6,8,11,15,18,21,23,25,27};
    for (int i=0;i<11;++i) if (totalAge>=decoration[i]) layout.visible[113+i]=true;
    if (totalAge>=55) {
        layout.number(148,4,std::min<uint64_t>(9999,score.total()),kLargeDigits);
        layout.visible[142]=true;
    }
    if (phase==2 && totalAge==55) world.TypingSound(0x000716A9u);
    const int gradeAge=phase==3 ? 407-remaining : phase>3 ? 407 : -1;
    constexpr int gradeDecoration[] = {2,4,6,8,11,16,20,23,25,27,29,31};
    for (int i=0;i<12;++i) if (gradeAge>=gradeDecoration[i]) layout.visible[124+i]=true;
    if (gradeAge>=66) {
        const unsigned ordinal=std::clamp(world.typing.chapterOrdinal(),1u,99u);
        layout.number(184,3,ordinal,kLargeDigits);
        const unsigned suffix=ordinal>10 && ordinal<20 ? 3u : std::min(3u,(ordinal-1)%10);
        layout.link(187,kOrdinal,suffix);
    }
    if (phase==3 && gradeAge==66) world.TypingSound(0x000716A9u);
    ScoreGraph(world,layout,alpha,phase!=0);
    for (size_t i=0;i<layout.pieces.size();++i)
        if (layout.visible[i]) Submit(world,layout.pieces[i],int(i),alpha);
    const size_t before=world.hudSprites.size();
    world.TypingSprite(0,155,115,alpha,.1f,0xA,0x00490830);
    if (world.hudSprites.size()!=before) world.hudSprites.back().sx=world.hudSprites.back().sy=.25f;
}
}
