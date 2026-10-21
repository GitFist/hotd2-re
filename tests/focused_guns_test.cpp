#include "input/focused_guns.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {

int checks = 0, failures = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", why);
    }
}

struct Source final : input::GunSource {
    std::array<input::PlayerState, input::kMaxPlayers> players{};
    int begins = 0;
    bool rawCalibrationEdge = false;
    const char* Name() const override { return "scripted two-device source"; }
    input::PlayerState Player(int player) const override { return players[player]; }
    void BeginFrame() override {
        ++begins;
        rawCalibrationEdge = false;
        for (auto& player : players) {
            player.pressed = player.released = 0;
            player.menuPressed = 0;
        }
    }
};

bool Neutral(const input::PlayerState& state) {
    return !state.held && !state.pressed && !state.released && !state.menuHeld && !state.menuPressed;
}

void InitialAndLiveSource() {
    Source source;
    source.players[0].connected = true;
    source.players[0].held = source.players[0].pressed = input::kTrigger;
    source.players[0].menuHeld = source.players[0].menuPressed = 0x10;
    input::FocusedGuns focused(source);
    focused.UpdateFocus(true);
    Check(source.begins == 0, "initial foreground input is not drained");
    Check(focused.Player(0).pressed == input::kTrigger && focused.Player(0).menuPressed == 0x10,
          "initial shot and controller direction are preserved");
    Check(std::strcmp(focused.Name(), source.Name()) == 0, "source name is forwarded");
    focused.BeginFrame();
    Check(source.begins == 1, "each game-frame boundary is forwarded");
    Check(!focused.Player(0).pressed && !focused.Player(0).menuPressed && focused.Player(0).held == input::kTrigger,
          "multiple game steps do not replay cached edges");
    source.players[0].x = 0.75f;
    source.players[0].pressed = input::kReload;
    Check(focused.Player(0).x == 0.75f && focused.Player(0).pressed == input::kReload,
          "Player reads current source data rather than a cached outer-loop snapshot");
    Check(!focused.Player(-1).connected && !focused.Player(2).connected, "invalid player slots are harmless");
}

void BackgroundAndMetadata() {
    Source source;
    auto& device = source.players[0];
    device.connected = true;
    device.x = -0.25f;
    device.y = 1.25f;
    device.onScreen = false;
    device.relative = true;
    device.held = input::kTrigger | input::kStart;
    device.pressed = input::kTrigger;
    device.released = input::kReload;
    device.menuHeld = 0x90;
    device.menuPressed = 0x10;
    source.rawCalibrationEdge = true;
    input::FocusedGuns focused(source);
    focused.UpdateFocus(false);
    const auto inactive = focused.Player(0);
    Check(Neutral(inactive), "focus loss neutralizes all gameplay and menu buttons");
    Check(inactive.connected && inactive.relative && !inactive.onScreen && inactive.x == -0.25f && inactive.y == 1.25f,
          "focus gating preserves connection, source mode and calibrated offscreen aim");
    Check(!source.rawCalibrationEdge && source.begins == 1, "focus loss drains raw calibration edges after source polling");
    Check(source.players[0].held == (input::kTrigger | input::kStart) && source.players[0].menuHeld == 0x90,
          "focus handling does not rewrite physical held state");

    // New reports may arrive after the last focus sample. Player must still be
    // neutral, while the next inactive poll consumes those pending edges.
    device.held = device.pressed = input::kReload;
    device.released = input::kTrigger;
    device.menuHeld = device.menuPressed = 0x40;
    source.rawCalibrationEdge = true;
    Check(Neutral(focused.Player(0)), "new background reports cannot become game actions");
    focused.UpdateFocus(false);
    Check(Neutral(focused.Player(0)) && !source.rawCalibrationEdge && source.begins == 2,
          "continued background polling drains events without resuming input");
}

