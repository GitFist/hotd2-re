#pragma once
#include "game/world.h"

namespace game {
// Reserved port input bits, inside the native twelve-bit input envelope.
constexpr uint32_t kPrototypeKey = 0x400, kPrototypeGrenade = 0x800;
constexpr int kPrototypeMaterialBase = 0x7000, kPrototypeMaterialCount = 49;
bool PrototypeActive(const World::Impl& w);
bool PrototypeTryStart(World::Impl& w);
void PrototypeFrame(World::Impl& w);
// Read-only diagnostic interface used by the replay tests.
struct PrototypeStatus {
    int encounter = -1, route = -1, phase = 0, age = 0, kills = 0, grenades = 0;
    bool complete = false;
    std::vector<std::array<float, 3>> targets; // screen x/y and remaining hp
};
PrototypeStatus PrototypeInspect(const World::Impl& w);
}
