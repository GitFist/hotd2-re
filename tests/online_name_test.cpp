// Shared usernames use twelve characters; native saved initials remain three.
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

static void AutomaticSubmissionMenu(game::World& w) {
    Menu(w); Leaderboard(w);
    Require(TextAt(w, 256, 181) == "ABC", "native Arcade initials remain three letters");
    Require(TextAt(w, 24, 466) == "ELIGIBLE RUNS SUBMIT AUTOMATICALLY", "board explains automatic submission");
    for (const auto& line : w.Hud().menuText)
        Require(line.text.find("PUBLISH") == std::string::npos, "manual publish action is removed");
    w.SetPortLeaderboardPublishable(true, "SCORE SAVED / WILL SYNC WHEN ONLINE"); Step(w);
    Require(TextAt(w, 24, 466) == "SCORE SAVED / WILL SYNC WHEN ONLINE", "queued status is visible while browsing");
    Type(w, "IGNORED");
    Require(w.Hud().portLeaderboardInitials.empty(), "retired world name editor cannot change saved identity");
    Click(w, 80, 390);
    Require(w.Hud().nightShiftPage == 3 && w.Hud().portLeaderboardPublishRequests == 0,
            "removed last row does not trigger a stale publish action");
    Click(w, 80, 350, 1);
    Require(w.Hud().nightShiftPage == 0 && w.Hud().portLeaderboardPublishRequests == 0,
            "second gun activates Back where Publish used to be");
    Leaderboard(w); Press(w, 0x10); Press(w, 8);
    Require(w.Hud().nightShiftPage == 0, "keyboard wraps directly to Back");
    Leaderboard(w); Press(w, 2, 1);
    Require(w.Hud().nightShiftPage == 0, "controller reload still returns Home");
    std::puts("PASS automatic-submission status, no manual publication, and keyboard/second-gun Back");
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
    AutomaticSubmissionMenu(w);
    SharedBoardNames(w);
    return 0;
}
