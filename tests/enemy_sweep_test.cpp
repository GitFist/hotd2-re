// Exercise Enemy Sweep through the real menus, scripts and gun hit detection.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "game/save_data.h"
#include "game/world.h"

namespace {
constexpr uint32_t kInstructions = 0x00497440, kPassed = 0x00497C30, kFailed = 0x00497DB0;
constexpr uint32_t kLeech = 0x0043D800, kFlyer = 0x0042F420, kFlyerBody = 0x0042F7F0;
int checks = 0;
void Check(bool ok, const char* why, const game::World& w) {
    ++checks;
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s (frame %d, scene %d, live %d, attackers %d)\n",
                 why, w.Frame(), w.Events().scene, w.LiveEnemies(), w.Attackers());
    std::exit(1);
}
bool Has(const game::World& w, uint32_t fn) {
    for (const auto& t : w.Tasks()) if (t.fn == fn) return true;
    return false;
}
void Step(game::World& w, uint32_t keys = 0, int x = 639, int y = 0, bool shot = false) {
    game::WorldInput in;
    in.kind = 1; in.x = x; in.y = y; in.fired = shot;
    in.pressed = in.held = keys | (shot ? 12u : 0u);
    w.SetInput(0, in); w.SetInput(1, {}); w.Step();
}
void Wait(game::World& w, int frames) { while (frames--) Step(w); }
void Start(game::World& w, const char* directory, int difficulty, int order) {
    std::string error;
    Check(w.Load(directory, 0, &error), error.c_str(), w);
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = true;
    cfg.savePatch.push_back({game::SaveData::kTrainGrades, std::vector<uint8_t>(10, 5)});
    w.Boot(cfg);
    // The front end seeds its shuffle from time spent in the menus.
    Wait(w, 650 + order); Step(w, 8); Wait(w, 4);
    Step(w, 0, 40, 175, true); Wait(w, 2); // Play
    Step(w, 0, 40, 233, true); Wait(w, 600); // Challenges
    for (int i = 0; i < 5; ++i) { Step(w, 0, 480, 194, true); Wait(w, 30); }
    for (int i = difficulty; i < 4; ++i) { Step(w, 0x20); Wait(w, 2); }
    Step(w, 0, 540, 459, true); Wait(w, 80);
    Check(w.Events().scene == 11 && Has(w, kInstructions), "Enemy Sweep instructions open", w);
    Check(w.Events().cfg.playerVariant == difficulty, "selected difficulty reaches the lesson", w);
    Step(w, 8); Wait(w, 180);
}

struct Run {
    std::set<uint32_t> enemies, civilians, drawn;
    bool omitted = false;
    int frames = 0;
};

Run Play(game::World& w, uint32_t omit = 0) {
    Run run;
    uint8_t previous = 0;
    for (; run.frames < 9000 && !Has(w, kPassed) && !Has(w, kFailed); ++run.frames) {
        bool found = false;
        float x = .5f, y = .5f;
        const auto ordinary = w.Enemies(false, true);
        for (const auto& draw : w.Draws()) run.drawn.insert(draw.fn);
        for (const auto& target : w.ShotTargets()) {
            if (target.civilian) { run.civilians.insert(target.serial); continue; }
            const bool small = target.fn == kLeech || target.fn == kFlyer;
            bool living = small && !(target.flags & 8u);
            if (!small) for (const auto& enemy : ordinary)
                if (enemy.serial == target.serial && enemy.hp > 0 && !(enemy.flags34 & 0x4000000u)) living = true;
            if (!living) continue;
            run.enemies.insert(target.serial);
            if (target.fn == omit) { run.omitted = true; continue; }
            if (found) continue;
            auto aim = [&](const float* point) {
                if (point[2] >= -1) return false;
                const float sx = 320 + point[0] * w.Focal() / -point[2];
                const float sy = 240 - point[1] * w.Focal() / -point[2];
                if (!std::isfinite(sx) || !std::isfinite(sy) || sx < 0 || sx >= 640 || sy < 0 || sy >= 480) return false;
                if (w.PreviewShot(0, int(sx), int(sy)).serial != target.serial) return false;
                x = sx / 640; y = sy / 480;
                return true;
            };
            if (target.nodes.empty()) found = aim(target.view); // small enemies use a sphere
            else for (const auto& node : target.nodes) if (aim(node.data())) { found = true; break; }
        }
        uint8_t buttons = 0;
        if (found && w.Hud().canShoot)
            buttons = w.Hud().player[0].ammo == 0 ? 2 : (run.frames % 4 == 0 ? 1 : 0);
        w.SetInput(0, game::MakeWorldInput(x, y, buttons, previous));
        previous = buttons; w.SetInput(1, {}); w.Step();
    }
    Check(Has(w, kPassed) || Has(w, kFailed), "the challenge reaches a result without stalling", w);
    return run;
}

void Completed(game::World& w, const Run& run) {
    Check(Has(w, kPassed) && !Has(w, kFailed), "clearing every enemy passes the challenge", w);
    Check(run.enemies.size() == 6 && run.civilians.size() == 3, "all six enemies and three civilians participate", w);
    Check(run.drawn.count(kLeech) && run.drawn.count(kFlyer) && run.drawn.count(kFlyerBody),
          "both small enemies and the flyer's carried body render", w);
    Check(w.LiveEnemies() == 0 && w.Attackers() == 0, "completion releases all enemy counters", w);
    Wait(w, 90); Step(w, 8);
    for (int i = 0; i < 600 && !(w.Events().scene == 0 && Has(w, 0x00496DE0)); ++i) Step(w);
    Check(w.Events().scene == 0 && Has(w, 0x00496DE0), "completed results return to Challenge selection", w);
}

void Retry(game::World& w, int difficulty) {
    Wait(w, 90); Step(w, 0, 340, 382, true);
    for (int i = 0; i < 600 && !Has(w, kInstructions); ++i) Step(w);
    Check(w.Events().scene == 11 && Has(w, kInstructions), "Retry returns to Enemy Sweep instructions", w);
    Check(w.Events().cfg.playerVariant == difficulty, "Retry retains the selected difficulty", w);
    Check(w.LiveEnemies() == 0 && w.Attackers() == 0, "Retry clears old enemy counters", w);
    Wait(w, 80); Step(w, 8); Wait(w, 180);
    Completed(w, Play(w));
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: enemy_sweep_test <game directory>\n"); return 2; }
    for (int difficulty = 0; difficulty < 5; ++difficulty) {
        for (int order = 0; order < 4; ++order) {
            game::World w; Start(w, argv[1], difficulty, order);
            Completed(w, Play(w));
        }
        // Exercise both previously missing enemies escaping, then complete the
        // retry through the same real input path. No task/counter mutation.
        for (uint32_t omit : {kLeech, kFlyer}) {
            game::World w; Start(w, argv[1], difficulty, 3);
            const Run failed = Play(w, omit);
            Check(failed.omitted && Has(w, kFailed), "an escaped small enemy fails the challenge", w);
            Retry(w, difficulty);
        }
        std::printf("PASS Enemy Sweep level %d: 4 completions, 2 failures/retries/completions\n", difficulty + 1);
        std::fflush(stdout);
    }
    std::printf("PASS Enemy Sweep regression: %d checks\n", checks);
}
