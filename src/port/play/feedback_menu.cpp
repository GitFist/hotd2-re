#include "feedback_menu.h"

#include <algorithm>
#include "game/menu_audio.h"
#include "game/menu_text.h"
#include "play/device_menu_hud.h"

namespace play {
namespace {
constexpr const char* kLabels[]{"Player", "Controller rumble", "Light gun", "COM port", "Gun recoil", "Damage rumble", "Test shot", "Test damage", "Back"};
constexpr float kValueX = 330;
bool Adjustable(int row) { return row >= FeedbackMenu::kPlayer && row <= FeedbackMenu::kDamage; }
float TextScale(const std::string& text, float limit = .44f, float width = 560) {
    // Native proportional glyphs can be wider than the original 16px cells.
    return text.empty() ? limit : std::min(limit, width / (30.f * float(text.size())));
}
}

int FeedbackMenu::RowAt(float x, float y) {
    if (x < 24 || x >= 616) return -1;
    for (int row = 0; row < kRowCount; ++row)
        if (y >= RowY(row) + 8 && y < RowY(row) + 35) return row;
    return -1;
}

FeedbackMenu::Action FeedbackMenu::Update(const MenuInput& in, input::feedback::Settings& settings, std::vector<uint32_t>* sounds) {
    if (!open_) return Action::None;
    auto sound = [&](uint32_t id) { if (sounds) sounds->push_back(id); };
    auto close = [&] { Close(); sound(game::kOptionBackSound); return Action::None; };
    if (in.start || in.reload || (in.pressed & 2)) return close();
    const int under = in.pointer ? RowAt(in.px, in.py) : -1;
    if (pointer_.Update(in.pointer, in.px, in.py, in.trigger) && under >= 0) cursor_ = under;
    if (in.pressed & 0x10) cursor_ = (cursor_ + kRowCount - 1) % kRowCount;
    if (in.pressed & 0x20) cursor_ = (cursor_ + 1) % kRowCount;
    const bool choose = (in.pressed & 0xc) || (in.trigger && under == cursor_);
    if (cursor_ == kBack) return choose ? close() : Action::None;
    if (cursor_ == kTestShot || cursor_ == kTestDamage) {
        if (!choose) return Action::None;
        notice_.clear();
        sound(game::kOptionSelectSound);
        return cursor_ == kTestShot ? Action::TestShot : Action::TestDamage;
    }
    int delta = ((in.pressed & 0x80) ? 1 : 0) - ((in.pressed & 0x40) ? 1 : 0);
    if (!delta && in.scroll && under == cursor_ && Adjustable(cursor_)) delta = std::clamp(in.scroll, -256, 256);
    if (!delta && choose && Adjustable(cursor_)) delta = in.trigger ? game::MenuPointerStep(in.px, kValueX - 18) : 1;
    if (!delta) return Action::None;
    notice_.clear();
    if (cursor_ == kPlayer) {
        player_ = (player_ + delta % 2 + 2) % 2;
        sound(game::kOptionSelectSound);
        return Action::None;
    }
    auto& profile = settings.player[player_];
    const auto before = profile;
    profile.Normalize();
    if (cursor_ == kController) profile.controllerStrength = std::clamp(profile.controllerStrength + 25 * delta, 0, 100);
    else if (cursor_ == kGun) {
        constexpr int count = input::feedback::kGunTypeCount;
        profile.gun = static_cast<input::feedback::GunType>((static_cast<int>(profile.gun) + delta % count + count) % count);
    }
    else if (cursor_ == kPort) {
        if (profile.gun == input::feedback::GunType::Sinden) profile.sindenGun = (profile.sindenGun - 1 + delta % 2 + 2) % 2 + 1;
        else profile.port = std::clamp(profile.port + delta, 0, 256);
    }
    else if (cursor_ == kRecoil) profile.recoil = !profile.recoil;
    else if (cursor_ == kDamage) profile.damage = !profile.damage;
    if (profile != before) sound(game::kOptionSelectSound);
    return Action::None;
}

std::string FeedbackMenu::Value(const input::feedback::Settings& settings, int row) const {
    const auto& profile = settings.player[player_];
    if (row == kPlayer) return "Player " + std::to_string(player_ + 1);
    if (row == kController) return profile.controllerStrength ? std::to_string(profile.controllerStrength) + "%" : "Off";
    if (row == kGun) return input::feedback::GunTypeName(profile.gun);
    if (row == kPort) {
        if (profile.gun == input::feedback::GunType::Sinden) return profile.sindenGun == 2 ? "B / Gun 2" : "A / Gun 1";
        return profile.port ? "COM" + std::to_string(profile.port) : "Not set";
    }
    if (row == kRecoil) return profile.recoil ? "On" : "Off";
    if (row == kDamage) return profile.damage ? "On" : "Off";
    return {};
}

void FeedbackMenu::Sprites(const game::ExeImage& exe, const input::feedback::Settings& settings, std::vector<game::SpriteDef>& out) const {
    if (!open_) return;
    game::MenuBackground(out);
    auto text = [&](float x, float y, const std::string& value, float scale, bool selected = false) {
        game::MenuTextScaled(exe, x, y, value, selected ? 0x110 : 0, scale, out);
    };
    text(26, 20, "FEEDBACK", 1.05f);
    text(34, 73, "NATIVE CONTROLLER / LIGHT-GUN FEEDBACK", .42f);
    for (int row = 0; row < kRowCount; ++row) {
        const float y = RowY(row);
        const auto value = Value(settings, row);
        const char* label = row == kPort && settings.player[player_].gun == input::feedback::GunType::Sinden ? "Driver gun" : kLabels[row];
        text(34, y, label, .65f, row == cursor_);
        text(kValueX, y, value, TextScale(value, .52f, 260), row == cursor_);
        if (row == cursor_ && Adjustable(row)) game::MenuAdjustArrows(kValueX - 18, 600, y + 20, out);
    }
    for (int slot = 0; slot < 2; ++slot) {
        const std::string value = "P" + std::to_string(slot + 1) + " / " + (status_[slot].empty() ? "Off" : status_[slot]);
        text(34, 350.f + 25.f * slot, value, TextScale(value));
    }
    text(34, 400, notice_, TextScale(notice_));
    text(28, 434, "Online play uses local Player 1 feedback.", .4f);
    text(28, 457, "LEFT / RIGHT: ADJUST   ENTER: TEST / CHANGE   ESC: BACK", .36f);
}

void FeedbackMenu::ModernHud(const input::feedback::Settings& settings, game::WorldHud& hud) const {
    if (!open_) return;
    device_ui::Begin(hud, 12, "FEEDBACK", "NATIVE CONTROLLER / LIGHT-GUN FEEDBACK");
    hud.nightShiftRow = cursor_;
    for (int row = 0; row < kRowCount; ++row) {
        const char* label = row == kPort && settings.player[player_].gun == input::feedback::GunType::Sinden ? "Driver gun" : kLabels[row];
        device_ui::Row(hud, row, cursor_, label);
        const auto value = Value(settings, row);
        const float y = RowY(row) + 10;
        device_ui::Text(hud, kValueX, y, value, TextScale(value, .52f, 260));
        if (row == cursor_ && Adjustable(row)) {
            device_ui::Text(hud, kValueX - 24, y, "<", .52f);
            device_ui::Text(hud, 600, y, ">", .52f);
        }
    }
    for (int slot = 0; slot < 2; ++slot) {
        const std::string value = "P" + std::to_string(slot + 1) + " / " + (status_[slot].empty() ? "Off" : status_[slot]);
        device_ui::Text(hud, 34, 360.f + 25.f * slot, value, TextScale(value), device_ui::kMuted);
    }
    device_ui::Text(hud, 34, 410, notice_, TextScale(notice_), device_ui::kMuted);
    device_ui::Footer(hud, "Online play uses local Player 1 feedback.");
    device_ui::Text(hud, 28, 461, "LEFT / RIGHT: ADJUST   ENTER: TEST / CHANGE   ESC: BACK", .36f, device_ui::kMuted);
}
} // namespace play
