// strength_audio_test <game dir>: play both Strength fights and preserve their authored cue timing.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/world.h"
#include "game/world_internal.h"
#include "route_planner.h"

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

// Exercise the real shared-group creation/death callbacks rather than injecting
// audio events: one motor starts for two Max enemies and stops after the last.
static void RegularMotorGroup(const char* dir,int kind,bool port,bool typing,uint32_t on,uint32_t off,int startKind) {
    auto w=std::make_unique<game::World::Impl>();
    Require(w->exe.Load((std::filesystem::path(dir)/"Hod2.exe").string()),"load original tables for regular motor group");
    Require(w->MotionTicks(0x3BC)>1,"regular motor fixture must remain within its active death callback");
    w->runner=std::make_unique<game::EventRunner>(w->mem,*w,&w->env,&w->exe);
    w->cfg.portFeatures=port;w->cfg.portTyping=typing;w->cfg.gameType=3;w->gameMode=6;
    w->liveEnemies=w->attackers=2;
    game::EnemyTask first,second;
    for(auto* enemy:{&first,&second}) {
        enemy->anim.kind=kind;enemy->anim.motion[0]=0x3BC;enemy->sub=1;
        w->EnemySetupKind2(*enemy);
    }
    Require(w->GroupCount()==2 && first.c131B==1 && second.c131B==1,"real motor setup counted both living enemies");
    Require(w->sounds.size()==1 && w->sounds[0].id==on && w->sounds[0].kind==startKind,
            "first group member emits one intended motor start and second member does not restart it");
    w->EnemyBeh6(first);
    Require(w->GroupCount()==1 && first.c131B==0 && second.c131B==1 && w->sounds.size()==1,
            "first real death leaves the surviving enemy's shared motor running");
    w->EnemyBeh6(second);
    Require(w->GroupCount()==0 && second.c131B==0 && w->sounds.size()==2 && w->sounds[1].id==off &&
                w->sounds[1].kind==game::World::kPlayGameSound,
            "last real death emits the existing generic motor-off cue once");
    w->EnemyBeh6(second);
    Require(w->GroupCount()==0 && w->sounds.size()==2,"later death ticks neither repeat stop nor underflow the shared group");
}

static std::vector<std::pair<int, uint32_t>> Fight(const char* dir, int scene, bool port) {
    game::World world;
    std::string error;
    Require(world.Load(dir, 3, &error), error.c_str());
    game::WorldConfig cfg;
    cfg.eventMode = 3;
    cfg.scene = scene;
    cfg.gameMode = 6;
    cfg.gameType = 3;
    cfg.portFeatures = port;
    cfg.playerState[0] = 0;
    cfg.seedCounter = 12345;
    cfg.livesSetting = 99;
    world.Boot(cfg);  // initialize boss-mode state without a writable save path
    Require(world.StartStage(cfg, &error), error.c_str());
    plan::Planner planner;
    planner.Init();
    std::vector<std::pair<int, uint32_t>> cues;
    bool seenBoss = false;
    int starts = 0, stops = 0;
    for (int frame = 0; frame < 10000; ++frame) {
        bool hasBoss = false;
        for (const auto& task : world.Tasks()) if (task.fn == 0x004919D0) hasBoss = true;
        seenBoss |= hasBoss;
        for (const auto& sound : world.Sounds()) {
            if (sound.id != 0x4D17A9 && sound.id != 0x4E17A9) continue;
            cues.emplace_back(frame, sound.id);
            if (sound.id == 0x4D17A9) {
                ++starts;
                Require(sound.kind == (port ? game::World::kStrengthChainsawSound : game::World::kPlayGameSound),
                        "client Strength uses Dreamcast looping; original capture path retains its sound event");
            } else ++stops;
        }
        if (seenBoss && !hasBoss) {
            Require(starts > 1 && stops > 1, "fight exercises repeated chainsaw cues and quiet intervals");
            std::printf("Strength scene %d %s: defeated at frame %d, %d starts / %d stops\n",
                        scene, port ? "client" : "original", frame, starts, stops);
            return cues;
        }
        const uint8_t previous = planner.held;
        plan::Line input{};
        planner.Decide(world, frame + 1, &input);
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previous));
        world.Step();
    }
    std::fprintf(stderr, "scene %d port %d: mode %d scene %d camera %d/%d boss %d live %d starts %d stops %d\n",
                 scene, port, world.GameMode(), world.Events().scene, world.Events().camera.camId,
                 world.Events().camera.tick, seenBoss, world.LiveEnemies(), starts, stops);
    for (const auto& enemy : world.Enemies())
        std::fprintf(stderr, "kind %X hp %d motion %X tick %d\n", enemy.kind, enemy.hp, enemy.motion, enemy.tick);
    Require(false, "Strength fight completes within the frame limit");
    return cues;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: strength_audio_test <game dir>\n"); return 2; }
    RegularMotorGroup(argv[1],2,true,false,0x4D17A9,0x4E17A9,game::World::kStrengthChainsawSound);
    RegularMotorGroup(argv[1],2,false,false,0x4D17A9,0x4E17A9,game::World::kPlayGameSound);
    RegularMotorGroup(argv[1],2,true,true,0x4D17A9,0x4E17A9,game::World::kPlayGameSound);
    RegularMotorGroup(argv[1],3,true,false,0x1F25A9,0x2025A9,game::World::kPlayGameSound);
    std::puts("PASS regular Max motor group start/last-death stop; stock, Typing and other shared-group cues retained");
    for (int scene : {27, 29}) {
        const auto original = Fight(argv[1], scene, false);
        const auto client = Fight(argv[1], scene, true);
        Require(client == original, "audio correction preserves both fights' original cue timing and progression");
    }
    std::puts("PASS: both Strength fights use Dreamcast looping with unchanged cue timing");
}
