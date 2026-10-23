// The online name editor uses twelve characters; native saved initials remain three.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/save_data.h"
#include "game/world.h"

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static void Step(game::World& w, const game::WorldInput& in = {}, int player = 0) {
    w.SetInput(0, player == 0 ? in : game::WorldInput{});
    w.SetInput(1, player == 1 ? in : game::WorldInput{});
    w.Step();
}

static void Press(game::World& w, uint32_t buttons, int player = 0) {
    game::WorldInput in;
    in.held = in.pressed = buttons;
    Step(w, in, player);
    Step(w);
}

static void Click(game::World& w, int x, int y, int player = 0, bool offscreen = false) {
    game::WorldInput in;
    in.kind = 1; in.x = x; in.y = y; in.fired = true; in.offscreen = offscreen;
    in.held = in.pressed = 4;
    Step(w, in, player);
    Step(w);
}

static void Type(game::World& w, const std::string& text, bool backspace = false) {
    w.EditPortLeaderboardName(text, backspace);
    Step(w);
}

static std::string TextAt(const game::World& w, float x, float y) {
    for (const auto& item : w.Hud().menuText)
        if (item.x == x && item.y == y) return item.text;
    return {};
}

static void ExpectName(const game::World& w, const std::string& name, const char* why) {
    Require(w.Hud().portLeaderboardInitials == name, why);
    Require(TextAt(w, 80, 136) == (name.empty() ? "ENTER YOUR NAME" : name),
            "the complete edited name is visible in the input field");
    Require(TextAt(w, 474, 102) == std::to_string(name.size()) + " / 12",
            "the editor reports the actual name length out of twelve");
}

static void Menu(game::World& w) {
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = true;
    // Use known native records without modifying the game's files.
    cfg.savePatch = {{game::SaveData::kRankings, {'A','B','C',0}},
                     {game::SaveData::kRankings + 0xA0, {'D','E','F',0}}};
    w.Boot(cfg);
    w.ForceMode(4, 0x10000, 0, 0);
    Step(w); Step(w); Step(w);
    Require(w.GameMode() == 4 && w.Hud().nightShiftPage == 0, "enter Night Shift home");
}

static void Leaderboard(game::World& w) {
    Click(w, 100, 248);
    Require(w.Hud().nightShiftPage == 3, "Home opens the leaderboard");
}

static void OpenName(game::World& w) {
    w.SetPortLeaderboardPublishable(true, "NEW RUN AVAILABLE");
    Click(w, 80, 350);
    Require(w.Hud().nightShiftPage == 4, "a qualifying run opens online name entry");
}

static void EditingAndPublish(game::World& w) {
    Menu(w);
    Type(w, "HOME");
    Require(w.Hud().portLeaderboardInitials.empty(), "typing on Home cannot edit the online name");
    Leaderboard(w);
    Require(TextAt(w, 256, 181) == "ABC", "native Arcade initials start as three letters");
    Type(w, "BOARD");
    Require(w.Hud().portLeaderboardInitials.empty(), "typing on the leaderboard cannot edit the name");
    Click(w, 80, 350);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 0,
            "an ineligible run cannot open name entry or publish");
    OpenName(w);
    ExpectName(w, {}, "online name starts empty without copying local initials");
    Click(w, 380, 386);
    Require(w.Hud().nightShiftPage == 4 && w.Hud().portLeaderboardPublishRequests == 0,
            "empty Publish is blocked");
    Type(w, "   ");
    Press(w, 8);
    Require(w.Hud().nightShiftPage == 4 && w.Hud().portLeaderboardPublishRequests == 0,
            "a whitespace-only typed name cannot publish with Enter");
    Click(w, 250, 386);
    ExpectName(w, {}, "Clear removes all spaces");
    Type(w, std::string("a.z_9 -!?\t\n") + char(0) + "\xC3\xA9");
    ExpectName(w, "A.Z_9 -", "typing uppercases and accepts only the online name character set");
    Type(w, {}, true);
    ExpectName(w, "A.Z_9 ", "keyboard Backspace removes exactly one character");
    Click(w, 120, 386);
    ExpectName(w, "A.Z_9", "pointer Delete removes exactly one character");
    Click(w, 250, 386);
    Type(w, {}, true);
    Click(w, 120, 386);
    ExpectName(w, {}, "Backspace and Delete are safe on an empty name");
    Type(w, "abcdefghijklmnop");
    ExpectName(w, "ABCDEFGHIJKL", "typed names stop at twelve characters");
    Click(w, 80, 200);
    ExpectName(w, "ABCDEFGHIJKL", "pointer keys obey the same twelve-character limit");
    Click(w, 250, 386);
    Type(w, "long_name-12extra");
    ExpectName(w, "LONG_NAME-12", "all twelve characters of an alternate-character name are retained");
    Click(w, 630, 465);
    Require(w.Hud().nightShiftPage == 4 && w.Hud().portLeaderboardPublishRequests == 0,
            "a blank pointer shot does not activate the keyboard-selected Publish");
    Press(w, 8);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 1 &&
            w.Hud().portLeaderboardInitials == "LONG_NAME-12",
            "typing followed by Enter publishes the full name exactly once");
    Require(TextAt(w, 256, 181) == "ABC", "publishing does not replace native three-letter initials");
    Type(w, "Z", true);
    Click(w, 80, 350);
    Press(w, 8); Press(w, 4, 1);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 1 &&
            w.Hud().portLeaderboardInitials == "LONG_NAME-12",
            "a consumed run cannot publish again and editing outside the dialog is ignored");
    Click(w, 80, 230);
    Require(TextAt(w, 256, 181) == "DEF", "native Original initials remain a separate three-letter record");
    std::puts("PASS online typing, validation, twelve-character limit and exactly one final publish");
}

