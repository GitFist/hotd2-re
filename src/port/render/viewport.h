#pragma once

#include <algorithm>
#include <cmath>
#include "game/camera.h"

namespace port {
constexpr float kNativeAspect = 4.f/3.f;
inline float ViewportAspect(float aspect) {
    return std::isfinite(aspect)?std::clamp(aspect,kNativeAspect,32.f/9.f):kNativeAspect;
}
inline float ViewportXScale(float aspect) {return kNativeAspect/ViewportAspect(aspect);}
inline float CenteredViewportX(float nativeX,float aspect) {
    return 320.f+(nativeX-320.f)*ViewportXScale(aspect);
}
// Hor+: preserve vertical FOV and reveal geometry horizontally. Scale the whole
// clip-X column, including any authored projection-center translation.
inline game::Mat4 HorPlusProjection(game::Mat4 projection,float aspect) {
    const float scale=ViewportXScale(aspect);
    if(scale!=1.f)for(int row=0;row<4;++row)projection.m[row*4]*=scale;
    return projection;
}
} // namespace port
