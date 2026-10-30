#include "game/world_typing_drill_name.h"
#include "game/world_typing_drill_result_visuals.h"
#include "game/world_internal.h"
#include "game/menu_text.h"

#include <algorithm>
#include <cmath>

namespace game {
namespace {
bool Character(unsigned char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
        std::string_view("\\.,?!_[]-% '$#@\"&/():*").find(char(c))!=std::string_view::npos;
}
void Sprite(World::Impl& world,int id,float x,float y,float depth,float sx=1,float sy=1,
            float alpha=1,uint32_t flags=5) {
    const size_t before=world.hudSprites.size();
    world.TypingSprite(id,x,y,alpha,depth,flags,0x00498250);
    if (world.hudSprites.size()!=before) {
        world.hudSprites.back().sx=sx;
        world.hudSprites.back().sy=sy;
    }
}
void Glyph(World::Impl& world,unsigned char c,int index,float alpha) {
    if (c<32 || c>127) return;
    const int slot=c-32;
    const float x=320+world.focal*(float(index)*.11599999666213989f-.2200000137090683f)/3;
    const float y=240-world.focal*.04399999976158142f/3;
    const float a=8000.f/(8000.f-.5f),z=a-.5f*a/3;
    const size_t before=world.hudSprites.size();
    world.TypingSprite(0xF74+slot,x,y,alpha,.9f/(1-z),0x200A,0x00498578);
    if (world.hudSprites.size()==before) return;
    auto& draw=world.hudSprites.back();
    const float pixels=world.focal*.15f/3;
    draw.sx=pixels/512; draw.sy=pixels/256;
    draw.atlas=true; draw.tinted=true;
    draw.tint[0]=.20392157137393951f;
    draw.tint[1]=.19607843458652496f;
    draw.tint[2]=.35294118523597717f;
    draw.uv[0]=float((slot%16)*32)/512;
    draw.uv[1]=float((slot/16+1)*32)/256;
    draw.uv[2]=draw.uv[0]+32.f/512;
    draw.uv[3]=draw.uv[1]-32.f/256;
}
void Draw(World::Impl& world,const TypingDrillNameUi& ui) {
    // Source0x498250 popup art is shared by Drill and the final report.
    Sprite(world,0xE36,0,0,30.185001f,80,60,.5f);
    Sprite(world,0xBC6,320,240,10.185001f,1,1,1,0xA);
    Sprite(world,0xBC7,192,237,10.183f);
    if (ui.confirmation) {
        Sprite(world,0xBCA,192,187,10.183f);
        Sprite(world,0xBCD,289,242,10.183f);
        Sprite(world,0xBC9,288,268,10.183f);
        Sprite(world,0xBCC,258,ui.confirmation==1?243.f:270.f,10.184f);
    } else {
        Sprite(world,0xBC8,192,178,10.183f);
        Sprite(world,0xBCB,192,277,10.183f);
        Sprite(world,0xBBC,256,254,10.183f);
        Sprite(world,0xBBD,357,261,10.183f);
    }
    for (size_t i=0;i<ui.name.size();++i) Glyph(world,static_cast<unsigned char>(ui.name[i]),int(i),1);
    if (!ui.confirmation && ui.name.size()<5) {
        const int pulse=std::abs(20-int(world.frameCounter2BAC%40));
        Glyph(world,'_',int(ui.name.size()),.19f+float(pulse)*.04f);
    }
    if (ui.saveFailed) {
        constexpr std::string_view message="CANNOT SAVE NAME - ENTER RETRIES";
        world.hud.menuText.push_back({180,315,std::string(message),.45f,0xFFFFFFFF});
        MenuTextScaled(world.exe,180,309.6f,std::string(message),0,.45f,world.hudSprites);
    }
}
} // namespace

void BeginTypingDrillName(World::Impl& world) {
    auto& ui=world.typingDrill.nameUi;
    ui={};
    const auto& controller=world.typingDrill.controller;
    ui.active=controller.result().original && !controller.supplementary() && controller.recordRank()>=0;
}
bool TypingDrillNameActive(const World::Impl& world) {
    return world.gameMode==4 && world.front.nightShiftPage==12 && world.typingDrill.view==4 &&
        world.typingDrill.nameUi.active;
}
bool QueueTypingDrillName(World::Impl& world,std::string_view text) {
    if (!TypingDrillNameActive(world)) return false;
    auto& pending=world.typingDrill.nameUi.text;
    if (pending.size()<64) pending.append(text.substr(0,64-pending.size()));
    return true;
}
bool TypingDrillNameFrame(World::Impl& world,uint32_t buttons) {
    auto& ui=world.typingDrill.nameUi;
    if (!ui.active) {TypingDrillResultsVisuals(world);return true;}
    const bool enter=(buttons&0xCu)!=0 || ui.text.find('\r')!=std::string::npos;
    const bool escape=(buttons&2u)!=0 || ui.text.find('\x1b')!=std::string::npos;
    if (ui.confirmation) {
        if (buttons&0x50u) { ui.confirmation=1;ui.saveFailed=false;world.TypingSound(0x003E16A9u); }
        if (buttons&0xA0u) { ui.confirmation=2;ui.saveFailed=false;world.TypingSound(0x003E16A9u); }
        if (escape || (enter && ui.confirmation==2)) {
            ui.confirmation=0;ui.saveFailed=false;
            world.TypingSound(0x000500A9u);
        } else if (enter) {
            if (world.typingDrill.controller.setOriginalRecordName(ui.name)) {
                ui.active=false;ui.text.clear();
                world.typingDrill.bookUi.nameDone=true;
                world.TypingSound(0x003016A9u);
                TypingDrillResultsVisuals(world);
                return true;
            }
            ui.saveFailed=true;
        }
    } else {
        for (unsigned char c:ui.text) {
            if (c=='\b') {
                if (!ui.name.empty()) ui.name.pop_back();
                world.TypingSound(0x000936A9u);
            } else if (Character(c) && ui.name.size()<5) {
                ui.name+=char(c);
                world.TypingSound(0x000736A9u);
            }
        }
        // The original opens confirmation on No, preserving the edit buffer.
        if (enter || escape) { ui.confirmation=2;world.TypingSound(0x000536A9u); }
    }
    ui.text.clear();
    TypingDrillResultsVisuals(world);
    if(ui.confirmation || ui.saveFailed)Draw(world,ui);
    return false;
}
} // namespace game
