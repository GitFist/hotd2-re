#include "port/input/gamepad_guns.h"
#include "port/play/display_pointer.h"
#include "port/play/gamepad_input.h"
#include "port/play/pause_menu.h"
#include "port/play/update_prompt.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void Check(bool condition, const char* why) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
using Button = input::GamepadButton;
using Axis = input::GamepadAxis;
}

int main() {
    input::GamepadGuns pads;
    pads.SetConnected(0, true);
    pads.SetConnected(1, true);
    uint8_t previous = 0;
    auto tap = [&](int player, Button button) {
        pads.BeginFrame();
        pads.SetButton(player, button, true);
        pads.SetButton(player, button, false);
        return pads.Player(player);
    };

    auto fire = play::GamepadWorldInput(tap(0, Button::South), 5, true, previous);
    Check(fire.kind == 1 && fire.fired && !fire.offscreen && (fire.pressed & 4),
          "A/Cross becomes a real on-screen shot, even for a sub-frame tap");
    play::PlayerGun gun;
    play::ShotInput shot;
    shot.x = static_cast<uint16_t>(fire.x); shot.y = static_cast<uint16_t>(fire.y); shot.fired = fire.fired;
    Check(play::UpdatePlayerGun(play::MakeGunRecord(shot), true, gun) == game::TriggerAction::Shot && gun.ammo == 5,
          "controller shot reaches native ammo/trigger handling");

    const auto pause = play::GamepadWorldInput(tap(0, Button::Start), 5, true, previous);
    Check((pause.pressed & 8) && !(pause.pressed & 4) && !pause.fired && !pause.offscreen,
          "Menu/Options pauses without also firing or reloading");
    const auto reload = play::GamepadWorldInput(tap(0, Button::West), 5, true, previous);
    Check(reload.kind == 1 && reload.offscreen && !(reload.pressed & 8),
          "X/Square produces reload without pausing");

    pads.BeginFrame();
    pads.SetAxis(0, Axis::RightX, 1);
    for (int i = 0; i < 10; ++i) pads.Update(.1);
    pads.SetAxis(0, Axis::RightX, 0);
    const auto edge = play::DisplayPointer(tap(0, Button::South), 1);
    Check(edge.x == 1 && edge.onScreen, "CRT does not turn a controller edge shot into an offscreen reload");
    const auto edgeShot = play::GamepadWorldInput(edge, 5, true, previous);
    Check(edgeShot.fired && !edgeShot.offscreen, "right-edge controller shot remains valid");

    const auto frontend = play::GamepadWorldInput(tap(0, Button::South), 9, false, previous);
    Check(frontend.kind == 0 && !frontend.fired && !frontend.offscreen && (frontend.pressed & 8),
          "front-end confirm uses pad selection instead of clicking the resting sight");
    const auto p2 = play::GamepadWorldInput(tap(1, Button::Down), 9, false, previous);
    Check(p2.kind == 0 && (p2.pressed & 0x20), "second controller provides ordinary front-end navigation");

    play::PauseMenu menu;
    play::Settings settings;
    std::vector<uint32_t> sounds;
    menu.SetModern(true);
    menu.Open();
    menu.Update(play::GamepadMenuInput(tap(0, Button::Down)), settings, sounds);
    Check(menu.Cursor() == play::PauseMenu::kOptions, "D-pad selects Options without pointer hover");
    menu.Update(play::GamepadMenuInput(tap(0, Button::South)), settings, sounds);
    Check(menu.DisplayOpen(), "A/Cross opens real Options menu");
    menu.Update(play::GamepadMenuInput(tap(1, Button::East)), settings, sounds);
    Check(!menu.DisplayOpen() && menu.IsOpen(), "P2 B/Circle backs out of Options without resuming play");
    Check(menu.Update(play::GamepadMenuInput(tap(0, Button::Start), true), settings, sounds) == play::PauseMenu::Action::Resume,
          "Menu/Options resumes from pause");

    play::UpdatePrompt update;
    update.Open("1.7");
    update.Update(play::GamepadMenuInput(tap(1, Button::Right)));
    Check(update.Update(play::GamepadMenuInput(tap(1, Button::Start))) == play::UpdatePrompt::Action::Download,
          "Start confirms a selected modal action instead of canceling it");
    update.Open("1.7");
    Check(update.Update(play::GamepadMenuInput(tap(0, Button::East))) == play::UpdatePrompt::Action::Later,
          "B/Circle cancels a modal normally");
    auto& profile = pads.settings.player[0];
    using Control = input::gamepad::Control;
    profile.bindings[0] = input::gamepad::ControlMask(Control::East);
    profile.bindings[1] = input::gamepad::ControlMask(Control::West);
    profile.bindings[2] = input::gamepad::ControlMask(Control::North);
    pads.Configure();
    const auto remappedFire = play::GamepadWorldInput(tap(0, Button::East), 5, true, previous);
    Check(remappedFire.fired && !remappedFire.offscreen && (remappedFire.pressed & 4) && !(remappedFire.pressed & 2),
          "B/Circle remapped to Fire does not also deliver fixed menu Back as a reload");
    const auto remappedPause = play::GamepadWorldInput(tap(0, Button::North), 5, true, previous);
    Check((remappedPause.pressed & 8) && !remappedPause.fired && !remappedPause.offscreen,
          "remapped Pause reaches gameplay without firing");
    const auto menuWithRemappedPause = play::GamepadWorldInput(tap(0, Button::North), 9, false, previous);
    Check(!(menuWithRemappedPause.held & 8) && !(menuWithRemappedPause.pressed & 8),
          "remapped gameplay Pause does not synthesize a front-end Confirm");
    const auto remappedBack = play::GamepadMenuInput(tap(0, Button::East));
    Check((remappedBack.pressed & 2) && !remappedBack.pointer,
          "menu Back remains available after rebinding gameplay Fire");
    profile.bindings[2] = input::gamepad::ControlMask(Control::South);
    pads.Configure();
    menu.Open();
    menu.Update(play::GamepadMenuInput(tap(0, Button::Down)), settings, sounds);
    const auto confirmWithRemappedPause = play::GamepadMenuInput(tap(0, Button::South), true);
    Check(!confirmWithRemappedPause.start, "gameplay Pause remapped to A does not override fixed menu Confirm");
    menu.Update(confirmWithRemappedPause, settings, sounds);
    Check(menu.DisplayOpen(), "A/Cross still opens pause Options after rebinding gameplay Pause to A/Cross");
    Check(play::GamepadMenuInput(tap(0, Button::Start), true).start,
          "physical Menu/Options remains the dedicated menu resume shortcut");
    std::puts("PASS gamepad gameplay, pause, CRT edge aim, two-player menu and modal integration");
}
