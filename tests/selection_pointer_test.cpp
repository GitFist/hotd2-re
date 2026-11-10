// Actual front-end / selection-task flows, including independent inventory choices and network authority.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "game/world.h"
#include "game/exe_image.h"
#include "game/menu_text.h"
#include "game/save_data.h"
#include "game/world_modes.h"

static void Check(bool ok, const char* why) { if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); } }
static void Step(game::World& w, int x = 639, int y = 0, bool shot = false, int player = 0, uint32_t keys = 0) {
    w.SetInput(0, {}); w.SetInput(1, {});
    game::WorldInput in;
    in.kind = 1; in.x = x; in.y = y; in.fired = shot;
    in.pressed = in.held = keys | (shot ? 12u : 0u); // exercise the device's synthetic Start edge too
    w.SetInput(player, in); w.Step();
}
static void Wait(game::World& w, int n = 30) { for (int i = 0; i < n; ++i) Step(w); }
static game::WorldConfig Config() {
    game::WorldConfig c; c.portFeatures = c.portModernMenu = true;
    c.savePatch.push_back({game::SaveData::kItems, std::vector<uint8_t>(33, 3)});
    c.savePatch.push_back({game::SaveData::kBossGrades, std::vector<uint8_t>(10, 3)});
    c.savePatch.push_back({game::SaveData::kTrainGrades, std::vector<uint8_t>(10, 3)});
    return c;
}
static void Open(game::World& w, int row) {
    auto c = Config();
    if (row == 4) c.portModernMenu = false;  // the original ranking carousel belongs to Classic
    w.Boot(c); Wait(w, 650); Step(w, 639, 0, false, 0, 8); Wait(w, 4);
    Check(w.GameMode() == 4, "main menu opens");
    if (row == 4) {
        for (int i = 0; i < 4; ++i) { Step(w, 639, 0, false, 0, 0x20); Step(w); }
        Step(w, 639, 0, false, 0, 8);
    } else {
        Step(w, 40, row == 7 ? 211 : 175, true); Wait(w, 2);  // Practice or Play category
        const int y = row == 1 ? 193 : row == 3 ? 273 : row == 2 ? 233 : row == 7 ? 162 : 153;
        Step(w, 40, y, true);
    }
    Wait(w, 600);
}
static bool Sprite(const game::World& w, int id, float x = -1, float y = -1) {
    for (const auto& s : w.Hud().sprites)
        if (s.id == id && (x < 0 || s.x == x) && (y < 0 || s.y == y)) return true;
    return false;
}
static bool CenteredText(const game::World& w, const game::ExeImage& exe, const std::string& text, float y, float scale) {
    std::vector<game::SpriteDef> expected;
    game::MenuTextScaled(exe, 320 - text.size() * game::MenuTextAdvance(exe, scale) * .5f, y, text, 1, scale, expected);
    for (const auto& glyph : expected) {
        bool found = false;
        for (const auto& actual : w.Hud().sprites)
            if (actual.id == glyph.id && actual.x == glyph.x && actual.y == glyph.y &&
                actual.sx == glyph.sx && actual.sy == glyph.sy) found = true;
        if (!found) return false;
    }
    return !expected.empty();
}
static void InventoryScroll(const game::World& w, bool above, bool below) {
    Check(w.Hud().menuScrollHints.size() == 1, "inventory supplies one current scroll cue");
    const auto& hint = w.Hud().menuScrollHints[0];
    Check(hint.left == 40 && hint.top == 74 && hint.right == 308 && hint.bottom == 294,
          "inventory scroll cue stays within the stock list");
    Check(hint.above == above && hint.below == below, "inventory scroll cue matches reachable stock rows");
}
static void Room(game::World& w, const char* mode, int stock = 3) {
    auto c = Config(); c.portNetworkRoom = true; c.gameMode = 6; c.gameOver = true;
    c.savePatch[0].second.assign(33, static_cast<uint8_t>(stock));
    std::string error; Check(w.StartNetworkGame(c, &error), error.c_str());
    Wait(w, 3);
    Check(w.NetworkLobby(), "connected room opens before selecting a mode");
    int x = -1, y = -1;
    for (const auto& label : w.Hud().menuText) {
        if (label.text != mode) continue;
        x = int(label.x) + 8;
        y = int(label.y) + 6;
        break;
    }
    Check(x >= 0, "requested room mode has a visible label");
    // Click the displayed mode. The old 30-pixel row assumption clicked Horde
    // instead of Boss Mode in the lobby's 24-pixel layout.
    Step(w, x, y, true); Wait(w, 600);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    {
        auto credits = std::make_unique<game::World::Impl>();
        credits->modes = std::make_unique<game::ModesState>();
        credits->cfg.portFeatures = true;
        credits->cfg.freePlay = false;
        credits->gameMode = 4;
        credits->front.menuConfirmed = true;
        credits->front.gameType = 3;
        credits->cfg.credits = game::ModesCredits(*credits, 3);
        Check(credits->cfg.credits == 0, "Boss entries do not create shared continue credits");
        Check(credits->UseCredit(1, false) && credits->UseCredit(0, false), "either player can take the first Boss entry");
        credits->gameMode = 6; credits->cfg.gameType = 3;
        Check(!credits->UseCredit(0, true) && !credits->UseCredit(1, true), "Boss entries cannot be spent on continues");
        Check(!credits->UseCredit(0, false) && !credits->UseCredit(1, false), "eliminated players cannot rejoin the same Boss attempt");
        Check(!credits->CanContinue(), "a solo death still enters the normal Boss retry flow");
        credits->cfg.freePlay = true;
        Check(!credits->CanContinue(), "saved unlimited Arcade continues cannot stall a Boss retry");
        credits->modes->bossJoinedPlayers = 0;
        credits->subState = 9;
        Check(!credits->UseCredit(1, false), "new Boss joins cannot interrupt result/name entry");
    }
    game::World w; std::string error;
    Check(w.Load(argv[1], 0, &error), error.c_str());
    game::ExeImage exe;
    Check(exe.Load((std::filesystem::path(argv[1]) / "Hod2.exe").string()), "tables load");
    const int firstItem = exe.S16(0x0059721C), nextItem = exe.S16(0x0059721E);
    Open(w, 1);
    Step(w, 80, 90, true);
    Check(Sprite(w, firstItem, 365, 159), "shoot stock row equips the pointed-at item");
    Step(w, 390, 165, true);
    Check(!Sprite(w, firstItem, 365, 159), "shoot equipped item returns it to stock");
    Step(w, 80, 122, true);
    Check(Sprite(w, nextItem, 365, 159), "aim selects another stock row");
    Step(w, 80, 90, true); // either compatible second item or warning, never overwrites slot 0
    Check(Sprite(w, nextItem, 365, 159), "item combination preserves first item");
    Wait(w, 70);
    Step(w, 639, 20, true); Wait(w);
    Check(Sprite(w, 0x5E2), "blank shot does not finish Original selection");
    Step(w, 550, 226, true); Wait(w, 180);
    Check(!Sprite(w, 0x5E2), "shoot End starts Original game");

    Open(w, 1);
    InventoryScroll(w, false, true);
    Step(w, 180, 304, true);
    const int scrolledItem = exe.S16(0x0059721C + 2);
    Check(Sprite(w, scrolledItem, 43, 78), "down arrow scrolls inventory without equipping");
    InventoryScroll(w, true, true);
    Step(w, 180, 50, true);
    Check(Sprite(w, firstItem, 43, 78), "up arrow scrolls inventory back");
    InventoryScroll(w, false, true);
    for (int i = 0; i < 40; ++i) Step(w, 180, 304, true);
    InventoryScroll(w, true, false);
    Step(w, 180, 304, true);
    InventoryScroll(w, true, false);
    Step(w, 50, 458, true); Wait(w, 3);
    Check(w.GameMode() == 4, "item Back button returns to main menu");
    Check(w.Hud().menuScrollHints.empty(), "inventory scroll cue clears when leaving selection");

    Room(w, "ORIGINAL");
    Check(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5, "both players enter item select");
    Step(w, 80, 90, true);
    Check(Sprite(w, firstItem, 365, 114), "P1 equips own slot");
    Step(w, 80, 122, true, 1);
    Check(Sprite(w, nextItem, 365, 242), "P2 equips own slot");
    Step(w, 390, 250, true);
    Check(Sprite(w, nextItem, 365, 242), "P1 cannot remove P2 item");
    Step(w, 550, 168, true); Wait(w);
    Check(Sprite(w, 0x5E2), "one End waits for the other player");
    Step(w, 550, 296, true, 1); Wait(w, 180);
    Check(!Sprite(w, 0x5E2), "both End buttons start the game");
    Room(w, "ORIGINAL", 1);
    game::WorldInput simultaneous;
    simultaneous.kind = 1; simultaneous.x = 80; simultaneous.y = 90;
    simultaneous.fired = true; simultaneous.pressed = simultaneous.held = 12;
    w.SetInput(0, simultaneous); w.SetInput(1, simultaneous); w.Step();
    Check(Sprite(w, firstItem, 365, 114), "first player takes the last copy");
    Check(!Sprite(w, firstItem, 365, 242) && !Sprite(w, nextItem, 365, 242),
          "simultaneous second shot cannot duplicate the last item or equip a shifted stock row");

    Open(w, 3);
    const int boss0 = int(exe.U32(0x0055CC0C)), boss1 = int(exe.U32(0x0055CC10));
    Check(Sprite(w, boss0, 321, 100), "Boss starts on first card");
    Step(w, 480, 194, true); Wait(w);
    Check(Sprite(w, boss1, 321, 100), "shoot right arrow chooses next boss without starting");
    Step(w, 160, 194, true); Wait(w);
    Check(Sprite(w, boss0, 321, 100), "shoot left arrow chooses previous boss");
    Step(w, 190, 459, true); Wait(w);
    Check(Sprite(w, boss0, 321, 100), "difficulty arrow does not start boss");
    Check(Sprite(w, exe.S16(0x0056AED0 + '3' * 2), 320, 434), "difficulty previous changes Level 4 to Level 3");
    Step(w, 394, 459, true); Wait(w);
    Check(Sprite(w, exe.S16(0x0056AED0 + '4' * 2), 320, 434), "difficulty next restores Level 4");
    Step(w, 639, 20, true); Wait(w);
    Check(Sprite(w, boss0, 321, 100), "blank shot does not start boss");
    Step(w, 320, 200, true); Wait(w, 180);
    Check(!Sprite(w, 0xB30), "shoot centre card starts boss");

    Open(w, 3);
    Step(w, 639, 0, false, 1, 8); Wait(w);
    Check(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5 && Sprite(w, 0xB30),
          "local P2 joins the Boss chooser without spending P1's credit or starting the fight");
    Step(w, 540, 459, true); Wait(w, 300);
    Check(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5 && (w.Hud().flags5900 & 0x18) == 0,
          "local co-op starts the chosen Boss with both players");
    bool retry = false;
    for (int frame = 0; frame < 16000; ++frame) {
        Step(w);
        if (w.GameMode() == 6 && (w.Hud().flags5900 & 0x18) == 0x18 && Sprite(w, 0xB30)) { retry = true; break; }
    }
    Check(retry && w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5,
          "both eliminated local players return to the Boss chooser for a fresh attempt");
    Wait(w); Step(w, 540, 459, true); Wait(w, 300);
    Check(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5 && (w.Hud().flags5900 & 0x18) == 0,
          "local Boss retry starts both players again");
    Open(w, 3); Step(w, 540, 459, true); Wait(w, 300);
    Step(w, 639, 0, false, 1, 8); Wait(w, 600);
    Check(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5,
          "local P2 can also join after the Boss fight starts");

    Open(w, 2);
    Check(CenteredText(w, exe, "CHALLENGE MODE", 30, 1.25f), "Challenges uses Challenge Mode heading");
    const char* challenges[] = {
        "CIVILIAN RESCUE", "30-SHOT CLEAR", "BARREL BLITZ", "ONE-SHOT FROG", "RAPID-FIRE BOSS",
        "ENEMY SWEEP", "DISTANT TARGETS", "CAR DEFENSE", "FRIEND OR FOE", "COIN JUGGLE"
    };
    for (int i = 0; i < 10; ++i) {
        if (i) { Step(w, 480, 194, true); Wait(w); }
        Check(CenteredText(w, exe, challenges[i], 78, .85f), "carousel shows each challenge's objective title");
        Check(!Sprite(w, 0xAFE) && !Sprite(w, 0xB38), "challenge titles replace stock training labels");
    }
    Step(w, 50, 458, true); Wait(w, 3);
    Check(w.GameMode() == 4, "Challenges Back returns to main menu");
    Open(w, 2);
    Step(w, 540, 459, true); Wait(w, 80);
    Check(CenteredText(w, exe, challenges[0], 78, .85f), "challenge instructions retain the objective title");
    Check(!Sprite(w, 0xAFE), "challenge instructions replace stock training name");
    Step(w, 639, 20, true); Wait(w);
    Check(!w.Hud().canShoot, "blank shot does not begin the challenge instructions");
    Step(w, 540, 459, true);
    for (int i = 0; i < 5000 && !Sprite(w, 0xB01); ++i) Step(w);
    Check(Sprite(w, 0xB01) && Sprite(w, 0xB2E), "failed challenge exposes Retry and Back");
    Check(CenteredText(w, exe, challenges[0], 78, .85f), "challenge result retains the objective title");
    Step(w, 510, 382);
    bool backHighlight = false;
    for (const auto& s : w.Hud().sprites) if (s.id == 0xB2E && s.sx == 1) backHighlight = true;
    Check(backHighlight, "aiming at challenge Back highlights it");
    Step(w, 510, 382, true); Wait(w, 120);
    Check(CenteredText(w, exe, "CHALLENGE MODE", 30, 1.25f), "shoot challenge result Back returns to selection");
    Open(w, 7);
    Step(w, 480, 194, true); Wait(w);
    Step(w, 480, 194, true); Wait(w);
    Step(w, 540, 459, true); Wait(w, 500);
    Check(w.Hud().titleChapter == 2 && w.Hud().gameType == 0, "Scene Select arrows and Start launch chapter three");
    Open(w, 4);
    Step(w, 570, 426, true); Wait(w, 100);
    Check(w.GameMode() == 8 && Sprite(w, 0xA1F), "ranking next shows Original rankings");
    Step(w, 80, 426, true); Wait(w, 100);
    Check(w.GameMode() == 8 && Sprite(w, 0x98D), "ranking previous returns to Arcade rankings");
    Step(w, 310, 458, true); Wait(w, 3);
    Check(w.GameMode() == 4, "ranking Back returns to menu");

    Room(w, "BOSS MODE");
    Check(w.Hud().gameType == 3 && Sprite(w, boss0, 321, 100), "room Boss Mode opens the first boss card");
    const int level3 = exe.S16(0x0056AED0 + '3' * 2), level4 = exe.S16(0x0056AED0 + '4' * 2);
    for (uint32_t key : {0x40u, 0x80u, 0x10u, 0x20u, 4u, 8u, 2u}) {
        Step(w, 639, 0, false, 1, key); Wait(w);
        Check(Sprite(w, boss0, 321, 100) && Sprite(w, level4, 320, 434),
              "guest keyboard cannot change boss or difficulty, start, or leave the chooser");
    }
    Step(w, 480, 194, true, 1); Wait(w);
    Check(Sprite(w, boss0, 321, 100), "guest cannot choose the boss");
    Step(w, 190, 459, true, 1); Wait(w);
    Check(Sprite(w, level4, 320, 434), "guest pointer cannot change boss difficulty");
    Step(w, 540, 459, true, 1); Wait(w);
    Check(Sprite(w, 0xB30), "guest cannot start the boss");
    Step(w, 50, 458, true, 1); Wait(w);
    Check(!w.NetworkLobby() && Sprite(w, boss0, 321, 100), "guest pointer cannot leave the boss chooser");
    Step(w, 480, 194, true); Wait(w);
    Check(Sprite(w, boss1, 321, 100), "host can choose the boss");
    Step(w, 639, 0, false, 0, 0x40); Wait(w);
    Check(Sprite(w, boss0, 321, 100), "host keyboard can choose the previous boss");
    Step(w, 190, 459, true); Wait(w);
    Check(Sprite(w, level3, 320, 434), "host pointer changes boss difficulty");
    Step(w, 639, 0, false, 0, 0x10); Wait(w);
    Check(Sprite(w, level4, 320, 434), "host keyboard changes boss difficulty");
    Step(w, 50, 458, true); Wait(w, 3);
    Check(w.NetworkLobby(), "Back returns both players to shared lobby");
    Step(w, 639, 20, true); Wait(w);
    Check(w.NetworkLobby(), "blank lobby shot cannot start a game");

    Room(w, "BOSS MODE");
    Step(w, 540, 459, true); Wait(w, 300);
    Check(w.NetworkRoom() && !w.NetworkLobby() && w.Hud().gameType == 3 && (w.Hud().flags5900 & 0x18) == 0,
          "host Start launches the boss fight inside the same room");
    Check(w.Hud().player[0].state == 5 && w.Hud().player[1].state == 5,
          "both reserved players enter the boss fight");
    const int beforeReturn = w.Frame();
    w.RequestTitle(); Wait(w, 3);
    Check(w.NetworkLobby() && w.Frame() == beforeReturn + 3, "leaving the boss fight preserves the room and timeline");
    puts("PASS: Original inventory and both players' End buttons; Boss, Challenges, Scene, rankings and host authority");
}
