#pragma once
#include "game/world.h"

namespace game {
void TypingOriginalHud(World::Impl& world);
uint32_t TypingOriginalSharedFlags(const World::Impl& world);
// Returns true only when the typing-owned item path consumed the source id.
bool TypingOriginalPickup(World::Impl& world,int player,int item);
}
