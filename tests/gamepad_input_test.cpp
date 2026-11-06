#include "input/gamepad_guns.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
using input::GamepadAxis;
using input::GamepadButton;
using input::GamepadGuns;
int checks = 0, failures = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", why); }
}
bool Near(float a, float b) { return std::abs(a-b) < .00001f; }
bool Neutral(const input::PlayerState& s) {
    return !s.held && !s.pressed && !s.released && !s.menuHeld && !s.menuPressed;
}
struct Fallback final : input::GunSource {
    std::array<input::PlayerState, 2> players{};
    int frames = 0;
    Fallback() {
        players[0].connected = players[1].connected = true;
        players[0].x = .1f; players[1].x = .9f;
        players[0].held = input::kTrigger; players[1].held = input::kReload;
        players[0].pressed = input::kTrigger; players[1].pressed = input::kReload;
    }
    const char* Name() const override { return "test pointers"; }
    input::PlayerState Player(int p) const override { return p >= 0 && p < 2 ? players[p] : input::PlayerState{}; }
    void BeginFrame() override {
        ++frames;
        for (auto& p : players) { p.pressed = p.released = 0; p.menuPressed = 0; }
    }
};

void IdleAndAim() {
    GamepadGuns guns;
    guns.SetConnected(0, true);
    guns.SetConnected(1, true);
    guns.Update(.1);
    Check(!guns.Player(0).connected && !guns.Player(1).connected, "attached idle pads do not claim players");
    guns.SetAxis(0, GamepadAxis::LeftX, .1f);
    guns.SetAxis(0, GamepadAxis::LeftY, -.1f);
    guns.Update(.1);
    Check(!guns.Player(0).connected, "radial deadzone ignores resting stick noise");
    guns.SetAxis(0, GamepadAxis::LeftY, 0);
    guns.SetAxis(0, GamepadAxis::LeftX, 1);
    guns.Update(.1);
    auto state = guns.Player(0);
    Check(state.connected && state.gamepad && !state.relative && state.onScreen, "deliberate stick motion claims a normalized controller player");
    Check(Near(state.x, .58f) && Near(state.y, .5f), "full stick aims at the configured per-second speed");
    guns.SetAxis(0, GamepadAxis::LeftX, .17f);
    for (int i = 0; i < 10; ++i) guns.Update(.1);
    Check(Near(guns.Player(0).x, state.x), "claimed pad has no drift inside deadzone");
    guns.SetAxis(0, GamepadAxis::LeftX, 0);
    guns.SetAxis(0, GamepadAxis::RightY, -1);
    guns.Update(.1);
    Check(Near(guns.Player(0).x, .58f) && Near(guns.Player(0).y, .42f), "right stick also aims with up decreasing image y");

    GamepadGuns one, two;
    one.SetConnected(0, true); two.SetConnected(0, true);
    one.SetAxis(0, GamepadAxis::LeftX, .6f); two.SetAxis(0, GamepadAxis::LeftX, .6f);
    for (int i = 0; i < 12; ++i) one.Update(1./60.);
    for (int i = 0; i < 24; ++i) two.Update(1./120.);
    Check(Near(one.Player(0).x, two.Player(0).x), "aim displacement is consistent across polling rates");
    Check(one.Player(0).x > .5f && one.Player(0).x < .66f, "partial stick deflection scales speed continuously");
    two.SetAxis(0, GamepadAxis::RightX, .6f);
    const float before = two.Player(0).x;
    one.Update(.1); two.Update(.1);
    Check(Near(one.Player(0).x, two.Player(0).x), "both sticks together do not double aim speed");
    Check(two.Player(0).x > before, "strongest-stick selection still moves the cursor");
    two.SetAxis(0, GamepadAxis::RightX, 1);
    two.SetAxis(0, GamepadAxis::RightY, 1);
    const auto diagonalBefore = two.Player(0);
    two.Update(.1);
    const auto diagonalAfter = two.Player(0);
    const float dx = diagonalAfter.x-diagonalBefore.x, dy = diagonalAfter.y-diagonalBefore.y;
    Check(Near(std::sqrt(dx*dx+dy*dy), .08f), "diagonal sticks have the same bounded radial speed");
    for (int i = 0; i < 20; ++i) two.Update(.1);
    Check(two.Player(0).x == 1.f && two.Player(0).y == 1.f, "aim clamps at image edges");

    GamepadGuns bounded;
    bounded.SetConnected(0, true); bounded.SetAxis(0, GamepadAxis::RightX, 1);
    bounded.Update(60);
    Check(Near(bounded.Player(0).x, .58f), "long stalls cap aim time instead of jumping across the image");
    bounded.Update(-1); bounded.Update(std::numeric_limits<double>::infinity());
    bounded.Update(std::numeric_limits<double>::quiet_NaN());
    Check(Near(bounded.Player(0).x, .58f), "invalid and negative time steps cannot move aim");
    bounded.SetAxis(0, GamepadAxis::RightX, std::numeric_limits<float>::quiet_NaN());
    bounded.Update(.1);
    Check(Near(bounded.Player(0).x, .58f), "nonfinite axis samples settle to neutral");
}

