#include "rng.h"

namespace game {

uint32_t CrtRand::Advance(uint32_t state, uint64_t n) {
    // Compose the affine map x -> a*x + c with itself (square-and-multiply), all mod 2^32.
    uint32_t a = 1, c = 0;           // accumulated map
    uint32_t ma = kMul, mc = kAdd;   // current power of the step map
    while (n) {
        if (n & 1) {
            a = a * ma;
            c = c * ma + mc;
        }
        mc = mc * ma + mc;
        ma = ma * ma;
        n >>= 1;
    }
    return a * state + c;
}

// 0x00413280
bool GameIsDemoMode(int mode, int subMode) {
    switch (mode) {
    case 5:
    case 9:
    case 10:
    case 11:
        return true;
    case 6:
        return subMode == 8;
    default:
        return false;
    }
}

// 0x00415710
DemoInput PlayerDemoInput(CrtRand& rng, bool busy) {
    DemoInput in;
    const int r = rng.Next() % 60;  // 0x0041572E (busy == 0) / 0x00415740
    in.fire = r < (busy ? 6 : 1);
    if (in.fire) {
        in.x = static_cast<int16_t>((rng.Next() % 150) * 2 - 150);  // 0x00415757
        in.y = static_cast<int16_t>((rng.Next() % 100) * 2 - 100);  // 0x0041576B
    }
    return in;
}

}  // namespace game