void RefocusHeldUntilReleased() {
    Source source;
    auto& device = source.players[0];
    device.connected = true;
    input::FocusedGuns focused(source);
    focused.UpdateFocus(false);

    // Emulate a fresh backend poll on the first foreground iteration. Both
    // currently held controls and queued tap edges must be discarded.
    device.held = input::kTrigger | input::kStart;
    device.pressed = input::kTrigger | input::kReload | input::kStart;
    device.menuHeld = 0x28;
    device.menuPressed = 0xA8;
    source.rawCalibrationEdge = true;
    focused.UpdateFocus(true);
    Check(Neutral(focused.Player(0)), "first refocus sample does not fire, reload, pause or navigate");
    Check(!source.rawCalibrationEdge && source.begins == 2, "refocus drains newly polled raw calibration/tap edges");
    focused.UpdateFocus(true);
    Check(Neutral(focused.Player(0)) && source.begins == 2, "still-held controls stay suppressed without draining every active sample");

    device.held = input::kStart;
    device.released = input::kTrigger;
    device.menuHeld = 8;
    focused.UpdateFocus(true);
    Check(Neutral(focused.Player(0)), "releasing a suppressed control creates no phantom release action");
    focused.BeginFrame();
    device.held |= input::kTrigger;
    device.pressed = input::kTrigger;
    device.menuHeld |= 0x20;
    device.menuPressed = 0x20;
    focused.UpdateFocus(true);
    const auto fresh = focused.Player(0);
    Check(fresh.held == input::kTrigger && fresh.pressed == input::kTrigger && fresh.menuHeld == 0x20 && fresh.menuPressed == 0x20,
          "released then freshly pressed controls work while other held controls remain blocked");
    focused.BeginFrame();
    Check(!focused.Player(0).pressed && !focused.Player(0).menuPressed, "fresh edges are consumed exactly once per game frame");

    device.held = 0;
    device.released = input::kTrigger | input::kStart;
    device.menuHeld = 0;
    focused.UpdateFocus(true);
    Check(focused.Player(0).released == input::kTrigger, "normal foreground releases survive while suppressed releases stay hidden");
    focused.BeginFrame();
    device.held = device.pressed = input::kStart;
    device.menuHeld = device.menuPressed = 8;
    focused.UpdateFocus(true);
    Check(focused.Player(0).held == input::kStart && focused.Player(0).pressed == input::kStart && focused.Player(0).menuPressed == 8,
          "manual pause/confirm work after the held controls have been released");
}

void IndependentPlayersAndFocusCycles() {
    Source source;
    source.players[0].connected = source.players[1].connected = true;
    source.players[0].held = input::kTrigger;
    source.players[0].menuHeld = 0x10;
    source.players[1].held = input::kReload;
    source.players[1].menuHeld = 0x80;
    input::FocusedGuns focused(source);
    focused.UpdateFocus(false);
    focused.UpdateFocus(true);
    source.players[0].held = 0;
    source.players[0].menuHeld = 0;
    focused.UpdateFocus(true);
    source.players[0].held = source.players[0].pressed = input::kTrigger;
    source.players[0].menuHeld = source.players[0].menuPressed = 0x10;
    focused.UpdateFocus(true);
    Check(focused.Player(0).pressed == input::kTrigger && focused.Player(0).menuPressed == 0x10, "P1 can resume fresh input independently");
    Check(Neutral(focused.Player(1)), "P1 release does not unblock P2 held controls");
    source.players[1].connected = false;
    source.players[1].held = 0;
    source.players[1].menuHeld = 0;
    focused.UpdateFocus(true);
    Check(!focused.Player(1).connected, "device disconnection metadata remains live");

    for (int cycle = 0; cycle < 3; ++cycle) {
        focused.UpdateFocus(false);
        Check(Neutral(focused.Player(0)), "repeated focus loss suppresses the current input");
        focused.UpdateFocus(true);
        Check(Neutral(focused.Player(0)), "repeated refocus does not create synthetic presses");
        source.players[0].held = 0;
        source.players[0].menuHeld = 0;
        focused.UpdateFocus(true);
        source.players[0].held = source.players[0].pressed = input::kTrigger;
        source.players[0].menuHeld = source.players[0].menuPressed = 0x10;
        focused.UpdateFocus(true);
        Check(focused.Player(0).pressed == input::kTrigger && focused.Player(0).menuPressed == 0x10,
              "fresh input works after every focus cycle");
        focused.BeginFrame();
    }
}

}  // namespace

int main() {
    InitialAndLiveSource();
    BackgroundAndMetadata();
    RefocusHeldUntilReleased();
    IndependentPlayersAndFocusCycles();
    std::printf("Focused guns: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
