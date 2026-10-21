#pragma once

#include "game/world.h"

namespace play {

struct CursorContext {
    bool active = true;
    bool menuOpen = false;
    bool lobbyOpen = false;
    bool hordeActive = false;
};

// The system pointer is separate from the rendered gameplay sights. Keep it out
// of the scene for every input device, while retaining it for pointing at UI.
inline bool SystemCursorVisible(const game::WorldHud& hud, const CursorContext& context) {
    if (!context.active || context.menuOpen || context.lobbyOpen || hud.nightShiftPage >= 0 || hud.gameMode != 6 ||
        hud.subState != 2 || hud.paused || hud.stageResults.active || (hud.flags5900 & 0x18) != 0)
        return true;
    // 0x20 also marks noninteractive chapter/boss cards. Only Horde's active
    // run uses it for a pointer-driven store, so those cutscenes stay hidden.
    return context.hordeActive && (hud.flags5900 & 0x20) != 0;
}

}  // namespace play
