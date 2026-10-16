#pragma once
#include "game/menu_text.h"

namespace game {
// Port-only stage totals. They live in the deterministic world, including during recovery replay.
struct StageResults {
    struct Player {
        bool participated = false;
        int score = 0, shots = 0, hits = 0, headshots = 0, rescues = 0, damage = 0, continues = 0;
    } player[2];
    bool active = false, shown = false, dismiss = false;
    int chapter = 0, frames = 0, civilians = 0, lifeBonus = 0;
    static constexpr int kMinimumFrames = 180;
};

// Uses the original options-screen tiles and glyphs; optional texture overrides apply normally.
void StageResultsSprites(const ExeImage& exe, const StageResults& results, int visibleFrames,
                         std::vector<SpriteDef>& out);
}  // namespace game
