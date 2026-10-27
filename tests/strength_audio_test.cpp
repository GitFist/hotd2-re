// strength_audio_test <game dir>: play both Strength fights and preserve their authored cue timing.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "game/world.h"
#include "route_planner.h"

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
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
    for (int scene : {27, 29}) {
        const auto original = Fight(argv[1], scene, false);
        const auto client = Fight(argv[1], scene, true);
        Require(client == original, "audio correction preserves both fights' original cue timing and progression");
    }
    std::puts("PASS: both Strength fights use Dreamcast looping with unchanged cue timing");
}