void ActionEdgesAndMappings() {
    GamepadGuns guns;
    guns.SetConnected(0, true);
    guns.SetButton(0, GamepadButton::South, true);
    guns.SetButton(0, GamepadButton::South, false);
    guns.SetButton(0, GamepadButton::Start, true);
    guns.SetButton(0, GamepadButton::Start, false);
    guns.Update(.01); guns.Update(.01);
    auto state = guns.Player(0);
    Check(!state.held && state.pressed == (input::kTrigger | input::kStart) && state.released == state.pressed,
          "fire and start taps between polls preserve press and release edges");
    Check(!state.menuHeld && state.menuPressed == (8 | input::gamepad::kMenuStart), "South and Start taps confirm and preserve dedicated menu Start");
    guns.BeginFrame();
    Check(Neutral(guns.Player(0)), "BeginFrame clears pending edges exactly once");
    guns.SetButton(0, GamepadButton::Start, true);
    Check(guns.Player(0).held == input::kStart && guns.Player(0).pressed == input::kStart && guns.Player(0).menuHeld == (8 | input::gamepad::kMenuStart),
          "Start exposes pause/start and menu confirm together");
    guns.BeginFrame();
    guns.SetConnected(0, true);
    guns.SetButton(0, GamepadButton::Start, true);
    guns.Update(.1);
    Check(!guns.Player(0).pressed && !guns.Player(0).menuPressed && guns.Player(0).held == input::kStart,
          "duplicate connection/button samples do not replay a held action");
    guns.SetButton(0, GamepadButton::Start, false); guns.BeginFrame();
    for (auto button : {GamepadButton::West, GamepadButton::East, GamepadButton::RightShoulder}) {
        guns.SetButton(0, button, true);
        state = guns.Player(0);
        Check(state.held == input::kReload && state.pressed == input::kReload, "West, East and right shoulder reload");
        Check(state.menuPressed == (button == GamepadButton::East ? 2u : button == GamepadButton::RightShoulder ? input::gamepad::kMenuNextCategory : 0u),
              "reload bindings retain independent menu Back and category actions");
        guns.SetButton(0, button, false); guns.BeginFrame();
    }
    guns.SetButton(0, GamepadButton::Back, true);
    guns.SetButton(0, GamepadButton::Back, false);
    Check(!guns.Player(0).held && !guns.Player(0).pressed && guns.Player(0).menuPressed == 2,
          "Back tap returns through menus without firing or reloading");
    guns.BeginFrame();
    guns.SetButton(0, GamepadButton::North, true);
    guns.SetButton(0, GamepadButton::LeftShoulder, true);
    Check(!guns.Player(0).held && !guns.Player(0).pressed && guns.Player(0).menuPressed == input::gamepad::kMenuPreviousCategory,
          "unassigned shoulder changes categories without a gameplay action");
}

