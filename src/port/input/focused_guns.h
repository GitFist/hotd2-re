#pragma once

#include <array>

#include "player_state.h"

namespace input {

// Presentation/input boundary: device polling continues while the game is in
// the background, but desktop activity must not become game or menu actions.
// UpdateFocus runs after the underlying source's poll, once per outer loop.
class FocusedGuns final : public GunSource {
   public:
    explicit FocusedGuns(GunSource& source) : source_(source) {}

    const char* Name() const override { return source_.Name(); }

    void UpdateFocus(bool active) {
        const bool changed = active != active_;
        for (int player = 0; player < kMaxPlayers; ++player) {
            const auto state = source_.Player(player);
            // A suppressed press's release is not a new game action either.
            blockedRelease_[player] = blocked_[player] & state.released;
            blocked_[player] &= state.held;
            blockedMenu_[player] &= state.menuHeld;
            if (!active || changed) {
                blocked_[player] |= state.held;
                blockedMenu_[player] |= state.menuHeld;
            }
        }
        active_ = active;
        // In particular, drain AFTER WiiRemotes::Update: its newly polled raw
        // snapshot is also used directly by calibration, beyond Player().
        if (!active || changed) source_.BeginFrame();
    }

    PlayerState Player(int player) const override {
        if (player < 0 || player >= kMaxPlayers) return {};
        auto state = source_.Player(player);
        if (!active_) {
            state.held = state.pressed = state.released = 0;
            state.menuHeld = state.menuPressed = 0;
        } else {
            state.held &= static_cast<uint8_t>(~blocked_[player]);
            state.pressed &= static_cast<uint8_t>(~blocked_[player]);
            state.released &= static_cast<uint8_t>(~(blocked_[player] | blockedRelease_[player]));
            state.menuHeld &= ~blockedMenu_[player];
            state.menuPressed &= ~blockedMenu_[player];
        }
        return state;
    }

    void BeginFrame() override { source_.BeginFrame(); }

   private:
    GunSource& source_;
    bool active_ = true;
    std::array<uint8_t, kMaxPlayers> blocked_{}, blockedRelease_{};
    std::array<uint32_t, kMaxPlayers> blockedMenu_{};
};

}  // namespace input
