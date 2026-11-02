#include "gamepad_guns.h"

#include <algorithm>
#include <cmath>

namespace input {
namespace {
constexpr float kAimSpeed = .8f;
constexpr float kTriggerPress = .55f, kTriggerRelease = .45f;
constexpr double kRepeatDelay = .35, kRepeatInterval = .1;
constexpr std::array<uint32_t, 4> kDirections{0x10, 0x20, 0x40, 0x80};

bool ValidSlot(int slot) { return slot >= 0 && slot < kMaxPlayers; }
bool Down(uint16_t buttons, GamepadButton button) {
    return (buttons & (uint16_t(1) << static_cast<int>(button))) != 0;
}
bool TriggerHeld(float value, bool held) {
    return held ? value > kTriggerRelease : value >= kTriggerPress;
}
struct Stick { float x = 0, y = 0; };
Stick AimStick(float x, float y, float deadzone) {
    const float length = std::sqrt(x * x + y * y);
    if (length <= deadzone) return {};
    const float amount = (std::min(length, 1.f) - deadzone) / (1.f - deadzone);
    return {x / length * amount, y / length * amount};
}
} // namespace

void GamepadGuns::Configure() {
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto& profile = settings.player[slot];
        profile.Normalize();
        if (profile == appliedSettings_.player[slot]) continue;
        appliedSettings_.player[slot] = profile;
        auto& pad = slots_[slot];
        pad.blockedControls = HeldControls(pad);
        pad.player.held = pad.player.pressed = pad.player.released = 0;
        if (pad.connected) Refresh(slot);
    }
}

bool GamepadGuns::Connected(int slot) const {
    return ValidSlot(slot) && slots_[slot].connected;
}

uint16_t GamepadGuns::HeldControls(const Slot& pad) {
    return (pad.buttons & 0xff) |
           (pad.leftTrigger ? gamepad::ControlMask(gamepad::Control::LeftTrigger) : 0) |
           (pad.rightTrigger ? gamepad::ControlMask(gamepad::Control::RightTrigger) : 0);
}

void GamepadGuns::SetConnected(int slot, bool connected) {
    if (!ValidSlot(slot) || slots_[slot].connected == connected) return;
    // A new device in a reused slot must not inherit ownership, held buttons,
    // navigation repeats, cursor position, or pending edges from its predecessor.
    slots_[slot] = {};
    slots_[slot].connected = connected;
}

void GamepadGuns::SetAxis(int slot, GamepadAxis axis, float value) {
    const int index = static_cast<int>(axis);
    if (!ValidSlot(slot) || index < 0 || index >= 6 || !slots_[slot].connected) return;
    auto& pad = slots_[slot];
    if (!std::isfinite(value)) value = 0;
    pad.axes[index] = std::clamp(value, index < 4 ? -1.f : 0.f, 1.f);
    pad.leftTrigger = TriggerHeld(pad.axes[4], pad.leftTrigger);
    pad.rightTrigger = TriggerHeld(pad.axes[5], pad.rightTrigger);
    Refresh(slot);
}

void GamepadGuns::SetButton(int slot, GamepadButton button, bool down) {
    const int index = static_cast<int>(button);
    if (!ValidSlot(slot) || index < 0 || index >= 12 || !slots_[slot].connected) return;
    auto& pad = slots_[slot];
    const uint16_t mask = uint16_t(1) << index;
    if (down) pad.buttons |= mask;
    else pad.buttons &= static_cast<uint16_t>(~mask);
    Refresh(slot);
}

