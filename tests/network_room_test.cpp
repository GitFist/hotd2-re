// Connected room lifecycle: P1 authority, all modes, repeat games, and deterministic replay.
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    Require(w.StartNetworkGame(cfg, &error), error.c_str());
    Require(w.NetworkLobby() && w.Frame() == 901, "connect opens room at replay origin");
    Input(w, 1, 0x2C); w.Step();
    Require(w.NetworkLobby() && w.Hud().menuCursor == 0, "guest cannot select or start a mode");
    Step(w, 90);
    Require(w.NetworkLobby(), "room does not automatically start");
    for (int choice : {0, 1, 2, 3, 5, 0}) {
        for (int i = 0; i < choice; ++i) { Input(w, 0, 0x20); w.Step(); Step(w); }
        Require(w.Hud().menuCursor == choice, "host navigates room");
        Input(w, 0, 4); w.Step(); Step(w, 500);
        Require(w.GameMode() == 6, "selected mode starts");
        Require(w.Hud().gameType == ((choice == 1 || choice == 5) ? 1 : choice >= 2 ? 3 : 0), "correct mode type");
        Require((w.Hud().randomizerSeed != 0) == (choice == 5), "randomizer is isolated from other modes");
        Require(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5, "both reserved players start together");
        Require(w.Draws().size() > 5, "mode has scene geometry");
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
        Require(w.Hud().texFiles.empty() && w.Hud().sprites.size() > 100,
                "returning lobby font and background remain visible after stage texture residency");
        printf("PASS mode %d: both players start and return to the same room\n", choice);
    }
    // A fresh replay of room input must reproduce each frame's RNG, mode, cursor and draw matrices.
    game::World a, b;
    Require(a.Load(argv[1]) && b.Load(argv[1]), "replay worlds load");
    Require(a.StartNetworkGame(cfg) && b.StartNetworkGame(cfg), "replay worlds start");
    for (int f = 0; f < 1300; ++f) {
        for (auto* world : {&a, &b}) {
            if (f == 700) world->RequestTitle();
            Input(*world, 0, f == 100 || f == 800 ? 4 : 0);
            Input(*world, 1, 0);
            world->Step();
        }
        Require(a.Frame() == b.Frame() && a.RandState() == b.RandState() && a.GameMode() == b.GameMode()
                && a.Hud().menuCursor == b.Hud().menuCursor, "replayed room/game state matches");
        Require(a.Draws().size() == b.Draws().size(), "replayed draw count");
        for (size_t i = 0; i < a.Draws().size(); ++i)
            Require(a.Draws()[i].model == b.Draws()[i].model &&
                    memcmp(a.Draws()[i].world.m, b.Draws()[i].world.m, sizeof(a.Draws()[i].world.m)) == 0,
                    "replayed draw matrices");
    }
    w.LeaveNetworkRoom(); Step(w);
    Require(!w.NetworkRoom() && w.GameMode() == 3, "explicit leave ends room");
    printf("PASS keyboard mappings, host authority, repeat games, and 1300 replay frames\n");
}
