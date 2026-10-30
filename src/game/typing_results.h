#pragma once
#include "game/world.h"

namespace game {
// Authored one-player English PC chapter result presentation (0x00492860).
// The actor owns timing and scoring; this function only submits its artwork.
void DrawTypingChapterResults(World::Impl& world, int phase, int remaining, float alpha);
}
