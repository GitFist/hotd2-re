// Real options task/handler flow with mouse/gun input, including saved settings. No renderer or user save needed.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include "game/menu_audio.h"
#include "game/world_modes.h"
#include "game/world_nightshift.h"

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void Step(game::World::Impl& w, int x, int y, bool shot = false, bool reload = false, uint32_t keys = 0, int p = 0) {
    for (int k = 0; k < 2; ++k) { w.input[k] = {}; w.input[k].kind = 1; w.input[k].x = 639; w.input[k].y = 0; }
    auto& in = w.input[p];
    in.x = x; in.y = y; in.fired = shot; in.offscreen = reload;
    // A real trigger also synthesizes Start for a player who is not currently in a game.
    in.pressed = in.held = (shot ? 12u : 0u) | (reload ? 2u : 0u) | keys;
    w.pressed = w.held = in.pressed << (p * 16);
    w.hudSprites.clear();
    w.sounds.clear();
    game::SetCurrentWorld(&w);
    game::OptionsFrame(w);
}
static void RequireSounds(const game::World::Impl& w, std::initializer_list<uint32_t> expected, const char* why,
                          int feedbackIndex = -1) {
    bool match = w.sounds.size() == expected.size();
    size_t i = 0;
    for (const auto id : expected) {
        const int kind = int(i) == feedbackIndex ? game::World::kOptionFeedback : game::World::kPlayGameSound;
        if (i >= w.sounds.size() || w.sounds[i].id != id || w.sounds[i].kind != kind) match = false;
        ++i;
    }
    Require(match, why);
}
static void OptionFeedback(const char* dir, bool modern) {
    game::World::Impl w;
    w.gameDir = dir;
    Require(w.exe.Load((std::filesystem::path(dir) / "Hod2.exe").string()), "load options feedback tables");
    Require(w.exe.U32(0x00569798 + 74 * 8) == game::kOptionSelectSound &&
            w.exe.U32(0x00569798 + 76 * 8) == game::kOptionBackSound,
            "select/back feedback must match original Sound Preview 74 and 76");
    w.cfg.portFeatures = true;
    w.cfg.portModernMenu = modern;
    for (int& volume : w.cfg.portVolumes) volume = 5;
    w.gameMode = 12;
    for (auto& p : w.player) p.inputKind = 1;
    game::ModesBoot(w);
    w.modes->savePath.clear();
    Step(w, 639, 0);
    const int ids[] = {0, 1, 2, 4, 5, 15, 11, 12, 13, 16, 14, 8, 9, 10};
    const int cells[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};
    auto y = [&](int id) {
        for (int i = 0; i < 14; ++i) if (ids[i] == id)
            return modern ? int(game::kNightShiftOptionsTop + i * game::NightShiftOptionsPitch(14) + 10) :
                            id == 10 ? 440 : cells[i] * 24 + 22;
        return 0;
    };
    // Every real pointer row remains silent; previews have moved to Extras.
    for (int id : ids) {
        Step(w, 40, y(id));
        if (!modern && id == 10) Step(w, 320, y(id));
        Require(w.modes->optCursor == id, "feedback check hovers the actual visible row");
        RequireSounds(w, {}, "hovering an options row is silent in both themes");
    }
    Step(w, 40, y(0));
    Step(w, 40, y(0), false, false, 0x20);
    Require(w.modes->optCursor == 1, "keyboard feedback check moves to next option");
    RequireSounds(w, {}, "keyboard row navigation is silent");
    Step(w, 40, y(0), false, false, 0x10);
    Require(w.modes->optCursor == 0, "keyboard feedback check moves to previous option");
    RequireSounds(w, {}, "keyboard reverse navigation is silent");
    Step(w, 600, y(0), true);
    RequireSounds(w, {game::kOptionSelectSound}, "click changes a value with exactly Preview 74", 0);
    Step(w, 600, y(0), false, true);
    RequireSounds(w, {game::kOptionSelectSound}, "reload reverses a value with select feedback, not Back", 0);
    const int volumeRows[] = {11, 12, 13, 15, 16};
    for (int index = 0; index < 5; ++index) {
        const int id = volumeRows[index];
        w.modes->optVolume[index] = 10;
        Step(w, 600, y(id), true);
        Require(w.modes->optVolume[index] == 10, "all five volume upper bounds remain clamped");
        RequireSounds(w, {}, "volume increase at upper bound is silent");
        Step(w, modern ? 360 : 440, y(id), true);
        Require(w.modes->optVolume[index] == 9, "volume decrease changes the bounded value");
        RequireSounds(w, {game::kOptionSelectSound}, "actual volume decrease uses one select cue", 0);
        w.modes->optVolume[index] = 0;
        Step(w, modern ? 360 : 440, y(id), true);
        Require(w.modes->optVolume[index] == 0, "all five volume lower bounds remain clamped");
        RequireSounds(w, {}, "volume decrease at lower bound is silent");
    }
    for (int id : {4, 5, 14, 8, 9}) {
        Step(w, id == 4 ? 600 : 40, y(id), true);
        RequireSounds(w, {game::kOptionSelectSound}, "crosshair, device/graphics opens and game reset use one select cue", 0);
    }
    for (int index = 0; index < 5; ++index)
        Require(w.modes->optVolume[index] == 5, "both menu themes reset all five audio levels to five");
    Step(w, 40, y(5));
    Step(w, 40, y(5), false, false, 0x20);
    Require(w.modes->optCursor == 15, "Down skips removed previews between Wii Remotes and Master Volume");
    RequireSounds(w, {}, "skipping removed preview rows is silent");
    Step(w, 40, y(5), false, false, 0x10);
    Require(w.modes->optCursor == 5, "Up skips removed previews in reverse");
    RequireSounds(w, {}, "reverse navigation across removed previews is silent");
    Require(w.modes->optBgm == 0 && w.modes->optVoice == 0, "Options does not alter Extras preview selections");
    Step(w, 600, y(0));
    Step(w, modern ? 40 : 320, y(10), true);
    Require(w.subState == 2, "visible Back action leaves Options");
    RequireSounds(w, {game::kOptionBackSound}, "visible Back action emits exactly Preview 76", 0);
    w.subState = 0;
    Step(w, 639, 0);
    Step(w, 40, y(11));
    Step(w, 40, y(11), false, false, 2);
    Require(w.subState == 2, "Escape saves and leaves Options");
    RequireSounds(w, {game::kOptionBackSound}, "Escape plays Back once", 0);
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    OptionFeedback(argv[1], false);
    OptionFeedback(argv[1], true);
    game::World::Impl w;
    w.gameDir = argv[1];
    Require(w.exe.Load((std::filesystem::path(argv[1]) / "Hod2.exe").string()), "load tables");
    w.cfg.portFeatures = true;
    Require(w.cfg.portVolumes[0] == -1 && w.cfg.portVolumes[1] == -1 && w.cfg.portVolumes[2] == -1 &&
            w.cfg.portVolumes[3] == 5 && w.cfg.portVolumes[4] == 5,
            "WorldConfig keeps original-menu sentinels and Master / Attraction defaults of five");
    for (int& volume : w.cfg.portVolumes) volume = 5;
    w.gameMode = 12;
    for (auto& p : w.player) p.inputKind = 1;
    game::ModesBoot(w);
    for (int index = 0; index < 5; ++index)
        Require(w.modes->optVolume[index] == 5, "initial option volumes default all five audio levels to five");
    w.modes->savePath = argv[2];
    Step(w, 639, 0);
    auto& m = *w.modes;
    // Coordinates are the visible original-font rows, including the inserted volume/display rows.
    const int ids[] = {0, 1, 2, 4, 5, 15, 11, 12, 13, 16, 14, 8, 9};
    const int cells[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < 13; ++i)
        for (int ink : {13, 22, 30}) {
            Step(w, 40, cells[i] * 24 + ink);
            Require(m.optCursor == ids[i], "hover visible top/centre/bottom selects the rendered options row");
        }
    const int difficulty = m.optDifficulty;
    Step(w, 600, 94, true);
    Require(m.optDifficulty == (difficulty + 1) % 5, "click Difficulty advances once, despite synthetic Start");
    Step(w, 440, 94, true);
    Require(m.optDifficulty == difficulty, "shoot previous arrow reverses difficulty");
    Step(w, 600, 94, false, true);
    Require(m.optDifficulty == (difficulty + 4) % 5, "reload reverses a value");
    Step(w, 600, 94, false, false, 8);
    Require(m.optDifficulty == difficulty, "Enter advances selected value once");
    const int lives = m.optLives, credits = m.optCredits;
    Step(w, 600, 118, true);
    Step(w, 600, 142, true);
    Require(m.optLives == (lives + 1) % 5 && m.optCredits != credits, "shoot Life and Continue");
    const int volumeOrder[] = {3, 0, 1, 2, 4};
    for (int i = 0; i < 5; ++i) {
        const int index = volumeOrder[i];
        Step(w, 600, 214 + i * 24, true);
        Require(m.optVolume[index] == 6, "shoot volume increases once");
        Step(w, 440, 214 + i * 24, true);
        Require(m.optVolume[index] == 5, "previous arrow decreases volume");
        Step(w, 600, 214 + i * 24, false, true);
        Require(m.optVolume[index] == 4, "reload decreases volume");
    }
    for (int i = 0; i < 20; ++i) Step(w, 600, 238, true);
    Require(m.optVolume[0] == 10, "volume clamps at ten");
    for (int i = 0; i < 20; ++i) Step(w, 440, 238, true);
    Require(m.optVolume[0] == 0, "volume clamps at zero");
    const int c0 = m.optCross[0], c1 = m.optCross[1];
    Step(w, 600, 166, true);
    Require(m.optCross[0] == c0 && m.optCross[1] == (c1 + 1) % 4, "P1 can shoot P2's sight preview independently");
    Step(w, 506, 166, true, false, 0, 1);
    Require(m.optCross[0] == (c0 + 1) % 4 && m.optCross[1] == (c1 + 1) % 4, "P2 can shoot P1's sight preview");
    Step(w, 40, 334, true);
    Require(m.optDisplayRequests == 1, "shoot Display Settings opens page exactly once");
    Step(w, 639, 20, true);
    Require(m.optDisplayRequests == 1 && w.subState == 1, "blank shot cannot activate stale selection");
    Step(w, 40, 334);
    Step(w, 40, 334, false, false, 0x20);
    Step(w, 40, 334);
    Require(m.optCursor == 8, "stationary pointer does not override keyboard selection");
    Step(w, 41, 334);
    Require(m.optCursor == 14, "small mouse movement restores hover selection");
    Step(w, 40, 190, true);
    Require(m.optWiiRequests == 1 && m.optDisplayRequests == 1, "shoot Wii Remotes opens the local setup page once");
    Step(w, 40, 358, true);
    Require(m.optLightgunRequests == 1, "shoot Light Guns opens setup once");
    Step(w, 40, 382, true);
    Require(m.optVolume[0] == 5 && m.optVolume[1] == 5 && m.optVolume[2] == 5 && m.optVolume[3] == 5 && m.optVolume[4] == 5,
            "shoot Default restores all five audio levels to five");
    Step(w, 600, 94, true);
    const int savedDifficulty = m.optDifficulty;
    Step(w, 320, 440, true);
    Require(w.subState == 2, "shoot lower half of Exit saves and leaves Options");
    game::SaveData saved;
    Require(saved.LoadNative(argv[2]) && saved.U8(game::SaveData::kDifficulty) == savedDifficulty,
            "click changes persist in the native save");
    w.subState = 0;
    Step(w, 639, 0);
    Step(w, 40, 94, false, false, 2);
    Require(w.subState == 2, "keyboard Escape/Back leaves Options through the save handler");
    game::SetCurrentWorld(nullptr);
    puts("PASS: 39 hover points; five volumes, bounded feedback, audio defaults of five, arrows, both sights, display, keyboard, Default, Exit and saved settings");
}
