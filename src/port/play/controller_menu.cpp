#include "controller_menu.h"

#include <algorithm>

#include "game/menu_audio.h"
#include "game/menu_text.h"
#include "play/device_menu_hud.h"

namespace play {
namespace {
constexpr const char* kLabels[]{"Player", "Aim sensitivity", "Stick deadzone", "Fire", "Reload", "Pause", "Restore defaults", "Back"};
constexpr float kValueX = 308;
float ValueScale(const std::string& text) {
    return text.empty() ? .52f : std::min(.52f, 280.f / (16.f * float(text.size())));
}
bool Adjustable(int row) { return row >= ControllerMenu::kPlayer && row <= ControllerMenu::kPause; }
}

int ControllerMenu::RowAt(float x, float y) {
    if (x < 24 || x >= 616) return -1;
    for (int row = 0; row < kRowCount; ++row)
        if (y >= RowY(row) + 8 && y < RowY(row) + 35) return row;
    return -1;
}

bool ControllerMenu::Update(const MenuInput& in, input::GamepadGuns& pads, std::vector<uint32_t>* sounds) {
    if (!open_) return false;
    auto sound = [&](uint32_t id) { if (sounds) sounds->push_back(id); };
    auto close = [&] { Close(); sound(game::kOptionBackSound); return true; };
    if (in.start || in.reload || (in.pressed & 2)) return close();

    const int under = in.pointer ? RowAt(in.px, in.py) : -1;
    if (pointer_.Update(in.pointer, in.px, in.py, in.trigger) && under >= 0) cursor_ = under;
    if (in.pressed & 0x10) cursor_ = (cursor_ + kRowCount - 1) % kRowCount;
    if (in.pressed & 0x20) cursor_ = (cursor_ + 1) % kRowCount;
    const bool choose = (in.pressed & 0xc) || (in.trigger && under == cursor_);
    int delta = ((in.pressed & 0x80) ? 1 : 0) - ((in.pressed & 0x40) ? 1 : 0);
    if (!delta && choose && Adjustable(cursor_))
        delta = in.trigger ? game::MenuPointerStep(in.px, kValueX - 18) : 1;
    if (cursor_ == kBack) return choose ? close() : false;
    if (cursor_ == kDefaults) {
        if (choose) {
            pads.settings.player[player_] = {};
            notice_ = "Player " + std::to_string(player_ + 1) + " defaults restored";
            sound(game::kOptionSelectSound);
        }
        return false;
    }
    if (!delta) return false;
    notice_.clear();
    if (cursor_ == kPlayer) {
        player_ = (player_ + delta % 2 + 2) % 2;
        sound(game::kOptionSelectSound);
        return false;
    }
    auto& profile = pads.settings.player[player_];
    const auto before = profile;
    if (cursor_ == kSensitivity) profile.sensitivity = std::clamp(profile.sensitivity + 25 * delta, 25, 300);
    else if (cursor_ == kDeadzone) profile.deadzone = std::clamp(profile.deadzone + 2 * delta, 0, 40);
    else if (cursor_ >= kFire && cursor_ <= kPause) profile.CycleBinding(cursor_ - kFire, delta);
    if (profile.sensitivity != before.sensitivity || profile.deadzone != before.deadzone || profile.bindings != before.bindings)
        sound(game::kOptionSelectSound);
    return false;
}

std::string ControllerMenu::Value(const input::GamepadGuns& pads, int row) const {
    const auto& profile = pads.settings.player[player_];
    if (row == kPlayer) return "Player " + std::to_string(player_ + 1);
    if (row == kSensitivity) return std::to_string(profile.sensitivity) + "%";
    if (row == kDeadzone) return std::to_string(profile.deadzone) + "%";
    if (row >= kFire && row <= kPause) return input::gamepad::BindingName(profile.bindings[row - kFire]);
    return {};
}

std::string ControllerMenu::Status(const input::GamepadGuns& pads) const {
    return std::string("P1: ") + (pads.Connected(0) ? "Connected" : "Not connected") +
           "    P2: " + (pads.Connected(1) ? "Connected" : "Not connected");
}

void ControllerMenu::Sprites(const game::ExeImage& exe, const input::GamepadGuns& pads, std::vector<game::SpriteDef>& out) const {
    if (!open_) return;
    game::MenuBackground(out);
    auto text = [&](float x, float y, const std::string& value, float scale, bool selected = false) {
        game::MenuTextScaled(exe, x, y, value, selected ? 0x110 : 0, scale, out);
    };
    text(26, 20, "CONTROLLERS", 1.05f);
    text(34, 73, "PER-PLAYER AIM / GAMEPLAY BUTTONS", .42f);
    for (int row = 0; row < kRowCount; ++row) {
        const float y = RowY(row);
        const auto value = Value(pads, row);
        text(34, y, kLabels[row], .65f, row == cursor_);
        text(kValueX, y, value, ValueScale(value), row == cursor_);
        if (row == cursor_ && Adjustable(row)) game::MenuAdjustArrows(kValueX - 18, 600, y + 20, out);
    }
    text(34, 328, Status(pads), .48f);
    text(34, 355, "Either stick aims. D-pad moves through menus.", .44f);
    text(34, 378, "Bindings change gameplay; menu A / Cross and B / Circle stay fixed.", .4f);
    text(34, 405, notice_, .44f);
    text(28, 438, "A / CROSS: CHANGE   B / CIRCLE: BACK", .44f);
    text(28, 457, "LEFT / RIGHT: ADJUST   ENTER: CHANGE   ESC: BACK", .36f);
}

void ControllerMenu::ModernHud(const input::GamepadGuns& pads, game::WorldHud& hud) const {
    if (!open_) return;
    device_ui::Begin(hud, 11, "CONTROLLERS", "PER-PLAYER AIM / GAMEPLAY BUTTONS");
    hud.nightShiftRow = cursor_;
    for (int row = 0; row < kRowCount; ++row) {
        device_ui::Row(hud, row, cursor_, kLabels[row]);
        const float y = RowY(row) + 10;
        const auto value = Value(pads, row);
        device_ui::Text(hud, kValueX, y, value, ValueScale(value));
        if (row == cursor_ && Adjustable(row)) {
            device_ui::Text(hud, kValueX - 24, y, "<", .52f);
            device_ui::Text(hud, 600, y, ">", .52f);
        }
    }
    device_ui::Text(hud, 34, 338, Status(pads), .48f, device_ui::kMuted);
    device_ui::Text(hud, 34, 365, "Either stick aims. D-pad moves through menus.", .44f, device_ui::kMuted);
    device_ui::Text(hud, 34, 388, "Bindings change gameplay; menu A / Cross and B / Circle stay fixed.", .4f, device_ui::kMuted);
    device_ui::Text(hud, 34, 415, notice_, .44f, device_ui::kMuted);
    device_ui::Text(hud, 28, 442, "A / CROSS: CHANGE   B / CIRCLE: BACK", .44f, device_ui::kMuted);
    device_ui::Text(hud, 28, 461, "LEFT / RIGHT: ADJUST   ENTER: CHANGE   ESC: BACK", .36f, device_ui::kMuted);
}

} // namespace play
