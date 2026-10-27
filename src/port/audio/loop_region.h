#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace port {

// PCM frame bounds, with an exclusive end. The prefix before begin plays once.
struct PcmLoopRegion {
    uint32_t begin = 0;
    uint32_t end = 0;
    bool Active() const { return begin < end; }
};

// Loop points are authored at sourceRate. Preserve their timing in resampled asset packs,
// and reject clips that cannot contain the whole loop rather than looping the full file.
inline bool ResolveLoopRegion(uint32_t startFrame, uint32_t endFrame, uint32_t sourceRate,
                              uint32_t pcmRate, size_t pcmFrames, PcmLoopRegion& out) {
    if (!sourceRate || !pcmRate || startFrame >= endFrame) return false;
    const uint64_t begin = (uint64_t(startFrame) * pcmRate + sourceRate / 2) / sourceRate;
    const uint64_t end = (uint64_t(endFrame) * pcmRate + sourceRate / 2) / sourceRate;
    if (begin >= end || end > pcmFrames || end > std::numeric_limits<uint32_t>::max()) return false;
    out = {static_cast<uint32_t>(begin), static_cast<uint32_t>(end)};
    return true;
}

}  // namespace port
