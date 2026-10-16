#pragma once

// game::World - stage 6 (st6evtbl.bin), the final boss and the ending: helpers shared by the world_st6*.cpp /
// world_ending*.cpp files. notes/agents/stage6.md describes the captures (build\scene_capture_auto_st6.txt,
// build\scene_capture_auto_st6_end.txt), the stage start (world_test --stage-start 70806) and what is verified.

#include <cstdint>
#include <cstring>

#include "game/world_st5.h"

namespace game {

// Per-world stage-6 state (World::Impl::st6)
struct St6State {
    // final boss orb slots 0x009A5F40 + i * 0x14 {idx, active, order, -, pos (+4), view z (+0x10)} and the sorted copy
    // 0x009C8D60 (0x00429680)
    struct OrbSlot {
        int8_t idx = 0, active = 0, order = 0;
        float pos[3] = {};
        float z = 0;
    };
    OrbSlot orbs[8], sorted[8];
    int8_t f2448 = 0;  // 0x009A2448 (1: the creature of the second phase is out)
    // the final boss's block as it was removed (the orbs keep reading it in that frame): copy + the original pointer
    std::shared_ptr<TaskData> bossGhost;
    const void* bossGhostOf = nullptr;
};

namespace st6 {

using st5::Bits;
using st5::F32;
using st5::F64;

St6State& State(World::Impl& w);

}  // namespace st6
}  // namespace game
