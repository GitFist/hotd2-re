#pragma once

// Native port front end. All gameplay selections feed the existing mode handlers.
#include "game/world.h"
namespace game {
void NightShiftMenuFrame(World::Impl& w);
// The original Game Options handlers supply these presentation-only rows. Their
// IDs and order also define the modern pointer bands; no settings live here.
struct NightShiftOptionRow {
    int id = 0;
    std::string label, value, secondValue;
    bool adjustable = false, enabled = true, secondEnabled = true;
};
inline constexpr float kNightShiftOptionsTop = 102, kNightShiftOptionsPitch = 21, kNightShiftOptionsHeight = 20;
inline constexpr float NightShiftOptionsPitch(size_t rows) { return rows > 14 ? 20.f : kNightShiftOptionsPitch; }
inline constexpr float NightShiftOptionsHeight(size_t rows) { return NightShiftOptionsPitch(rows)-1; }
void NightShiftGameOptionsFrame(World::Impl& w, const std::vector<NightShiftOptionRow>& rows, int cursor,
                                const std::string& help);
void PortLeaderboardBegin(World::Impl& w);
void PortLeaderboardPlayerJoined(World::Impl& w,int player);
void PortLeaderboardComplete(World::Impl& w,int table,int score,int stageOrDifficulty);
}
