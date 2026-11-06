// Native-only opaque polygonal blood is presentation state: real hit triggers,
// exact lifetime, active-camera placement, and unchanged PC/network simulation.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_config_io.h"
#include "route_planner.h"

namespace {
int checks = 0;
void Require(bool ok, const char* why, int frame = -1) {
    ++checks;
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s (frame %d)\n", why, frame);
    std::exit(1);
}
struct Hash {
    uint32_t value = 2166136261u;
    void Bytes(const void* p, size_t n) {
        const auto* bytes = static_cast<const uint8_t*>(p);
        while (n--) { value ^= *bytes++; value *= 16777619u; }
    }
    template<class T> void Add(const T& v) { Bytes(&v, sizeof(v)); }
};

// The non-Typing WorldHash contract in play.cpp. Keep effect presentation out
// of this fingerprint just as it is kept out of the production network hash.
uint32_t NetworkHash(const game::World& w) {
    Hash hash;
    hash.Add(w.Frame()); hash.Add(w.GameMode()); hash.Add(w.RandState());
    const auto& hud = w.Hud();
    hash.Add(hud.randomizerSeed); hash.Add(hud.randomizerHash);
    hash.Add(hud.menuCursor); hash.Add(hud.gameType); hash.Add(hud.paused);
    const bool typing = w.PortTypingActive(); hash.Add(typing);
    const auto& results = hud.stageResults;
    for (int value : {int(results.active), int(results.shown), int(results.dismiss), results.chapter,
                     results.frames, results.civilians, results.lifeBonus}) hash.Add(value);
    for (const auto& p : results.player)
        for (int value : {int(p.participated), p.score, p.shots, p.hits, p.headshots, p.rescues, p.damage, p.continues})
            hash.Add(value);
    for (const auto& p : hud.player) { hash.Add(p.state); hash.Add(p.lives); hash.Add(p.score); }
    for (const auto& d : w.Draws()) {
        hash.Add(d.model); hash.Bytes(d.world.m, sizeof(d.world.m)); hash.Add(d.alpha); hash.Add(d.node);
    }
    return hash.value;
}
uint32_t TaskHash(const game::World& w) {
    Hash hash;
    for (const auto& task : w.Tasks()) { hash.Add(task.serial); hash.Add(task.fn); }
    for (const auto& random : w.RandCalls()) { hash.Add(random.site); hash.Add(random.serial); hash.Add(random.value); }
    for (const auto& sound : w.Sounds()) { hash.Add(sound.serial); hash.Add(sound.id); hash.Add(sound.kind); }
    for (const auto& op : w.EventOps()) { hash.Add(op.frame); hash.Add(op.opcode); hash.Add(op.addr); }
    for (const auto& d : w.Draws()) {
        hash.Add(d.task); hash.Add(d.fn); hash.Add(d.model); hash.Bytes(d.world.m, sizeof(d.world.m));
        hash.Add(d.drawMode); hash.Add(d.alpha); hash.Add(d.alt); hash.Add(d.viewSpace); hash.Add(d.node);
        hash.Add(d.light.table); hash.Add(d.light.intensity); hash.Add(d.light.pitch); hash.Add(d.light.yaw);
        hash.Bytes(d.light.color, sizeof(d.light.color));
    }
    return hash.value;
}

void TriggerFixtures() {
    for (int type = 0; type <= 6; ++type) {
        auto native = std::make_unique<game::World::Impl>();
        auto pc = std::make_unique<game::World::Impl>();
        for (auto* w : {native.get(), pc.get()}) {
            w->gameMode = 6;
            w->rng.state = 0x12345678;
            w->hitType[0] = type; w->hitSlot[0] = 7;
        }
        pc->cfg.portTyping = true; // Suppress only the new presentation branch in this isolated callback.
        game::EnemyTask enemy, baseline;
        for (auto* e : {&enemy, &baseline}) {
            e->anim.kind = 0;
            auto& node = e->anim.nodes[7];
            node.hit[0] = 2; node.hit[1] = -3; node.hit[2] = -20; node.radius = 2.5f;
            node.flags = 0x10; // Type 5's unrelated spark is outside this fixture.
        }
        const uint32_t serial = native->ts.NextSerial();
        native->EnemyHitEffects(enemy, 0);
        pc->EnemyHitEffects(baseline, 0);
        const bool emits = type == 1 || type == 3 || type == 4;
        Require(native->nativeVisualEffects.size() == (emits ? 1u : 0u), "only hit types 1/3/4 emit native polygon blood");
        Require(pc->nativeVisualEffects.empty(), "Typing does not inherit arcade polygon blood");
        Require(native->ts.NextSerial() == pc->ts.NextSerial(), "restored blood does not consume a task serial");
        Require(native->ts.Live().size() == pc->ts.Live().size(), "restored blood does not create simulation tasks");
        Require(native->rng.state == 0x12345678 && native->randCalls.empty(), "hit presentation uses no game RNG");
        Require(native->draws.empty(), "native effect does not add fake PC model draws");
        Require(native->ts.NextSerial() == serial + ((type >= 1 && type <= 4) ? 1 : 0),
                "existing 25-frame PC blood task creation is preserved");
    }

    auto w = std::make_unique<game::World::Impl>();
    w->runner = std::make_unique<game::EventRunner>(w->mem, *w, &w->env, &w->exe);
    w->runner->camera.activeCtx = 2;
    game::Mat4 camera = game::Mat4::Identity();
    game::mx::Translate(camera, 100, -25, 70);
    game::mx::RotY(camera, 0x4000);
    std::memcpy(w->runner->camera.ctx[2].camera, camera.m, sizeof(camera.m));
    w->cam.inverse = game::Mat4::Translate(-999, 500, 600); // Deliberately stale snapshot.
    w->gameMode = 6;
    game::EnemyTask enemy;
    auto& node = enemy.anim.nodes[7];
    node.hit[0] = 2; node.hit[1] = -3; node.hit[2] = -20; node.radius = 2.5f;
    const auto serial = w->ts.NextSerial(), random = w->rng.state;
    w->EmitNativePolygonBlood(enemy, 7);
    const auto effect = w->nativeVisualEffects.at(0);
    Require(effect.frame == 0 && effect.duration == 58, "all 58 source mesh phases start at zero");
    Require(effect.kind == game::NativeVisualEffect::Kind::PolygonBlood && effect.serial != 0, "separate native effect identity");
    // Quarter-turn Y maps (2,-3,-20) to (-20,-3,-2), then translates.
    Require(std::abs(effect.world.m[12] - 80) < 1e-5f && std::abs(effect.world.m[13] + 28) < 1e-5f &&
            std::abs(effect.world.m[14] - 68) < 1e-5f, "spawn uses active camera matrix and unoffset hit point");
    const float scale = 0.3999999761581421f * 2.5f;
    Require(effect.world.m[0] == scale && effect.world.m[5] == scale && effect.world.m[10] == scale &&
            effect.world.m[1] == 0 && effect.world.m[8] == 0, "native radius scale has no billboard rotation");
    Require(w->ts.NextSerial() == serial && w->rng.state == random, "emitter itself preserves task/RNG counters");
    w->paused = true;
    for (int i = 0; i < 12; ++i) w->AdvanceNativeVisualEffects();
    Require(w->nativeVisualEffects[0].frame == 0, "paused native blood does not advance");
    w->paused = false;
    for (int i = 1; i <= 57; ++i) {
        w->AdvanceNativeVisualEffects();
        Require(w->nativeVisualEffects.size() == 1 && w->nativeVisualEffects[0].frame == i,
                "every native source phase remains visible exactly one simulation step");
    }
    w->AdvanceNativeVisualEffects();
    Require(w->nativeVisualEffects.empty(), "polygon blood expires after phase 57");
    for (int mode : {3, 5, 7, 10, 11}) {
        w->gameMode = mode; w->EmitNativePolygonBlood(enemy, 7);
        Require(w->nativeVisualEffects.empty(), "arcade gore does not leak into demo/story/menus");
    }
    w->gameMode = 6; w->EmitNativePolygonBlood(enemy, 7);
    const uint64_t before = w->nativeVisualEffects[0].serial;
    w->SetGameMode(6);
    Require(w->nativeVisualEffects.empty(), "mode transition discards old native effects");
    w->EmitNativePolygonBlood(enemy, 7);
    Require(w->nativeVisualEffects[0].serial > before, "visual identity does not collide across stage/mode resets");
}

struct Script {
    struct Line { int frame, player; float x, y; uint8_t buttons; };
    std::vector<Line> lines;
    size_t cursor = 0;
    bool connected[2] = {};
    float x[2] = {.5f, .5f}, y[2] = {.5f, .5f};
    uint8_t buttons[2] = {};
    explicit Script(const char* path) {
        std::ifstream file(path);
        for (std::string text; std::getline(file, text);) {
            std::istringstream row(text); Line line{}; int bits = 0;
            if ((row >> line.frame >> line.player >> line.x >> line.y >> bits) && line.player >= 0 && line.player < 2) {
                line.buttons = static_cast<uint8_t>(bits); lines.push_back(line);
            }
        }
        Require(!lines.empty(), "real replay script loads");
    }
    std::array<game::WorldInput, 2> Frame(int frame) {
        uint8_t edges[2] = {};
        while (cursor < lines.size() && lines[cursor].frame <= frame) {
            const auto& line = lines[cursor++]; const int p = line.player;
            connected[p] = true; x[p] = line.x; y[p] = line.y;
            edges[p] |= line.buttons & ~buttons[p]; buttons[p] = line.buttons;
        }
        std::array<game::WorldInput, 2> input{};
        for (int p = 0; p < 2; ++p) if (connected[p])
            input[p] = game::MakeWorldInput(x[p], y[p], buttons[p], buttons[p] & ~edges[p]);
        return input;
    }
};

void DeathBloodFixtures() {
    auto w = std::make_unique<game::World::Impl>();
    w->gameMode = 6; w->rng.state = 0x12345678;
    w->runner = std::make_unique<game::EventRunner>(w->mem, *w, &w->env, &w->exe);
    w->runner->camera.activeCtx = 1;
    auto camera = game::Mat4::Translate(100, -25, 70);
    std::memcpy(w->runner->camera.ctx[1].camera, camera.m, sizeof(camera.m));
    w->cam.inverse = game::Mat4::Translate(-999, 500, 600);
    auto create = [&]() {
        auto* owner = w->ts.Create([](game::Task& task) {
            static_cast<game::World::Impl*>(task.user)->StartNativeDeathBlood(23);
        }, std::make_unique<game::EnemyTask>());
        owner->user = w.get();
        return owner;
    };
    auto* owner = create();
    auto& node = owner->As<game::EnemyTask>().anim.nodes[23];
    node.radius = 5; node.hit[1] = -3; node.hit[2] = -20;
    const auto nextTask = w->ts.NextSerial();
    w->ts.Run(owner);
    Require(w->nativeBloodEmitters.size() == 1, "lethal-hit callback starts one presentation tracker");
    const std::set<int> sourceTicks = {1, 21, 41, 61, 81};
    uint64_t lastVisual = 0;
    for (int tick = 1; tick <= 159; ++tick) {
        node.hit[0] = static_cast<float>(tick);
        if (tick == 20) {
            w->paused = true;
            const int phase = w->nativeVisualEffects.front().frame;
            for (int pause = 0; pause < 12; ++pause) {
                w->AdvanceNativeVisualEffects(); w->UpdateNativeDeathBlood();
            }
            Require(w->nativeBloodEmitters.front().ticks == 19 && w->nativeVisualEffects.front().frame == phase,
                    "pause freezes both recurring emission and already visible blood");
            w->paused = false;
        }
        w->AdvanceNativeVisualEffects(); w->UpdateNativeDeathBlood();
        int born = 0;
        for (const auto& effect : w->nativeVisualEffects) if (effect.serial > lastVisual) {
            ++born; lastVisual = effect.serial;
            Require(effect.frame == 0 && effect.duration == 58, "each death burst begins the complete 58-frame animation");
            Require(std::fabs(effect.world.m[12] - (100 + tick)) < 0.0001f &&
                    std::fabs(effect.world.m[13] + 28) < 0.0001f && std::fabs(effect.world.m[14] - 50) < 0.0001f,
                    "each burst follows the current hit node, including node indices above 15, through the active camera");
            Require(std::fabs(effect.world.m[0] - 0.3999999761581421f * 5) < 0.0001f,
                    "death burst keeps the source hit-radius scale");
        }
        Require(born == (sourceTicks.count(tick) ? 1 : 0), "source death-emission timing and finite duration", tick);
        Require(w->rng.state == 0x12345678 && w->randCalls.empty() && w->ts.NextSerial() == nextTask &&
                w->ts.Live().size() == 1 && w->draws.empty() && w->sounds.empty(),
                "death presentation preserves RNG, simulation tasks, PC draws and sounds", tick);
    }
    Require(w->nativeBloodEmitters.empty() && w->nativeVisualEffects.empty(), "death blood completely expires");

    // Losing the body must stop future emission without removing existing bursts
    // or following a replacement allocation. No retained actor pointers are used.
    w->ts.Run(owner); w->UpdateNativeDeathBlood();
    Require(w->nativeVisualEffects.size() == 1, "replacement fixture starts with a visible burst");
    owner->fn = [](game::Task& task) { static_cast<game::World::Impl*>(task.user)->ts.KillCurrent(); };
    w->ts.Run(owner); w->ts.FreeDead();
    create(); w->UpdateNativeDeathBlood();
    Require(w->nativeBloodEmitters.empty() && w->nativeVisualEffects.size() == 1,
            "freed owner cancels future bursts, preserves existing burst and does not attach to a replacement actor");
    for (int i = 0; i < 58; ++i) w->AdvanceNativeVisualEffects();
    Require(w->nativeVisualEffects.empty(), "orphaned burst expires safely");

    owner = create();
    for (int mode : {3, 5, 7, 10, 11}) {
        w->gameMode = mode; w->ts.Run(owner);
        Require(w->nativeBloodEmitters.empty(), "death blood is excluded from demos and menus");
    }
    w->gameMode = 6; w->cfg.portTyping = true; w->ts.Run(owner);
    Require(w->nativeBloodEmitters.empty(), "Typing retains its existing effects");
    w->cfg.portTyping = false; w->ts.Run(owner); w->UpdateNativeDeathBlood();
    w->SetGameMode(6);
    Require(w->nativeBloodEmitters.empty() && w->nativeVisualEffects.empty(), "mode reset discards death trackers and bursts");
    std::printf("PASS death-blood source timing, moving nodes, pause, owner removal, resets and simulation isolation\n");
}

void Replay(const char* directory, const char* scriptPath) {
    game::World native, emptyEffects;
    std::string error;
    Require(native.Load(directory, 0, &error), error.c_str());
    Require(emptyEffects.Load(directory, 0, &error), error.c_str());
    game::WorldConfig config; config.gameOver = true;
    native.Boot(config); emptyEffects.Boot(config);
    Script script(scriptPath);
    int first = -1, lastBirth = -1, births = 0;
    uint64_t lastSerial = 0;
    int pauseStart = -1;
    std::vector<game::NativeVisualEffect> frozen;
    // This existing original-PC input script boots the menu and shoots the
    // chapter-one enemies. Erase only the second world's presentation vector:
    // the remaining task/PC draw/RNG/network outputs must stay bit-identical.
    for (int frame = 1; frame <= 2700; ++frame) {
        auto input = script.Frame(frame);
        if (pauseStart >= 0 && frame >= pauseStart && frame <= pauseStart + 11) {
            input = {};
            input[0].kind = 1;
            if (frame == pauseStart || frame == pauseStart + 11) input[0].pressed = input[0].held = 8;
        }
        for (auto* w : {&native, &emptyEffects}) {
            w->SetInput(0, input[0]); w->SetInput(1, input[1]); w->Step();
        }
        Require(NetworkHash(native) == NetworkHash(emptyEffects), "effect lifetime leaves actual network fingerprint unchanged", frame);
        Require(TaskHash(native) == TaskHash(emptyEffects), "effect lifetime leaves PC draw/task/RNG/event/sound streams unchanged", frame);
        if (frame == pauseStart) {
            Require(native.Hud().paused, "real gameplay Start input pauses", frame);
            frozen = native.NativeVisualEffects();
            Require(!frozen.empty(), "pause begins while the real hit effect is active", frame);
        }
        if (pauseStart >= 0 && frame >= pauseStart && frame < pauseStart + 11) {
            Require(native.NativeVisualEffects().size() == frozen.size(), "pause preserves active effect count", frame);
            for (size_t i = 0; i < frozen.size(); ++i) {
                const auto& effect = native.NativeVisualEffects()[i];
                Require(effect.serial == frozen[i].serial && effect.frame == frozen[i].frame &&
                        std::memcmp(effect.world.m, frozen[i].world.m, sizeof(effect.world.m)) == 0,
                        "real paused simulation preserves exact effect phase/placement", frame);
            }
        }
        if (frame == pauseStart + 11 && pauseStart >= 0) {
            Require(!native.Hud().paused, "real gameplay Start input resumes", frame);
            for (const auto& held : frozen) {
                bool advanced = false;
                for (const auto& effect : native.NativeVisualEffects())
                    if (effect.serial == held.serial) advanced = effect.frame == held.frame + 1;
                Require(advanced, "real resume advances effect one source phase", frame);
            }
        }
        for (const auto& effect : native.NativeVisualEffects()) {
            Require(effect.frame >= 0 && effect.frame < 58, "real replay effect has a valid source phase", frame);
            if (effect.serial > lastSerial) {
                Require(effect.frame == 0, "real EnemyHitEffects callback emits source phase zero", frame);
                lastSerial = effect.serial; ++births; lastBirth = frame;
                if (first < 0) { first = frame; pauseStart = frame + 1; }
            }
        }
        // Deliberately do not mutate any simulation state or its task stream.
        const_cast<std::vector<game::NativeVisualEffect>&>(emptyEffects.NativeVisualEffects()).clear();
    }
    Require(first >= 0 && births > 0, "real chapter-one shot replay exercises the restored callback");

    // No new shot edges: wait until the last real hit's independent animation
    // is gone, while continuing to compare the game's normal outputs.
    for (int i = 0; i < 90; ++i) {
        native.SetInput(0, {}); native.SetInput(1, {}); native.Step();
    }
    Require(native.NativeVisualEffects().empty(), "real replay effects expire without retaining actor pointers");
    auto& active = const_cast<std::vector<game::NativeVisualEffect>&>(native.NativeVisualEffects());
    active.push_back({});
    game::WorldConfig stage; stage.scene = 0; stage.gameMode = 6; stage.playerState[0] = 0;
    Require(native.StartStage(stage, &error), error.c_str());
    Require(native.NativeVisualEffects().empty(), "public StartStage clears the effect tracker");
    active.push_back({}); native.Boot(config);
    Require(native.NativeVisualEffects().empty(), "public Boot clears the effect tracker");
    std::printf("PASS real chapter-one polygon blood: %d births, first frame %d, last birth %d; 2700 identical PC/network frames\n",
                births, first, lastBirth);
}

// Follow real campaign events and fire ordinary gun inputs. Stop shooting at
// the first Actor31 death so its subsequent bursts cannot be confused with
// another enemy's hit blood. Chapters 1/2, 4 and 5/6 exercise the different
// skeletons sharing this callback, including Chapter 6's knife throwers.
void Actor31Replay(const char* directory, int chapter, bool coop, bool baseline, const char* capture = nullptr) {
    game::World world;
    std::string error;
    Require(world.Load(directory, chapter - 1, &error), error.c_str());
    game::WorldConfig cfg;
    cfg.eventMode = chapter - 1; cfg.scene = 0; cfg.gameMode = 6;
    cfg.portFeatures = true; cfg.playerState[0] = 0;
    if (coop) cfg.playerState[1] = 0;
    cfg.seedCounter = 12345; cfg.livesSetting = 99;
    Require(world.StartStage(cfg, &error), error.c_str());
    plan::Planner planner; planner.Init();
    std::map<uint32_t, int> previousHp;
    uint32_t victim = 0;
    int death = -1, kind = -1, gone = -1, bursts = 0;
    Hash simulation;
    for (int frame = 1; frame <= 18000; ++frame) {
        const auto previousButtons = planner.held;
        if (!victim) {
            plan::Line line{}; planner.Decide(world, frame, &line);
        } else planner.held = 0;
        world.SetInput(0, game::MakeWorldInput(planner.x, planner.y, planner.held, previousButtons));
        world.Step();
        simulation.Add(NetworkHash(world)); simulation.Add(TaskHash(world));
        std::set<uint32_t> actors;
        for (const auto& task : world.Tasks()) if (task.fn == 0x00449910) actors.insert(task.serial);
        bool present = false;
        for (const auto& enemy : world.Enemies(true)) {
            if (!actors.count(enemy.serial)) continue;
            if (!victim && previousHp.count(enemy.serial) && previousHp[enemy.serial] > 0 && enemy.hp <= 0) {
                victim = enemy.serial; death = frame; kind = enemy.kind;
            }
            if (enemy.serial == victim) present = true;
            previousHp[enemy.serial] = enemy.hp;
        }
        if (!victim) continue;
        if (!present && gone < 0) gone = frame;
        int born = 0;
        for (const auto& effect : world.NativeVisualEffects()) {
            if (effect.frame != 0) continue;
            ++born;
            for (float value : effect.world.m) Require(std::isfinite(value), "death blood has a finite placement", frame);
        }
        const int age = frame - death;
        const bool expected = present && age < 100 && age % 20 == 0;
        if (!baseline) Require(born == (expected ? 1 : 0), "Actor31 death emits at source ticks 1/21/41/61/81 only while alive", frame);
        bursts += born;
        if (age == 160) {
            Require(world.NativeVisualEffects().empty(), "Actor31 death effects finish without lingering", frame);
            if (capture) {
                Require(game::SaveWorldConfig(cfg, std::string(capture) + ".cfg"), "save optional visual-replay configuration");
                std::ofstream script(std::string(capture) + ".txt");
                for (const auto& line : planner.lines) script << line << '\n';
                script << death + 1 << " 0 " << planner.x << ' ' << planner.y << " 0\n";
                Require(bool(script), "save optional visual-replay inputs");
            }
            std::printf("%s chapter %d %s Actor31 kind %X: death %d, removed %d, bursts %d, simulation %08X\n",
                        baseline ? "BASELINE" : "PASS", chapter, coop ? "coop" : "solo", kind, death, gone, bursts, simulation.value);
            return;
        }
    }
    std::fprintf(stderr, "chapter %d stopped at scene %d, frame %d\n", chapter, world.Events().scene, world.Frame());
    Require(false, "real campaign reaches an Actor31 death");
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: native_blood_world_test <game directory> <original input script | --actor31 | --actor31-baseline> [capture prefix]\n");
        return 2;
    }
    if (std::string(argv[2]) == "--actor31" || std::string(argv[2]) == "--actor31-baseline") {
        const bool baseline = std::string(argv[2]) == "--actor31-baseline";
        if (!baseline) { TriggerFixtures(); DeathBloodFixtures(); }
        for (int chapter : {1, 2, 4, 5, 6}) Actor31Replay(argv[1], chapter, false, baseline, chapter == 6 && argc == 4 ? argv[3] : nullptr);
        Actor31Replay(argv[1], 6, true, baseline);
        return 0;
    }
    TriggerFixtures();
    DeathBloodFixtures();
    Replay(argv[1], argv[2]);
    std::printf("PASS native polygon blood trigger/lifetime: %d checks\n", checks);
}
