#pragma once

#include <algorithm>
#include <cmath>
#include "game/world.h"
#include "game/shooting.h"
#include "render/viewport.h"

namespace play {
// Gameplay only: menus retain native pointer space. Coordinates cover the entire
// wide image after device calibration. The existing input record carries the ray
// independently of the receiving peer's local display shape.
inline game::WorldInput AspectedWorldInput(game::WorldInput in,float nx,float ny,float aspect) {
    if(in.kind!=1||in.deviceMode!=6||in.offscreen||
       (in.x==0xFFFF&&in.y==0xFFFF)||port::ViewportXScale(aspect)==1.f)return in;
    if(!std::isfinite(nx)||!std::isfinite(ny)||nx<0||nx>1||ny<0||ny>1)return in;
    const float width=480.f*port::ViewportAspect(aspect);
    const int gx=int(std::floor((nx-.5f)*width));
    in.deviceMode=game::kPortWideGunMode;
    in.x=game::kPortWideGunBias+gx;
    in.y=std::clamp(int(std::floor(ny*480.f)),0,479);
    return in;
}
inline game::WorldInput AspectedWorldInput(game::WorldInput in,float aspect) {
    return AspectedWorldInput(in,float(in.x)/640.f,float(in.y)/480.f,aspect);
}
} // namespace play
