// chapter2_room_test GAME
// Chapter 2 scene 17: save the left-hand civilian, shoot the right-hand civilian,
// then clear the room. Reach the room through its real failed-rescue route.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include "game/world.h"
#include "route_planner.h"

static void Require(bool ok, const char* why, int frame) {
    if (!ok) { std::fprintf(stderr, "FAIL frame %d: %s\n", frame, why); std::exit(1); }
}

static void CheckRoom(const char* directory, bool shootRight, bool lastEnemy, int leftDelay) {
    game::World world;
    std::string error;
    Require(world.Load(directory, 1, &error), error.c_str(), 0);
    game::WorldConfig cfg;
    cfg.eventMode = 1; cfg.scene = 0; cfg.gameMode = 6;
    cfg.portFeatures = true; cfg.playerState[0] = 0;
    cfg.seedCounter = 12345; cfg.livesSetting = 99;
    Require(world.StartStage(cfg, &error), error.c_str(), 0);
    plan::Planner planner; planner.Init();
    uint32_t right = 0, left = 0;
    int entered = -1, rightDied = -1, leftSeen = -1, rescuedBefore = -1;
    bool rightVisible = false, leftDied = false;
    for (int frame = 0; frame < 14000; ++frame) {
        const int scene = world.Events().scene;
        int rescued = 0, met = 0; world.Civilians(&rescued, &met);
        if (scene == 17 && entered < 0) { entered = frame; rescuedBefore = rescued; }
        if (scene == 17) for (const auto& e : world.Enemies(true)) {
            if (!left && e.kind == 0x27 && std::fabs(e.pos[0] + 625.0f) < 0.1f) {
                left = e.serial; leftSeen = frame;
            }
            if (!right && e.kind == 0x20 && std::fabs(e.pos[0] + 600.9f) < 0.1f)
                right = e.serial;
            if (left && e.serial == left) leftDied |= (e.flags34 & 0x4000000) != 0;
            if (!right || e.serial != right) continue;
            if ((e.flags34 & 0x4000000) && rightDied < 0) rightDied = frame;
            if (world.Hud().canShoot && e.viewPos[2] < -1) {
                const float x = 320 + world.Focal() * e.viewPos[0] / -e.viewPos[2];
                const float y = 240 - world.Focal() * e.viewPos[1] / -e.viewPos[2];
                rightVisible |= x >= 0 && x < 640 && y >= 0 && y < 480;
            }
        }
        if (entered >= 0 && scene != 17) {
            Require(left && right, "both bookcase-room civilians were exercised", frame);
            Require(!leftDied && rescued > rescuedBefore, "the left civilian was saved", frame);
            Require(shootRight ? rightDied >= 0 : rightDied < 0,
                    "the requested right-civilian outcome occurred", frame);
            Require(scene == 18, "the room advanced to its authored next scene", frame);
            std::printf("PASS room %s%s, left rescue delay %d: entered %d, death %d, scene 18 at %d\n",
                        shootRight ? "right shot" : "both saved", lastEnemy ? " with last enemy alive" : "",
                        leftDelay, entered, rightDied, frame);
            return;
        }
        if (entered >= 0 && frame - entered > 6000) break;
        const uint8_t previous = planner.held;
        planner.civ.clear();
        // Killing the cemetery civilian selects the house branch (scene 17).
        if (scene == 6) planner.civ.push_back(plan::Planner::ParseFilter("0"));
        if (scene == 17 && rightVisible && shootRight && rightDied < 0 &&
            (!lastEnemy || world.LiveEnemies() == 1)) {
            planner.civ.push_back(plan::Planner::ParseFilter("0"));
            for (const auto& target : world.ShotTargets())
                if (target.civilian && target.serial != right) planner.blocked[target.serial] = frame + 2;
        }
        plan::Line line{};
        if (leftSeen >= 0 && frame < leftSeen + leftDelay) planner.held = 0;
        else planner.Decide(world, frame + 1, &line);
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previous));
        world.Step();
    }
    std::fprintf(stderr, "room stalled: scene %d camera %X/%d live %d; right %u death %d left %u dead %d\n",
                 world.Events().scene, world.Events().camera.camId, world.Events().camera.tick,
                 world.LiveEnemies(), right, rightDied, left, leftDied);
    for (const auto& e : world.Enemies(true))
        std::fprintf(stderr, "actor %u kind %X hp %d motion %X flags %X behavior %d pos %.2f %.2f %.2f\n",
                     e.serial, e.kind, e.hp, e.motion, e.flags34, e.behaviour, e.pos[0], e.pos[1], e.pos[2]);
    Require(false, "the room completed after the civilian outcome", world.Frame());
}