void TriggerHysteresis() {
    GamepadGuns guns;
    guns.SetConnected(0, true);
    guns.SetAxis(0, GamepadAxis::RightTrigger, .54f);
    Check(guns.Player(0).gamepad && !guns.Player(0).held, "deliberate trigger travel claims a pad before firing threshold");
    guns.SetAxis(0, GamepadAxis::RightTrigger, .55f);
    Check(guns.Player(0).held == input::kTrigger && guns.Player(0).pressed == input::kTrigger,
          "right trigger fires at its press threshold");
    guns.BeginFrame();
    guns.SetAxis(0, GamepadAxis::RightTrigger, .5f);
    guns.SetAxis(0, GamepadAxis::RightTrigger, .54f);
    Check(guns.Player(0).held == input::kTrigger && !guns.Player(0).pressed && !guns.Player(0).released,
          "trigger threshold noise preserves the held state without repeated shots");
    guns.SetAxis(0, GamepadAxis::RightTrigger, .45f);
    Check(!guns.Player(0).held && guns.Player(0).released == input::kTrigger, "trigger releases at its lower threshold");
    guns.BeginFrame();
    guns.SetAxis(0, GamepadAxis::RightTrigger, .5f);
    Check(!guns.Player(0).held && !guns.Player(0).pressed, "released trigger must cross upper threshold to fire again");
    guns.SetAxis(0, GamepadAxis::RightTrigger, 1);
    guns.SetAxis(0, GamepadAxis::RightTrigger, 0);
    Check(!guns.Player(0).held && guns.Player(0).pressed == input::kTrigger && guns.Player(0).released == input::kTrigger,
          "complete trigger taps between updates remain latched");
    guns.BeginFrame();
    guns.SetAxis(0, GamepadAxis::LeftTrigger, 1);
    guns.SetAxis(0, GamepadAxis::LeftTrigger, .5f);
    Check(guns.Player(0).held == input::kReload && guns.Player(0).pressed == input::kReload,
          "left trigger provides reload with the same hysteresis");
    guns.SetAxis(0, GamepadAxis::LeftTrigger, 0); guns.BeginFrame();
    guns.SetButton(0, GamepadButton::South, true); guns.BeginFrame();
    guns.SetAxis(0, GamepadAxis::RightTrigger, 1);
    guns.SetButton(0, GamepadButton::South, false);
    Check(guns.Player(0).held == input::kTrigger && !guns.Player(0).pressed && !guns.Player(0).released,
          "overlapping fire bindings keep one logical held action");
}

void MenuDirectionsAndRepeat() {
    GamepadGuns guns;
    guns.SetConnected(0, true);
    guns.SetButton(0, GamepadButton::Up, true);
    Check(guns.Player(0).menuHeld == 0x10 && guns.Player(0).menuPressed == 0x10, "D-pad navigation has an immediate edge");
    guns.BeginFrame();
    for (int i = 0; i < 3; ++i) guns.Update(.1);
    Check(!guns.Player(0).menuPressed, "held navigation waits through its initial repeat delay");
    guns.Update(.05);
    Check(guns.Player(0).menuPressed == 0x10, "held navigation repeats after .35 seconds");
    guns.BeginFrame(); guns.Update(.05);
    Check(!guns.Player(0).menuPressed, "repeat interval does not depend on BeginFrame count");
    guns.Update(.05);
    Check(guns.Player(0).menuPressed == 0x10, "held navigation repeats every .1 seconds after initial delay");
    guns.SetButton(0, GamepadButton::Up, false); guns.BeginFrame();
    for (int i = 0; i < 4; ++i) guns.Update(.1);
    Check(!guns.Player(0).menuPressed && !guns.Player(0).menuHeld, "released navigation stops repeating");
    guns.SetButton(0, GamepadButton::Up, true); guns.BeginFrame(); guns.Update(.1);
    Check(!guns.Player(0).menuPressed, "a new direction hold gets a fresh initial repeat delay");
    guns.SetButton(0, GamepadButton::Up, false); guns.BeginFrame();
    guns.SetAxis(0, GamepadAxis::LeftX, .49f);
    Check(!guns.Player(0).menuHeld, "light stick aim does not accidentally navigate menus");
    guns.SetAxis(0, GamepadAxis::LeftX, -.6f);
    guns.SetAxis(0, GamepadAxis::LeftY, .6f);
    Check(guns.Player(0).menuHeld == 0x60 && guns.Player(0).menuPressed == 0x60,
          "left stick navigates diagonally using native left/down menu bits");
    guns.BeginFrame();
    guns.SetAxis(0, GamepadAxis::LeftX, 0); guns.SetAxis(0, GamepadAxis::LeftY, 0);
    guns.SetAxis(0, GamepadAxis::RightY, -1);
    Check(!guns.Player(0).menuHeld && !guns.Player(0).menuPressed, "right stick aims without menu navigation");
    guns.SetButton(0, GamepadButton::Right, true); guns.SetButton(0, GamepadButton::Right, false);
    Check(guns.Player(0).menuPressed == 0x80 && !guns.Player(0).menuHeld, "D-pad taps between polls preserve navigation edges");
    guns.BeginFrame();
    guns.SetButton(0, GamepadButton::South, true);
    guns.SetButton(0, GamepadButton::Back, true);
    guns.BeginFrame();
    for (int i = 0; i < 10; ++i) guns.Update(.1);
    Check(guns.Player(0).menuHeld == 10 && !guns.Player(0).menuPressed, "confirm and Back do not repeat while held");
}

