#include "game/world_typing_drill_hud.h"
#include "game/world_internal.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

// Source 0x0058952C maps the menu's exercise order to the instruction
// subtitles. These entries follow the native controller's semantic order.
constexpr std::array<int,13> kSubtitle = {
    0x2AF,0x2B0,0x2B1,0x2A9,0x2AA,0x2AB,
    0x2A5,0x2A6,0x2A7,0x2AC,0x2AD,0x2AE,0x2A8
};

void Sprite(World::Impl& w,int source,float x,float y,float depth=.1f,float alpha=1) {
    SpriteDef d;
    d.id=0x4000+source;
    d.x=x;d.y=y;d.depth=depth;d.alpha=alpha;
    d.flags=5;
    d.source=0x004A85A0;
    w.hudSprites.push_back(d);
}

// Original Spr_DrawNumber 0x00493280, with the same fixed-width leading zeros.
void Number(World::Impl& w,unsigned value,int digits,int first,float x,float y,float spacing) {
    unsigned divisor=1;
    for(int i=1;i<digits;++i)divisor*=10;
    for(int i=0;i<digits;++i) {
        const unsigned digit=std::min(9u,value/divisor);
        Sprite(w,first+int(digit),x+spacing*i,y);
        value%=divisor;
        divisor=std::max(1u,divisor/10);
    }
}

void Time(World::Impl& w,double seconds) {
    // Source 0x004A891C: two digits each for minutes, seconds, centiseconds;
    // the original m/s punctuation occupies its own 32x64 sprites.
    const unsigned ticks=unsigned(std::clamp(std::llround(seconds*60),0ll,359999ll));
    const unsigned minutes=ticks/3600,whole=(ticks%3600)/60,hundredths=(ticks%60)*100/60;
    Number(w,minutes,2,0xE97,220,16,24);
    Sprite(w,0xEA5,268,16);
    Number(w,whole,2,0xE97,292,16,24);
    Sprite(w,0xEA6,340,16);
    Number(w,hundredths,2,0xE97,364,16,24);
}

void PromptCount(World::Impl& w,unsigned count,bool remaining,int exercise) {
    // Source counter variants 13..17 at 0x004A873A: Rest, three small
    // digits, multiplication mark, and the exercise's original target icon.
    // Open-ended native exercises report completed prompts, so omit Rest.
    if(remaining) Sprite(w,0xE80,232,76);
    Number(w,std::min(999u,count),3,0xE72,285,75,24);
    Sprite(w,0xE7F,365,75);
    int icon=0xE82; // zombie / typed target
    if(exercise==3) icon=0xE7D; // Reflex1: barrels
    else if(exercise==9) icon=0xE7C; // Special key1: axes
    else if(exercise==10) icon=0xE7E; // Special key2: frogs
    Sprite(w,icon,382,75);
}

} // namespace

bool TypingDrillHudVisuals(World::Impl& w) {
    if(!w.typingReference.Available() || !w.typingReference.SourceSprite(0x4E97)) return false;
    const auto& ui=w.typingDrill;
    const auto& drill=ui.controller;
    const int exercise=drill.exercise();
    if(exercise<0 || exercise>=int(kSubtitle.size())) return false;
    Time(w,drill.remaining());

    // The title artwork and its position come from the original instruction
    // layout at 0x005E4AB8 / renderer 0x004A8240. Briefly retain that heading
    // over the scene while native practice continues; no opaque page hides it.
    if(ui.visualAge<180) {
        const float alpha=ui.visualAge<150?1.f:float(180-ui.visualAge)/30;
        Sprite(w,kSubtitle[size_t(exercise)],-40,69,1,alpha);
    } else {
        const unsigned quota=drill.specification().quota;
        const unsigned completed=drill.result().words;
        PromptCount(w,quota?quota-std::min(quota,completed):completed,quota!=0,exercise);
    }
    return true;
}

} // namespace game
