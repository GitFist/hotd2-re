// Connected room lifecycle: P1 authority, all modes, repeat games, and deterministic replay.
#include <algorithm>
#include <cctype>
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
    gun.kind = 1; gun.x = 320; gun.y = 310;  // Difficulty's displayed row.
    gun.fired = true; gun.pressed = gun.held = 4;
    w.SetInput(p, gun); w.Step(); Step(w);
}
static bool HasMenuText(const game::World& w, const char* text, bool ignoreCase = false) {
    for (const auto& line : w.Hud().menuText) {
        if (line.text == text) return true;
        if (ignoreCase && line.text.size() == std::strlen(text) &&
            std::equal(line.text.begin(), line.text.end(), text, [](unsigned char a, unsigned char b) {
                return std::toupper(a) == std::toupper(b);
            })) return true;
    }
    return false;
}
static void ModernRoom(const game::World& w, const char* difficulty) {
    Require(w.Hud().nightShiftPage == 10 && !w.Hud().menuPanels.empty() && !w.Hud().menuText.empty(),
            "connected room exposes the Night Shift panel and text presentation");
    for (const char* label : {"ARCADE", "ORIGINAL", "BOSS MODE", "HORDE MODE", "RANDOMIZER", "TYPING ORIGINAL", "LEAVE ROOM", "DIFFICULTY"})
        Require(HasMenuText(w, label), "modern room retains its mode, difficulty and leave labels");
    Require(HasMenuText(w, difficulty, true), "modern room displays the effective shared difficulty");
    int selected = 0;
    bool matchesCursor = false;
    const float centerY = (5.0f + w.Hud().menuCursor) * 24.0f + 22.0f;
    for (const auto& panel : w.Hud().menuPanels) {
        if (panel.color != 0xED8F2026) continue;
        ++selected;
        matchesCursor = panel.left == 184 && panel.right == 600 && panel.top <= centerY && panel.bottom > centerY;
    }
    Require(selected == 1 && matchesCursor, "one red room button follows the selected clickable row");
}
static void Aim(game::World& w, int x, int y, uint32_t buttons = 0, bool shot = false) {
    game::WorldInput gun;
    gun.kind = 1; gun.x = x; gun.y = y;
    gun.pressed = gun.held = buttons; gun.fired = shot;
    w.SetInput(0, gun); w.SetInput(1, {}); w.Step();
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Require(std::getenv("HOTD2_TYPING_REFERENCE")!=nullptr,"set HOTD2_TYPING_REFERENCE to verify the Typing room mode");
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
    ModernRoom(w, "NORMAL");
    Aim(w, 320, 168);
    Require(w.Hud().menuCursor == 1, "host aim selects the displayed Original row");
    Aim(w, 320, 168, 0x20); Aim(w, 320, 168);
    Require(w.Hud().menuCursor == 2, "parked room pointer preserves keyboard navigation");
    ModernRoom(w, "NORMAL");
    Aim(w, 630, 465, 4, true);
    Require(w.NetworkLobby() && w.Hud().menuCursor == 2, "blank room shot cannot start the focused mode");
    Aim(w, 320, 142);
    Require(w.Hud().menuCursor == 0, "moving room pointer selects Arcade again");
    for (int i = 0; i < 7; ++i) Tap(w, 0, 0x20);
    Require(w.Hud().menuCursor == 7, "difficulty follows the seven mode rows including Typing Original");
    ModernRoom(w, "NORMAL");
    Tap(w, 1, 0xCC); ShootDifficulty(w, 1);
    Require(w.NetworkLobby() && w.Hud().menuCursor == 7 && w.GetPortGameOptions().difficulty == 2,
            "guest keyboard and gun cannot change difficulty or start a game");
    Tap(w, 0, 0x80);
    Require(w.GetPortGameOptions().difficulty == 3 && w.NetworkLobby(), "host right selects Hard without starting");
    ModernRoom(w, "HARD");
    Tap(w, 0, 0x40);
    Require(w.GetPortGameOptions().difficulty == 2, "host left selects previous difficulty");
    Tap(w, 0, 4);
    Require(w.GetPortGameOptions().difficulty == 3 && w.NetworkLobby(), "host Enter cycles difficulty");
    ShootDifficulty(w, 0);
    Require(w.GetPortGameOptions().difficulty == 4 && w.NetworkLobby(), "host click or shot cycles difficulty");
    ModernRoom(w, "VERY HARD");
    Tap(w, 0, 0x80);
    Require(w.GetPortGameOptions().difficulty == 0, "difficulty wraps from Very Hard to Very Easy");
    Tap(w, 0, 0x40);
    Require(w.GetPortGameOptions().difficulty == 4, "difficulty wraps backward");
    Tap(w, 0, 0x40);  // Hard for the following games.
    Tap(w, 0, 0x20);
    Require(w.Hud().menuCursor == 8, "Leave follows Difficulty");
    Tap(w, 0, 0x20);
    for (int choice : {0, 1, 2, 3, 5, 6, 0}) {
        for (int i = 0; i < choice; ++i) { Input(w, 0, 0x20); w.Step(); Step(w); }
        Require(w.Hud().menuCursor == choice, "host navigates room");
        ModernRoom(w, "HARD");
        Input(w, 0, 4); w.Step(); Step(w, 500);
        Require(w.GameMode() == 6, "selected mode starts");
        Require(w.Hud().nightShiftPage != 10,"connected-room page clears when the selected game starts");
        if (choice == 6) {
            bool original[2]={};int itemSlots=0;
            for (const auto& text:w.Hud().menuText) {
                original[0]|=text.text.rfind("1P ORIGINAL",0)==0;
                original[1]|=text.text.rfind("2P ORIGINAL",0)==0;
                if (text.text.size()>3&&text.text[0]=='F'&&text.text[1]>='1'&&text.text[1]<='4'&&text.text[2]==':') ++itemSlots;
            }
            Require(original[0]&&original[1]&&itemSlots==8,"both Typing players receive Original inventory HUDs");
            Require(!HasMenuText(w,"MULTIPLAYER LOBBY")&&!HasMenuText(w,"YOUR PARTY")&&
                std::none_of(w.Hud().menuPanels.begin(),w.Hud().menuPanels.end(),[](const auto& panel) {
                    return panel.left==184&&panel.right==600&&panel.top>=130&&panel.top<365;
                }),"Typing gameplay retains no connected-room labels or button bands");
        } else Require(w.Hud().menuPanels.empty()&&w.Hud().menuText.empty(),
                       "room presentation clears when the selected gun game starts");
        Require(w.Hud().gameType == ((choice == 1 || choice == 5) ? 1 : (choice == 2 || choice == 3) ? 3 : 0), "correct mode type");
        Require(w.PortTypingActive() == (choice == 6), "Typing Original is selectable and isolated from gun modes");
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
        ModernRoom(w, "HARD");
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
            if ((f >= 20 && f <= 32 && f % 2 == 0) || (f >= 720 && f <= 732 && f % 2 == 0)) host = 0x20;
            if ((f >= 60 && f <= 72 && f % 2 == 0) || (f >= 750 && f <= 762 && f % 2 == 0)) host = 0x10;
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
    game::World classic;
    Require(classic.Load(argv[1]), "classic room world loads");
    auto classicCfg = cfg;
    classicCfg.portModernMenu = false;
    Require(classic.StartNetworkGame(classicCfg), "classic connected room starts");
    Step(classic, 2);
    Require(classic.NetworkLobby() && classic.Hud().nightShiftPage < 0 && classic.Hud().menuPanels.empty() &&
            classic.Hud().menuText.empty() && classic.Hud().sprites.size() > 100,
            "Classic connected room retains its original sprite presentation");
    Tap(classic, 0, 4); Step(classic, 100);
    Require(classic.GameMode() == 6, "Classic connected room still starts Arcade");
    classic.RequestTitle(); Step(classic);
    Require(classic.NetworkLobby() && classic.Hud().nightShiftPage < 0 && classic.Hud().menuPanels.empty() &&
            classic.Hud().menuText.empty() && classic.Hud().sprites.size() > 100,
            "Classic room presentation returns after gameplay");
    w.LeaveNetworkRoom(); Step(w);
    Require(!w.NetworkRoom() && w.GameMode() == 3, "explicit leave ends room");
    printf("PASS modern/Classic room presentation, pointer focus, host difficulty authority, applied ranks, room rules, repeat games, and 1300 replay frames\n");
}
