// chapter6_encounter_test <game dir>
// Chapter 6: kill the opening three enemies, kill one knife thrower, and let the
// two survivors exhaust and replace their knives before finishing the encounter.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "game/world.h"
#include "route_planner.h"

static void Require(bool ok, const char* why, int victim, int frame) {
    if (!ok) {
        fprintf(stderr, "FAIL victim %d frame %d: %s\n", victim, frame, why);
        std::exit(1);
    }
}

static void CheckEncounter(const char* gameDir, int victim) {
    game::World world;
    std::string error;
    Require(world.Load(gameDir, 5, &error), error.c_str(), victim, 0);
    game::WorldConfig cfg;
    cfg.eventMode = 5;
    cfg.scene = 0;
    cfg.gameMode = 6;
    cfg.portFeatures = true;
    cfg.playerState[0] = 0;
    cfg.seedCounter = 12345;
    // This test deliberately ignores attacks for several cycles.
    cfg.livesSetting = 99;
    Require(world.StartStage(cfg, &error), error.c_str(), victim, 0);
    plan::Planner planner;
    planner.Init();
    std::vector<uint32_t> throwers;
    std::map<uint32_t, std::array<float, 3>> spawn;
    std::map<uint32_t, int> previousBehaviour, rearmStarted, rearmFrames, rearmed, throwsAfterRearming;
    int waitStart = -1;
    bool finishing = false;
    for (int frame = 0; frame < 8500; ++frame) {
        const auto enemies = world.Enemies(true); // Actor 0x31 has a separate task function.
        if (throwers.empty()) {
            for (const auto& e : enemies) {
                if (e.kind != 0x18) continue;
                throwers.push_back(e.serial);
                spawn[e.serial] = {e.pos[0], e.pos[1], e.pos[2]};
            }
            if (!throwers.empty()) {
                Require(throwers.size() == 3 && world.LiveEnemies() == 3,
                        "opening wave cleared and three knife throwers spawned", victim, frame);
            }
        }
        if (!throwers.empty()) {
            for (const auto& e : enemies) {
                if (!spawn.count(e.serial)) continue;
                if (e.serial == throwers[victim]) {
                    if (waitStart < 0 && e.hp <= 0) {
                        Require(world.LiveEnemies() == 2, "exactly one knife thrower killed", victim, frame);
                        waitStart = frame;
                        planner.held = 0;
                    }
                    continue;
                }
                if (waitStart >= 0 && !finishing) {
                    Require(e.hp > 0, "both surviving throwers remain alive while waiting", victim, frame);
                    const auto& origin = spawn[e.serial];
                    const float dx = e.pos[0] - origin[0], dy = e.pos[1] - origin[1], dz = e.pos[2] - origin[2];
                    Require(std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) &&
                            dx * dx + dy * dy + dz * dz < 300.0f * 300.0f,
                            "surviving thrower stays in the encounter instead of running away", victim, frame);
                    rearmFrames[e.serial] = e.behaviour == 30 ? rearmFrames[e.serial] + 1 : 0;
                    Require(rearmFrames[e.serial] <= 180,
                            "knife replacement finishes instead of leaving a survivor stuck", victim, frame);
                    if (previousBehaviour[e.serial] != 30 && e.behaviour == 30) ++rearmStarted[e.serial];
                    if (previousBehaviour[e.serial] == 30 && e.behaviour == 7) ++rearmed[e.serial];
                    if (rearmed[e.serial] && previousBehaviour[e.serial] != 31 && e.behaviour == 31)
                        ++throwsAfterRearming[e.serial];
                }
                previousBehaviour[e.serial] = e.behaviour;
            }
        }
        if (waitStart >= 0 && !finishing) {
            Require(world.LiveEnemies() == 2 && world.GameMode() == 6 && world.Hud().player[0].lives > 0,
                    "waiting preserves the active encounter", victim, frame);
            for (float v : world.Camera().view.m)
                Require(std::isfinite(v), "camera stays finite", victim, frame);
            if (frame - waitStart == 3600) {
                int cycles = 0;
                for (uint32_t serial : throwers) if (serial != throwers[victim]) {
                    // AI can keep one survivor in close-range melee without exhausting its knives.
                    // Every survivor that does exhaust them must finish rearming and throw again.
                    Require(!rearmStarted[serial] || (rearmed[serial] > 0 && throwsAfterRearming[serial] > 0),
                            "each survivor that starts rearming finishes and throws again", victim, frame);
                    cycles += rearmed[serial];
                }
                Require(cycles >= 2, "knife replacement completes across repeated cycles", victim, frame);
                finishing = true;
                planner.blocked.clear();
                planner.misses.clear();
            }
        }
        if (finishing && world.LiveEnemies() == 0 && world.Events().camera.tick > 685) {
            printf("PASS victim %d: repeated rearming, both survivors defeated, camera released at frame %d\n", victim, frame);
            return;
        }
        const uint8_t previousButtons = planner.held;
        plan::Line line{};
        if (waitStart < 0 || finishing) {
            if (!throwers.empty() && !finishing)
                for (uint32_t serial : throwers) if (serial != throwers[victim]) planner.blocked[serial] = 100000;
            planner.Decide(world, frame + 1, &line);
        } else {
            planner.held = 0;
        }
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previousButtons));
        world.Step();
    }
    Require(false, "surviving throwers can still be shot and the chapter advances", victim, world.Frame());
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: chapter6_encounter_test <game dir>\n");
        return 2;
    }
    for (int victim = 0; victim < 3; ++victim) CheckEncounter(argv[1], victim);
}

