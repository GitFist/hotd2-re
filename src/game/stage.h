#pragma once

// Stage section tables (see notes/formats.md). From Stage_SelectTables 0x004014C0, Stage_LoadSection 0x00401510
// and Stage_UnloadSection 0x004015A0, driven by event opcodes 0x28 (preload) / 0x29 (switch section).
// Validated: every level-chunk draw of the original's stage-1 demo belongs to the section the script selected.

#include <cstdint>
#include <vector>

#include "exe_image.h"

namespace game {

struct StageSections {
    // sections[s] = object indexes (each section: up to 12 s16, -1 terminated, 0x18-byte stride)
    std::vector<std::vector<int>> sections;
    // objects[o] = {modelId, drawMode}. Stage_DrawSection 0x00401260 culls each object by its bounding sphere (0x00419200 ->
    // 0x00406310) and draws it by mode: 2 = pass-1 priority 7 (0x004A79F0(7), back to 8 after), 1 = alternate draw
    // 0x004185E0 (when 0x009A2BB4 != 0), else normal 0x00418560.
    std::vector<std::pair<int, int>> objects;

    // stage: 0-based stage index; alt: the table pair used when 0x009CA08C == 1 (0x00576A8C/0x00576ABC)
    bool Load(const ExeImage& exe, int stage, bool alt = false);
    std::vector<int> SectionModels(int section) const;
};

}  // namespace game
