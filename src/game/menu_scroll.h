#pragma once

namespace game {

// A list viewport in 640 x 480 menu coordinates, with content beyond each edge.
struct MenuScrollHint {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool above = false, below = false;
};

}  // namespace game
