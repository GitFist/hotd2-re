#include "game/world_internal.h"
#include <algorithm>

namespace game {
void World::Impl::BeginStageResults(int bonus) {
    if (!cfg.portStageResults || gameMode != 6 || cfg.eventMode < 0 || cfg.eventMode >= 6 ||
        cfg.gameType == 2 || stageResults.shown) return;
    auto& r = stageResults;
    r.active = r.shown = true;
    r.chapter = cfg.eventMode + 1;
    r.frames = 0;
    r.civilians = rescueCount[cfg.eventMode];
    r.lifeBonus = bonus;
    for (int p = 0; p < 2; ++p) {
        r.player[p].shots = static_cast<uint16_t>(player[p].shots);
        r.player[p].hits = static_cast<uint16_t>(player[p].hits);
    }
    Note("stage results: chapter " + std::to_string(r.chapter));
}

bool World::Impl::StageResultsFrame() {
    if (!stageResults.active) return false;
    for (auto& p : player) { p.pending = 0; p.firedThisFrame = false; }
    if (stageResults.dismiss) {
        stageResults.active = false;
        pressed = 0;  // the confirming input belongs to the results page only
        for (auto& p : player) p.pending = 0;
        Note("stage results: continue");
        return false;
    }
    ++stageResults.frames;
    // Edges only: an early press or a held trigger cannot dismiss the page later.
    if (stageResults.frames >= StageResults::kMinimumFrames && (pressed & 0xC000C)) {
        stageResults.dismiss = true;
        Sound(0x121A9);
    }
    return true;
}

void World::Impl::ResultRescue(int p) {
    if (!cfg.portStageResults || gameMode != 6 || stageResults.shown) return;
    for (int i = 0; i < 2; ++i)
        if (p == -1 || p == i) ++stageResults.player[i].rescues;
}
}  // namespace game
