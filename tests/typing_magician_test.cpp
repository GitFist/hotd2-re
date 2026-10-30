#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "game/world_internal.h"

namespace game {
void TypingBoss5Mistype(World::Impl&,uint32_t,int);
bool TypingBoss5(World::Impl&,ActorTask&,TypingTargetSpec&,bool);
}
namespace {
const char* referencePath=nullptr;
void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Fixture {
    std::unique_ptr<game::World::Impl> storage=std::make_unique<game::World::Impl>();
    game::World::Impl& w=*storage;
    game::EnemyTask* boss=nullptr;
    explicit Fixture(const char* directory,bool source=true) {
        w.gameDir=directory;
        Require(w.exe.Load((std::filesystem::path(directory)/"Hod2.exe").string()),"load native scalar and motion tables");
        Require(w.enemyTables.Load(w.exe),"load skeleton metadata");
        w.motionFiles=game::ListMotionFiles(w.exe);
        if (source) {
            std::string error;
            Require(w.typingReference.Load(referencePath,&error),error.c_str());
            Require(w.typingReference.ApplyStage(0,w.mem,w.cameraBank,&error),error.c_str());
        }
        w.cfg.portTyping=source; w.typingReferenceActive=source;
        w.cfg.gameMode=w.gameMode=6; w.cfg.gameType=0;
        w.players=1; w.playersInGame=0; w.route=1;
        w.player[0].state=5; w.player[0].lives=5; w.rank=0;
        w.runner=std::make_unique<game::EventRunner>(w.mem,w,&w.env,&w.exe);
        w.runner->camera.modeCur=2;
        game::SetCurrentWorld(&w);
        auto* task=w.ts.Create([](game::Task& task) {
            game::SpawnRecord record;
            record.type=0x32; record.state=300;
            record.params[0]=0x4B; record.params[2]=1; record.params[0x10]=5;
            record.params[0x19]=8; record.params[0x1C]=8;
            game::CurrentWorld()->SpawnBoss5(task,record);
        });
        tick();
        boss=dynamic_cast<game::EnemyTask*>(task->data.get());
        Require(boss!=nullptr,"Magician factory creates the real boss task");
        for (int i=0;i<160 && boss->behaviour==1;++i) tick();
        Require(boss->behaviour==(source?8:6),"source opens with hopping while native opens with its volley phase");
    }
    ~Fixture() { game::SetCurrentWorld(nullptr); }
    void tick() {
        ++w.seedCounter; w.draws.clear(); w.hudSprites.clear(); w.targets.clear();
        w.ts.RunFrame(); w.ts.FreeDead();
    }
};
}
int main(int argc,char** argv) {
    Require(argc==2 || argc==3,"typing_magician_test <HOD2-game-dir> [prepared-reference-bank]");
    referencePath=argc==3 ? argv[2] : std::getenv("HOTD2_TYPING_REFERENCE");
    Require(referencePath && *referencePath,"supply the prepared reference bank or HOTD2_TYPING_REFERENCE");
    {
        Fixture f(argv[1]);
        auto& b=*f.boss;
        f.tick();
        Require(b.sub==3 && !(b.flags34&0x4100u),"source hopping opens the word and animation window");
        for (int i=0;i<28;++i) f.tick();
        Require(b.sub==3,"source hop still moves on tick29");
        f.tick();
        Require(b.sub==4,"source hop reaches its hold at tick30");
        for (int i=0;i<13;++i) f.tick();
        Require(b.sub==4,"hold includes fourteen updates after its fall-through decrement");
        f.tick();
        Require(b.sub==2,"fifteen hold updates schedule the next hop");

        // A wrong letter ends the current word, finishes the current movement,
        // and charges only the player who made the mistake (PC4B2940/46E233).
        f.w.players=2; f.w.playersInGame=2; f.w.route=2;
        f.w.player[1].state=5; f.w.player[1].lives=5;
        game::TypingBoss5Mistype(f.w,0,0);
        Require(!(b.flags34&0x100u),"stale serial cannot close an unrelated boss window");
        const auto tasks=f.w.ts.Live();
        uint32_t serial=0;
        for (const auto* t:tasks) if (t->data.get()==&b) serial=t->serial;
        game::TypingBoss5Mistype(f.w,serial,1);
        Require(b.flags34&0x100u,"mistype closes the current Magician word immediately");
        for (int i=0;i<35 && b.behaviour!=9;++i) f.tick();
        Require(b.behaviour==9,"mistype skips the remaining hop repetitions");
        f.tick();
        Require((b.flags34&0x10000100u)==0x10000100u,"source charge is invulnerable");
        for (int i=0;i<88;++i) f.tick();
        Require(f.w.player[1].lives==5,"charge does not hit before its ninetieth movement update");
        f.tick();
        Require(f.w.player[0].lives==5 && f.w.player[1].lives==4,"mistype charge hits only its source-selected victim");
    }
    {
        Fixture f(argv[1]); auto& b=*f.boss;
        b.behaviour=10; b.sub=0; b.hp=100;
        f.tick();
        Require(b.sub==21 && (b.typingFlags&0x400u),"final phase hides the body word and begins original instructions");
        for (int i=0;i<178;++i) f.tick();
        Require(b.sub==21,"instructions retain the full 180-call window");
        f.tick();
        Require(b.sub==1,"instruction completion starts the thirty-frame move");
        for (int i=0;i<450 && b.sub!=5;++i) f.tick();
        Require(b.sub==5,"final charged pose reaches the three-orb word window");
        std::set<uint32_t> small;
        for (auto* t:f.w.ts.Live()) {
            auto* shot=dynamic_cast<game::EnemyTask*>(t->data.get());
            if (!shot || shot->fn!=0x0047EFA0 || shot->typingFlags!=0x38000004u) continue;
            small.insert(t->serial);
            game::TypingTargetSpec spec;
            Require(game::TypingBoss5(f.w,*shot,spec,true) && spec.node==1,"small orb accepts its completed word");
        }
        Require(small.size()==3,"PC final volley contains exactly three typeable small orbs");
        for (int i=0;i<4;++i) f.tick();
        Require(b.sub==5 && b.hp==100,"orb completion retains the PC five-update destruction delay");
        f.tick(); f.tick();
        Require(b.sub==8 && b.hp==100,"three small words unlock the large word without damaging the body");
        for (int i=0;i<240 && b.sub!=10;++i) f.tick();
        Require(b.sub==10,"large-orb charged pose reaches its completion window");
        game::EnemyTask* large=nullptr;
        for (auto* t:f.w.ts.Live()) {
            auto* shot=dynamic_cast<game::EnemyTask*>(t->data.get());
            if (shot && shot->fn==0x0047EFA0 && shot->typingFlags==0x38000004u && !small.contains(t->serial)) large=shot;
        }
        Require(large!=nullptr,"second pose creates the single large typeable orb");
        game::TypingTargetSpec spec;
        Require(game::TypingBoss5(f.w,*large,spec,true) && spec.node==1,"large orb accepts its completed word");
        for (int i=0;i<7;++i) f.tick();
        Require(b.hp==80,"large-word completion schedules exactly twenty solo boss damage");
    }
    {
        Fixture f(argv[1]);
        f.w.ts.Reset();
        auto* task=f.w.ts.Create([](game::Task& task) {
            game::SpawnRecord record;
            record.type=0x22; record.state=300; record.params[1]=1;
            game::CurrentWorld()->SpawnBoss22(task,record);
        });
        f.tick();
        auto* judgment=dynamic_cast<game::Actor22Task*>(task->data.get());
        Require(judgment!=nullptr,"Judgment factory creates its linked skeletons");
        judgment->behaviour=2; judgment->sub=0;
        judgment->flags34=1; judgment->pos[1]=0;
        f.tick();
        Require(judgment->sub==1 && judgment->pos[1]==0 && (judgment->flags34&0x100u),
                "source second phase delays its first rise update and suppresses the word outside its approach");
        for (int i=0;i<300 && judgment->sub!=14;++i) f.tick();
        Require(judgment->sub==14,"chapter-one Judgment starts the additional PC phase instruction window");
        for (int i=0;i<179;++i) f.tick();
        Require(judgment->sub==14,"Judgment instruction window lasts through update179");
        f.tick();
        Require(judgment->sub==7,"Judgment instructions precede the three-second countdown");
        for (int i=0;i<180;++i) f.tick();
        Require(judgment->sub==7 && (judgment->flags34&0x100u),"countdown retains its frame-zero display before launching");
        f.tick();
        Require(judgment->sub==3 && !(judgment->flags34&0x100u),"only Judgment's actual approach opens the source word window");
    }
    {
        Fixture f(argv[1]);
        f.w.ts.Reset();
        f.w.runner->scene=0xB;
        auto* task=f.w.ts.Create([](game::Task& task) {
            game::SpawnTask spawn;
            spawn.op=0x09; spawn.rec.type=0x45; spawn.rec.params[1]=5;
            game::CurrentWorld()->SpawnHydra(task,spawn);
        });
        f.tick(); f.tick();
        auto* tower=dynamic_cast<game::EnemyTask*>(task->data.get());
        Require(tower && tower->hp==120,"swimming Tower factory retains its original 120 HP");
        tower->behaviour=0xB; tower->flags34|=0xAu;
        tower->hitNode[0]=24; tower->typingStrike=true;
        f.tick();
        Require(tower->hp==96 && tower->behaviour==10,
                "one swimming Tower word deals twenty-four damage and ends that rise immediately");
    }
    { Fixture native(argv[1],false); native.tick(); Require(native.boss->behaviour==6,"native gun Magician retains its original phase"); }
    std::puts("typing_magician_test: OK");
}
