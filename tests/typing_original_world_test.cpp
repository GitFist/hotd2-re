#include "game/world_internal.h"
#include "game/world_typing_original.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
void Require(bool condition,const char* why) {
    if (!condition) {std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
void Idle(game::Task&) {}
void Setup(game::World::Impl& world) {
    world.cfg.portTyping=world.cfg.typingOriginal=true;
    world.gameMode=6;world.canShoot=1;world.typingPropsEnabled=true;
    world.runner=std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
    world.runner->camera.modeCur=2;
    for (auto& p:world.player) {p.state=5;p.lives=3;}
    world.ResetTyping();
}
game::ActorTask* Prop(game::World::Impl& world,int item) {
    auto* prop=world.NewTask<game::ActorTask>(Idle,0x00468F00);
    game::SpawnRecord record;record.type=0x41;record.params[0]=uint8_t(item);record.params[1]=19;
    world.SetTypingSpawnOrigin(*prop,record);
    prop->viewPos[2]=-10;
    world.RegisterTarget(prop);
    return prop;
}
std::vector<game::ActorTask*> Axes(game::World::Impl& world) {
    game::EnemyTask johnny;johnny.anim.kind=0x13;johnny.variant=7;johnny.target=1;
    johnny.anim.nodes[5].model=0x1ECE;johnny.anim.nodes[5].hit[2]=-10;
    world.SpawnThrownWeapon(johnny,5);world.SpawnThrownWeapon(johnny,5);
    std::vector<game::ActorTask*> axes;
    for (auto* task:world.ts.Live()) if (auto* actor=dynamic_cast<game::ActorTask*>(task->data.get());
        actor && actor->fn==0x0045A4F0) {
        actor->viewPos[2]=-10;world.RegisterTarget(actor);axes.push_back(actor);
    }
    Require(axes.size()==2,"real Johnny throw path creates two registered source axes");
    return axes;
}
void PickupAndPain() {
    game::World::Impl world;Setup(world);
    auto* prop=Prop(world,21);
    Require(prop->typingPickupItem==21 && prop->typingFlags==8,
            "source prop P0 is retained separately from its registered typing flags");
    world.TypingStep();
    Require(world.typing2.prompts().size()==1,"P2 sees the authored source pickup word");
    world.typingCharacters2=world.typing2.prompts().front().text;
    world.TypingStep();
    Require(world.typing.original().slots()[0]==game::TypingOriginalItem::None &&
            world.typing2.original().slots()[0]==game::TypingOriginalItem::PainKiller &&
            prop->typingPickupItem==0 && prop->typingRevision==1,
            "P2 completing a real prop receives its stocked item exactly once with P2 hit ownership");
    Require((prop->flags34&4u) && !(prop->flags34&2u),"the pickup receives the real P2 hit bit");
    world.typingItemKeys[1]=1;world.TypingStep();
    Require(world.typing2.original().invulnerable() && !world.typing.original().invulnerable(),
            "P2 F1 consumes P2's slot and activates only P2 Pain Killer");
    world.PlayerTakeHit(1,0,0);world.PlayerTakeHit(0,0,0);
    Require(world.player[1].lives==3 && world.player[0].lives==2,
            "the native damage path blocks protected P2 and still damages unprotected P1");
    world.typing2.update(15,true);world.PlayerTakeHit(1,0,0);
    Require(world.player[1].lives==2,"native damage resumes after the exact active-time item duration");
}
void OffensiveItems() {
    for (const bool all:{false,true}) {
        game::World::Impl world;Setup(world);
        const auto axes=Axes(world);
        const int item=all?19:20;
        Require(game::TypingOriginalPickup(world,1,item),"authored offensive item enters P2 inventory");
        world.TypingStep();world.typingItemKeys[1]=1;world.TypingStep();
        unsigned hit=0;
        for (const auto* axe:axes) if (axe->typingRevision) {
            ++hit;Require((axe->flags34&4u) && !(axe->flags34&2u) && axe->typingPlayer==1,
                          "offensive item delivers its real native P2 hit through the target adapter");
        }
        Require(hit==(all?2u:1u),"Molotov hits every eligible actor while sulphuric acid hits one");
        Require(world.typing.stats().completedWords==0 && world.typing2.stats().completedWords==0,
                "offensive item hits cannot fabricate typed-word statistics");
    }
    game::World::Impl empty;Setup(empty);
    game::TypingOriginalPickup(empty,0,20);empty.typingItemKeys[0]=1;empty.TypingStep();
    Require(empty.typing.original().slots()[0]==game::TypingOriginalItem::SulphuricAcid,
            "an F-key with no eligible target preserves the stocked offensive item");
}
void OriginalWorldCosmetics(const char* dir,const char* bank) {
    game::World::Impl world;Setup(world);
    Require(world.exe.Load((std::filesystem::path(dir)/"Hod2.exe").string()),"load native skeleton tables");
    std::string error;
    Require(world.typingReference.Load(bank,&error,false),error.c_str());
    world.typingReferenceActive=true;world.residency=true;world.modelLoaded.assign(0x8000,0);
    game::TypingOriginalPickup(world,0,14);
    game::EnemyTask zombie;zombie.anim.kind=0x39;zombie.anim.motion[0]=0x319;
    world.EnemyInitPose(zombie);
    Require(zombie.anim.kind==7,"actual skeleton initializer applies the source zombie-agent costume");
    const int mapped=world.typingReference.NativeEnemyModel(7,1,zombie.anim.nodes[1].model);
    Require(mapped>0 && world.modelLoaded[mapped],"changed costume's actual prepared body model becomes resident");
    game::TypingOriginalPickup(world,0,15);
    game::EnemyTask human;human.anim.kind=0x3A;human.anim.motion[0]=0x322;
    world.EnemyInitPose(human);
    Require(human.anim.kind==0x21,"actual skeleton initializer applies the source civilian-agent costume");
    game::TypingOriginalPickup(world,1,5);
    Require((game::TypingOriginalSharedFlags(world)&0x82)==0x82,
            "world presentation sees the active source Meat and civilian-costume flags across both players");
    game::TypingOriginalPickup(world,1,14);
    Require((game::TypingOriginalSharedFlags(world)&0xC0)==0x40,
            "the other player's later costume pickup replaces the shared previous costume");
    world.hud.menuText.clear();world.hud.menuPanels.clear();
    game::TypingOriginalHud(world);
    Require(world.hud.menuPanels.size()==2 && std::all_of(world.hud.menuPanels.begin(),world.hud.menuPanels.end(),
        [](const auto& panel){return panel.top>=430;}),"both inventories occupy the footer below source life icons");
}
}
int main(int argc,char** argv) {
    if (argc<3) {std::fprintf(stderr,"usage: typing_original_world_test <native-game-dir> <prepared-bank>\n");return 2;}
    PickupAndPain();OffensiveItems();OriginalWorldCosmetics(argv[1],argv[2]);
    std::puts("PASS: Original source pickup ownership, function-key effects, native damage, costumes and inventory HUD");
}
