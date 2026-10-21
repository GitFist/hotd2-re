#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace port {
// Windows scRGB defines 1.0 as 80 cd/m2. Native art is SDR; output remapping
// changes the presentation encoding/reference white, not its authored gamut.
inline float SrgbToLinear(float value) {
    value = value < 0 ? 0 : value > 1 ? 1 : value;
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}
inline float HalfToFloat(uint16_t bits) {
    const bool negative = (bits & 0x8000) != 0;
    const int exponent = (bits >> 10) & 31;
    const int fraction = bits & 1023;
    float value;
    if (!exponent) value = std::ldexp(static_cast<float>(fraction), -24);
    else if (exponent == 31) value = fraction ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else value = std::ldexp(1.0f + fraction / 1024.0f, exponent - 15);
    return negative ? -value : value;
}
} // namespace port
