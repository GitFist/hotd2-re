#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace play {

// A local display preference, deliberately outside the synchronized game state.
class ScoreDisplayCode {
public:
    struct Input {
        uint32_t keys;
        bool toggled;
    };

    static bool FrontEnd(int mode, int page) { return mode == 3 || (mode == 4 && page <= 0); }
    bool Enabled() const { return enabled_; }
    void ResetSequence() { count_ = 0; }

    Input Update(uint32_t held, bool eligible) {
        const uint32_t pressed = held & ~previous_;
        previous_ = held;
        if (!(held & kEnter)) consumeEnter_ = false;
        bool toggled = false;
        if (!eligible) {
            ResetSequence();
        } else if (pressed) {
            if (pressed == kLeft || pressed == kRight) {
                if (held & ~(kLeft | kRight) || (held & (kLeft | kRight)) == (kLeft | kRight)) {
                    ResetSequence();
                } else {
                    if (count_ == recent_.size()) {
                        for (std::size_t i = 1; i < recent_.size(); ++i) recent_[i - 1] = recent_[i];
                        --count_;
                    }
                    recent_[count_++] = pressed;
                }
            } else {
                if (pressed == kEnter && held == kEnter && count_ == kCode.size() && recent_ == kCode) {
                    enabled_ = !enabled_;
                    toggled = consumeEnter_ = true;
                }
                ResetSequence();
            }
        }
        // Keep the confirming Enter consumed through key-repeat events and screen transitions.
        return {consumeEnter_ ? held & ~kEnter : held, toggled};
    }

private:
    static constexpr uint32_t kLeft = 0x40, kRight = 0x80, kEnter = 8;
    static constexpr std::array<uint32_t, 7> kCode{kLeft, kLeft, kRight, kRight, kRight, kLeft, kRight};
    std::array<uint32_t, 7> recent_{};
    std::size_t count_ = 0;
    uint32_t previous_ = 0;
    bool enabled_ = false, consumeEnter_ = false;
};

}  // namespace play
