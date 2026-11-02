#pragma once
#include "input/player_state.h"
#include "render/crt_geometry.h"

namespace play {
// Presentation output -> source picture, before menu centering or wide aiming.
// Gamepad sights, replay and peer inputs are already in source-picture space.
inline input::PlayerState DisplayPointer(input::PlayerState pointer, int curvature) {
    if (pointer.gamepad) return pointer;
    port::CrtWarpUV(&pointer.x, &pointer.y, curvature);
    pointer.onScreen = pointer.onScreen && pointer.x >= 0 && pointer.x <= 1 && pointer.y >= 0 && pointer.y <= 1;
    return pointer;
}
}  // namespace play
