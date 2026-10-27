// chapter2_ryan_test <game dir>
// Leave Chapter 2 scene 5's Ryan pair (records 0x009794E8 / 0x00979520) alive long
// enough to exhaust and replace their weapons, then defeat them through ordinary gun input.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "game/world.h"
#include "route_planner.h"

static void Require(bool ok, const char* why, int frame) {
    if (!ok) {
        fprintf(stderr, "FAIL frame %d: %s\n", frame, why);
        std::exit(1);
    }
}

struct Ryan {
    std::array<float, 3> spawn{};
    int previousBehaviour = -1;
    int rearmFrames = 0, longestRearm = 0;
    int rearmStarted = 0, rearmed = 0, throwsAfterRearming = 0;
    float farthest = 0;
};

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: chapter2_ryan_test <game dir>\n");
        return 2;
    }
    constexpr int scene = 5;
    game::World world;
    std::string error;
    Require(world.Load(argv[1], 1, &error), error.c_str(), 0);
    game::WorldConfig cfg;
    cfg.eventMode = 1;
    cfg.scene = 0;  // Follow the real approach so models, collision and route flags are initialized.
    cfg.gameMode = 6;
    cfg.portFeatures = true;
    cfg.playerState[0] = 0;
    cfg.seedCounter = 12345;
    cfg.livesSetting = 99;  // Intentionally ignore attacks for a minute.
    Require(world.StartStage(cfg, &error), error.c_str(), 0);
    plan::Planner planner;
    planner.Init();
    std::map<uint32_t, Ryan> ryans;
    int waitStart = -1;
    int previousScene = -1;
    bool finishing = false;
    for (int frame = 0; frame < 18000; ++frame) {
        const auto enemies = world.Enemies(true);
        if (world.Events().scene != previousScene) {
            printf("frame %d scene %d variant %d live %d\n", frame, world.Events().scene,
                   world.Events().variant, world.LiveEnemies());
            previousScene = world.Events().scene;
        }
        int liveRyans = 0;
        for (const auto& e : enemies) if (e.kind == 0x16 && e.hp > 0) ++liveRyans;
        const bool atPair = world.Events().scene == scene && liveRyans == 2;
        if (ryans.empty() && atPair) {
            Require(world.LiveEnemies() == 2, "the reported Ryan pair is the active encounter", frame);
            for (const auto& e : enemies) if (e.kind == 0x16 && e.hp > 0)
                ryans[e.serial].spawn = {e.pos[0], e.pos[1], e.pos[2]};
            waitStart = frame;
            printf("waiting at frame %d scene %d variant %d\n", frame, scene, world.Events().variant);
        }
        if (waitStart >= 0 && !finishing) {
            Require(world.LiveEnemies() == 2 && world.GameMode() == 6 && world.Hud().player[0].lives > 0,
                    "both Ryans remain alive while the player waits", frame);
            for (const auto& e : enemies) {
                auto it = ryans.find(e.serial);
                if (it == ryans.end()) continue;
                Ryan& r = it->second;
                Require(e.hp > 0, "waiting does not defeat either Ryan", frame);
                const float dx = e.pos[0] - r.spawn[0], dy = e.pos[1] - r.spawn[1], dz = e.pos[2] - r.spawn[2];
                const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                Require(std::isfinite(distance), "Ryan position remains finite", frame);
                r.farthest = std::max(r.farthest, distance);
                r.rearmFrames = e.behaviour == 29 ? r.rearmFrames + 1 : 0;
                r.longestRearm = std::max(r.longestRearm, r.rearmFrames);
                if (r.previousBehaviour != 29 && e.behaviour == 29) ++r.rearmStarted;
                if (r.previousBehaviour == 29 && e.behaviour == 7) ++r.rearmed;
                if (r.rearmed && r.previousBehaviour != 31 && e.behaviour == 31) ++r.throwsAfterRearming;
                r.previousBehaviour = e.behaviour;
            }
            for (float v : world.Camera().view.m)
                Require(std::isfinite(v), "camera remains finite", frame);
            if (frame - waitStart == 3600) {
                int cycles = 0;
                // Report the whole idle interval before asserting so a regression
                // records the runaway distance as well as the unfinished rearm.
                for (const auto& [serial, r] : ryans) {
                    printf("Ryan %u: farthest %.1f, longest rearm %d, starts %d, completed %d, later throws %d\n",
                           serial, r.farthest, r.longestRearm, r.rearmStarted, r.rearmed, r.throwsAfterRearming);
                }
                for (const auto& [serial, r] : ryans) {
                    Require(r.farthest < 300.0f, "Ryan stays in the encounter instead of walking away", frame);
                    Require(r.longestRearm <= 60, "weapon replacement finishes promptly", frame);
                    Require(!r.rearmStarted || (r.rearmed > 0 && r.throwsAfterRearming > 0),
                            "every Ryan who starts rearming completes it and throws again", frame);
                    cycles += r.rearmed;
                }
                Require(cycles >= 2, "weapon replacement completes across repeated cycles", frame);
                finishing = true;
            }
        }
        if (finishing && world.Events().scene != scene) {
            for (const auto& e : enemies)
                Require(!ryans.count(e.serial) || e.hp <= 0, "both Ryans are defeated before scene advance", frame);
            printf("PASS: both Ryans rearm while waiting, remain shootable, and advance scene %d to %d at frame %d\n",
                   scene, world.Events().scene, frame);
            return 0;
        }
        const uint8_t previousButtons = planner.held;
        plan::Line line{};
        if (finishing || (waitStart < 0 && !atPair)) planner.Decide(world, frame + 1, &line);
        else planner.held = 0;
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previousButtons));
        world.Step();
    }
    fprintf(stderr, "ending scene %d variant %d live %d, game mode %d lives %d\n", world.Events().scene,
            world.Events().variant, world.LiveEnemies(), world.GameMode(), world.Hud().player[0].lives);
    for (const auto& note : world.Notes()) fprintf(stderr, "note: %s\n", note.c_str());
    Require(false, waitStart < 0 ? "the reported Ryan pair spawns" : "both Ryans can be defeated and the scene advances", world.Frame());
}
