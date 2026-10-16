#pragma once

// Carried objects (task 0x00442740 / 0x005926A8[kind] functions): shared by world_st1b_held.cpp (behaviour 37: held
// between an enemy's hands) and world_st4_held.cpp (the stage-4 boss's objects, type 2: 0x00443200 / 0x00443B90 /
// 0x00444160 / 0x00444F70).

#include <set>

#include "game/world_internal.h"

namespace game {

// The carried object (task block 0x19C; +0x198 = the 0xE0 control block, fields below by its offsets as "c+")
struct HeldTask : ActorTask {
    float pos[3] = {};          // +0x40
    float vel[3] = {};          // +0x4C
    float acc[3] = {};          // +0x58 (+0x5C = 0xBCDF0123)
    int rot[3] = {};            // +0x64 / +0x68 / +0x6C
    float body[3] = {};         // +0x100 lock-on point (thrown objects)
    int16_t hp = 0;             // +0x11C
    int8_t slot121 = -1;        // +0x121 player whose 0x009A2BA0 flag the throw took (the target)
    uint32_t params = 0;        // +0x194 event-memory record of the holder's motion list
    EnemyTask* holder = nullptr;  // c+0x00
    uint32_t holderSerial = 0;
    EnemyTask* civ = nullptr;   // c+0x04 the holder's civilian (+0x1394)
    uint32_t civSerial = 0;
    int type = 0;               // c+0x08 definition 0x005644D8[type]
    int model = 0;              // c+0x0C
    int state = 10;             // c+0x10 (10 held; else the 0x005926A8 function index set at the holder's throw tick)
    float lastView[3] = {};     // c+0x14
    float pivot[3] = {};        // c+0x20 spin pivot of 0x004432D0 (set by the contact test 0x00443540, else 0)
    int spin[3] = {};           // c+0x2C / c+0x30 / c+0x34 (angle about the axis)
    float axis[3] = {};         // c+0x38
    PropAnim anim;              // c+0x44 debris {skeleton, motion, frame, prevFrame}
    int stuck = 0;              // c+0x98 frames left in front of the view (0x00444160)
    LockOnTarget* lock = nullptr;  // lock-on record of a thrown object (0x00408EC0)
    static std::set<const HeldTask*>& Live() {
        static std::set<const HeldTask*> s;
        return s;
    }
    HeldTask() { Live().insert(this); }
    ~HeldTask() override { Live().erase(this); }
};

// world_st1b_held.cpp
void HeldShotsExt(World::Impl& w, HeldTask& k, const Mat4& top);  // 0x004423F0 (top: the object's model-view)
bool HeldOnScreenExt(const World::Impl& w, const HeldTask& k);    // 0x004459C0
void HeldDestroyExt(World::Impl& w, HeldTask& k);                 // Actor_Destroy 0x00409CC0

// world_st4_held.cpp
HeldTask* SpawnCarriedType2(World::Impl& w, EnemyTask& holder, bool phase3);  // 0x00494F70 (the stage-4 boss)
bool HeldAlive(const HeldTask* k);
void HeldType2Break(World::Impl& w, HeldTask& k);  // 0x004423F0 type 2 at hp 0: deflected fly-off 0x00444F70
void HeldRegisterLock(World::Impl& w, HeldTask& k);  // 0x00408EC0 for a thrown object
void HeldSyncLock(HeldTask& k);
void TaskHeldStuck(Task& t);  // 0x00444160 (in front of the view after a hit)

}  // namespace game
