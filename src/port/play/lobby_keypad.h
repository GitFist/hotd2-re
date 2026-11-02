#pragma once

#include <cstdint>

namespace play {

// The lobby's existing keypad: 1..5, 6..0, then Delete / Back.
// Indices match Lobby::KeyAt and the keypad's pointer highlights.
struct LobbyKeypadResult {
    int key = 0;
    char digit = 0;
    bool erase = false;
    bool back = false;
};

inline LobbyKeypadResult UpdateLobbyKeypad(int key, uint32_t pressed) {
    if (key < 0 || key >= 12) key = 0;
    const auto rowOf = [](int i) { return i < 10 ? i / 5 : 2; };
    const int horizontal = ((pressed & 0x80) != 0) - ((pressed & 0x40) != 0);
    if (horizontal) {
        const int row = rowOf(key);
        const int first = row * 5, count = row == 2 ? 2 : 5;
        key = first + (key - first + count + horizontal) % count;
    }
    const int vertical = ((pressed & 0x20) != 0) - ((pressed & 0x10) != 0);
    if (vertical) {
        // Pick the nearest key in the next row using the existing hit-box centers.
        constexpr int centers[] = {208, 264, 320, 376, 432, 208, 264, 320, 376, 432, 240, 408};
        const int row = (rowOf(key) + 3 + vertical) % 3;
        int nearest = row * 5, distance = 641;
        const int end = row == 2 ? 12 : nearest + 5;
        for (int i = nearest; i < end; ++i) {
            const int delta = centers[i] - centers[key];
            const int candidate = delta < 0 ? -delta : delta;
            if (candidate < distance) {
                nearest = i;
                distance = candidate;
            }
        }
        key = nearest;
    }
    LobbyKeypadResult result;
    result.key = key;
    if (pressed & 0xC) {
        if (key < 10) result.digit = static_cast<char>('0' + (key + 1) % 10);
        else if (key == 10) result.erase = true;
        else result.back = true;
    }
    return result;
}

}  // namespace play
