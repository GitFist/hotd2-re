#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "game/menu_audio.h"
#include "port/play/controller_menu.h"
#include "port/play/gamepad_input.h"
#include "port/play/pause_menu.h"

namespace {
using Menu = play::ControllerMenu;
using Button = input::GamepadButton;
void Check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
play::MenuInput Key(uint32_t key) { play::MenuInput in; in.pressed = key; return in; }
play::MenuInput Point(int row, bool click = false, float x = 50) {
    play::MenuInput in;
    in.pointer = true; in.trigger = click; in.px = x; in.py = Menu::RowY(row) + 20;
    return in;
}
play::MenuInput Tap(input::GamepadGuns& pads, Button button, int player = 0, bool resumeWithStart = false) {
    pads.BeginFrame();
    pads.SetButton(player, button, true);
    pads.SetButton(player, button, false);
    return play::GamepadMenuInput(pads.Player(player), resumeWithStart);
}
bool Text(const game::WorldHud& hud, const std::string& value) {
    return std::any_of(hud.menuText.begin(), hud.menuText.end(), [&](const auto& text) { return text.text == value; });
}
uint16_t Mask(input::gamepad::Control control) { return uint16_t(1) << static_cast<int>(control); }

void DevicesEntry() {
    input::GamepadGuns pads;
    pads.SetConnected(0, true);
    play::DisplayMenu options;
    play::DisplaySettings display;
    std::vector<uint32_t> sounds;
    options.Open();
    // The fixed Back row cycles categories using an ordinary D-pad. No mouse or keyboard is needed.
    for (int page = 0; page < play::DisplayMenu::kDevices; ++page) {
        options.Update(Tap(pads, Button::Up), display, sounds);
        Check(options.Cursor() == play::DisplayMenu::kBack, "D-pad up reaches fixed category Back row");
        options.Update(Tap(pads, Button::Right), display, sounds);
    }
    Check(options.CurrentPage() == play::DisplayMenu::kDevices, "controller reaches Devices category");
    options.Update(Tap(pads, Button::Down), display, sounds);
    options.Update(Tap(pads, Button::Down), display, sounds);
    Check(options.Cursor() == play::DisplayMenu::kControllers, "D-pad reaches Controllers entry");
    sounds.clear();
    options.Update(Tap(pads, Button::Right), display, sounds);
    Check(options.TakeDeviceAction() == play::DisplayMenu::DeviceAction::None && sounds.empty(),
          "device action waits for explicit confirm");
    options.Update(Tap(pads, Button::South), display, sounds);
    Check(options.TakeDeviceAction() == play::DisplayMenu::DeviceAction::Controllers &&
              options.TakeDeviceAction() == play::DisplayMenu::DeviceAction::None,
          "A/Cross requests controller page exactly once");
    Check(sounds == std::vector<uint32_t>{game::kOptionSelectSound}, "Controllers entry plays select once");

    options.SetPage(play::DisplayMenu::kDevices);
    play::MenuInput point;
    point.pointer = point.trigger = true; point.px = 100;
    point.py = play::DisplayMenu::RowY(play::DisplayMenu::kControllers, play::DisplayMenu::kDevices) * 24 + 22;
    options.Update(point, display, sounds);
    Check(options.TakeDeviceAction() == play::DisplayMenu::DeviceAction::Controllers, "pointer opens same Controllers entry");
    game::WorldHud hud;
    options.ModernHud(display, hud);
    Check(Text(hud, "Controllers") && Text(hud, "Open"), "native Devices page renders controller action");
}

void ShoulderCategories() {
    using Display = play::DisplayMenu;
    input::GamepadGuns pads;
    pads.SetConnected(0, true); pads.SetConnected(1, true);
    Display options;
    play::DisplaySettings settings;
    const auto unchanged = settings;
    std::vector<uint32_t> sounds;
    options.Open();
    options.FocusRow(Display::kGamma);
    auto shoulder = [&](Button button, int player = 0) {
        const auto in = Tap(pads, button, player);
        Check(in.pageDelta == (button == Button::RightShoulder ? 1 : -1),
              "physical bumpers become previous/next category input");
        Check(in.pressed == 0 && !in.start, "bumper category input contains no confirm, back, start or metadata bits");
        Check(!options.Update(in, settings, sounds) && options.IsOpen(), "bumper keeps Options open");
        Check(settings == unchanged && sounds.empty(), "bumper category switching changes no settings and is silent");
    };
    shoulder(Button::LeftShoulder);
    Check(options.CurrentPage() == Display::kRetro, "LB/L1 wraps Display to the last category from a value row");
    shoulder(Button::RightShoulder);
    Check(options.CurrentPage() == Display::kDisplay, "RB/R1 wraps the last category to Display");
    shoulder(Button::RightShoulder, 1);
    Check(options.CurrentPage() == Display::kGraphics, "second controller advances category with RB/R1");
    shoulder(Button::LeftShoulder, 1);
    Check(options.CurrentPage() == Display::kDisplay, "second controller returns category with LB/L1");

    pads.BeginFrame();
    pads.SetButton(0, Button::RightShoulder, true);
    options.Update(play::GamepadMenuInput(pads.Player(0)), settings, sounds);
    Check(options.CurrentPage() == Display::kGraphics, "bumper press advances category once");
    for (int frame = 0; frame < 8; ++frame) {
        pads.BeginFrame();
        pads.Update(.1);
        const auto held = play::GamepadMenuInput(pads.Player(0));
        Check(held.pageDelta == 0 && held.pressed == 0, "held bumper does not repeat category or activation input");
        options.Update(held, settings, sounds);
    }
    Check(options.CurrentPage() == Display::kGraphics && settings == unchanged,
          "holding bumper longer than D-pad repeat delay leaves category and values unchanged");
    pads.SetButton(0, Button::RightShoulder, false);
    shoulder(Button::RightShoulder);
    Check(options.CurrentPage() == Display::kGameplay, "release and press advances one more category");

    pads.BeginFrame();
    pads.SetButton(0, Button::LeftShoulder, true);
    pads.SetButton(0, Button::RightShoulder, true);
    const auto both = play::GamepadMenuInput(pads.Player(0));
    Check(both.pageDelta == 0 && both.pressed == 0 && !both.start, "simultaneous bumpers are neutral without activation");
    options.Update(both, settings, sounds);
    Check(options.CurrentPage() == Display::kGameplay && settings == unchanged && sounds.empty(),
          "simultaneous bumpers preserve current category and values");
    pads.SetButton(0, Button::LeftShoulder, false);
    pads.SetButton(0, Button::RightShoulder, false);
}

void ShoulderCategoriesFromPause() {
    input::GamepadGuns pads;
    pads.SetConnected(0, true); pads.SetConnected(1, true);
    play::PauseMenu pause;
    play::Settings settings;
    const auto unchanged = settings;
    std::vector<uint32_t> sounds;
    pause.SetModern(true);
    pause.Open();
    pause.Update(Tap(pads, Button::Down), settings, sounds);
    pause.Update(Tap(pads, Button::South), settings, sounds);
    Check(pause.DisplayOpen(), "controller opens Options from pause");
    sounds.clear();
    Check(pause.Update(Tap(pads, Button::RightShoulder, 1, true), settings, sounds) == play::PauseMenu::Action::None &&
              pause.IsOpen() && pause.DisplayOpen() && pause.Display().CurrentPage() == play::DisplayMenu::kGraphics,
          "second controller RB/R1 switches category inside paused Options");
    pause.Update(Tap(pads, Button::LeftShoulder, 0, true), settings, sounds);
    Check(pause.Display().CurrentPage() == play::DisplayMenu::kDisplay, "LB/L1 switches back inside paused Options");
    pause.Update(Tap(pads, Button::LeftShoulder, 0, true), settings, sounds);
    Check(pause.Display().CurrentPage() == play::DisplayMenu::kRetro && pause.DisplayOpen(),
          "paused Options category navigation wraps without resuming");
    Check(settings == unchanged && sounds.empty(), "paused category navigation changes no settings and is silent");
}

void ShoulderGameplayBindings() {
    using Display = play::DisplayMenu;
    using Control = input::gamepad::Control;
    input::GamepadGuns pads;
    pads.SetConnected(0, true);
    Display options;
    play::DisplaySettings settings;
    const auto unchanged = settings;
    std::vector<uint32_t> sounds;
    for (int action : {0, 2}) {
        auto& profile = pads.settings.player[0];
        profile.bindings = {Mask(Control::South), Mask(Control::East), Mask(Control::Start)};
        profile.bindings[action] = Mask(Control::LeftShoulder) | Mask(Control::RightShoulder);
        pads.Configure();
        for (auto button : {Button::RightShoulder, Button::LeftShoulder}) {
            options.Open();
            options.SetPage(Display::kDevices);
            options.FocusRow(Display::kControllers);
            const auto in = Tap(pads, button, 0, true);
            Check(in.pageDelta == (button == Button::RightShoulder ? 1 : -1) && in.pressed == 0 && !in.start,
                  "bumper mapped to gameplay Fire or Pause keeps fixed category navigation without activation");
            options.Update(in, settings, sounds);
            const auto expected = button == Button::RightShoulder ? Display::kRetro : Display::kSound;
            Check(options.CurrentPage() == expected && options.IsOpen() && settings == unchanged &&
                      options.TakeDeviceAction() == Display::DeviceAction::None && sounds.empty(),
                  "rebound bumper changes category without editing or opening selected device action");
        }
    }
}

void PadEditing() {
    input::GamepadGuns pads;
    pads.SetConnected(0, true); pads.SetConnected(1, true);
    Menu menu;
    menu.Open();
    std::vector<uint32_t> sounds;
    auto pad = [&](Button button, int player = 0) {
        const auto before = pads.settings;
        const bool closed = menu.Update(Tap(pads, button, player), pads, &sounds);
        if (before != pads.settings) pads.Configure();
        return closed;
    };
    Check(menu.SelectedPlayer() == 0 && menu.Cursor() == Menu::kPlayer, "controller page starts at P1 selector");
    Check(!pad(Button::Down) && menu.Cursor() == Menu::kSensitivity && sounds.empty(), "D-pad navigation is silent");
    pad(Button::Right);
    Check(pads.settings.player[0].sensitivity == 125 && pads.settings.player[1].sensitivity == 100,
          "sensitivity changes selected player by 25 percent");
    for (int i = 0; i < 10; ++i) pad(Button::Right);
    Check(pads.settings.player[0].sensitivity == 300, "sensitivity clamps at 300 percent");
    for (int i = 0; i < 15; ++i) pad(Button::Left);
    Check(pads.settings.player[0].sensitivity == 25, "sensitivity clamps at 25 percent");
    pad(Button::Down);
    pad(Button::Right);
    Check(pads.settings.player[0].deadzone == 20, "deadzone changes by two percent from default 18");
    for (int i = 0; i < 20; ++i) pad(Button::Left);
    Check(pads.settings.player[0].deadzone == 0, "deadzone clamps at zero");
    for (int i = 0; i < 25; ++i) pad(Button::Right);
    Check(pads.settings.player[0].deadzone == 40, "deadzone clamps at 40 percent");
    const auto p1 = pads.settings.player[0];
    pad(Button::Up); pad(Button::Up); pad(Button::Right, 1);
    Check(menu.SelectedPlayer() == 1, "either controller selects Player 2 profile");
    pad(Button::Down); pad(Button::South, 1);
    Check(pads.settings.player[1].sensitivity == 125 && pads.settings.player[0] == p1,
          "P2 confirmation edits P2 without changing P1");
    pad(Button::Down); pad(Button::Down);
    const auto oldFire = pads.settings.player[1].bindings[0];
    pad(Button::Right);
    Check(pads.settings.player[1].bindings[0] != oldFire, "right cycles Fire binding");
    Check(pads.settings.player[1].bindings[0] & Mask(input::gamepad::Control::RightTrigger), "Fire remap preserves the alternate trigger");
    { game::WorldHud notice; menu.ModernHud(pads, notice);
      Check(Text(notice, "Shared button swapped; alternate buttons kept."), "conflicting remap visibly explains the swap"); }
    pad(Button::Down);
    const auto oldReload = pads.settings.player[1].bindings[1];
    pad(Button::Left);
    Check(pads.settings.player[1].bindings[1] != oldReload, "left cycles Reload binding");
    pad(Button::Down);
    const auto oldPause = pads.settings.player[1].bindings[2];
    pad(Button::South);
    Check(pads.settings.player[1].bindings[2] != oldPause, "confirm cycles Pause binding");
    pad(Button::Down); pad(Button::South);
    Check(pads.settings.player[1] == input::gamepad::Profile{} && pads.settings.player[0] == p1,
          "Restore defaults resets only selected player");
    pad(Button::Down);
    Check(pad(Button::South) && !menu.IsOpen(), "confirm on Back closes and reports closure");
    Check(!menu.Update(Key(8), pads), "closed controller menu ignores input");
}

void PointerAndFixedControls() {
    input::GamepadGuns pads;
    pads.SetConnected(0, true);
    Menu menu;
    menu.Open();
    menu.Update(Point(Menu::kPlayer, true), pads);
    Check(menu.SelectedPlayer() == 1, "pointer selects Player 2");
    menu.Update(Point(Menu::kSensitivity, true), pads);
    Check(pads.settings.player[1].sensitivity == 125, "pointer changes sensitivity");
    menu.Update(Point(Menu::kSensitivity, true, 290), pads);
    Check(pads.settings.player[1].sensitivity == 100, "pointer left arrow decreases sensitivity");
    menu.Update(Point(Menu::kDeadzone, true), pads);
    Check(pads.settings.player[1].deadzone == 20, "pointer changes deadzone");
    menu.Update(Point(Menu::kFire, true), pads);
    menu.Update(Point(Menu::kReload, true), pads);
    menu.Update(Point(Menu::kPause, true), pads);
    Check(pads.settings.player[1] != input::gamepad::Profile{}, "pointer edits gameplay bindings");
    menu.Update(Point(Menu::kDefaults, true), pads);
    Check(pads.settings.player[1] == input::gamepad::Profile{}, "pointer restores selected defaults");
    Check(Menu::RowAt(10, 100) == -1 && Menu::RowAt(50, 340) == -1, "status text and outside margins are not menu rows");
    Check(menu.Update(Point(Menu::kBack, true), pads) && !menu.IsOpen(), "pointer Back closes page");

    auto& profile = pads.settings.player[0];
    profile.bindings = {Mask(input::gamepad::Control::East), Mask(input::gamepad::Control::South), Mask(input::gamepad::Control::West)};
    pads.Configure();
    menu.Open();
    Check(!menu.Update(Tap(pads, Button::South), pads) && menu.SelectedPlayer() == 1,
          "A/Cross confirms even when gameplay mapping assigns it to Reload");
    Check(menu.Update(Tap(pads, Button::East), pads) && !menu.IsOpen(),
          "B/Circle backs out even when gameplay mapping assigns it to Fire");
    menu.Open();
    auto reload = Key(0); reload.reload = true;
    Check(menu.Update(reload, pads), "pointer Reload is a consistent Back control");
}

void HudContent() {
    input::GamepadGuns pads;
    pads.SetConnected(0, true);
    Menu menu;
    menu.Open();
    game::WorldHud hud;
    menu.ModernHud(pads, hud);
    Check(Text(hud, "CONTROLLERS") && Text(hud, "Player 1") && Text(hud, "100%") && Text(hud, "18%"),
          "native page displays profile and default aim values");
    Check(Text(hud, "P1: Connected    P2: Not connected"), "connection status shows both player slots");
    for (int action = 0; action < 3; ++action)
        Check(Text(hud, input::gamepad::BindingName(pads.settings.player[0].bindings[action])), "native page displays universal binding labels");
    Check(Text(hud, "A / CROSS: CHANGE   B / CIRCLE: BACK"), "fixed controller navigation is documented");
    for (const auto& panel : hud.menuPanels)
        Check(panel.left >= 0 && panel.top >= 0 && panel.right <= 640 && panel.bottom <= 480,
              "native menu panels remain on canvas");
    menu.Notice("Controller settings could not be saved");
    menu.ModernHud(pads, hud);
    Check(Text(hud, "Controller settings could not be saved"), "caller save error is visible");
}
} // namespace

int main() {
    DevicesEntry();
    ShoulderCategories();
    ShoulderCategoriesFromPause();
    ShoulderGameplayBindings();
    PadEditing();
    PointerAndFixedControls();
    HudContent();
    std::puts("PASS controller settings, bumper categories, P1/P2 profiles, fixed menu controls and pointer editing");
}
