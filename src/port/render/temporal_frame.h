#pragma once

// Render-only temporal data. None of these values are simulation state or enter
// replay/netplay hashes. Motion is current pixel -> previous pixel, in render
// pixels, with projection jitter removed (positive Y points down).
#include <array>
#include <cstdint>
#include <tuple>

namespace port {
struct MotionIdentity {
    uint32_t task = 0, model = 0, node = 0, role = 0, occurrence = 0;
    bool operator<(const MotionIdentity& b) const {
        return std::tie(task,model,node,role,occurrence) < std::tie(b.task,b.model,b.node,b.role,b.occurrence);
    }
};
struct TemporalSceneStamp {
    uintptr_t world = 0;
    int64_t simulationFrame = 0;
    std::array<int32_t,8> camera = {};
};
struct TemporalFrameInfo {
    bool enabled = false;
    bool reset = false;
    uint64_t frameId = 0; // monotonically increasing successfully rendered frames
    float jitterPixels[2] = {}; // positive X right, positive Y down
    float frameTimeMs = 1000.f / 60.f; // actual rendered interval; simulation stays at 60Hz
};
struct MotionVertex {
    float position[3];
    float previous[3]; // previous fully deformed model position (CPU skin/wave/edit)
    float uv[2];
};
struct MotionConstants {
    float modelView[16], projection[16];
    float previousModelView[16], previousProjection[16];
    float target[4]; // width,height,CPU correspondence valid,backend previous-depth ready
    float jitter[4]; // current xy, previous xy, render pixels
    float alpha[4];  // textured,threshold (<0 off),material alpha,alpha source (2=texture only)
};
static_assert(sizeof(MotionVertex)==32,"motion vertex ABI");
static_assert(sizeof(MotionConstants)==304,"motion uniform ABI");
} // namespace port
