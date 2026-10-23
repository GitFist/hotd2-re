// Real World transition coverage for the local end-of-run prompt. No service
// requests or save writes: score records are local fixtures except the boss replay.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/world.h"
#include "play/run_name_prompt.h"

namespace {
void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
void Tick(game::World& world, uint32_t p1 = 0, uint32_t p2 = 0) {
    for (int p = 0; p < 2; ++p) {
        game::WorldInput input;
        input.held = input.pressed = p == 0 ? p1 : p2;
        world.SetInput(p, input);
    }
    world.Step();
}
void Load(game::World& world, const char* directory) {
    std::string error;
    Require(world.Load(directory, 0, &error), error.c_str());
}
void Skip(play::RunNamePrompt& prompt) {
    play::MenuInput input; input.start = true;
    Require(prompt.Update(input) == play::RunNamePrompt::Action::Skip && !prompt.IsOpen(),
            "Skip closes the local prompt without a publish action");
}
bool BossSelector(const game::WorldHud& hud) {
    return hud.gameMode == 6 && hud.gameType == 3 && hud.subState == 2 && (hud.flags5900 & 0x18) == 0x18;
}
void SameWorld(const game::World& a, const game::World& b) {
    Require(a.Frame() == b.Frame() && a.RandState() == b.RandState() && a.GameMode() == b.GameMode(),
            "local name UI preserves synchronized world frame, random state and mode");
    const auto& x = a.Hud(); const auto& y = b.Hud();
    Require(x.subState == y.subState && x.menuCursor == y.menuCursor && x.gameType == y.gameType &&
                x.paused == y.paused && x.flags5900 == y.flags5900 && a.Draws().size() == b.Draws().size(),
            "local prompt cannot navigate or pause the connected room");
    for (int p = 0; p < 2; ++p)
        Require(x.player[p].state == y.player[p].state && x.player[p].lives == y.player[p].lives &&
                    x.player[p].score == y.player[p].score,
                "local name UI leaves both players unchanged");
}

void GameOverAndTitle(const char* directory, bool modern) {
    game::World world; Load(world, directory);
    game::WorldConfig config; config.portFeatures = true; config.portModernMenu = modern;
    world.Boot(config);
    world.ForceMode(7, 0x10000, 0, 0);
    play::RunNamePrompt prompt;
    Require(prompt.Queue(1, 0, 12345, 0), "queue fixture after a completed run");
    bool sawGameOver = false, sawMap = false;
    for (int count = 0; count < 1000 && world.GameMode() == 7; ++count) {
        const bool map = world.Hud().gameMode == 7 && world.Hud().subState == 5;
        Tick(world, map ? 2 : 0);  // The normal map-dismiss action, after its first frame.
        if (world.GameMode() == 7) {
            sawGameOver |= world.Hud().subState == 1 || world.Hud().subState == 3;
            sawMap |= world.Hud().subState == 5;
            Require(!prompt.MaybeOpen(world.Hud()) && !prompt.IsOpen(),
                    "queued name prompt does not replace game-over or route-map screens");
        }
    }
    Require(sawGameOver && sawMap && world.GameMode() == 3, "native game-over flow reaches the title");
    const int frame = world.Frame(); const auto random = world.RandState();
    Require(prompt.MaybeOpen(world.Hud()) && prompt.IsOpen(), "prompt opens automatically at completed title transition");
    prompt.Edit("player-one");
    game::WorldHud overlay; prompt.ModernHud(overlay);
    Require(!overlay.menuText.empty() && prompt.Name() == "PLAYER-ONE", "either menu style has an editable local name UI");
    Require(world.Frame() == frame && world.RandState() == random && world.GameMode() == 3,
            "opening and editing the prompt do not mutate the underlying title");
    Skip(prompt);
    for (int count = 0; count < 30; ++count) {
        Tick(world);
        Require(!prompt.MaybeOpen(world.Hud()), "skipped completion never reopens on later title frames");
    }
}

void ConnectedRoom(const char* directory, bool modern) {
    game::World withPrompt, control; Load(withPrompt, directory); Load(control, directory);
    game::WorldConfig config;
    config.portFeatures = config.portNetworkRoom = true; config.portModernMenu = modern;
    config.gameMode = 6; config.gameOver = true; config.startFrame = 901; config.seedCounter = 902;
    config.creditSetting = -1;
    std::string error;
    Require(withPrompt.StartNetworkGame(config, &error), error.c_str());
    Require(control.StartNetworkGame(config, &error), error.c_str());
    Require(withPrompt.NetworkLobby(), "connected fixture starts inside the retained room");
    play::RunNamePrompt prompt;
    Require(prompt.Queue(1, 1, 45678, 0) && prompt.MaybeOpen(withPrompt.Hud()),
            "completed online run prompts in the room without disconnecting");
    for (int count = 0; count < 60; ++count) {
        prompt.Edit(count == 0 ? "REMOTE-AGENT" : "");
        play::MenuInput input; input.pressed = count % 2 ? 0x20 : 0x10;
        Require(prompt.Update(input) == play::RunNamePrompt::Action::None, "local navigation does not submit");
        Tick(withPrompt); Tick(control); SameWorld(withPrompt, control);
    }
    Require(withPrompt.NetworkLobby() && prompt.IsOpen(), "room keeps ticking while its local name UI stays open");
    Skip(prompt);
    Tick(withPrompt); Tick(control); SameWorld(withPrompt, control);
    Require(!prompt.MaybeOpen(withPrompt.Hud()), "finished room prompt does not reopen");
    // The next host selection still works normally once the local dialog is dismissed.
    Tick(withPrompt, 4); Tick(control, 4);
    for (int count = 0; count < 5; ++count) { Tick(withPrompt); Tick(control); SameWorld(withPrompt, control); }
    Require(withPrompt.GameMode() == 6, "host can start the next match after entering or skipping a name");
}

void BossReplay(const char* directory, const char* script, bool modern) {
    struct Event { int frame, player; float x, y; int buttons; };
    std::vector<Event> events;
    std::ifstream file(script); std::string line;
    while (std::getline(file, line)) {
        Event event{}; std::istringstream stream(line);
        if (stream >> event.frame >> event.player >> event.x >> event.y >> event.buttons) events.push_back(event);
    }
    Require(!events.empty(), "load actual boss victory input replay");
    game::World world; Load(world, directory);
    game::WorldConfig config; config.portFeatures = true;
    world.Boot(config); world.SetPortLeaderboardRecording(true);
    play::RunNamePrompt prompt;
    size_t next = 0; float x[2] = {.5f, .5f}, y[2] = {.5f, .5f};
    uint8_t buttons[2] = {}, previous[2] = {};
    bool selectedTheme = false, sawInitials = false, queued = false, opened = false;
    uint64_t sequence = 0;
    for (int frame = 1; frame < 9000; ++frame) {
        while (next < events.size() && events[next].frame <= frame) {
            const auto& event = events[next++];
            if (event.player >= 0 && event.player < 2) {
                x[event.player] = event.x; y[event.player] = event.y; buttons[event.player] = uint8_t(event.buttons);
            }
        }
        for (int p = 0; p < 2; ++p) {
            world.SetInput(p, game::MakeWorldInput(x[p], y[p], buttons[p], previous[p])); previous[p] = buttons[p];
        }
        world.Step();
        // The archived fixture navigates the original menu. Switch the chosen
        // presentation after launch so both themes exercise the identical fight.
        if (!selectedTheme && world.GameMode() == 6) { world.SetPortModernMenu(modern); selectedTheme = true; }
        const auto result = world.PortLeaderboardRun();
        if (result.sequence != sequence) {
            sequence = result.sequence;
            Require(result.eligible && result.table == 3 && result.score > 0 && result.players == 1,
                    "real Judgment victory produces one eligible frozen completion");
            Require(prompt.Queue(sequence, result.table, result.score, result.rounds), "queue real boss completion");
            queued = true;
        }
        const auto& hud = world.Hud();
        sawInitials |= queued && hud.gameMode == 6 && (hud.subState == 9 || hud.subState == 10);
        if (!queued) continue;
        if (prompt.MaybeOpen(hud)) {
            Require(sawInitials && BossSelector(hud), "boss prompt waits for native initials and opens back at the selector");
            opened = true;
            break;
        }
        Require(!prompt.IsOpen(), "boss completion cannot interrupt the native finish or initials flow");
    }
    Require(selectedTheme && queued && opened && sequence == 1, "one real boss win opens exactly one automatic prompt");
    const int frame = world.Frame(); const auto random = world.RandState();
    prompt.Edit("BOSS-HUNTER");
    Require(world.Frame() == frame && world.RandState() == random && BossSelector(world.Hud()),
            "editing a boss record leaves the selector and timing untouched");
    Skip(prompt);
    for (int count = 0; count < 30; ++count) {
        Tick(world);
        Require(!prompt.MaybeOpen(world.Hud()), "native boss selector does not reopen the consumed completion");
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: run_name_prompt_world_test GAME_DIR BOSS_REPLAY\n"); return 2; }
    for (bool modern : {false, true}) {
        GameOverAndTitle(argv[1], modern);
        ConnectedRoom(argv[1], modern);
        BossReplay(argv[1], argv[2], modern);
    }
    std::puts("PASS automatic run names: native game-over/map and boss initials finish first; both menu styles; retained room keeps deterministic world state");
}
