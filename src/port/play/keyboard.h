#pragma once
#include <cstdint>

namespace play {
constexpr uint32_t kEscapeKey = 0x1000;
constexpr uint32_t kPreviewKey = 0x2000;  // local graphics-menu UI visibility; never transmitted as gameplay input
// Enter confirms during active play, and still starts/continues an idle player. Escape is
// pause in a game and Back in menus. Separate source bits keep Enter and Escape edges distinct.
inline uint32_t WorldKeyboardBits(uint32_t keys, bool inGame, bool activePlayer) {
    uint32_t out = keys & ~(kEscapeKey | kPreviewKey);
    if (inGame && activePlayer && (out & 8)) out = (out & ~8u) | 4;
    if (keys & kEscapeKey) out |= inGame ? 8 : 2;
    return out;
}
}  // namespace play
