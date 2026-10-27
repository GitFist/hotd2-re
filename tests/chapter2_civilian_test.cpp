// chapter2_civilian_test <game dir> [rescue delay] [gate delay] [skip offset]
// Follow the escaped-rider route to the actual woman behind the locked gate.
// Original scene_capture_st2_escape.txt: script 0x0056CE44 uses walking motion
// 0x29D until camera 92/0, then 0x19B, 0x297, 0x27C, 0x286 and departure.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "game/world.h"
#include "route_planner.h"

static void Require(bool ok, const char* why, int frame) {
    if (!ok) {
        std::fprintf(stderr, "FAIL frame %d: %s\n", frame, why);
        std::exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::fprintf(stderr, "usage: chapter2_civilian_test <game dir> [rescue delay] [gate delay] [skip offset]\n");
        return 2;
    }
    const int rescueDelay = argc > 2 ? std::atoi(argv[2]) : 0;
    const int gateDelay = argc > 3 ? std::atoi(argv[3]) : 0;
    const int skipOffset = argc > 4 ? std::atoi(argv[4]) : -1;
    Require(rescueDelay >= 0 && gateDelay >= 0, "delays must be nonnegative", 0);
    game::World world;
    std::string error;
    Require(world.Load(argv[1], 1, &error), error.c_str(), 0);
    game::WorldConfig cfg;
    cfg.eventMode = 1;
    cfg.scene = 0;
    cfg.gameMode = 6;
    cfg.portFeatures = true;
    cfg.playerState[0] = 0;
    cfg.seedCounter = 12345;
    cfg.livesSetting = 99;
    Require(world.StartStage(cfg, &error), error.c_str(), 0);
    plan::Planner planner;
    planner.Init();
    uint32_t woman = 0;
    int rescuedBefore = -1, rescueWindow = -1, gateWindow = -1, rescuedAt = -1;
    int lastMotion = -1, motionStart = -1, longestWalk = 0;
    std::set<int> motions;
    float lastPos[3] = {};
    int skippedAt = -1, departedAt = -1;
    bool observedSkip = false;
    for (int frame = 0; frame < 15000 + rescueDelay + gateDelay; ++frame) {
        int rescued = 0, met = 0;
        world.Civilians(&rescued, &met);
        if (skippedAt >= 0 && world.Hud().skipping) observedSkip = true;
        bool womanAlive = false;
        for (const auto& e : world.Enemies(true)) {
            // Actual event record 0x0097FA20, identified by its original pose.
            if (!woman && world.Events().scene == 14 && e.kind == 0x26 && e.motion == 0x18A &&
                std::fabs(e.pos[0] + 994.9f) < 0.1f && std::fabs(e.pos[2] + 1291.4f) < 0.1f) {
                woman = e.serial;
                rescuedBefore = rescued;
            }
            if (!woman || e.serial != woman) continue;
            womanAlive = true;
            Require(!(e.flags34 & 0x4000000), "gate woman survives the rescue timing", frame);
            for (int i = 0; i < 3; ++i) {
                Require(std::isfinite(e.pos[i]), "woman position remains finite", frame);
                lastPos[i] = e.pos[i];
            }
            motions.insert(e.motion);
            if (e.motion != lastMotion) {
                if (lastMotion == 0x29D && frame - motionStart > longestWalk) longestWalk = frame - motionStart;
                lastMotion = e.motion;
                motionStart = frame;
            }
            if (rescueWindow < 0 && world.Hud().canShoot) rescueWindow = frame;
        }
        if (woman && rescued > rescuedBefore && rescuedAt < 0) rescuedAt = frame;
        if (woman && world.Events().scene == 22 && gateWindow < 0) gateWindow = frame;
        if (woman && !womanAlive && departedAt < 0) {
            Require(rescued == rescuedBefore + 1, "this woman adds exactly one rescue", frame);
            Require(gateWindow >= 0, "the rescued branch advances to scene 22", frame);
            Require(skippedAt >= 0 || (motions.count(0x19B) && motions.count(0x297) && motions.count(0x27C) && motions.count(0x286)),
                    "the woman completes her authored rescue and departure animations", frame);
            Require(frame - rescuedAt <= 1800 + gateDelay, "the rescued woman departs within the encounter", frame);
            Require(skipOffset < 0 || observedSkip, "the requested dialogue skip was exercised", frame);
            departedAt = frame;
        }
        if (departedAt >= 0 && world.Events().scene == 23) {
            Require(frame - departedAt <= 600, "gameplay advances after the woman departs", frame);
            std::printf("PASS gate woman: rescue delay %d, gate delay %d, skip %d, rescued %d, departed %d, scene 23 at %d, longest walk %d\n",
                        rescueDelay, gateDelay, skippedAt, rescuedAt, departedAt, frame, longestWalk);
            return 0;
        }
        const uint8_t previous = planner.held;
        const bool waitForRescue = rescueWindow >= 0 && frame < rescueWindow + rescueDelay;
        const bool waitForGate = gateWindow >= 0 && frame < gateWindow + gateDelay;
        plan::Line line{};
        if (skipOffset >= 0 && skippedAt < 0 && !(previous & 2) && gateWindow >= 0 && frame >= gateWindow + skipOffset &&
            !world.Hud().canShoot && world.Hud().skippable) {
            planner.held = 2;
            skippedAt = frame;
        } else if (world.Events().scene == 0 || waitForRescue || waitForGate) planner.held = 0;
        else planner.Decide(world, frame + 1, &line);
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previous));
        world.Step();
    }
    std::fprintf(stderr, "woman %u motion %X pos %.2f %.2f %.2f scene %d camera %d/%d live %d\n", woman, lastMotion,
                 lastPos[0], lastPos[1], lastPos[2], world.Events().scene, world.Events().camera.camId,
                 world.Events().camera.tick, world.LiveEnemies());
    Require(false, "gate woman completes the authored rescue sequence", world.Frame());
}