static void PointerAndCancel(game::World& w) {
    Menu(w); Leaderboard(w); OpenName(w);
    const std::string keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
    for (size_t i = 0; i < keys.size(); ++i) {
        Click(w, 250, 386);
        Click(w, 80 + int(i % 10) * 52, 200 + int(i / 10) * 40, int(i % 2));
        ExpectName(w, keys.substr(i, 1), "either gun can enter every letter, digit and punctuation key");
    }
    Click(w, 80, 200, 1, true);
    ExpectName(w, "-", "an offscreen shot cannot select a character key");
    Click(w, 500, 386, 1);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 0,
            "the second gun can Cancel without publishing");
    OpenName(w);
    Press(w, 2, 1);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 0,
            "controller Reload cancels without publishing");
    OpenName(w);
    Press(w, 0x40);  // Left from the first character wraps to Cancel.
    Press(w, 8);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 0,
            "keyboard navigation reaches Cancel without publishing");
    OpenName(w);
    Click(w, 250, 386);
    Type(w, "  a.b_9-  ");
    Click(w, 380, 386, 1);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 1 &&
            w.Hud().portLeaderboardInitials == "A.B_9-",
            "the second gun publishes once and trims only outside spaces");
    std::puts("PASS all forty pointer keys, both guns, Cancel paths and trimmed pointer publishing");
}

static void KeyboardAndController(game::World& w) {
    Menu(w); Leaderboard(w); OpenName(w);
    Press(w, 8);                          // A
    Press(w, 0x80); Press(w, 4);          // B
    Press(w, 0x20); Press(w, 8);          // L
    Press(w, 0x40); Press(w, 8);          // K
    Press(w, 0x10); Press(w, 8);          // A
    Press(w, 0x80, 1); Press(w, 4, 1);    // B, using the second pad.
    ExpectName(w, "ABLKAB", "both pads navigate the character grid in all four directions");
    Click(w, 250, 386);                  // Clear, then navigate between actions and the grid.
    Press(w, 0x10, 1); Press(w, 4, 1);    // 7
    ExpectName(w, "7", "Up from Clear reaches the fourth row of character keys");
    Press(w, 0x20, 1); Press(w, 4, 1);    // Clear
    ExpectName(w, {}, "Down from the final character row reaches the action buttons");
    Press(w, 0x40); Press(w, 8);          // Delete on an empty name.
    ExpectName(w, {}, "the action row remains keyboard accessible");
    Press(w, 2);
    Require(w.Hud().portLeaderboardPublishRequests == 0, "navigation and editing never publish implicitly");
    std::puts("PASS keyboard and controller grid/action navigation");
}

static void SharedBoardNames(game::World& w) {
    Menu(w); Leaderboard(w);
    Click(w, 80, 150);
    Require(w.Hud().portLeaderboardShared, "switch to shared leaderboards");
    for (int table = 0; table <= 12; ++table) {
        if (table < 4) Click(w, 80, 190 + table * 40);
        else Click(w, 589, 100);
        Require(w.Hud().portLeaderboardTable == table, "select every shared board including all bosses");
        w.SetPortSharedLeaderboard(table, {{"LONG_NAME-12", 12345, 7, 2, 1}}, "COMMUNITY RUNS / UNVERIFIED");
        Step(w);
        Require(TextAt(w, 250, 181) == "LONG_NAME-12",
                "Arcade, Original, Horde and every Boss board show the complete twelve-character name");
    }
    Click(w, 80, 190);
    Click(w, 80, 150);
    Require(!w.Hud().portLeaderboardShared && TextAt(w, 256, 181) == "ABC",
            "returning from shared boards preserves the native three-letter Arcade name");
    std::puts("PASS twelve-character names on all thirteen shared boards with separate local initials");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    static_assert(sizeof(game::RankEntry::name) == 4, "native save names remain three letters plus terminator");
    game::World w;
    std::string error;
    Require(w.Load(argv[1], 0, &error), error.c_str());
    EditingAndPublish(w);
    PointerAndCancel(w);
    KeyboardAndController(w);
    SharedBoardNames(w);
    return 0;
}
