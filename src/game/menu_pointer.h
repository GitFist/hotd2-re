#pragma once

namespace game {

// MenuText's capitals occupy texture rows 12..32, below transparent top padding.
// Adjacent text baselines are 24 pixels apart; use disjoint bands centred on the visible letters.
inline bool MenuTextRowHit(float x, float y, float cellY, float left = 16, float right = 636) {
    return x >= left && x < right && y >= cellY * 24 + 10 && y < cellY * 24 + 34;
}

// A resting pointer must not undo keyboard navigation. A fresh aim, movement or explicit device button restores it.
struct MenuPointerFocus {
    bool seen = false;
    float x = 0, y = 0;
    bool Update(bool present, float px, float py, bool button) {
        const bool select = present && (!seen || px != x || py != y || button);
        seen = present;
        x = px; y = py;
        return select;
    }
};

inline int MenuPointerStep(float x, float previousX) {
    return x >= previousX - 16 && x < previousX + 16 ? -1 : 1;
}

}  // namespace game
