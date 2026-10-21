#pragma once

#include <string>

#include "game/world.h"

namespace play::device_ui {

constexpr uint32_t kText = 0xFFECE7DC;
constexpr uint32_t kMuted = 0xFFC1BAAC;

inline void Text(game::WorldHud& hud, float x, float y, const std::string& text, float scale = .5f, uint32_t color = kText) {
    if (!text.empty()) hud.menuText.push_back({x, y, text, scale, color});
}

// Device setup shares Night Shift's typography, panels and row geometry. A
// calibration canvas has no chrome so all four target corners stay clear.
inline void Begin(game::WorldHud& hud, int page, const std::string& title, const std::string& subtitle = "", bool background = true) {
    hud.nightShiftPage = page;
    hud.nightShiftRow = 0;
    hud.menuPanels.clear();
    hud.menuText.clear();
    if (!background) return;
    hud.menuPanels = {{0, 0, 640, 480, 0xEC090D10}, {26, 65, 614, 67, 0xFF92272D}};
    Text(hud, 26, 20, title, 1.05f);
    Text(hud, 34, 73, subtitle, .42f, kMuted);
}

inline void Row(game::WorldHud& hud, int row, int cursor, const std::string& label, const std::string& value = "") {
    const float top = 88.f + 27.f * row;
    if (row == cursor)
        hud.menuPanels.push_back({24, top + 8, 616, top + 35, 0xCC8F2026});
    else
        hud.menuPanels.push_back({24, top + 34, 616, top + 35, 0x354E514E});
    Text(hud, 34, top + 10, label, .57f);
    Text(hud, 440, top + 10, value, .52f);
}

inline void Footer(game::WorldHud& hud, const std::string& text) {
    Text(hud, 28, 444, text, .4f, kMuted);
}

inline void Target(game::WorldHud& hud, float x, float y, int player = 0) {
    // Opaque black outline and white arms remain legible over bright/dark
    // scenery. The small center identifies P1/P2 without obscuring the aim.
    hud.menuPanels.insert(hud.menuPanels.end(), {{x - 14, y - 4, x + 14, y + 4, 0xFF000000},
                                               {x - 4, y - 14, x + 4, y + 14, 0xFF000000},
                                               {x - 11, y - 1, x + 11, y + 1, 0xFFFFFFFF},
                                               {x - 1, y - 11, x + 1, y + 11, 0xFFFFFFFF},
                                               {x - 2, y - 2, x + 2, y + 2, player == 1 ? 0xFF54BCCFu : 0xFFCF454Bu}});
}

}  // namespace play::device_ui
