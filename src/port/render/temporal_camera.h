#pragma once
namespace port {
// Exact render camera matrices, row vectors. Projection is reversed depth,
// without temporal jitter. Camera history is render-only and resets on cuts.
struct TemporalCameraInfo {
    float view[16] = {}, projection[16] = {};
    float previousView[16] = {}, previousProjection[16] = {};
    bool valid = false;
};
} // namespace port
