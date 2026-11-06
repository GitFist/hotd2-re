#pragma once
#include <cmath>
#include <limits>

namespace port {
struct FsrDepthRange {
    float nearPlane = 0, farPlane = 0;
    bool infinite = false;
};

// Reversed depth endpoints expressed as homogeneous view-space Z/W. The SR
// adapter obtains these directly from projection; FG uses inverse projection.
// Only an exactly zero far W is infinite: a distant finite plane must retain
// the finite-depth SDK permutation. FidelityFX accepts FLT_MAX for infinite far.
inline bool DecodeFsrDepthRange(double nearZ, double nearW, double farZ, double farW, FsrDepthRange* out) {
    if (!out || !std::isfinite(nearZ) || !std::isfinite(nearW) || !std::isfinite(farZ) || !std::isfinite(farW) ||
        nearW == 0 || (farW == 0 && farZ == 0))
        return false;
    FsrDepthRange range;
    range.nearPlane = static_cast<float>(std::abs(nearZ / nearW));
    range.infinite = farW == 0;
    range.farPlane = range.infinite ? (std::numeric_limits<float>::max)() : static_cast<float>(std::abs(farZ / farW));
    if (!std::isfinite(range.nearPlane) || !std::isfinite(range.farPlane) || range.nearPlane <= 0 ||
        range.farPlane <= range.nearPlane)
        return false;
    *out = range;
    return true;
}
} // namespace port
