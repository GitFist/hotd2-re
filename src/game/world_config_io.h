// game::WorldConfig to / from a small text file: world_test --save-config writes the configuration it built from a
// capture for a mid-game stage start (WorldCarry included); play.exe --config starts the World with it, so a later
// stage of a captured route can be rendered without running the earlier stages natively (visual checks).
#pragma once

#include <string>

#include "game/world.h"

namespace game {

bool SaveWorldConfig(const WorldConfig& c, const std::string& path);
bool LoadWorldConfig(const std::string& path, WorldConfig* c, std::string* err);

}  // namespace game
