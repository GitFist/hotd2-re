#pragma once

// game::World - stage 5 (st5evtbl.bin): helpers shared by the world_st5*.cpp files. notes/agents/stage5.md describes
// the capture (build\scene_capture_auto_st5.txt), the stage start (world_test --stage-start 56007) and what is verified.

#include <cstdint>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

// Per-world stage-5 state (World::Impl::st5)
struct St5State {
    // light slots 0x009A1A20 + i * 0x74 (slots 3..15 handed out by 0x00480A60; +0x6C in use)
    bool lightUsed[16] = {};
};

// Objects with skeleton 0x4C (actor 0x2D: 0x00426A70; variant 0 = world_st5_actors.cpp, variant 1 = the final boss):
// +0x1370 is the alpha of their node / attachment models (0x00429040, 0x00419B40 case 0x4C)
struct Kind4CTask : EnemyTask {
    float alpha1370 = 1.0f;  // +0x1370
};

// Actor type 0x2D object (variant 0 here, variant 1 = the final boss: world_st6_boss.cpp derives from it)
struct Actor2DTask : Kind4CTask {
    float light[3] = { 1.0f, 1.0f, 1.0f };  // +0x1344 / +0x1348 / +0x134C (renderer: 0x0041DC10 colour)
    int c1338 = 0;                          // +0x1338
    int mode1360 = 0;                       // +0x1360
    int c1364 = 0;                          // +0x1364
    int c136C = 0;                          // +0x136C
    float f1374 = 2.0f;                     // +0x1374
};

namespace st5 {

inline float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
inline float F32(const ExeImage& exe, uint32_t va) { return Bits(exe.U32(va)); }
inline double F64(const ExeImage& exe, uint32_t va) {
    const uint64_t lo = exe.U32(va), hi = exe.U32(va + 4);
    const uint64_t u = lo | (hi << 32);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

St5State& State(World::Impl& w);
// 0x00429040 (node callback of actor 0x2D) and 0x00428F70 (light ramp + skeleton), shared with the final boss
void Node2D(World::Impl& w, EnemyTask& e, int idx, const Mat4& m);
void Draw2D(World::Impl& w, Actor2DTask& a);
// Cam path position / angles at time t (0x004042D0)
bool EvalPath(World::Impl& w, int path, float t, float pos[3], int ang[3]);
// world_st5_car.cpp: actor 0x26 state 4, komono 51
bool SpawnCar(World::Impl& w, Task& t, SpawnTask& s);

// Actor_RegisterSlot 0x00409270
void RegisterSlot(World::Impl& w, EnemyTask& e);
// 0x004092D0
void ReleaseSlot(World::Impl& w, EnemyTask& e);
// Actor_Destroy 0x00409CC0 for a non-enemy actor object (target copies cleared), then Task_Kill
void DestroyActor(World::Impl& w, ActorTask& a);
// Spawned actor object from its record (0x00408A20 / 0x004088A0)
template <class T> std::unique_ptr<T> FromRecord(const SpawnRecord& r, uint32_t fn) {
    auto d = std::make_unique<T>();
    d->fn = fn;
    if (auto* world = CurrentWorld()) world->SetTypingSpawnOrigin(*d, r);
    d->record = r;
    d->flags34 = r.flags | 1;
    memcpy(d->pos, r.pos, 12);
    memcpy(d->rot, r.rot, 12);
    d->hp = d->hpMax = static_cast<int16_t>(r.state);
    d->lo = static_cast<int16_t>(r.lo);
    d->anim.kind = r.ParamS8(0);
    d->variant = r.ParamS8(1);
    return d;
}

}  // namespace st5
}  // namespace game