void PlayersFallbackAndDisconnect() {
    Fallback source;
    GamepadGuns guns(&source);
    guns.SetConnected(0, true); guns.SetConnected(1, true);
    Check(Near(guns.Player(0).x, .1f) && Near(guns.Player(1).x, .9f), "unused attached pads preserve both fallback players");
    guns.SetButton(1, GamepadButton::Start, true);
    Check(!guns.Player(0).gamepad && Near(guns.Player(0).x, .1f) && guns.Player(1).gamepad,
          "slot two can claim P2 while the first fallback keeps P1");
    guns.SetButton(0, GamepadButton::South, true);
    Check(guns.Player(0).gamepad && guns.Player(0).held == input::kTrigger && guns.Player(1).held == input::kStart,
          "two pads have independent actions and fixed player slots");
    guns.SetAxis(1, GamepadAxis::RightX, 1); guns.Update(.1);
    Check(Near(guns.Player(0).x, .5f) && Near(guns.Player(1).x, .58f), "each pad has its own aim position");
    guns.SetConnected(1, false);
    Check(!guns.Player(1).gamepad && Near(guns.Player(1).x, .1f) && guns.Player(1).held == input::kTrigger,
          "P1 pad ownership shifts the first fallback into newly available P2");
    guns.BeginFrame();
    Check(source.frames == 1 && !source.players[0].pressed && !guns.Player(0).pressed,
          "frame boundaries consume both controller and fallback edges once");
    guns.SetConnected(0, false);
    Check(Near(guns.Player(0).x, .1f) && Near(guns.Player(1).x, .9f), "disconnect releases reservations and restores fallback order");
    guns.SetConnected(0, true);
    Check(!guns.Player(0).gamepad && guns.Player(0).held == input::kTrigger, "reconnected idle pad leaves fallback ownership intact");
    guns.SetButton(0, GamepadButton::Back, true); guns.SetButton(0, GamepadButton::Back, false);
    Check(guns.Player(0).gamepad && !guns.Player(0).held && !guns.Player(0).pressed && Near(guns.Player(0).x, .5f),
          "newly claimed reconnection has no inherited actions or aim");

    GamepadGuns alone;
    alone.SetConnected(0, true);
    alone.SetButton(0, GamepadButton::South, true);
    alone.SetButton(0, GamepadButton::Start, true);
    alone.SetButton(0, GamepadButton::Up, true);
    alone.SetConnected(0, false);
    Check(!alone.Player(0).connected && !alone.Player(0).gamepad && Neutral(alone.Player(0)),
          "disconnect clears held actions, pending edges and repeats immediately");
    alone.SetButton(0, GamepadButton::South, true); alone.SetAxis(0, GamepadAxis::RightTrigger, 1);
    alone.SetConnected(0, true); alone.Update(.1);
    Check(!alone.Player(0).connected && Neutral(alone.Player(0)), "events for detached pads cannot survive reconnection");
    alone.SetButton(0, GamepadButton::Start, true);
    Check(alone.Player(0).held == input::kStart && alone.Player(0).menuHeld == (8 | input::gamepad::kMenuStart), "fresh reconnect input works without stale fire or navigation");
}

void InvalidSlotsAndEnums() {
    GamepadGuns guns;
    for (int slot : {-1, 2, 100}) {
        guns.SetConnected(slot, true);
        guns.SetAxis(slot, GamepadAxis::LeftX, 1);
        guns.SetButton(slot, GamepadButton::South, true);
        Check(!guns.Player(slot).connected && Neutral(guns.Player(slot)), "invalid player slots are harmless");
    }
    guns.SetConnected(0, true);
    guns.SetAxis(0, static_cast<GamepadAxis>(-1), 1);
    guns.SetAxis(0, static_cast<GamepadAxis>(6), 1);
    guns.SetButton(0, static_cast<GamepadButton>(-1), true);
    guns.SetButton(0, static_cast<GamepadButton>(12), true);
    Check(!guns.Player(0).connected && Neutral(guns.Player(0)), "invalid control enums cannot claim or mutate a pad");
}

