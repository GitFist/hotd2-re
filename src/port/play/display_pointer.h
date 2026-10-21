#pragma once
#include "input/player_state.h"
#include "render/crt_geometry.h"

namespace play {
// Presentation output -> source picture, before menu centering or wide aiming.
// Only local physical pointers enter here; replay and peer inputs are already mapped.
inline input::PlayerState DisplayPointer(input::PlayerState pointer, int curvature) {
    port::CrtWarpUV(&pointer.x, &pointer.y, curvature);
    pointer.onScreen = pointer.onScreen && pointer.x >= 0 && pointer.x <= 1 && pointer.y >= 0 && pointer.y <= 1;
    return pointer;
}
}  // namespace play
