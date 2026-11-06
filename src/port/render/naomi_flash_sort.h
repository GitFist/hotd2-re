#pragma once

#include "game/model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace port {

// One already transformed camera-space triangle. The eye is (0,0,0).
// Splitting retains the source draw and its original coplanar submission order.
struct NaomiFlashVertex : game::ModelVertex {
    float color[3]{};
    float specular[3]{};
    NaomiFlashVertex& operator=(const game::ModelVertex& v) {
        static_cast<game::ModelVertex&>(*this)=v;
        return *this;
    }
};
struct NaomiFlashTriangle {
    NaomiFlashVertex vertices[3]{};
    uint32_t sourceDraw = 0;
    uint64_t sequence = 0;
    bool additive = false; // ONE+ONE or SRC_ALPHA+ONE: source-only additions commute
};

struct NaomiFlashSortLimits {
    size_t maxInputTriangles = 8192;
    size_t maxFragments = 32768;
    size_t maxOutputTriangles = 65536;
    unsigned maxDepth = 128;
};

// Conservative homogeneous clip rejection using this draw's actual GPU
// projection. Guard bands are in NDC units (e.g. one pixel on each side).
// Borderline/roundoff cases are retained; no occlusion or facing test is used.
bool NaomiFlashOutsideProjection(const NaomiFlashTriangle& triangle,
                                 const float projection[16],
                                 float guardX=0,float guardY=0);

// Authored D3DCULL 1=none,2=screen-CW,3=screen-CCW. Only reject a definite
// culled winding with every clipW safely positive; uncertain/thin/eye-crossing
// triangles are retained for the GPU. Pixel spans are NDC units per pixel.
bool NaomiFlashCulledByWinding(const NaomiFlashTriangle& triangle,
                               const float projection[16],uint8_t cull,
                               float pixelX=0,float pixelY=0);

// BSP painter ordering, far to near, including intersections between different
// flash planes. Attributes are linearly interpolated at new camera-space points.
// Invalid input or a budget failure returns false and leaves output unchanged.
// Finite zero-area triangles are discarded. No GPU/backend state is changed.
bool SortNaomiFlashTriangles(const std::vector<NaomiFlashTriangle>& input,
                            std::vector<NaomiFlashTriangle>* output,
                            std::string* error = nullptr,
                            const NaomiFlashSortLimits& limits = {});

} // namespace port
