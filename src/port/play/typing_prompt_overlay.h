#pragma once

#include "game/world.h"

#include <algorithm>
#include <array>
#include <map>
#include <utility>
#include <vector>

namespace play {

// Fallback for installations without the original Typing poster models.
inline void AppendTypingPromptOverlay(game::WorldHud& hud,
                                      const std::array<std::vector<game::PortTypingPrompt>, 2>& players,
                                      bool largeWords, bool hasQuestion) {
    using Rows = std::array<const game::PortTypingPrompt*, 2>;
    std::map<std::pair<uint32_t, uint32_t>, Rows> targets;
    for (int p = 0; p < 2; ++p)
        for (const auto& prompt : players[p])
            targets[{prompt.serial, prompt.token}][p] = &prompt;

    struct Box { float x, y, w, h; };
    std::vector<Box> occupied;
    const float minimumY = hasQuestion ? 94.f : 48.f;
    for (const auto& [identity, rows] : targets) {
        const auto& prompt = *(rows[0] ? rows[0] : rows[1]);
        const bool shared = rows[0] && rows[1];
        const bool labelled = rows[1] != nullptr;
        const float inset = labelled ? 32.f : 8.f;
        const float scale = prompt.projectile ? .6f : largeWords ? .65f : .5f;
        const float wordWidth = prompt.projectile ? 42.f : std::max(72.f, 18.f + float(prompt.text.size()) * scale * 16.f);
        const float width = std::min(614.f, wordWidth + inset - 8.f);
        const float height = shared ? 49.f : 26.f;
        const float maximumY = 422.f - height;
        const float x = std::clamp(prompt.x - width * .5f, 6.f, 634.f - width);
        float y = std::clamp(prompt.y + 18.f, minimumY, maximumY);
        for (int attempt = 0; attempt < 16; ++attempt) {
            const bool overlaps = std::any_of(occupied.begin(), occupied.end(), [&](const Box& b) {
                return x < b.x + b.w + 4 && x + width + 4 > b.x && y < b.y + b.h + 4 && y + height + 4 > b.y;
            });
            if (!overlaps) break;
            y += height + 6;
            if (y > maximumY) y = minimumY + float(attempt % 3) * 7;
        }
        occupied.push_back({x, y, width, height});
        const bool selected = prompt.selected || (rows[1] && rows[1]->selected);
        const uint32_t edge = prompt.projectile ? 0xFFFFAD62 : prompt.waiting || selected ? 0xFFF0BF48 :
                              prompt.boss ? 0xFFCC6269 : 0xFF66B8CA;
        const float anchorX = std::clamp(prompt.x, 4.f, 636.f);
        const float anchorY = std::clamp(prompt.y, 38.f, 427.f);
        const float joinX = std::clamp(anchorX, x + 3, x + width - 3);
        hud.menuPanels.push_back({joinX-.5f, std::min(anchorY,y), joinX+.5f, std::max(anchorY,y), edge});
        hud.menuPanels.push_back({std::min(anchorX,joinX), anchorY-.5f, std::max(anchorX,joinX), anchorY+.5f, edge});
        hud.menuPanels.push_back({anchorX-2, anchorY-2, anchorX+2, anchorY+2, edge});
        hud.menuPanels.push_back({x-1, y-1, x+width+1, y+height+1, edge});
        hud.menuPanels.push_back({x, y, x+width, y+height, 0xF509121D});
        float rowY = y + 3;
        for (int p = 0; p < 2; ++p) {
            if (!rows[p]) continue;
            const auto& row = *rows[p];
            const uint32_t color = labelled && p == 0 ? 0xFFFF7366 : 0xFF69E4A5;
            if (labelled) hud.menuText.push_back({x+5, rowY, p == 0 ? "P1" : "P2", .42f, color});
            hud.menuText.push_back({x+inset, rowY, row.text, scale, 0xFFF4EFE2});
            if (row.progress && !row.text.empty()) {
                const auto count = std::min(row.progress, row.text.size());
                hud.menuText.push_back({x+inset, rowY, row.text.substr(0,count), scale, color});
                if (labelled) {
                    // Also visible when the fallback font can only draw monochrome glyphs.
                    const float run = (width - inset - 4) * float(count) / float(row.text.size());
                    hud.menuPanels.push_back({x+inset, rowY+19, x+inset+run, rowY+21, color});
                }
            }
            rowY += 23.f;
        }
    }
}

} // namespace play
