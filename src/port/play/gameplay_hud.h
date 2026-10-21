#pragma once

#include "game/hud.h"

namespace play {

// Port-only presentation choice. HudPlayerTask submits its crosshair first;
// always run the full task so ammo/reload animation advances identically. Menu,
// store, name-entry and calibration sights use separate paths and remain visible.
inline std::vector<game::SpriteDef> PlayerHudSprites(game::HudState& state, int player, bool showCrosshairs) {
    const auto crosshairCount = showCrosshairs ? 0 : game::HudCrosshair(state, player).size();
    auto sprites = game::HudPlayerTask(state, player);
    sprites.erase(sprites.begin(), sprites.begin() + crosshairCount);
    return sprites;
}

}  // namespace play
