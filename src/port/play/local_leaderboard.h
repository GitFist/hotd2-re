#pragma once

#include <string>
#include <vector>
#include "game/world.h"

namespace play {
// Independent port sidecar: never changes the original's binary ranking/save block.
class LocalHordeLeaderboard {
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    // Call once per live game tick. true means a completed/abandoned run was added.
    // Do not call while restoring/replaying a network session. Debug runs are ignored.
    bool Observe(const game::HordeRunSnapshot& run);
    bool Finish();  // on normal application exit, saves a partial run's best progress
    const std::vector<game::HordeLeaderboardEntry>& Entries() const {return entries_;}
    const game::HordeLeaderboardEntry& LastCompleted() const {return lastCompleted_;}
    void DiscardPending() {active_=false;debug_=false;pending_={};}
private:
    std::vector<game::HordeLeaderboardEntry> entries_;
    game::HordeLeaderboardEntry pending_;
    game::HordeLeaderboardEntry lastCompleted_;
    bool active_=false,debug_=false;
};
} // namespace play