enum class CrawlerCase { Ignore, Rescue, ReplaceAttackers, ShootCivilian };

static void CheckCrawlers(const char* directory, CrawlerCase outcome, int difficulty = 2,
                          bool coop = false, bool original = false) {
    game::World world;
    std::string error;
    Require(world.Load(directory, 1, &error), error.c_str(), 0);
    game::WorldConfig cfg;
    cfg.eventMode = 1; cfg.scene = 0; cfg.gameMode = 6;
    cfg.portFeatures = true; cfg.playerState[0] = 0;
    cfg.seedCounter = 12345; cfg.livesSetting = 99;
    cfg.difficulty = difficulty; cfg.gameType = original ? 1 : 0;
    if (coop) cfg.playerState[1] = 0;
    Require(world.StartStage(cfg, &error), error.c_str(), 0);
    plan::Planner planner; planner.Init();
    struct Crawler { int behaviour = -1, attacks = 0; bool leaped = false, dead = false; };
    std::map<uint32_t, Crawler> crawlers;
    uint32_t civilian = 0, shooting = 0;
    int waiting = -1, civilianDied = -1, resumed = -1, rescuedBefore = -1, livesBefore = -1;
    bool shotCivilian = false;
    const bool save = outcome == CrawlerCase::Rescue || outcome == CrawlerCase::ReplaceAttackers;
    for (int frame = 0; frame < 14000; ++frame) {
        const int scene = world.Events().scene;
        int rescued = 0, met = 0; world.Civilians(&rescued, &met);
        const auto enemies = world.Enemies(true);
        if (scene == 17 && waiting < 0) for (const auto& e : enemies)
            if (e.kind == 0x20 && std::fabs(e.pos[0] + 600.9f) < 0.1f) {
                civilian = e.serial; waiting = frame; rescuedBefore = rescued;
                livesBefore = world.Hud().player[0].lives + world.Hud().player[1].lives;
            }
        if (waiting >= 0) {
            for (const auto& e : enemies) {
                if (e.serial == civilian && (e.flags34 & 0x4000000) && civilianDied < 0) civilianDied = frame;
                if (e.kind != 0xC || scene != 17 || (frame != waiting && !crawlers.count(e.serial))) continue;
                auto& crawler = crawlers[e.serial];
                crawler.dead |= e.hp <= 0 || (e.flags34 & 0x4000000) != 0;
                if (crawler.dead) { if (shooting == e.serial) shooting = 0; continue; }
                // Both missing behaviours used to retain the translating crawl
                // motion indefinitely, escaping opposite sides of this room.
                Require(std::isfinite(e.pos[0]) && std::isfinite(e.pos[1]) && std::isfinite(e.pos[2]),
                        "crawler position remains finite", frame);
                if (!(e.pos[0] > -675 && e.pos[0] < -590 && e.pos[2] > -1555 && e.pos[2] < -1495))
                    std::fprintf(stderr, "escaped crawler %u hp %d behaviour %d/%d motion %X flags %X pos %.2f %.2f %.2f; case %d difficulty %d waiting %d\n",
                                 e.serial, e.hp, e.behaviour, e.sub, e.motion, e.flags34, e.pos[0], e.pos[1], e.pos[2],
                                 static_cast<int>(outcome), difficulty, waiting);
                Require(e.pos[0] > -675 && e.pos[0] < -590 && e.pos[2] > -1555 && e.pos[2] < -1495,
                        "living crawlers stay inside the encounter", frame);
                if (crawler.behaviour == 52 && e.behaviour == 4) ++crawler.attacks;
                crawler.behaviour = e.behaviour;
                if (e.behaviour == 44 && e.sub == 2 && e.tick >= 12) {
                    crawler.leaped = true;
                    if (save && !shooting) { shooting = e.serial; if (resumed < 0) resumed = frame; }
                    if (outcome == CrawlerCase::ShootCivilian) shotCivilian = true;
                }
            }
            Require(!save || civilianDied < 0, "shooting the leaping crawlers saves the civilian", frame);
            if (!save && frame - waiting >= 600)
                Require(civilianDied >= 0, "the ignored civilian attack resolves promptly", frame);
            if (scene != 17) {
                Require(scene == 18 && crawlers.size() == 3, "all three crawlers resolve and the room advances", frame);
                int leaped = 0, attacks = 0;
                for (const auto& [serial, crawler] : crawlers) {
                    Require(crawler.dead, "all crawlers are defeated before advancing", frame);
                    leaped += crawler.leaped; attacks += crawler.attacks;
                }
                Require(leaped > 0, "the civilian leap was exercised before shooting", frame);
                if (outcome == CrawlerCase::ReplaceAttackers)
                    Require(leaped == 3, "each waiting crawler takes over when the attacker is shot", frame);
                if (save) Require(rescued > rescuedBefore, "the civilian rescue is credited", frame);
                else {
                    Require(attacks >= 2, "crawlers repeatedly attack the players and recover", frame);
                    Require(world.Hud().player[0].lives + world.Hud().player[1].lives < livesBefore,
                            "crawler player attacks can inflict damage", frame);
                }
                for (const auto& note : world.Notes())
                    Require(note.find("enemy behaviour 44 ") == std::string::npos &&
                            note.find("enemy behaviour 52 ") == std::string::npos,
                            "both crawler behaviours are implemented", frame);
                std::printf("PASS crawlers outcome %d difficulty %d %s %s: civilian death %d, leaps %d, attacks %d, scene 18 at %d\n",
                            static_cast<int>(outcome), difficulty, coop ? "co-op" : "solo", original ? "Original" : "Arcade",
                            civilianDied, leaped, attacks, frame);
                return;
            }
        }
        const uint8_t previous = planner.held;
        planner.civ.clear();
        if (scene == 6) planner.civ.push_back(plan::Planner::ParseFilter("0"));
        plan::Line line{};
        bool fire = waiting < 0;
        if (waiting >= 0) {
            const bool defeated = crawlers.size() == 3 && std::all_of(crawlers.begin(), crawlers.end(),
                [](const auto& crawler) { return crawler.second.dead; });
            fire = save ? (outcome == CrawlerCase::Rescue ? resumed >= 0 : shooting != 0 || defeated) : frame - waiting >= 1800;
            if (outcome == CrawlerCase::ShootCivilian && shotCivilian && civilianDied < 0) {
                fire = true;
                planner.civ.push_back(plan::Planner::ParseFilter("0"));
                for (const auto& target : world.ShotTargets())
                    if (target.serial != civilian) planner.blocked[target.serial] = frame + 2;
            } else if (shooting) {
                for (const auto& target : world.ShotTargets())
                    if (target.serial != shooting) planner.blocked[target.serial] = frame + 2;
            }
        }
        if (fire) planner.Decide(world, frame + 1, &line);
        else planner.held = 0;
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previous));
        world.Step();
    }
    std::fprintf(stderr, "crawler stall: scene %d camera %d/%d live %d civilian %u died %d\n",
                 world.Events().scene, world.Events().camera.camId, world.Events().camera.tick,
                 world.LiveEnemies(), civilian, civilianDied);
    Require(false, "the crawler encounter completes without a softlock", world.Frame());
}

int main(int argc, char** argv) {
    Require(argc == 2, "usage: chapter2_room_test GAME", 0);
    CheckRoom(argv[1], true, false, 0);
    CheckRoom(argv[1], true, true, 0);
    CheckRoom(argv[1], true, true, 180);
    CheckRoom(argv[1], false, false, 0);
    for (int difficulty = 0; difficulty < 5; ++difficulty) {
        CheckCrawlers(argv[1], CrawlerCase::Ignore, difficulty);
        CheckCrawlers(argv[1], CrawlerCase::Rescue, difficulty);
    }
    CheckCrawlers(argv[1], CrawlerCase::ReplaceAttackers);
    CheckCrawlers(argv[1], CrawlerCase::ShootCivilian);
    CheckCrawlers(argv[1], CrawlerCase::Ignore, 2, true);
    CheckCrawlers(argv[1], CrawlerCase::ReplaceAttackers, 2, true);
    CheckCrawlers(argv[1], CrawlerCase::Ignore, 2, false, true);
}