void GamepadGuns::Refresh(int slot) {
    auto& pad = slots_[slot];
    const auto& profile = appliedSettings_.player[slot];
    const float deadzone = profile.deadzone / 100.f;
    if (!pad.reserved) {
        const auto& a = pad.axes;
        const bool moved = a[0]*a[0] + a[1]*a[1] > deadzone*deadzone ||
                           a[2]*a[2] + a[3]*a[3] > deadzone*deadzone ||
                           a[4] > deadzone || a[5] > deadzone;
        if (!pad.buttons && !moved) return;
        pad.reserved = true;
        pad.player.connected = pad.player.gamepad = true;
    }
    auto& state = pad.player;
    const auto down = [&](GamepadButton button) { return Down(pad.buttons, button); };
    const uint16_t controls = HeldControls(pad);
    pad.blockedControls &= controls;
    const uint16_t usable = controls & ~pad.blockedControls;
    const uint8_t held = (usable & profile.bindings[0] ? kTrigger : 0) |
                         (usable & profile.bindings[1] ? kReload : 0) |
                         (usable & profile.bindings[2] ? kStart : 0);
    state.pressed |= held & ~state.held;
    state.released |= state.held & ~held;
    state.held = held;

    // Front-menu controls remain consistent regardless of gameplay bindings.
    const uint32_t menu = (down(GamepadButton::Start) ? gamepad::kMenuStart : 0) |
                          (down(GamepadButton::LeftShoulder) ? gamepad::kMenuPreviousCategory : 0) |
                          (down(GamepadButton::RightShoulder) ? gamepad::kMenuNextCategory : 0) |
                          (down(GamepadButton::South) || down(GamepadButton::Start) ? 8 : 0) |
                          (down(GamepadButton::East) || down(GamepadButton::Back) ? 2 : 0) |
                          (down(GamepadButton::Up) || pad.axes[1] <= -.5f ? 0x10 : 0) |
                          (down(GamepadButton::Down) || pad.axes[1] >= .5f ? 0x20 : 0) |
                          (down(GamepadButton::Left) || pad.axes[0] <= -.5f ? 0x40 : 0) |
                          (down(GamepadButton::Right) || pad.axes[0] >= .5f ? 0x80 : 0);
    for (size_t i = 0; i < kDirections.size(); ++i) {
        if (!(menu & kDirections[i])) pad.repeatAfter[i] = 0;
        else if (!(state.menuHeld & kDirections[i])) pad.repeatAfter[i] = kRepeatDelay;
    }
    state.menuPressed |= menu & ~state.menuHeld;
    state.menuHeld = menu;
}

void GamepadGuns::Update(double seconds) {
    const double dt = std::isfinite(seconds) ? std::clamp(seconds, 0., .1) : 0.;
    if (dt == 0) return;
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        auto& pad = slots_[slot];
        if (!pad.connected || !pad.reserved) continue;
        const auto& profile = appliedSettings_.player[slot];
        const float deadzone = profile.deadzone / 100.f;
        const float speed = kAimSpeed * profile.sensitivity / 100.f;
        auto left = AimStick(pad.axes[0], pad.axes[1], deadzone);
        const auto right = AimStick(pad.axes[2], pad.axes[3], deadzone);
        // Either stick aims; selecting the stronger one keeps two sticks from
        // doubling cursor speed and leaves a neutral stick unable to cause drift.
        if (right.x*right.x + right.y*right.y > left.x*left.x + left.y*left.y) left = right;
        auto& state = pad.player;
        state.x = std::clamp(state.x + left.x * speed * float(dt), 0.f, 1.f);
        state.y = std::clamp(state.y + left.y * speed * float(dt), 0.f, 1.f);
        for (size_t i = 0; i < kDirections.size(); ++i) {
            if (!(state.menuHeld & kDirections[i])) continue;
            pad.repeatAfter[i] -= dt;
            if (pad.repeatAfter[i] <= 1e-9) {
                state.menuPressed |= kDirections[i];
                do { pad.repeatAfter[i] += kRepeatInterval; } while (pad.repeatAfter[i] <= 1e-9);
            }
        }
    }
}

PlayerState GamepadGuns::Player(int player) const {
    if (!ValidSlot(player)) return {};
    if (slots_[player].reserved) return slots_[player].player;
    int fallbackIndex = player;
    for (int i = 0; i < player; ++i) if (slots_[i].reserved) --fallbackIndex;
    auto state = fallback_ ? fallback_->Player(fallbackIndex) : PlayerState{};
    state.gamepad = false;
    return state;
}

void GamepadGuns::BeginFrame() {
    for (auto& pad : slots_) {
        pad.player.pressed = pad.player.released = 0;
        pad.player.menuPressed = 0;
    }
    if (fallback_) fallback_->BeginFrame();
}
} // namespace input
