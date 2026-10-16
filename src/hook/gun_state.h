#pragma once

// Pure conversion from a raw-input player to the values Input_Poll's mode 5 (mouse-as-gun)
// writes for a gun slot. No memory access; unit-tested in tests/input_test_unit.cpp.

#include <cstdint>

#include "input/aim_map.h"
#include "input/raw_input.h"

namespace hook {

// Button bits as produced by the original mouse reader (0x0041F370).
constexpr uint32_t kBitReload  = 0x2;  // right
constexpr uint32_t kBitTrigger = 0x4;  // left
constexpr uint32_t kBitStart   = 0x8;  // middle

struct GunSlotState {
    uint32_t held = 0, pressed = 0, released = 0;  // record +0x08 / +0x10 / +0x14 (+0x0C = ~held)
    int fired = 0;      // 0x007DC120[slot]
    int offscreen = 0;  // 0x007DB7C8[slot]
    int x = 0, y = 0;   // 0x007DBD50[slot*2], game pixels
};

struct AimConfig {
    int gameW = 640, gameH = 480;  // 0x007DE634 / 0x007DE638
    input::Aspect aspect = input::Aspect::Stretch;
    float displayW = 640, displayH = 480;  // size of the area the normalized position covers
};

// prevHeld: held bits from the previous poll (for `released`); updated in place.
GunSlotState ComputeGunSlot(const input::PlayerState& p, uint32_t& prevHeld, const AimConfig& cfg);

}  // namespace hook
