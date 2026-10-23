// Connected room lifecycle: P1 authority, all modes, repeat games, and deterministic replay.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/save_data.h"
#include "game/world.h"
#include "port/play/keyboard.h"

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void Input(game::World& w, int p, uint32_t bits) {
    game::WorldInput in;
    in.pressed = in.held = bits;
    w.SetInput(p, in);
}
static void Step(game::World& w, int n = 1) {
    for (int i = 0; i < n; ++i) { Input(w, 0, 0); Input(w, 1, 0); w.Step(); }
}
static void Tap(game::World& w, int p, uint32_t bits) {
    Input(w, 0, 0); Input(w, 1, 0); Input(w, p, bits); w.Step(); Step(w);
}
static void ShootDifficulty(game::World& w, int p) {
    Input(w, 0, 0); Input(w, 1, 0);
    game::WorldInput gun;
    gun.kind = 1; gun.x = 320; gun.y = 300;  // Difficulty's displayed row.
    gun.fired = true; gun.pressed = gun.held = 4;
    w.SetInput(p, gun); w.Step(); Step(w);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Require(play::WorldKeyboardBits(8, true, true) == 4, "Enter confirms, not pauses");
    Require(play::WorldKeyboardBits(play::kEscapeKey, true, true) == 8, "Escape pauses");
    Require(play::WorldKeyboardBits(play::kEscapeKey, false, false) == 2, "Escape backs out of menus");
    Require(play::WorldKeyboardBits(8, true, false) == 8, "Enter still continues an inactive player");
    game::World w;
    std::string error;
    Require(w.Load(argv[1], 0, &error), error.c_str());
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = cfg.portNetworkRoom = true;
    cfg.gameMode = 6; cfg.gameOver = true;
    cfg.startFrame = 901; cfg.seedCounter = 902;
    cfg.creditSetting = -1;
    cfg.applySaveSettings = true;
    cfg.savePatch = {{game::SaveData::kDifficulty, {2}},
                     {game::SaveData::kCredits, {8}}};  // The room's explicit unlimited rule wins.
    Require(w.StartNetworkGame(cfg, &error), error.c_str());
    Require(w.NetworkLobby() && w.Frame() == 901, "connect opens room at replay origin");
    Input(w, 1, 0x2C); w.Step();
    Require(w.NetworkLobby() && w.Hud().menuCursor == 0, "guest cannot select or start a mode");
    Step(w, 90);
    Require(w.NetworkLobby(), "room does not automatically start");
    Require(w.GetPortGameOptions().difficulty == 2, "room starts with shared saved difficulty");
    for (int i = 0; i < 6; ++i) Tap(w, 0, 0x20);
    Require(w.Hud().menuCursor == 6, "difficulty follows the six existing mode rows");
    Tap(w, 1, 0xCC); ShootDifficulty(w, 1);
    Require(w.NetworkLobby() && w.Hud().menuCursor == 6 && w.GetPortGameOptions().difficulty == 2,
            "guest keyboard and gun cannot change difficulty or start a game");
    Tap(w, 0, 0x80);
    Require(w.GetPortGameOptions().difficulty == 3 && w.NetworkLobby(), "host right selects Hard without starting");
    Tap(w, 0, 0x40);
    Require(w.GetPortGameOptions().difficulty == 2, "host left selects previous difficulty");
    Tap(w, 0, 4);
    Require(w.GetPortGameOptions().difficulty == 3 && w.NetworkLobby(), "host Enter cycles difficulty");
    ShootDifficulty(w, 0);
    Require(w.GetPortGameOptions().difficulty == 4 && w.NetworkLobby(), "host click or shot cycles difficulty");
    Tap(w, 0, 0x80);
    Require(w.GetPortGameOptions().difficulty == 0, "difficulty wraps from Very Hard to Very Easy");
    Tap(w, 0, 0x40);
    Require(w.GetPortGameOptions().difficulty == 4, "difficulty wraps backward");
    Tap(w, 0, 0x40);  // Hard for the following games.
    Tap(w, 0, 0x20);
    Require(w.Hud().menuCursor == 7, "Leave follows Difficulty");
    Tap(w, 0, 0x20);
    for (int choice : {0, 1, 2, 3, 5, 0}) {
        for (int i = 0; i < choice; ++i) { Input(w, 0, 0x20); w.Step(); Step(w); }
        Require(w.Hud().menuCursor == choice, "host navigates room");
        Input(w, 0, 4); w.Step(); Step(w, 500);
        Require(w.GameMode() == 6, "selected mode starts");
        Require(w.Hud().gameType == ((choice == 1 || choice == 5) ? 1 : choice >= 2 ? 3 : 0), "correct mode type");
        Require((w.Hud().randomizerSeed != 0) == (choice == 5), "randomizer is isolated from other modes");
        Require(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5, "both reserved players start together");
        Require(w.Draws().size() > 5, "mode has scene geometry");
        Require(w.GetPortGameOptions().difficulty == 3, "selected difficulty survives mode setup and Store");
        if (choice == 0) {
            Require(w.Hud().rank == 12, "Hard is actually applied: initial two-player rank is 12");
            Require(w.Hud().credits == 0, "shared save cannot override unlimited room continues");
        }
        if (choice == 2) {
            // Scene Selection is the native chapter chooser. Select Chapter 3, then launch it.
            for (int i = 0; i < 2; ++i) { Input(w, 0, 0x80); w.Step(); Step(w, 90); }
            Input(w, 0, 4); w.Step(); Step(w, 500);
            printf("scene result: mode %d type %d chapter %d flags %X frame %d\n", w.GameMode(), w.Hud().gameType, w.Hud().titleChapter, w.Hud().flags5900, w.Frame());
            Require(w.Hud().gameType == 0 && w.Hud().titleChapter == 2, "scene selection starts Chapter 3");
        }
        if (choice == 3) {
            Input(w, 0, 4); w.Step(); Step(w, 300);
            Require((w.Hud().flags5900 & 0x18) == 0, "boss selected and fight started");
        }
        if (choice == 0) {
            Input(w, 1, 8); w.Step();
            Require(w.Hud().paused && w.Hud().pausePlayer == 1, "guest can pause");
        }
        const int before = w.Frame();
        w.RequestTitle(); Step(w);
        Require(w.NetworkLobby() && !w.Hud().paused && w.Frame() == before + 1,
                "return preserves room and timeline, clears pause");
        Require(w.Hud().menuCursor == 0, "room resets to Arcade");
        Require(w.GetPortGameOptions().difficulty == 3, "difficulty persists between games in the same room");
        Require(w.Hud().texFiles.empty() && w.Hud().sprites.size() > 100,
                "returning lobby font and background remain visible after stage texture residency");
        printf("PASS mode %d: both players start and return to the same room\n", choice);
    }
    // A fresh replay includes difficulty changes before both games. Each peer
    // must reproduce the effective rank as well as the shared save and geometry.
    game::World a, b;
    Require(a.Load(argv[1]) && b.Load(argv[1]), "replay worlds load");
    Require(a.StartNetworkGame(cfg) && b.StartNetworkGame(cfg), "replay worlds start");
    for (int f = 0; f < 1300; ++f) {
        for (auto* world : {&a, &b}) {
            if (f == 700) world->RequestTitle();
            uint32_t host = 0;
            if ((f >= 20 && f <= 30 && f % 2 == 0) || (f >= 720 && f <= 730 && f % 2 == 0)) host = 0x20;
            if ((f >= 60 && f <= 70 && f % 2 == 0) || (f >= 750 && f <= 760 && f % 2 == 0)) host = 0x10;
            if (f == 40 || f == 42 || f == 48 || f == 50) host = 0x80;
            if (f == 44 || f == 740 || f == 742) host = 0x40;
            if (f == 46 || f == 100 || f == 800) host = 4;
            Input(*world, 0, host);
            Input(*world, 1, f == 35 || f == 735 ? 0x8C : 0);
            world->Step();
        }
        Require(a.Frame() == b.Frame() && a.RandState() == b.RandState() && a.GameMode() == b.GameMode()
                && a.Hud().menuCursor == b.Hud().menuCursor && a.Hud().rank == b.Hud().rank
                && a.GetPortGameOptions().difficulty == b.GetPortGameOptions().difficulty,
                "replayed room/game state and difficulty match");
        if (f == 51) Require(a.GetPortGameOptions().difficulty == 1, "first replay game selects Easy");
        if (f == 743) Require(a.GetPortGameOptions().difficulty == 4, "second replay game selects Very Hard");
        if (f == 110) Require(a.Hud().rank == 7, "Easy replay game applies two-player rank 7");
        if (f == 810) Require(a.Hud().rank == 15, "Very Hard replay game applies capped two-player rank 15");
        Require(a.Draws().size() == b.Draws().size(), "replayed draw count");
        for (size_t i = 0; i < a.Draws().size(); ++i)
            Require(a.Draws()[i].model == b.Draws()[i].model &&
                    memcmp(a.Draws()[i].world.m, b.Draws()[i].world.m, sizeof(a.Draws()[i].world.m)) == 0,
                    "replayed draw matrices");
    }
    // The inverse case: a locally saved free-play preference must not replace
    // the host's finite-continues room rule when saved difficulty is applied.
    game::World finite;
    Require(finite.Load(argv[1]), "finite room world loads");
    auto finiteCfg = cfg;
    finiteCfg.creditSetting = 2;
    finiteCfg.savePatch.push_back({game::SaveData::kCredits, {0xFF}});
    Require(finite.StartNetworkGame(finiteCfg), "finite room starts");
    Tap(finite, 0, 4); Step(finite, 100);
    Require(finite.GameMode() == 6 && finite.Hud().credits > 0, "finite room continues survive saved free play");
    w.LeaveNetworkRoom(); Step(w);
    Require(!w.NetworkRoom() && w.GameMode() == 3, "explicit leave ends room");
    printf("PASS keyboard mappings, host difficulty authority, applied ranks, room rules, repeat games, and 1300 replay frames\n");
}
