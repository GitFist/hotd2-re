#pragma once

#include "game/world.h"

namespace game {
// Native visual presentation of the authored English PC drill arenas.
// The practice controller remains responsible for prompts, scoring and timing.
bool TypingDrillSceneVisuals(World::Impl& world);
void RestoreTypingDrillCamera(World::Impl& world);
}
