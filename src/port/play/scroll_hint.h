#pragma once

#include <algorithm>

#include "game/menu_scroll.h"
#include "render/model_renderer.h"
#include "render/viewport.h"

namespace play {

// Foreground edge fades: shared by native-font menus and the original sprite UI.
// The caller supplies actual overflow, so a short list never implies hidden rows.
inline void AppendMenuScrollHint(const game::MenuScrollHint& hint,
                                 std::vector<port::ModelRenderer::SpriteDraw>& out,
                                 float aspect = 4.f / 3.f) {
    if ((!hint.above && !hint.below) || hint.right <= hint.left || hint.bottom <= hint.top) return;
    const float height = std::min(18.f, (hint.bottom - hint.top) / 4.f);
    unsigned layer = 0;
    auto quad = [&](float left, float right, float y0, float y1, float y2, float y3,
                    uint32_t topColor, uint32_t bottomColor) {
        port::ModelRenderer::SpriteDraw sprite{};
        sprite.key = 0x8000000F;
        sprite.isp = 0xE4000000;
        sprite.tsp = 0x94918000;
        sprite.depth = .00005f - float(layer++) * .0000001f;
        left = port::CenteredViewportX(left, aspect);
        right = port::CenteredViewportX(right, aspect);
        sprite.v[0] = {left, y0, 0, 1, topColor, 0, 0, 0};
        sprite.v[1] = {left, y1, 0, 1, bottomColor, 0, 0, 1};
        sprite.v[2] = {right, y2, 0, 1, topColor, 0, 1, 0};
        sprite.v[3] = {right, y3, 0, 1, bottomColor, 0, 1, 1};
        out.push_back(sprite);
    };
    auto edge = [&](bool top) {
        const float outer = top ? hint.top : hint.bottom;
        const float inner = top ? outer + height : outer - height;
        const float y0 = std::min(outer, inner), y1 = std::max(outer, inner);
        // A dark veil keeps the cue visible over both scenery and opaque menus;
        // the lighter rim gives it the soft, hazy edge of a continuing list.
        quad(hint.left, hint.right, y0, y1, y0, y1,
             top ? 0xC80B1119 : 0x000B1119, top ? 0x000B1119 : 0xC80B1119);
        quad(hint.left, hint.right, y0, y1, y0, y1,
             top ? 0x487E98AC : 0x007E98AC, top ? 0x007E98AC : 0x487E98AC);
        const float center = (hint.left + hint.right) * .5f;
        const float tip = top ? outer + 3 : outer - 5;
        const float arm = top ? tip + 4 : tip - 4;
        quad(center - 6, center, arm, arm + 2, tip, tip + 2, 0xE8E5D9BB, 0xE8E5D9BB);
        quad(center, center + 6, tip, tip + 2, arm, arm + 2, 0xE8E5D9BB, 0xE8E5D9BB);
    };
    if (hint.above) edge(true);
    if (hint.below) edge(false);
}

}  // namespace play
