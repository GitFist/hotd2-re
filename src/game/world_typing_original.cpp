#include "game/world_typing_original.h"
#include "game/world_internal.h"
#include "game/mstack.h"
#include <algorithm>
#include <string>

namespace game {
namespace {
struct OriginalPickupTask : WTask { int player=0,age=0;std::string text; };
void PickupCaption(Task& task) {
    auto& world=*CurrentWorld();
    auto& item=task.As<OriginalPickupTask>();
    if (!world.cfg.portTyping || ++item.age>120) { world.ts.KillCurrent();return; }
    const bool pair=world.player[0].state==5 && world.player[1].state==5;
    const float x=pair?float(18+320*item.player):18.f;
    world.hud.menuText.push_back({x,374,item.text,.48f,0xFFF3D270});
}
}

uint32_t TypingOriginalSharedFlags(const World::Impl& world) {
    if (!world.cfg.portTyping || !world.cfg.typingOriginal) return 0;
    return world.TypingController(0).original().sourceFlags()|world.TypingController(1).original().sourceFlags();
}

bool TypingOriginalPickup(World::Impl& world,int player,int item) {
    if (!world.cfg.portTyping || !world.cfg.typingOriginal) return false;
    player=std::clamp(player,0,1);
    auto& controller=world.TypingController(player);
    if (item==14 || item==15) {
        // The source costume selection is shared; the most recently collected
        // costume replaces the earlier one even when the other player found it.
        for (int p=0;p<2;++p) world.TypingController(p).original().clearSourceFlags(0xC0);
    }
    std::string text;
    if (controller.collectOriginalItem(item)) {
        if (item>=17) text=std::string(TypingOriginalItemName(static_cast<TypingOriginalItem>(item)))+" STOCKED";
        else {
            switch (item) {
            case 5:text="MEAT";break;
            case 6:text="GENRE DICTIONARY";break;
            case 11:text="ALIEN DICTIONARY";break;
            case 12:text="GOLD";break;
            case 13:text="HAMBURGER";break;
            case 14:text="ZOMBIE";break;
            case 15:text="CITIZEN";break;
            }
        }
    } else if (item==1) {
        auto& actor=world.player[player];
        const auto reward=TypingAddLife(actor.lives);
        actor.lives=int16_t(reward.lives);
        if (reward.basePoints) controller.addBasePoints(reward.basePoints);
        text="LIFE RECOVERY";
    } else if (item==2) {
        // Source54172/5B1B68 gives the frog a zero point award.
        text="FROG";
    } else if (item==3 || item==4) {
        controller.addBasePoints(item==3?50:100);
        text=item==3?"50 POINTS":"100 POINTS";
    } else if (item>=7 && item<=10) {
        constexpr int changes[]={-2,-4,-6,2};
        world.typingSavedRank=world.rank;
        world.typingRankClears+=2;
        controller.original().noteRankEffect();
        world.rank=std::clamp(world.rank+changes[item-7],0,15);
        text=item==10?"EXCITEMENT PILL":"TRANQUILIZER";
    } else return false;
    auto* caption=world.NewTask<OriginalPickupTask>(PickupCaption,0x0040B070);
    caption->player=player;caption->text=std::move(text);
    world.TypingSound(0x003616A9u);
    return true;
}

void TypingOriginalHud(World::Impl& world) {
    if (!world.cfg.portTyping || !world.cfg.typingOriginal || world.gameMode!=6 || world.paused) return;
    if (!world.canShoot || (world.flags5900&0x18) || world.typingResultsActive || world.stageResults.shown) return;
    const bool pair=world.player[0].state==5 && world.player[1].state==5;
    for (int player=0;player<2;++player) {
        if (world.player[player].state!=5) continue;
        const auto& controller=world.TypingController(player);
        const auto& items=controller.original();
        if (world.typingReferenceActive && world.focal>0) {
            // PC4ACB40/4ACA10: four holders, F-key labels and rotating stocked items.
            constexpr int digits[]={0x1B7F,0x1B81,0x1B83,0x1B84};
            constexpr int models[]={7084,7090,7088,7079,7083};
            constexpr float scales[]={.675f,.75f,.525f,.825f,.9f};
            const float unit=-3.f/world.focal;
            const auto model=[&](int raw,float x,float y,float scale,int yaw) {
                const int id=world.LoadTypingModel(world.typingReference.SourceModel(raw));
                if (!id) return;
                Mat4 matrix=Mat4::Identity();
                mx::Translate(matrix,unit*x,unit*y,-3);
                mx::RotY(matrix,yaw);
                mx::Scale(matrix,.04f*scale,.04f*scale,.04f*scale);
                if (auto* draw=world.Draw(id,matrix,8,-1,false,-1,true)) {
                    draw->fn=0x004ACB40;draw->light.table=false;
                    draw->light.pitch=draw->light.yaw=0x2000;
                    for (float& color:draw->light.color) color=1;
                }
            };
            for (int slot=0;slot<4;++slot) {
                const float x=float(267-slot*40-(player?436:0));
                model(0x1BA5,x,-175,1,0x8000);
                const int item=int(items.slots()[slot])-17;
                if (item>=0 && item<5) model(models[item],x+17,-182,scales[item],world.frameCounter2BAC<<8);
                else { model(0x1B9B,x+21,-184,1,0);model(digits[slot],x+11,-184,1,0); }
            }
            continue;
        }
        const float x=pair?float(12+320*player):12.f;
        const float width=pair?304.f:416.f;
        world.hud.menuPanels.push_back({x-4,430,x+width,477,0xB8000000});
        std::string heading=std::string(player?"2P ORIGINAL":"1P ORIGINAL")+
            "  COINS "+std::to_string(controller.originalProgress().coins())+"/30";
        if (items.anyKeyCorrect() || items.invulnerable()) heading+=std::string(items.anyKeyCorrect()?"  HAND ":"  PAIN ")+
            std::to_string((items.remainingFrames()+59)/60)+"s";
        world.hud.menuText.push_back({x,433,std::move(heading),.40f,0xFFF3D270});
        for (unsigned slot=0;slot<4;++slot) {
            world.hud.menuText.push_back({x+float(slot%2)*(width*.5f),447+float(slot/2)*13,
                "F"+std::to_string(slot+1)+": "+std::string(TypingOriginalItemName(items.slots()[slot])),.37f,
                items.slots()[slot]==TypingOriginalItem::None?0xFF909090u:0xFFFFFFFFu});
        }
    }
}
}
