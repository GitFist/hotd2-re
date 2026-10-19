// Real options task/handler flow with mouse/gun input, including saved settings. No renderer or user save needed.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "game/world_modes.h"

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
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    game::World::Impl w;
    w.gameDir = argv[1];
    Require(w.exe.Load((std::filesystem::path(argv[1]) / "Hod2.exe").string()), "load tables");
    w.cfg.portFeatures = true;
    for (int& volume : w.cfg.portVolumes) volume = 5;
    w.gameMode = 12;
    for (auto& p : w.player) p.inputKind = 1;
    game::ModesBoot(w);
    w.modes->savePath = argv[2];
    Step(w, 639, 0);
    auto& m = *w.modes;
    // Coordinates are the visible original-font rows, including the inserted volume/display rows.
    const int ids[] = {0, 1, 2, 4, 5, 6, 7, 11, 12, 13, 14, 9};
    const int cells[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    for (int i = 0; i < 12; ++i)
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
    for (int i = 0; i < 3; ++i) {
        Step(w, 600, 262 + i * 24, true);
        Require(m.optVolume[i] == 6, "shoot volume increases once");
        Step(w, 440, 262 + i * 24, true);
        Require(m.optVolume[i] == 5, "previous arrow decreases volume");
        Step(w, 600, 262 + i * 24, false, true);
        Require(m.optVolume[i] == 4, "reload decreases volume");
    }
    for (int i = 0; i < 20; ++i) Step(w, 600, 262, true);
    Require(m.optVolume[0] == 10, "volume clamps at ten");
    for (int i = 0; i < 20; ++i) Step(w, 440, 262, true);
    Require(m.optVolume[0] == 0, "volume clamps at zero");
    const int c0 = m.optCross[0], c1 = m.optCross[1];
    Step(w, 600, 166, true);
    Require(m.optCross[0] == c0 && m.optCross[1] == (c1 + 1) % 4, "P1 can shoot P2's sight preview independently");
    Step(w, 506, 166, true, false, 0, 1);
    Require(m.optCross[0] == (c0 + 1) % 4 && m.optCross[1] == (c1 + 1) % 4, "P2 can shoot P1's sight preview");
    for (int i = 0; i < 2; ++i) {
        const int y = 214 + i * 24;
        Step(w, 620, y, true);
        Require((i == 0 ? m.optBgm : m.optVoice) == 1, "sound arrow chooses next recording");
        Step(w, 40, y, true);
        Require((i == 0 ? m.optBgm : m.optVoice) == 1, "shooting sound-test name plays without advancing");
        const auto soundId = w.exe.U32(i == 0 ? 0x00569798 + 8 : 0x005970C4 + 4);
        bool heard = false;
        for (const auto& sound : w.sounds) if (sound.id == soundId) heard = true;
        Require(heard, "sound-test click emits the selected recording");
        Step(w, 484, y, true);
        Require((i == 0 ? m.optBgm : m.optVoice) == 0, "sound previous arrow reverses selection");
    }
    Step(w, 40, 334, true);
    Require(m.optDisplayRequests == 1, "shoot Display Settings opens page exactly once");
    Step(w, 639, 20, true);
    Require(m.optDisplayRequests == 1 && w.subState == 1, "blank shot cannot activate stale selection");
    Step(w, 40, 334);
    Step(w, 40, 334, false, false, 0x20);
    Step(w, 40, 334);
    Require(m.optCursor == 9, "stationary pointer does not override keyboard selection");
    Step(w, 41, 334);
    Require(m.optCursor == 14, "small mouse movement restores hover selection");
    Step(w, 40, 190, true);
    Require(m.optWiiRequests == 1 && m.optDisplayRequests == 1, "shoot Wii Remotes opens the local setup page once");
    Step(w, 40, 358, true);
    Require(m.optVolume[0] == 10 && m.optVolume[1] == 10 && m.optVolume[2] == 10, "shoot Default restores settings");
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
    puts("PASS: 36 hover points; values, arrows, both sights, audio tests, display, blank shots, keyboard, Default, Exit and saved settings");
}
