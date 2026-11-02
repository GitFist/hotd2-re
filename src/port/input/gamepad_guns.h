#pragma once

// Platform-free standard-controller input. The platform assigns connected pads
// to slots 0/1 and feeds normalized sticks (-1..1) and triggers (0..1).
#include <array>
#include <cstdint>

#include "gamepad_settings.h"
#include "player_state.h"

namespace input {

enum class GamepadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };
enum class GamepadButton {
    South, East, West, North, Back, Start, LeftShoulder, RightShoulder, Up, Down, Left, Right
};

class GamepadGuns final : public GunSource {
public:
    explicit GamepadGuns(GunSource* fallback = nullptr) : fallback_(fallback) {}
    gamepad::Settings settings;
    // Apply settings edits while suppressing controls already held until release.
    void Configure();
    bool Connected(int slot) const;
    void SetConnected(int slot, bool connected);
    void SetAxis(int slot, GamepadAxis axis, float value);
    void SetButton(int slot, GamepadButton button, bool down);
    // At 100% sensitivity aim moves .8 image widths/heights per second at full stick deflection.
    // Long polling gaps are capped at .1 seconds, avoiding a jump after a stall.
    void Update(double seconds);

    const char* Name() const override { return "gamepad / pointer"; }
    PlayerState Player(int player) const override;
    void BeginFrame() override;

private:
    struct Slot {
        bool connected = false, reserved = false;
        std::array<float, 6> axes{};
        uint16_t buttons = 0;
        bool leftTrigger = false, rightTrigger = false;
        uint16_t blockedControls = 0;
        PlayerState player;
        std::array<double, 4> repeatAfter{};
    };

    void Refresh(int slot);
    static uint16_t HeldControls(const Slot& pad);
    GunSource* fallback_;
    gamepad::Settings appliedSettings_;
    std::array<Slot, kMaxPlayers> slots_{};
};

} // namespace input