void SettingsProfilesAndPersistence() {
    using namespace input::gamepad;
    const Profile defaults;
    Check(defaults.sensitivity == 100 && defaults.deadzone == 18 &&
          defaults.bindings[0] == (ControlMask(Control::South) | ControlMask(Control::RightTrigger)) &&
          defaults.bindings[1] == (ControlMask(Control::East) | ControlMask(Control::West) |
                                  ControlMask(Control::RightShoulder) | ControlMask(Control::LeftTrigger)) &&
          defaults.bindings[2] == ControlMask(Control::Start), "default profiles preserve the existing aim and action mappings");
    Profile profile;
    profile.CycleBinding(0, 1);
    Check(profile.bindings[0] == (ControlMask(Control::East) | ControlMask(Control::RightTrigger)) &&
          (profile.bindings[1] & ControlMask(Control::South)) && !(profile.bindings[1] & ControlMask(Control::East)),
          "cycling swaps conflicting primary buttons and keeps alternate trigger bindings");
    profile.CycleBinding(2, -1);
    Check(profile.bindings[2] == ControlMask(Control::Back), "reverse cycling selects the previous control");
    profile.bindings[0] = ControlMask(Control::RightTrigger);
    profile.CycleBinding(0, 1);
    Check(profile.bindings[0] == ControlMask(Control::South), "binding cycles wrap around the available controls");
    profile.bindings[0] = 0;
    const auto otherBindings = profile.bindings;
    profile.CycleBinding(0, -1);
    Check(profile.bindings[0] && profile.bindings[1] == otherBindings[1] && profile.bindings[2] == otherBindings[2],
          "an unassigned action selects a free control without stealing another action's button");
    for (int direction : {-1, 1}) {
        profile = {};
        for (int n = 0; n < 40; ++n) {
            profile.CycleBinding(0, direction);
            Check(profile.bindings[0] && profile.bindings[1] && profile.bindings[2], "repeated remapping never unbinds another action");
            Check(profile.bindings[0] & ControlMask(Control::RightTrigger), "remapping primary Fire retains RT");
            Check(!(profile.bindings[0] & profile.bindings[1]) && !(profile.bindings[0] & profile.bindings[2]) &&
                  !(profile.bindings[1] & profile.bindings[2]), "swaps keep all action bindings disjoint");
        }
    }
    const Profile beforeInvalid = profile;
    profile.CycleBinding(-1, 1); profile.CycleBinding(3, 1); profile.CycleBinding(0, 0);
    Check(profile == beforeInvalid, "invalid binding edits leave the profile unchanged");
    profile.sensitivity = -100; profile.deadzone = 100;
    profile.bindings = {0xffff, 0xffff, 0xffff};
    profile.Normalize();
    Check(profile.sensitivity == 25 && profile.deadzone == 40 && profile.bindings[0] == 1023 &&
          !profile.bindings[1] && !profile.bindings[2], "normalization clamps values and strips invalid or conflicting bindings");
    profile.sensitivity = 999; profile.deadzone = -5; profile.Normalize();
    Check(profile.sensitivity == 300 && profile.deadzone == 0, "sensitivity and deadzone also clamp at their opposite limits");
    profile = {};
    Check(profile == defaults, "a default profile restores every controller setting");
    Check(BindingName(0) == "Unassigned" && BindingName(ControlMask(Control::South)) == "A/Cross" &&
          BindingName(ControlMask(Control::RightTrigger)) == "RT/R2" &&
          BindingName(defaults.bindings[0]) == "A/Cross + RT/R2" &&
          std::string(ControlName(Control::Count)) == "Unassigned", "binding labels identify both Xbox and PlayStation controls");

    const auto path = std::filesystem::temp_directory_path() /
        ("hotd2-controller-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    Settings saved;
    saved.player[0].sensitivity = 75; saved.player[0].deadzone = 30;
    saved.player[0].bindings = {ControlMask(Control::North), ControlMask(Control::Back), ControlMask(Control::LeftShoulder)};
    saved.player[1].sensitivity = 175; saved.player[1].deadzone = 5;
    saved.player[1].bindings = {ControlMask(Control::East), ControlMask(Control::RightTrigger), ControlMask(Control::Start)};
    Check(saved.Save(path.string()), "controller settings save to a flat text file");
    Settings loaded;
    Check(loaded.Load(path.string()) && loaded == saved, "controller settings round-trip independent per-player values and bindings");
    {
        std::ofstream file(path);
        file << "# partial and invalid configuration\n"
                "player1.sensitivity = 900\nplayer1.deadzone=-5\n"
                "player1.fire=2 # B / Circle\nplayer1.reload=2\nplayer1.pause=0x20\n"
                "player2.sensitivity=garbage\nplayer2.deadzone=19percent\n"
                "player2.fire=-1\nplayer2.reload=65535\nplayer2.pause=999999999999999999999\n"
                "player3.sensitivity=200\nunknown=1\n";
    }
    Check(loaded.Load(path.string()) && loaded.player[0].sensitivity == 300 && loaded.player[0].deadzone == 0 &&
          loaded.player[0].bindings[0] == ControlMask(Control::East) && !loaded.player[0].bindings[1] &&
          loaded.player[0].bindings[2] == ControlMask(Control::Start), "file loading clamps numeric values and removes conflicting action controls");
    Check(loaded.player[1] == defaults, "missing and malformed settings retain sensible defaults for the other player");
    std::error_code error;
    std::filesystem::remove(path, error);
    const Settings beforeMissing = loaded;
    Check(!loaded.Load(path.string()) && loaded == beforeMissing, "a missing settings file leaves the current settings intact");
}

void RuntimeProfilesAndSafeRebinding() {
    using namespace input::gamepad;
    GamepadGuns guns;
    guns.settings.player[0].sensitivity = 50; guns.settings.player[0].deadzone = 30;
    guns.settings.player[1].sensitivity = 200; guns.settings.player[1].deadzone = 10;
    guns.Configure();
    guns.SetConnected(0, true); guns.SetConnected(1, true);
    Check(guns.Connected(0) && guns.Connected(1) && !guns.Connected(-1) && !guns.Connected(2) &&
          !guns.Player(0).connected, "connection reporting includes attached idle controllers independently of player ownership");
    guns.SetAxis(0, GamepadAxis::RightX, .2f);
    guns.SetAxis(1, GamepadAxis::RightX, .2f);
    guns.Update(.1);
    Check(!guns.Player(0).gamepad && guns.Player(1).gamepad && Near(guns.Player(1).x, .5f + .16f / 9),
          "each player's deadzone controls claiming and scaled aim independently");
    const float secondBefore = guns.Player(1).x;
    guns.SetAxis(0, GamepadAxis::RightX, 1);
    guns.SetAxis(1, GamepadAxis::RightX, 1);
    guns.Update(.1);
    Check(Near(guns.Player(0).x, .54f) && Near(guns.Player(1).x, secondBefore + .16f),
          "runtime sensitivity applies each player's percentage to base aim speed");
    guns.SetAxis(0, GamepadAxis::RightX, .29f); guns.Update(.1);
    Check(Near(guns.Player(0).x, .54f), "a changed deadzone also prevents drift after a player is claimed");
    guns.SetConnected(1, false);
    Check(!guns.Connected(1), "connection reporting clears when a controller disconnects");

    GamepadGuns rebound;
    rebound.SetConnected(0, true); rebound.SetConnected(1, true);
    rebound.SetButton(0, GamepadButton::South, true);
    rebound.SetButton(0, GamepadButton::Start, true);
    rebound.SetAxis(0, GamepadAxis::RightTrigger, 1);
    rebound.SetButton(1, GamepadButton::South, true);
    rebound.BeginFrame();
    rebound.settings.player[0].bindings = {ControlMask(Control::Start), ControlMask(Control::RightTrigger), ControlMask(Control::South)};
    rebound.Configure();
    Check(!rebound.Player(0).held && !rebound.Player(0).pressed && !rebound.Player(0).released,
          "applying changed bindings clears resolved actions and pending edges without synthetic shots or pause");
    Check(rebound.Player(0).menuHeld == (8 | input::gamepad::kMenuStart) && !rebound.Player(0).menuPressed &&
          rebound.Player(1).held == input::kTrigger && !rebound.Player(1).pressed,
          "changing one profile preserves held menu controls and the other player's actions");
    rebound.SetAxis(0, GamepadAxis::LeftX, .6f);
    rebound.SetButton(0, GamepadButton::Start, true);
    rebound.SetAxis(0, GamepadAxis::RightTrigger, .5f);
    Check(!rebound.Player(0).held && !rebound.Player(0).pressed,
          "duplicate samples and trigger hysteresis cannot activate controls held across a settings change");
    rebound.SetButton(0, GamepadButton::Start, false);
    rebound.SetButton(0, GamepadButton::Start, true);
    Check(rebound.Player(0).held == input::kTrigger && rebound.Player(0).pressed == input::kTrigger,
          "a rebound control becomes usable after its own release and fresh press");
    rebound.SetButton(0, GamepadButton::Start, false);
    rebound.SetButton(0, GamepadButton::South, false);
    rebound.SetAxis(0, GamepadAxis::RightTrigger, .45f);
    rebound.BeginFrame();
    rebound.SetButton(0, GamepadButton::South, true);
    rebound.SetAxis(0, GamepadAxis::RightTrigger, .55f);
    Check(rebound.Player(0).held == (input::kStart | input::kReload) &&
          rebound.Player(0).pressed == (input::kStart | input::kReload),
          "rebound button and trigger actions work independently after release");
    rebound.settings.player[0] = {};
    rebound.Configure();
    Check(!rebound.Player(0).held && !rebound.Player(0).pressed && !rebound.Player(0).released,
          "restoring defaults while controls are held also suppresses stale gameplay input");
    rebound.SetButton(0, GamepadButton::South, false); rebound.SetAxis(0, GamepadAxis::RightTrigger, 0);
    rebound.BeginFrame(); rebound.SetButton(0, GamepadButton::South, true);
    Check(rebound.Player(0).held == input::kTrigger && rebound.Player(0).pressed == input::kTrigger,
          "restored default bindings work on the next fresh press");
    rebound.BeginFrame(); rebound.Configure();
    Check(rebound.Player(0).held == input::kTrigger && !rebound.Player(0).pressed && !rebound.Player(0).released,
          "reapplying unchanged settings preserves held gameplay actions without replaying edges");

    GamepadGuns mappings;
    mappings.SetConnected(0, true);
    for (int i = 0; i < static_cast<int>(Control::Count); ++i) {
        mappings.settings.player[0].bindings = {uint16_t(uint16_t(1) << i), 0, 0};
        mappings.Configure(); mappings.BeginFrame();
        if (i < static_cast<int>(Control::LeftTrigger)) mappings.SetButton(0, static_cast<GamepadButton>(i), true);
        else mappings.SetAxis(0, i == static_cast<int>(Control::LeftTrigger) ? GamepadAxis::LeftTrigger : GamepadAxis::RightTrigger, 1);
        const uint32_t menu = ((i == 0 || i == 5) ? 8 : (i == 1 || i == 4) ? 2 : 0) |
                              (i == 5 ? input::gamepad::kMenuStart : 0) |
                              (i == 6 ? input::gamepad::kMenuPreviousCategory : 0) |
                              (i == 7 ? input::gamepad::kMenuNextCategory : 0);
        Check(mappings.Player(0).held == input::kTrigger && mappings.Player(0).pressed == input::kTrigger &&
              mappings.Player(0).menuPressed == menu, "every configurable control maps to its action while front-menu controls remain fixed");
        if (i < static_cast<int>(Control::LeftTrigger)) mappings.SetButton(0, static_cast<GamepadButton>(i), false);
        else mappings.SetAxis(0, i == static_cast<int>(Control::LeftTrigger) ? GamepadAxis::LeftTrigger : GamepadAxis::RightTrigger, 0);
    }
    GamepadGuns zero;
    zero.settings.player[0].deadzone = 0; zero.Configure(); zero.SetConnected(0, true);
    zero.SetAxis(0, GamepadAxis::LeftX, 0); zero.Update(.1);
    Check(!zero.Player(0).gamepad, "zero deadzone keeps an exactly neutral stick idle");
    zero.SetAxis(0, GamepadAxis::LeftX, .01f); zero.Update(.1);
    Check(zero.Player(0).gamepad && Near(zero.Player(0).x, .5008f), "zero deadzone supports deliberate small stick motion without a divide-by-zero");
}
} // namespace

int main() {
    IdleAndAim();
    ActionEdgesAndMappings();
    TriggerHysteresis();
    MenuDirectionsAndRepeat();
    PlayersFallbackAndDisconnect();
    InvalidSlotsAndEnums();
    SettingsProfilesAndPersistence();
    RuntimeProfilesAndSafeRebinding();
    std::printf("Gamepad input: %d/%d checks passed\n", checks-failures, checks);
    return failures ? 1 : 0;
}
