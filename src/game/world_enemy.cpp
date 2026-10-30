// game::World - enemy actors (actor type 0x30): Enemy_Create 0x00452DA0, Enemy_Update 0x004533F0 and the enemy
// skeleton animation (0x004110D0 family). See notes/agents/enemy.md for the object layout.
// Ported behaviours: 23 (scripted lunge 0x00457B50), 9 (death knock-back fall 0x004550E0), 7 (corpse 0x00454F20);
// 1..6 and 15 (walking enemies) in world_enemy_ai.cpp.
// Others are reported through World::Notes() and do nothing.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"
#include "game/world_typing_original.h"

namespace game {

static int FindEntry(const Skeleton& s, int entry, int node);
static void SubtreeNodes(const Skeleton& s, int entry, std::vector<int>& out);

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

constexpr float kOneF = 1.0f;

// 0x00409E00: move angle a towards b by at most `step` (16-bit wrap).
uint32_t ApproachAngle(uint32_t a, uint32_t b, int step) {
    a &= 0xFFFF;
    b &= 0xFFFF;
    int d = static_cast<int>(a) - static_cast<int>(b);
    int ad = d < 0 ? static_cast<int>(b) - static_cast<int>(a) : d;
    int w1 = (static_cast<int>(b) - static_cast<int>(a)) + 0x10000;
    if (w1 < 0) w1 = d - 0x10000;
    int w2 = d + 0x10000;
    if (w2 < 0) w2 = (static_cast<int>(b) - static_cast<int>(a)) - 0x10000;
    if (step < ad && step < w1 && step < w2) {
        if (a < b) {
            if (w2 < ad) step = -step;
        } else if (ad <= w1) {
            return a - step;
        }
        return a + step;
    }
    return b;
}

// 0x0040A040: angle v within +-range of centre
bool AngleInRange(uint32_t v, uint32_t centre, int range) {
    uint32_t hi = (centre & 0xFFFF) + 0x10000 + range;
    uint32_t lo = ((centre & 0xFFFF) - range) + 0x10000;
    uint32_t mn = lo;
    if (hi < lo) {
        mn = hi;
        hi = lo;
    }
    v &= 0xFFFF;
    if (v < mn) v = v + (((mn - v) + 0xFFFF) & 0xFFFF0000u);
    return v <= hi;
}

// The odd-tick interpolation of 0x00411700 / 0x00410920: s1 + wrapped(s2 - s1) * f, truncated.
int InterpAngle(int s1, int s2, float f) {
    double d = static_cast<double>(static_cast<uint16_t>(s2 - s1));
    if (!(d <= 32768.0)) d = d - 65536.0;  // 0x004E1FE0 / 0x004E1FDC
    // fmul dword f; fiadd s1 (24-bit precision control: both results rounded to float); __ftol
    return static_cast<int>(static_cast<float>(static_cast<double>(static_cast<float>(d * f)) + s1));
}

Mat4 RotZYX(const int a[3]) {  // push; identity; RotZ(z); RotY(y); RotX(x)
    Mat4 m = Mat4::Identity();
    mx::RotZ(m, a[2]);
    mx::RotY(m, a[1]);
    mx::RotX(m, a[0]);
    return m;
}

// 0x00453B50 (game type 1 with item 0x15, 0x009C88A8 == 1): node 2 drawn scaled (kind 0xE: (1.8, 1.8, 1), else (2, 2, 2))
// through the enemy node draw 0x004534A0; the other nodes as usual
void BigHeadNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    if (idx != 2 || (!ItemFlag(w,0) && !(TypingOriginalSharedFlags(w)&2))) {
        w.EnemyDrawNode(e, idx, m);
        return;
    }
    Mat4 s = m;
    if (e.anim.kind == 0xE) {
        uint32_t u = 0x3FE66666;
        float f;
        memcpy(&f, &u, 4);
        mx::Scale(s, f, f, 1.0f);
    } else {
        mx::Scale(s, 2.0f, 2.0f, 2.0f);
    }
    w.EnemyDrawNode(e, idx, s);
}

}  // namespace

// =====================================================================================================================
// Task entry
// =====================================================================================================================

void World::Impl::TaskEnemy(Task& t) {
    EnemyTask& e = t.As<EnemyTask>();
    actor8E4C = &e;  // 0x009C8E4C (0x00452DA0 / 0x004533F0 / 0x00453290)
    if (e.fn == fn::kEnemyCreate) {
        EnemyCreate(e);
        return;
    }
    if (e.fn == 0x00453290) {  // kind 9 (world_st6_enemy.cpp)
        EnemyKind9Update(e);
        return;
    }
    if (e.fn != fn::kEnemy) return;  // 0x00453290 (kind 9) not ported
    EnemyUpdate(e);
}

// =====================================================================================================================
// Enemy_Create 0x00452DA0
// =====================================================================================================================

void World::Impl::EnemyCreate(EnemyTask& e) {
    e.anim.motion[0] = e.variant == 4 ? 0x41E : 0x3BC;
    if (e.anim.kind != 9) e.anim.kind = static_cast<int16_t>(e.record.ParamS8(0));
    e.hpMax = static_cast<int16_t>(EnemyInitialHp(enemyTables, e.hpMax, cfg.difficulty, cfg.gameMode));  // 0x0040A8B0
    e.hp = e.hpMax;
    e.variant = e.record.ParamS8(1);
    EnemyInitPose(e);  // Anim_InitEnemy 0x00410440 (+ actor slot)
    e.anim.drawFlags |= 4;
    e.anim.rotOrder = 1;
    e.enemyPost = true;  // +0x12F0 = 0x00454900
    e.anim.counter[0] = e.actorSlot % 10;
    e.radius = e.anim.kind >= 0 && e.anim.kind < 0x48 ? enemyTables.kindRadius[e.anim.kind] : 0.0f;
    e.radius128 = 3.5f;
    e.flags136C = 0x60000000u;
    e.c131F = e.anim.kind == 0 ? 2 : 0;
    e.flags136C = static_cast<uint32_t>(static_cast<int>(e.lo)) | 0x60000000u;
    if ((e.flags34 & 0x40000) == 0) {
        int pitch, yaw;
        DirToAngles(playerPos[0] - e.pos[0], static_cast<float>((playerPos[1] + 15.0) - e.pos[1]),
                        playerPos[2] - e.pos[2], &pitch, &yaw);  // + double 0x00565DD8
        e.headPitch = pitch & 0xFFFF;
        e.headYaw = yaw & 0xFFFF;
    }
    // Enemy_SetupKind 0x00452FD0
    if (e.flags136C & 0x20) e.flags38 |= 8;
    if (e.flags34 & 2) {
        e.flags34 &= ~2u;
        e.flags38 |= 0x10;
    }
    if (e.flags34 & 4) {
        e.flags34 &= ~4u;
        e.flags38 |= 0x20;
    }
    if (e.flags34 & 8) EnemySetupRide(e);  // 0x0045E770 (world_st5_enemy.cpp)
    switch (e.anim.kind) {
    case 0xC:
        EnemySetupKind12(e);  // world_st2_enemy.cpp
        break;
    case 2: case 3:
        EnemySetupKind2(e);  // world_st2c_enemy.cpp
        break;
    case 0xE:
        EnemySetupKind14(e);  // world_st4_enemy.cpp
        break;
    case 9: case 0x12:
        EnemySetupKind18(e);  // world_st6_enemy.cpp
        break;
    default:
        break;
    }
    e.target = -1;
    e.c131D = -1;
    e.variant = e.record.ParamS8(1);
    e.behaviour = static_cast<int16_t>(e.record.ParamS8(2));
    e.sub = 0;
    if (static const bool log = getenv("HOTD2_ENEMYLOG") != nullptr; log)  // debug: every enemy's kind / variant / behaviour
        fprintf(stderr, "enemy %d kind %X variant %d behaviour %d hp %d stage %d\n", frame, e.anim.kind, e.variant, e.behaviour, e.hpMax, cfg.eventMode);
    if (runner->cfg.gameType == 1 || (TypingOriginalSharedFlags(*this)&2)) {
        // item 0x15 (0x009C88A8 == 1): the head node's hit radius (node 2 +0x78 = +0x3A4) doubled (kind 0xE: x float
        // 0x0055DD48), node draws through 0x00453B50 (the head scaled)
        if (ItemFlag(*this, 0) || (TypingOriginalSharedFlags(*this)&2)) {
            float r = e.anim.nodes[2].radius;
            r = e.anim.kind == 0xE ? r * F32(exe, 0x0055DD48) : r + r;
            e.anim.nodes[2].radius = r;
            e.nodeFn = BigHeadNode;
        }
    } else if (runner->cfg.gameType == 2) {
        e.nodeFn = TrainPlainNode;  // +0x12EC = 0x00453B30 (world_modes_train.cpp)
    }
    if (e.anim.kind != 9) {
        if (e.behaviour != 0x1F) {
            ++attackers;
            ++liveEnemies;
        }
        e.fn = fn::kEnemy;
    } else {
        e.fn = 0x00453290;
    }
}

// Anim_InitEnemy 0x00410440 + Enemy_InitPose 0x00410590 / 0x004107E0, Actor_RegisterSlot 0x00409270
void World::Impl::EnemyInitPose(EnemyTask& e) {
    EnemyAnim& a = e.anim;
    const int originalKind=a.kind;
    a.kind=int16_t(TypingOriginalCharacterKind(TypingOriginalSharedFlags(*this),a.kind));
    const int k = a.kind;
    if (k == 0x1E) a.scale = 0.6f;            // 0x3F19999A
    else if (k == 0x1F) a.scale = 0.7f;       // 0x3F333333
    else if (k >= 0x20 && k <= 0x38) a.scale = 0.9f;  // 0x3F666666
    else a.scale = 1.0f;
    a.counter[0] = 0;
    a.prevFrame = 0;
    a.frame[0] = 0;
    a.tick[0] = 0;
    a.blendLen[0] = 0;
    a.blendStart[0] = 0;
    a.flags[0] = 0;
    a.track1 = 0;
    for (EnemyNode& n : a.nodes) n.track = 0;  // 0x00412200(0, 0)
    a.drawFlags = 3;
    a.rotOrder = 5;
    const uint32_t attach = exe.U32(0x0052ED08 + 4 * k);
    (void)attach;  // attachments: drawn after the node walk (AnimDraw)
    // 0x00410590: pose of frame 0 of the current motion
    const Skeleton& s = EnemySkeleton(k);
    const int nodes = exe.S16(0x004E0724 + k * 2);
    const Motion* m = EnemyMotion(a.motion[0], nodes);
    if (!s.nodes.empty() && s.nodes[0].children.size() > 0) e.flags34 |= 0x80;
    float root[3] = {};
    int16_t rr[3] = {};
    if (m && !m->data.empty()) {
        m->RootPosition(0, root);
        m->Rotation(0, 0, rr);
    }
    memcpy(a.root, root, 12);
    memcpy(a.prevRoot, root, 12);
    for (int i = 0; i < 3; ++i) a.nodes[0].cur[i] = rr[i];
    a.nodes[0].used = true;
    const uint32_t table = exe.U32(0x004D032C + 4 * k);
    for (size_t i = 1; i < s.nodes.size(); ++i) {
        const SkeletonNode& d = s.nodes[i];
        if (d.index < 0 || d.index >= 32) continue;
        EnemyNode& n = a.nodes[d.index];
        n.used = true;
        n.model = static_cast<int32_t>(d.modelId);
        if (a.kind!=originalKind && typingReferenceActive) {
            const int prepared=typingReference.NativeEnemyModel(a.kind,d.index,n.model);
            ProcessRequest({0x50,static_cast<uint32_t>(prepared)});
        }
        n.flags = 0x21;
        const uint32_t row = table + d.index * 0x14 - 0x14;
        if (exe.InData(table) && static_cast<int32_t>(exe.U32(row)) == n.model) {
            n.radius = a.scale * F32(exe, row + 0x10);
            n.local[0] = F32(exe, row + 4);
            n.local[1] = F32(exe, row + 8);
            n.local[2] = F32(exe, row + 12);
        }
        n.stage = 0;
        int16_t r[3] = {};
        if (m && !m->data.empty()) m->Rotation(0, d.index, r);
        for (int j = 0; j < 3; ++j) n.cur[j] = r[j];
    }
    a.partial[0] = a.partial[1] = 0;
    // Actor_RegisterSlot 0x00409270
    e.actorSlot = -1;
    for (int i = 0; i < 14; ++i)
        if (!actorSlots[i]) {
            e.flags38 |= 0x40;
            actorSlots[i] = &e;
            e.actorSlot = i;
            break;
        }
}

// =====================================================================================================================
// Enemy_Update 0x004533F0
// =====================================================================================================================

// The fields of the enemy the lock-on table and the shooting camera read (+0x40, +0x100, +0x121, +0x34).
void World::Impl::SyncLock(EnemyTask& e) {
    if (!e.lock) return;
    memcpy(e.lock->pos, e.pos, 12);
    memcpy(e.lock->point, e.body, 12);
    e.lock->targetPlayer = e.target;
    e.lock->flags = e.flags34;
    static const uint32_t dbgFn = getenv("HOTD2_LOCKDBG") ? static_cast<uint32_t>(strtoul(getenv("HOTD2_LOCKDBG"), nullptr, 16)) : 0;
    if (dbgFn && e.fn == dbgFn)  // debug: the lock-on point of the tasks with this function
        fprintf(stderr, "%d lock fn %X body %.6f %.6f %.6f bv %.6f %.6f %.6f\n", frame, e.fn, e.body[0], e.body[1], e.body[2],
                e.bodyView[0], e.bodyView[1], e.bodyView[2]);
}

void World::Impl::EnemyUpdate(EnemyTask& e) {
    if (e.lock) e.slot120 = static_cast<int8_t>(e.lock->slot);  // written by the table task 0x00408DD0
    // Enemy_UpdateUpperBody 0x004547C0: hold the wounded-arm motion 0x00592CBC[kind][variant][4 + last hit slot]
    if ((e.flags136C & 0x100) && (e.flags136C & 0x200) && !(e.flags34 & 0x50000000)) {
        const int m = EnemyMotionSet(e, 4 + e.lastHitSlot);
        if (e.anim.motion[1] == m && e.anim.track1 != 0) {
            if (MotionTicks(e.anim.motion[0]) / 2 <= e.anim.tick[0]) {
                e.flags34 &= ~0x2000u;
            }
        } else {
            AnimTrack1Hold(e, 1, m, 0, 5);  // 0x00411B20
        }
    }
    // Enemy_UpdateHitFlags 0x00454660
    if (e.flags34 & 0x40000000) {
        const EnemyAnim& a = e.anim;
        if (e.lastHitType == 3) {
            if ((e.flags136C & 0x1000) && a.track1 == 0) e.flags136C &= ~0x1000u;
            if ((e.flags136C & 0x2000) && a.ended) e.flags136C &= ~0x2000u;
        } else {
            if ((e.flags136C & 0x1000) && (a.track1 == 0 || MotionTicks(a.motion[1]) / 4 <= a.tick[1]))
                e.flags136C &= ~0x1000u;
            if ((e.flags136C & 0x2000) && (a.ended || MotionTicks(a.motion[0]) / 4 <= a.tick[0]))
                e.flags136C &= ~0x2000u;
        }
        if ((e.flags136C & 0x3000) == 0) e.flags34 &= ~0x40000000u;
    }
    EnemyProcessHits(e);  // 0x00453EB0
    if (e.flags136C & 0x10000000) EnemyRide5C34(e);  // 0x0045E770 (world_st5_enemy.cpp)
    EnemyBehaviour(e);
    for (int i = 0; i < 3; ++i) e.pos[i] = e.pos[i] + e.vel[i];
    EnemyAnimate(e);  // 0x00454860
    if (!(e.flags136C & 0x8000000)) {  // Actor_RegisterTarget 0x00409010
        if ((e.flags34 & 1) && !(e.flags34 & 0x4000000) && attackList.size() < 14) {
            const double dz = static_cast<double>(e.pos[2]) - playerPos[2], dx = static_cast<double>(e.pos[0]) - playerPos[0];
            attackList.push_back({ static_cast<int32_t>(std::sqrt(dx * dx + dz * dz) * 10.0f), &e });
        }
    }
    EnemyViewPos(e);  // 0x00409B70(4.0)
    // hook 0x00592BC0 = 0x00456920 (enters water): floor query; collision meshes not ported -> no material
    if (!(e.flags1368 & 2)) {
        int mat = 0;
        const float f = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);
        if ((mat == 0x37 || mat == 5) && e.pos[1] < f && (e.variant == 6 || f < e.anim.root[1] + e.pos[1])) {
            SpawnWake(e, f, e.rot[1]);  // 0x00408740 (world_st3_enemy.cpp)
            e.flags1368 |= 2;
        }
    }
    if (!(e.flags1368 & 2)) EnemyMotionSounds(e);
}

// Enemy_MotionSounds 0x00452A10: footstep / swing cues at fixed ticks of track 0's motion (+0x1B4, tick +0x19C);
// +0x1314 keeps the last cued tick so a tick held over several frames cues once.
void World::Impl::EnemyMotionSounds(EnemyTask& e) {
    struct Cue {
        int motion;
        uint32_t sound;
        int ticks[4];  // 0-terminated
    };
    static const Cue kCues[] = {
        { 0x00B, 0x2616A9, { 10, 0x14, 0x1E, 1 } }, { 0x0B3, 0x2616A9, { 9, 0x1B } },  { 0x0C3, 0x2916A9, { 6, 0x24 } },
        { 0x108, 0x2916A9, { 7, 0x12 } },          { 0x114, 0x2916A9, { 6, 0x24 } }, { 0x1AE, 0x2616A9, { 6, 0x15 } },
        { 0x1C1, 0x2916A9, { 2, 0x1A } },          { 0x1C3, 0x3C16A9, { 0x1E } },    { 0x1C4, 0x3D16A9, { 0x11, 0x32 } },
        { 0x2B5, 0x2616A9, { 4, 0x14 } },          { 0x307, 0x3C16A9, { 0x28 } },    { 0x30A, 0x3C16A9, { 0x28 } },
        { 0x310, 0x2916A9, { 8, 0x1B } },          { 0x3B0, 0x2616A9, { 8, 0x20 } }, { 0x3B1, 0x2616A9, { 2, 0x1A } },
        { 0x3C8, 0x2616A9, { 6, 0x25 } },
    };
    const int m = e.anim.motion[0], t = e.anim.tick[0];
    for (const Cue& c : kCues) {
        if (c.motion != m) continue;
        bool cue = false;
        for (int k : c.ticks) cue = cue || (k != 0 && k == t);
        if (cue && e.soundTick != t) {
            Sound(c.sound);
            e.soundTick = static_cast<int16_t>(t);
        }
        return;
    }
}

// 0x00409B70: view-space body point, hit-target registration, lock-on candidate (0x00408EC0)
void World::Impl::EnemyViewPos(EnemyTask& e) {
    mx::Transform(cam.view, e.body, e.viewPos);
    RegisterTarget(&e);
    e.body[1] = 4.0f + e.body[1];
    SyncLock(e);
    lockOn.Register(e.lock, playerPos);  // 0x00408EC0
}

// Coll_FloorHeight 0x00409D40: 0x004053B0 tests the collision-mesh objects of the target copy and the level meshes;
// not ported: returns the ground height 0x009C8E58 with no material (reported when meshes exist).
float World::Impl::FloorHeight(float x, float y, float z, int* material) {
    return FloorQuery(x, y, z, material);  // level meshes: world_collision.cpp
}

// 0x00409C10: the view-space point projects inside the screen (NaN -> off screen)
bool World::Impl::OnScreen(const float v[3]) {
    const double half = static_cast<double>(focal) * 0.5;
    const double x = static_cast<double>(focal) * v[0] / v[2];
    const float y = static_cast<float>(static_cast<double>(focal) * v[1] / v[2]);
    if (!(x >= -half)) return false;  // also unordered
    if (x > half) return false;
    if (!(y >= -240.0f)) return false;
    if (y > 240.0f) return false;
    return true;
}

// =====================================================================================================================
// Shots
// =====================================================================================================================

// Enemy_ProcessHits 0x00453EB0
void World::Impl::EnemyProcessHits(EnemyTask& e) {
    if (!(e.flags34 & 8)) return;
    EnemyCollectShots(e);
    if (e.flags34 & 0x100) return;
    for (int i = 0; i < 2; ++i) {
        const int p = shotOrder[i];
        if (p == -1 || hitSlot[p] <= 0) continue;
        e.lastHitType = hitType[p];
        e.flags136C &= ~0x400u;
        e.flags34 |= 0x8000000;
        EnemyHitEffects(e, p);  // 0x00454050 (types 1..5; none outside game mode 6 for this kind: 0x800 forces 0)
        if (!(e.flags136C & 0x80000000u)) {
            if (!(e.flags34 & 0x4000000)) {
                EnemyHitReaction(e, p);  // 0x004543F0
                if (hitType[p] != 5) EnemyVoice(e, 0);
            } else {
                e.flags136C |= 0x80000000u;
                EnemyVoice(e, hitSlot[p] == 2 ? 2 : 1);
                if (!(e.flags136C & 0x4000)) {
                    const int16_t beh = e.behaviour;
                    e.sub = 0;
                    if (beh == 0x34 || (e.flags136C & 0x100000) || e.variant == 5 || e.variant == 6) {
                        if (beh > 0x1A && beh < 0x1D) {
                            const double dz = static_cast<double>(e.pos[2]) - e.jumpTarget[2];
                            const double dx = static_cast<double>(e.pos[0]) - e.jumpTarget[0];
                            if (std::sqrt(dz * dz + dx * dx) < 18.0) e.flags136C |= 8;
                        }
                        e.behaviour = 9;
                    } else {
                        e.behaviour = 6;
                    }
                }
            }
        }
    }
}

namespace {
struct BloodTask : WTask {  // 0x00407230 (task +0x34 obj, +0x44 counter, +0x48 node slot, +0x4C scale)
    uint32_t enemy = 0;     // serial of the enemy task (the original keeps the object pointer)
    EnemyTask* obj = nullptr;  // that task's object (read only while the serial is live)
    int slot = 0, counter = 0;
    float scale = 0;
    float hit[3] = {}, radius = 0;  // last read node values (the original reads freed memory after the enemy ends)
};
}  // namespace

// Enemy_HitEffects 0x00454050
void World::Impl::EnemyHitEffects(EnemyTask& e, int p) {
    const int type = hitType[p], slot = std::clamp(hitSlot[p], 0, 15);
    EnemyNode& n = e.anim.nodes[slot];
    float at[3] = {};
    if (type == 1 || type == 5) mx::Transform(cam.inverse, n.hit, at);  // view hit point x camera matrix 0x009A6040
    float scale = 0;
    switch (type) {
    case 1:
        if (slot == 2 && e.anim.nodes[2].model == 0x1DC2) {
            EnemyVoice(e, 3);
            SpawnFlyingHeadModel(e, 0x1DC1);  // 0x0040A130(obj, 0x1DC1) (first met in stage 4)
            SpawnEffectAt(at, 0, 0, 2, 1, p);
            Sound(0x1116A9);
        } else {
            scale = 0.75f;
        }
        break;
    case 2: scale = 0.5f; break;
    case 3: case 4: scale = 1.0f; break;
    case 5:
        if (!(n.flags & 0x10)) SpawnEffectAt(at, 0, 0, e.anim.kind == 3 ? 0x51 : 3, 1, p);
        break;
    default: break;
    }
    switch (type) {
    case 1: case 3: case 4:
        EnemyArmVariant(e);  // 0x00454270
        [[fallthrough]];
    case 2: {  // 0x00407310: blood on the hit node
        BloodTask* b = NewTask<BloodTask>(WorldTramp<&Impl::TaskBlood>, 0x00407230);
        b->enemy = CurrentSerial();
        b->obj = &e;
        b->slot = slot;
        b->scale = scale;
        b->counter = 0;
        return;
    }
    case 5:
        Sound(e.anim.kind != 2 ? 0x1116A9u : 0xF16A9u);
        return;
    default:
        return;
    }
}

// 0x00407230: blood sprite at the node's view-space hit point, 25 frames
void World::Impl::TaskBlood(Task& t) {
    BloodTask& b = t.As<BloodTask>();
    for (Task* x : ts.Live())
        if (x->serial == b.enemy && x->data) {
            if (b.obj && static_cast<const TaskData*>(b.obj) == x->data.get()) {
                const EnemyNode& n = b.obj->anim.nodes[b.slot & 31];
                memcpy(b.hit, n.hit, 12);
                b.radius = n.radius;
            }
            break;
        }
    const float z = b.radius + b.hit[2];
    const float f = z < -20.0f ? 1.0f : -z * F32(exe, 0x004C4C5C) + F32(exe, 0x004C4C58);
    const float s = static_cast<float>(f * 0.3) * b.scale;  // double 0x004C4C48 (0x004C4C50 with 0x009C911E == 1)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, b.hit[0], b.hit[1], z);
    mx::Scale(m, s, s, s);
    Draw(b.counter + 0x3A, m, 8, -1.0f, false, -1, true);
    if (++b.counter > 0x18) ts.KillCurrent();
}

// 0x00407200(obj, node): the same blood sprite on a node at full scale (task 0x00407120; actor 0x20 hits)
void World::Impl::SpawnNodeBlood(EnemyTask& e, int node) {
    if (cfg.portTyping && !cfg.typingOptions.bloodEffects) return;
    BloodTask* b = NewTask<BloodTask>(WorldTramp<&Impl::TaskBlood>, 0x00407120);
    b->enemy = CurrentSerial();
    b->obj = &e;
    b->slot = node;
    b->scale = 1.0f;  // 0x00407120 draws without the +0x4C factor
    b->counter = 0;
}

// 0x00407310(obj, node, scale): blood sprite task 0x00407230 on the node
void World::Impl::SpawnHitBlood(EnemyTask& e, int slot, float scale) {
    if (cfg.portTyping && !cfg.typingOptions.bloodEffects) return;
    BloodTask* b = NewTask<BloodTask>(WorldTramp<&Impl::TaskBlood>, 0x00407230);
    b->enemy = CurrentSerial();
    b->obj = &e;
    b->slot = slot;
    b->scale = scale;
    b->counter = 0;
}

// Enemy_HitReaction 0x004543F0
void World::Impl::EnemyHitReaction(EnemyTask& e, int p) {
    if (hitSlot[p] == 2) e.lastHitSlot = 2;
    if (hitSlot[p] == 1) e.lastHitSlot = 1;
    const int t = hitType[p];
    if (t == 1) {
        if (e.anim.kind == 10 && hitSlot[p] == 1 && !(e.flags34 & 0x400)) {
            e.flags34 |= 0x4000400;
            AddScore(p, 0x50);
            e.killer = static_cast<int8_t>(p);
            e.behaviour = 0x19;
            e.sub = 0;
            return;
        }
    } else if (t != 3) {
        if (t == 4) {
            if (hitSlot[p] == 1 || hitSlot[p] == 9) Note("enemy 0x0045D9F0 (hit type 4) not ported");
            return;
        }
        if (e.anim.kind != 0x12 && e.anim.kind != 3) return;
    }
    EnemyHitMotion(e, p);
}

// 0x004544C0: flinch on the upper body (track 1) or a full hit motion
void World::Impl::EnemyHitMotion(EnemyTask& e, int p) {
    const int slot = hitSlot[p];
    if (slot == 0 || (e.flags34 & 0x10002000)) return;
    const uint32_t tab = exe.U32(exe.U32(0x00592FC8 + 4 * static_cast<uint32_t>(e.anim.kind)) + 4 * static_cast<uint32_t>(e.variant));
    const int m = static_cast<int>(exe.U32(tab + static_cast<uint32_t>(exe.S16(0x004C84A8 + slot * 2) & 0xFFFF) * 4));
    if (e.behaviour == 3 && e.sub == 2) {
        e.behaviour = 4;
        e.sub = 0;
    }
    if (slot < 9 && !(e.flags136C & 0x800)) {
        if (!(e.flags136C & 0x100)) {
            e.flags136C |= 0x1000;
            AnimTrack1Start(e, 1, m, 0, 1, e.lastHitType == 3 ? 0x14 : 10);  // 0x00411B70
            e.flags34 |= 0x40000000;
            return;
        }
        AnimTrack1Hold(e, 1, EnemyMotionSet(e, 4 + e.lastHitSlot), 0, 10);  // 0x00411B20
        if (e.lastHitSlot != 0) {
            const uint32_t f = e.flags34;
            e.flags34 = f | 0x2000;
            if (e.variant != 4 && e.hp > 0) {
                e.flags136C |= 0x200;
                e.flags34 = f | 0x40002000;
                return;
            }
        }
    } else {
        e.flags136C = (e.flags136C & ~0x1000u) | 0x2000;
        AnimSetMotion(e, m);
    }
    e.flags34 |= 0x40000000;
}

// 0x00454270: variant from the remaining arm models after a wound
void World::Impl::EnemyArmVariant(EnemyTask& e) {
    const int m5 = e.anim.nodes[5].model, m8 = e.anim.nodes[8].model;
    bool zero = false;
    switch (e.anim.kind) {
    case 1: {
        int c = m5 == 0x1BA9;
        if (m8 == 0x1BA5) ++c;
        zero = c == 0;
        break;
    }
    case 2: {
        int c = m5 == 0x1BD2;
        if (m8 == 0x1BCC) ++c;
        if (c == 0) {
            Note("enemy 0x00456600 (kind 2 arms lost) not ported");
            zero = true;
        } else if (c == 1) {
            e.variant = 1;
        } else {
            e.variant = 2;
        }
        break;
    }
    case 0xE:
        zero = m5 != 0x1E55;
        break;
    case 0x13: case 0x14: {
        if (e.variant == 7 || e.variant == 5) break;
        int c = m5 == 0x1ECE;
        if (m8 == 0x1ECA) ++c;
        if (m5 == 0x1EF9) ++c;
        if (m8 == 0x1EF5) ++c;
        zero = c == 0;
        break;
    }
    default:
        break;
    }
    if (zero) e.variant = 0;
    const int b1 = static_cast<int8_t>(exe.S16(0x004C4D1C) >> 8) & 0x1F, b2 = static_cast<int8_t>(exe.S16(0x004C4D20)) & 0x1F;
    if ((e.flags136C & 0x40) && e.variant == 0 && (static_cast<int8_t>(e.lostParts) & (1 << b1)) &&
        (static_cast<int8_t>(e.lostParts) & (1 << b2))) {
        e.variant = 5;
        e.flags136C &= ~0x40u;
    }
}

// Enemy_CollectShots 0x004092F0 (+ Shot_PlayerOrder 0x004093C0, Enemy_ApplyShot 0x00409430)
void World::Impl::EnemyCollectShots(EnemyTask& e) {
    shotOrder[0] = shotOrder[1] = -1;
    if (playersInGame == 0) shotOrder[0] = 0;
    else if (playersInGame == 1) shotOrder[0] = 1;
    else if (playersInGame == 2) {
        const int r = Rand(0x004093E2);
        if (r & 1) {
            shotOrder[0] = 0;
            shotOrder[1] = 1;
        } else {
            shotOrder[0] = 1;
            shotOrder[1] = 0;
        }
    }
    for (int i = 0; i < 2; ++i) {
        const int p = shotOrder[i];
        if (p == -1) continue;
        hitSlot[p] = 0;
        const uint32_t bit = 1u << (p + 1);
        if (e.flags34 & bit) {
            hitSlot[p] = e.hitNode[p];
            if (!(e.flags34 & 0x100)) {
                // Enemy_ApplyShot 0x00409430
                EnemyShotState s;
                s.kind = e.anim.kind;
                s.flags34 = e.flags34;
                s.flags136C = e.flags136C;
                s.hp = e.hp;
                s.severMask = e.lostParts;
                s.killer = e.killer;
                for (const EnemyNode& n : e.anim.nodes) s.nodes.push_back({ n.model, n.flags, n.stage });
                if (runner->cfg.gameType == 1) s.itemDamage = ItemDamage(*this, p, &s.damageFactor, &s.damageBoost);  // world_modes_orig.cpp
                if (horde) HordeShotDamage(*this, p, s);  // port only (world_horde.cpp)
                const int slot = std::clamp(hitSlot[p], 0, 31);  // (skeleton 0x17 nodes 16.. are hit in stage 4)
                SetSites({ 0 });
                // Completing a word is lethal even for an armored body node. Keep the native
                // score, effects and death reaction path below, including kind-specific deaths.
                const bool typed = cfg.portTyping && e.typingStrike;
                const bool drillLeaper=typed&&cfg.eventMode==7&&cfg.gameType==2&&e.typingOriginType==0x31;
                if (typed) {
                    // PC actor31's flag4 post delivers a35-HP native hit;
                    // ordinary flag1 zombie posts take the lethal word path.
                    if(!drillLeaper)s.hp = 0;
                    e.typingStrike = false;
                }
                EnemyShotResult r;
                if(drillLeaper) {
                    // Source 0x4381C0 -> 0x436300 -> 0x4366C0: full words
                    // subtract35, partial keys carry1000 and take no HP.
                    // Reuse native wound/death/effect rules with that exact
                    // damage, independent of gun node/rank damage tables.
                    EnemyTables wordTables;
                    wordTables.partBit=enemyTables.partBit;
                    wordTables.kinds.resize(size_t(s.kind)+1);
                    auto& kind=wordTables.kinds[size_t(s.kind)];
                    kind=enemyTables.kinds[size_t(s.kind)];
                    std::fill(kind.baseDamage.begin(),kind.baseDamage.end(),int16_t(35));
                    kind.rankMod.clear();
                    r=EnemyApplyShot(wordTables,s,slot,cfg.gameMode,rank,&rng);
                } else r=EnemyApplyShot(enemyTables, s, slot, cfg.gameMode, rank, &rng);
                // Actor 0x31 has its own reaction callback that ignores deflects.
                // A completed word must enter that callback's lethal branch.
                if (typed && r.killed && r.type == EnemyHitType::Deflect) r.type = EnemyHitType::Plain;
                e.flags34 = s.flags34;
                e.hp = s.hp;
                e.lostParts = s.severMask;
                for (size_t k = 0; k < 32; ++k) {
                    EnemyNode& nk = e.anim.nodes[k];
                    if (nk.model != s.nodes[k].model) {  // 0x004098E0 -> 0x004099A0: hit sphere of the new model
                        const int nm = s.nodes[k].model;
                        if (nm == 0 || nm == 1) {
                            nk.radius = 0;
                        } else {
                            auto lookup = [&](int kind) {
                                uint32_t a = exe.U32(0x004D032C + static_cast<uint32_t>(kind) * 4) +
                                             static_cast<uint32_t>(exe.S16(0x004E0724 + static_cast<uint32_t>(kind) * 2)) * 0x14 - 0x14;
                                for (int guard = 0; guard < 256; ++guard, a += 0x14) {
                                    const int32_t m = static_cast<int32_t>(exe.U32(a));
                                    if (m == -1) return false;
                                    if (m == nm) {
                                        nk.radius = F32(exe, a + 0x10);
                                        for (int c = 0; c < 3; ++c) nk.local[c] = F32(exe, a + 4 + 4 * c);
                                        return true;
                                    }
                                }
                                return false;
                            };
                            if (!lookup(e.anim.kind)) lookup(e.anim.kind == 0xD ? 0xB : 7);
                        }
                    }
                    e.anim.nodes[k].model = s.nodes[k].model;
                    e.anim.nodes[k].flags = s.nodes[k].flags;
                    e.anim.nodes[k].stage = s.nodes[k].stage;
                }
                hitType[p] = static_cast<int>(r.type);
                if (r.type == EnemyHitType::Sever) {  // 0x00409AB0 -> 0x00409AF0: the parts below the severed node
                    const Skeleton& sk = EnemySkeleton(e.anim.kind);
                    int found = -1;
                    if (!sk.nodes.empty())
                        for (int c : sk.nodes[0].children) {
                            found = FindEntry(sk, c, slot);
                            if (found >= 0) break;
                        }
                    std::vector<int> below;
                    if (found >= 0)
                        for (int c : sk.nodes[found].children) SubtreeNodes(sk, c, below);
                    for (int i : below) {
                        if (i < 0 || i >= 32) continue;
                        e.lostParts = static_cast<uint8_t>(e.lostParts | (1u << ((exe.S16(0x004C4D18 + static_cast<uint32_t>(i)) & 0xFF) & 0x1F)));
                        e.anim.nodes[i].model = 0;
                        e.anim.nodes[i].radius = 0;
                    }
                }
                if (r.decapitate) {  // rand() & 3 == 0 on a head kill (0x00409430)
                    // 0x00407200(obj, 2): neck blood task 0x00407120 (same as 0x00407230 with scale 1)
                    SpawnNodeBlood(e, 2);
                    SpawnFlyingHead(e);        // 0x0040A130 (world_enemy_beh.cpp)
                    e.anim.nodes[2].model = 0;  // 0x004098E0(node 2, 0, 2)
                    e.anim.nodes[2].radius = 0;
                }
                if (r.killed) e.killer = static_cast<int8_t>(p);
                // score / hit counters (0x00409430 tail): kill +0x50; head (slot 2) +0x78 + streak (+0x22, += 10);
                // other slots +10 unless deflected (type 5), streak reset; hits +0x26
                if (r.killed) AddScore(p, 0x50);
                if (r.killed && horde) HordeEnemyKilled(*this, e, p, slot == 2);  // port only (world_horde.cpp)
                Player& P = player[p];
                if (slot != 2) {
                    if (r.type != EnemyHitType::Deflect) {
                        AddScore(p, 10);
                        ++P.hits;
                    }
                    P.streak = 0;
                } else {
                    AddScore(p, 0x78);
                    if (cfg.portStageResults && gameMode == 6 && !stageResults.shown) ++stageResults.player[p].headshots;
                    AddScore(p, P.streak);
                    P.streak = static_cast<int16_t>(P.streak + 10);
                    ++P.hits;
                }
            }
            EnemyNode& n = e.anim.nodes[std::clamp(hitSlot[p], 0, 15)];
            n.flags &= ~8u;
            n.flags &= ~bit;
        }
        e.flags34 &= ~8u;
        e.flags34 &= ~bit;
    }
}

// Enemy_PlayVoice 0x0040A6F0
void World::Impl::EnemyVoice(EnemyTask& e, int which) {
    bool alt = true;
    switch (e.anim.kind) {
    case 0: case 2: case 5: case 6: case 0xE: case 0xF: case 0x10: case 0x11: case 9:
        alt = false;
        break;
    default:
        break;
    }
    switch (which) {
    case 0: {
        const int r = Rand(0x0040A737);
        Sound(exe.U32(0x00577674 + (r % 5) * 4));
        Sound(exe.U32(alt ? 0x0057768C : 0x00577688));
        return;
    }
    case 1: {
        const int r = Rand(0x0040A778);
        Sound(exe.U32(0x00577674 + (r % 5) * 4));
        Sound(exe.U32(alt ? 0x00577694 : 0x00577690));
        return;
    }
    case 2: {
        const int r = Rand(0x0040A7B9);
        Sound((r & 1) == 0 ? 0x516A9 : 0x116A9);
        Sound(exe.U32(alt ? 0x0057769C : 0x00577698));
        return;
    }
    case 3: {
        const int r = Rand(alt ? 0x0040A82B : 0x0040A808);  // (two call sites)
        Sound(exe.U32((alt ? 0x005776A8 : 0x005776A0) + (r & 1) * 4));
        return;
    }
    default:
        Sound(0);
        return;
    }
}

// =====================================================================================================================
// Behaviours (table 0x00592AE8)
// =====================================================================================================================

void World::Impl::EnemyBehaviour(EnemyTask& e) {
    switch (e.behaviour) {
    case 0: case 49: return;  // 0x0041EBB0
    case 1: EnemyBeh1(e); return;
    case 2: EnemyBeh2(e); return;
    case 3: EnemyBeh3(e); return;
    case 4: EnemyBeh4(e); return;
    case 5: EnemyBeh5(e); return;
    case 6: EnemyBeh6(e); return;
    case 7: EnemyBeh7(e); return;
    case 14: EnemyBeh14(e); return;
    case 15: EnemyBeh15(e); return;
    case 18: EnemyBeh18(e); return;
    case 13: EnemyBeh13(e); return;  // world_enemy_beh.cpp
    case 34: EnemyBeh34(e); return;
    case 35: EnemyBeh35(e); return;
    case 45: EnemyBeh45(e); return;
    case 40: EnemyBeh40(e); return;
    case 9: EnemyBeh9(e); return;
    case 23: EnemyBeh23(e); return;
    case 39: EnemyBeh39(e); return;  // world_st1b.cpp
    case 33: if (EnemyBehaviourSt2(e)) return; EnemyBeh33(e); return;  // world_st2_enemy.cpp (full), else world_st1b.cpp
    case 26: EnemyBeh26(e); return;  // world_st1b.cpp
    case 12: EnemyBeh12(e); return;  // world_st1b.cpp
    case 37: EnemyBeh37(e); return;  // world_st1b_held.cpp
    case 36: EnemyBeh36(e); return;  // world_st1b_held.cpp
    case kHordeBehaviour: HordeEnemyBehaviour(*this, e); return;  // port only (world_horde.cpp)
    default:
        if (EnemyBehaviourSt2(e)) return;  // world_st2_enemy.cpp
        if (EnemyBehaviourSt3(e)) return;  // world_st3_enemy.cpp
        if (EnemyBehaviourSt2c(e)) return;  // world_st2c_enemy.cpp
        if (EnemyBehaviourSt4(e)) return;  // world_st4_enemy.cpp
        if (EnemyBehaviourSt5(e)) return;  // world_st5_enemy.cpp
        if (EnemyBehaviourSt6(e)) return;  // world_st6_enemy.cpp
        if (EnemyBehaviourRoutes(e)) return;  // world_routes.cpp
        Note("enemy behaviour " + std::to_string(e.behaviour) + " (" +
             std::to_string(enemyTables.behaviour[std::clamp<int>(e.behaviour, 0, 53)]) + ") not ported");
        return;
    }
}

// 0x00457B50: scripted lunge
void World::Impl::EnemyBeh23(EnemyTask& e) {
    switch (e.sub) {
    case 0:
        e.counter1330 = e.P(1);
        if (e.P(2) == 0xBA) {
            AnimSetMotion(e, 0xBB);
            e.flags34 |= 0x100;
        } else {
            AnimSetMotion(e, e.P(2));
            e.flags34 |= 0x4100;
        }
        ++e.sub;
        e.flags136C |= 0x100000;
        [[fallthrough]];
    case 1: {
        const int trig = e.counter1330;
        // trigger: -1, or the tick of context 0 (0x009A6110) or of context 2 (0x009A6458, not modelled)
        if (trig != -1 && runner->camera.tick != trig) {
            Note("behaviour 23 trigger on the context-2 tick 0x009A6458 not modelled");
            return;
        }
        if (e.P(2) == 0xBA) AnimBlendTo(e, 0xBA, 0, 1);
        TypingAttackTimer(0);  // TOTD 0x00446593: authored lunge releases immediately
        EnemyPickTarget(e);
        EnemyFaceTarget(e);
        e.flags34 &= ~0x4100u;
        EnemyAttackSplash(e, false);  // hook 0x00592BCC = 0x00456C50
        ++e.sub;
        e.jumpStart[1] = e.pos[1];
        [[fallthrough]];
    }
    case 2:
        if (e.anim.tick[0] != e.P(3)) return;
        PlayerTakeHit(e.target, 1, e.anim.motion[0] != 0xB1 ? 9 : 0);  // 0x00415300 (no effect in game mode 5)
        ++e.sub;
        [[fallthrough]];
    case 3:
        if (MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
            SpawnEffectAt(e.pos, 0, 0, 0x62, 1, -1);
            EnemyDespawn(e);
        }
        return;
    default:
        return;
    }
}

// Enemy_PickTarget 0x00455DE0
int World::Impl::EnemyPickTarget(EnemyTask& e) {
    e.target = -1;
    if (!TypingAttackReady()) return 0;  // TOTD 0x004446A0
    if (attackSlot == 0) {
        if (route == 1) {
            int who = playersInGame;
            if (who == 0) {
                if (targetTaken[0] == 0) {
                    e.target = 0;
                    who = playersInGame;
                }
            }
            if (who == 1 && targetTaken[1] == 0) e.target = 1;
        } else if (route == 2) {
            if (players == 1) {
                const int r = Rand(0x00455E1D) & 1;
                if (targetTaken[r] == 0) e.target = static_cast<int8_t>(r);
            } else if (attackers == 1) {
                int r = Rand(0x00455E52) & 1;
                if (targetTaken[r] != 0) r = ~r;
                e.target = static_cast<int8_t>(r);
            } else {
                // 0x00409C90: screen side of the enemy (focal * view x / view z < 0: player 2's side). x87 fcomp: an
                // unordered result (NaN: view position still 0 / 0 on the frame after the spawn) counts as "less"
                const float sx = focal * e.viewPos[0] / e.viewPos[2];
                if (!(sx >= 0.0f)) {
                    if (targetTaken[1] == 0) e.target = 1;
                } else if (targetTaken[0] == 0) {
                    e.target = 0;
                }
            }
        }
        // 0x00409DC0: valid only in the shooting camera (mode 2) and (demo or player state 5)
        const int t = e.target;
        const bool valid = runner->camera.modeCur == 2 && (cfg.gameMode == 5 || (t >= 0 && player[t & 1].state == 5));
        if (!valid) e.target = -1;
    }
    if (e.target != -1) {
        if (!OnScreen(e.viewPos)) {
            e.flags136C |= 0x20000;
            attackSlot = 1;
        }
        targetTaken[e.target & 1] = 1;
        return 1;
    }
    return 0;
}

// Enemy_FaceTarget 0x00455F40
void World::Impl::EnemyFaceTarget(EnemyTask& e) {
    int pitch = 0;
    if (route != 1 && e.target != -1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, playerPos[0], playerPos[1], playerPos[2]);
        mx::RotY(m, cam.yaw);  // 0x009A60D0 of the active context
        const float local[3] = { static_cast<float>(e.target * -2 + 1) * F32(exe, 0x00565DFC), 0.0f, -1.5f };
        float p[3];
        mx::Transform(m, local, p);
        DirToAngles(e.pos[0] - p[0], e.pos[1] - p[1], e.pos[2] - p[2], &pitch, &e.rot[1]);
        memcpy(e.faceTarget, p, 12);
        return;
    }
    DirToAngles(e.pos[0] - playerPos[0], e.pos[1] - playerPos[1], e.pos[2] - playerPos[2], &pitch, &e.rot[1]);
    memcpy(e.faceTarget, playerPos, 12);
}

// 0x00456520
void World::Impl::EnemyReleaseTarget(EnemyTask& e) {
    if (e.target != -1) {
        targetTaken[e.target & 1] = 0;
        e.target = -1;
    }
    if (e.flags136C & 0x20000) {
        e.flags136C &= ~0x20000u;
        attackSlot = 0;
    }
}

// Enemy_OnDeath 0x004565A0
void World::Impl::EnemyOnDeath(EnemyTask& e) {
    EnemyReleaseTarget(e);
    if (!(e.flags38 & 1)) {
        if (!(e.flags34 & 0x800000) || liveEnemies != 1) {
            e.flags34 |= 0x10000;
            if (e.slot120 != -1) lockOn.Release(e.slot120);
        }
        if (!(e.flags38 & 2)) {  // 0x00456560
            e.flags38 |= 2;
            --liveEnemies;
        }
    }
}

// Enemy_StartDeathMotion 0x004560B0
void World::Impl::EnemyStartDeathMotion(EnemyTask& e) {
    const int v = e.variant;
    int motion = -1, start = 0;
    bool fromHit = false;  // 0x00456220
    if (v == 4) {
        if (!(e.flags136C & 0x2000000) || !(e.flags136C & 0x8000000)) {
            motion = (Rand(0x004561DE) & 1) != 0 ? 0x41A : 0x404;
        } else {
            motion = 0x3DA;
            start = 0x19;
        }
    } else if (v >= 5 && v <= 6) {
        if (e.flags136C & 7) fromHit = true;
        else motion = 0x3DB;
    } else if (!(e.flags34 & 0x1000000)) {
        const uint32_t f = e.flags1368;
        if (f & 8) motion = 0x1AC;
        else if (f & 0x10) motion = 0x1A5;
        else if (f & 0x40) motion = 0x279;
        else if (f & 0x80) motion = 0x229;
        else if (e.anim.kind == 10) {
            if (!(e.flags136C & 0x8000)) fromHit = true;
            // else: keep the current motion
        } else if (e.anim.kind >= 0xF && e.anim.kind <= 0x11 && (e.flags136C & 0x100000)) {
            motion = 0x3DB;
        } else {
            fromHit = true;
        }
    } else {
        motion = 0x3F9;
    }
    if (fromHit) EnemyDeathMotionWalk(e);
    else if (motion >= 0) AnimBlendTo(e, motion, start, 5);
    EnemySplashList(e);
    e.flags34 &= ~0x4000u;
}

// 0x004550E0: death knock-back jump, fall, bounces, landing
void World::Impl::EnemyBeh9(EnemyTask& e) {
    EnemyJump jump;
    memcpy(jump.start, e.jumpStart, 12);
    memcpy(jump.target, e.jumpTarget, 12);
    jump.counter = e.counter1330;
    jump.duration = e.counter1334;
    auto holdIfEnded = [&] {
        if (MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) e.flags34 |= 0x4000;
    };
    if (e.sub == 0) {
        e.flags34 |= 0x20000;
        EnemyOnDeath(e);
        float div = 1.0f, zoff = 0.0f, zt = 0.0f;
        if (e.variant == 4) {  // pitch jitter, no divisor
            const int s = Rand(0x0045519E) & 1;
            const int r = Rand(0x004551B8);
            e.rot[0] += (s * -2 + 1) * (r % 5) * 0x100;
            zt = e.viewPos[2] <= -25.0f ? 1.5f : -5.0f;  // 0x0055D2C8
        } else if (e.variant == 5 || e.variant == 6) {
            if (e.variant == 6) {
                zoff = 7.0f;
                e.flags136C &= ~0x20000000u;
            }
            zt = -7.0f;
        } else {
            div = 4.0f;
            zt = e.viewPos[2] <= -25.0f ? 1.5f : -5.0f;  // 0x0055D2C8
        }
        const int r1 = Rand(0x00455218) & 1;
        const float x = static_cast<float>(static_cast<double>(static_cast<float>(r1 * -2 + 1)) * 0.5 / div + e.viewPos[0]);
        const int r2 = Rand(0x0045524B);
        const float y = static_cast<float>(e.viewPos[1] - (static_cast<double>(static_cast<float>(r2 % 5 + 1)) * 0.5 + zoff + 3.0) / div);
        const float z = static_cast<float>(static_cast<double>(zt) / div + e.viewPos[2]);
        const float local[3] = { x, y, z };
        float target[3];
        mx::Transform(cam.inverse, local, target);
        if ((e.flags136C & 8) && !(e.flags136C & 0x10)) memcpy(target, e.jumpTarget, 12);
        jump.Setup(e.pos, target, e.flags136C, e.flags34);  // 0x0044DB50
        EnemyStartDeathMotion(e);
        for (int i = 0; i < 3; ++i) e.vel[i] = e.acc[i] = 0;  // 0x0044E120
        ++e.sub;
        e.flags136C &= ~0x10000u;
    }
    if (e.sub == 1) {
        holdIfEnded();
        float vel[3];
        const bool more = jump.Step(vel);
        memcpy(e.jumpStart, jump.start, 12);
        memcpy(e.jumpTarget, jump.target, 12);
        e.counter1330 = jump.counter;
        e.counter1334 = jump.duration;
        if (more) {
            memcpy(e.vel, vel, 12);
            return;
        }
        e.acc[1] = EnemyFallGravity(e.variant);
        ++e.sub;
        e.counter1334 = 0;
    } else if (e.sub != 2) {
        return;
    } else {
        memcpy(e.jumpStart, jump.start, 12);
        memcpy(e.jumpTarget, jump.target, 12);
    }
    // state 2
    holdIfEnded();
    ++e.counter1334;
    e.vel[1] = e.acc[1] + e.vel[1];
    int mat = 0;
    float floor = FloorHeight(e.pos[0], e.pos[1], e.pos[2], &mat);
    if (mat == 5 || mat == 0x37) floor = ground;
    if (static_cast<double>(floor) < static_cast<double>(e.pos[1]) + e.vel[1]) return;
    e.pos[1] = floor;
    // hook 0x00592BC8 = 0x00456B70: landing dust / splash (once)
    if (!(e.flags136C & 0x10000)) {
        int m2 = 0;
        const float fy = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &m2);
        const float at[3] = { e.pos[0], fy, e.pos[2] };
        mat = m2;  // the bounce test below reads the last floor query's material 0x009CAC40
        if (m2 == 5 || m2 == 0x37) {  // water: two splash rings (0x004567C0) and effect 0x61
            SpawnSplashRing(at, 1.0f);
            SpawnSplashRing(at, 0.5f);
            SpawnEffectAt(at, 0, 0, 0x61, 1, -1);
        } else if (rainFlag == 1) SpawnEffectAt(at, 0, 0, 0x61, 1, -1);
        else SpawnEffectAt(at, 0, 0, 0x46, 2, -1);
        e.flags136C |= 0x10000;
    }
    bool land = false;
    if (e.counter1334 < 0x78) {
        float f = e.vel[1];
        if (e.vel[1] < 0.0f) f = f * -1.0f;
        if (f <= 0.15f) land = true;
        else if (mat != 5 && mat != 0x37) {
            e.vel[1] = e.vel[1] * -0.25f;
            return;
        } else {
            land = true;
        }
    } else {
        land = true;
    }
    if (land) {
        if (mat == 5 || mat == 0x37) e.flags34 |= 0x80000;
        for (int i = 0; i < 3; ++i) e.vel[i] = e.acc[i] = 0;
        EnemyEnterCorpse(e);
    }
}

// Enemy_EnterCorpse 0x00456740
void World::Impl::EnemyEnterCorpse(EnemyTask& e) {
    e.flags136C &= 0x9FFFFFFFu;
    e.flags34 = (e.flags34 & 0xFFFDFFFEu) | 0xC000;
    if (!(e.flags38 & 1)) {
        if (!(e.flags38 & 4)) {  // 0x00456580
            e.flags38 |= 4;
            --attackers;
        }
    }
    --e.anim.counter[0];
    e.behaviour = (e.anim.kind != 0x12 && e.anim.kind != 3) ? 7 : 8;
    e.sub = 0;
}

// Enemy_BehCorpse 0x00454F20
void World::Impl::EnemyBeh7(EnemyTask& e) {
    if (e.sub == 0) {
        SpawnBloodPool(e);  // 0x00407DA0 (game mode 6 only)
        ++e.sub;
        e.flags136C &= 0xDFFFFDFFu;
        e.flags34 |= 0xA0000;
        e.counter1330 = 0x78;
        e.facing1350 = e.rot[1];
    } else if (e.sub != 1) {
        return;
    }
    e.pos[1] = e.pos[1] - 0.04f;  // 0x004C4D04
    if (--e.counter1330 < 1) {
        e.flags34 |= 0x10000;
        if (e.slot120 != -1) lockOn.Release(e.slot120);
        EnemyDestroy(e);
    }
    // Enemy_CorpseTwitch 0x00454E00
    const int m = e.anim.motion[0];
    if (EnemyCorpseHoldUsesRand(m)) {
        const uint32_t site = (m >= 0x3D9 && m <= 0x3E0) ? 0x00454E59u : 0u;
        e.anim.counter[0] = EnemyCorpseHoldTick(m, Rand(site));
    }
}

// Actor_Destroy 0x00409CC0
void World::Impl::EnemyDestroy(EnemyTask& e) {
    e.flags34 = (e.flags34 & ~1u) | 0x80018000u;
    SyncLock(e);
    for (Target& t : targetCopy)
        if (t.obj == &e) t.obj = nullptr;  // 0x00405220
    if ((e.flags38 & 0x40) && e.actorSlot != -1) {
        actorSlots[e.actorSlot] = nullptr;
        e.actorSlot = -1;
    }
    ts.KillCurrent();
}

// =====================================================================================================================
// Animation (enemy skeleton, block A = obj + 0x194)
// =====================================================================================================================

// Anim_SetMotion 0x00411930
void World::Impl::AnimSetMotion(EnemyTask& e, int motion) {
    EnemyAnim& a = e.anim;
    a.motion[0] = motion;
    a.counter[0] = 0;
    a.prevFrame = 0;
    a.frame[0] = 0;
    a.tick[0] = 0;
    a.flags[0] = 0;
    a.track1 = 0;
    for (EnemyNode& n : a.nodes) n.track = 0;  // 0x00412200(0, 0)
    if (a.drawFlags & 2) {
        const Motion* m = EnemyMotion(motion, exe.S16(0x004E0724 + a.kind * 2));
        if (m && !m->data.empty()) m->RootPosition(0, a.prevRoot);
    }
}

// Anim_BlendTo 0x004119A0 -> 0x004119F0(A, 0, motion, track 0)
void World::Impl::AnimBlendTo(EnemyTask& e, int motion, int startTick, int n) {
    EnemyAnim& a = e.anim;
    a.tick[0] = startTick;
    a.frame[0] = startTick / 2;
    a.blendStart[0] = a.counter[0] - 1;
    a.motion[0] = motion;
    a.blendLen[0] = static_cast<int8_t>(n + 1);
    a.flags[0] = static_cast<uint8_t>((a.flags[0] & 0xDF) | 1);
    AnimBlendTrack(e, 0, motion, 0);  // 0x004119F0
}

// 0x004119F0(A, partial node, motion, track): the track's node subtree, then the blend source / target slots
void World::Impl::AnimBlendTrack(EnemyTask& e, int partial, int motion, int track) {
    EnemyAnim& a = e.anim;
    a.partial[track] = static_cast<int8_t>(partial);
    a.track1 = static_cast<int8_t>(track);  // +0x36 = track (a track-0 blend ends track 1)
    AnimSetNodeTracks(e, partial, track);    // 0x00412200
    const int c = a.tick[track];
    const int len = MotionTicks(a.motion[track]);
    if ((c & 1) == 1 && c == len) {
        ++a.frame[track];
        AnimLoadSlot(e, 0xC, 0, 0, track);
        AnimLoadSlot(e, 2, motion, a.frame[track], track);
    } else if ((c & 1) == 0) {
        AnimLoadSlot(e, 0xC, 0, 0, track);
        AnimLoadSlot(e, 2, motion, a.frame[track], track);
    } else {
        // 0x00411F20: odd start tick -> target = halfway between frames f and f + 1. The original interpolates only
        // the root position and the root node's angles (0x00411F80, track 0 only) and the angles of the nodes in the
        // skeleton type's top-level list (0x004120C0 -> 0x00412100, which does not recurse: nodes 1 and 9 for
        // kind 19, whatever their track); the other nodes keep frame f + 1. Then current -> source (0xC).
        AnimLoadSlot(e, 1, motion, a.frame[track], track);
        AnimLoadSlot(e, 2, motion, a.frame[track] + 1, track);
        auto half = [&](EnemyNode& nd) {
            for (int i = 0; i < 3; ++i) nd.s2[i] = InterpAngle(nd.s1[i], nd.s2[i], 0.5f);
        };
        if (track == 0) {
            for (int i = 0; i < 3; ++i) a.rootS2[i] = (a.rootS2[i] - a.rootS1[i]) * 0.5f + a.rootS1[i];
            half(a.nodes[0]);
        }
        const uint32_t sk = exe.U32(0x004E0430 + a.kind * 4);
        const int top = exe.S16(sk + 0x16);
        for (int i = 0; i < top; ++i) {
            const int idx = exe.S16(exe.U32(sk + 0x18 + 4 * i) + 0x14);
            if (idx >= 0 && idx < 32) half(a.nodes[idx]);
        }
        AnimLoadSlot(e, 0xC, 0, 0, track);
    }
}

// Subtree of the skeleton entry whose node index is `node` (0x00409A00 / 0x00409A50: first match in a depth-first
// walk of the type's top-level list); -1 when absent.
static int FindEntry(const Skeleton& s, int entry, int node) {
    if (s.nodes[entry].index == node) return entry;
    for (int c : s.nodes[entry].children) {
        const int r = FindEntry(s, c, node);
        if (r >= 0) return r;
    }
    return -1;
}
static void SubtreeNodes(const Skeleton& s, int entry, std::vector<int>& out) {
    out.push_back(s.nodes[entry].index);
    for (int c : s.nodes[entry].children) SubtreeNodes(s, c, out);
}

// 0x00412200(partial, track) -> 0x00412290: the track of every node below the partial node (0: the whole type list)
// and of the partial node itself (node +0x8E)
void World::Impl::AnimSetNodeTracks(EnemyTask& e, int partial, int track) {
    EnemyAnim& a = e.anim;
    const Skeleton& s = EnemySkeleton(a.kind);
    if (s.nodes.empty()) return;
    std::vector<int> list;
    if (partial == 0) {
        for (int c : s.nodes[0].children) SubtreeNodes(s, c, list);
    } else {
        int found = -1;
        for (int c : s.nodes[0].children) {
            found = FindEntry(s, c, partial);
            if (found >= 0) break;
        }
        if (found < 0) {
            Note("partial skeleton node " + std::to_string(partial) + " not found (0x00409A00)");
            return;
        }
        for (int c : s.nodes[found].children) SubtreeNodes(s, c, list);
    }
    for (int i : list)
        if (i >= 0 && i < 32) a.nodes[i].track = static_cast<int16_t>(track);
    if (partial >= 0 && partial < 32) a.nodes[partial].track = static_cast<int16_t>(track);
}

// 0x00411B70: start motion on track 1 (partial skeleton) blending over n frames; `loops` frames to return to track 0
void World::Impl::AnimTrack1Start(EnemyTask& e, int partial, int motion, int start, int n, int loops) {
    EnemyAnim& a = e.anim;
    a.tick[1] = start;
    a.frame[1] = start / 2;
    a.blendStart[1] = a.counter[1] - 1;
    a.blendLen[1] = static_cast<int8_t>(n + 1);
    a.loops = static_cast<int8_t>(loops + 1);
    a.motion[1] = motion;
    a.flags[1] = static_cast<uint8_t>((a.flags[1] & 0xC7) | 1);
    AnimBlendTrack(e, partial, motion, 1);
}

// 0x00411B20: hold motion on track 1 (flag 8: no automatic return)
void World::Impl::AnimTrack1Hold(EnemyTask& e, int partial, int motion, int start, int n) {
    EnemyAnim& a = e.anim;
    a.tick[1] = start;
    a.frame[1] = start / 2;
    a.blendStart[1] = a.counter[1] - 1;
    a.blendLen[1] = static_cast<int8_t>(n + 1);
    a.motion[1] = motion;
    a.flags[1] = static_cast<uint8_t>((a.flags[1] & 0xDF) | 9);
    AnimBlendTrack(e, partial, motion, 1);
}

// 0x00411BD0: blend track 1 back to the track-0 motion over n frames (flag 0x10: ends track 1 when done)
void World::Impl::AnimTrack1Return(EnemyTask& e, int partial, int n) {
    EnemyAnim& a = e.anim;
    a.tick[1] = 0;
    a.frame[1] = 0;
    a.loops = 0;
    a.motion[1] = a.motion[0];
    a.blendStart[1] = a.counter[1] - 1;
    a.blendLen[1] = static_cast<int8_t>(n + 1);
    a.flags[1] = static_cast<uint8_t>((a.flags[1] & 0xD7) | 0x11);
    AnimBlendTrack(e, partial, a.motion[0], 1);
}

// 0x00411AC0: track-0 blend of the lower body (partial node 9) while track 1 keeps running
void World::Impl::AnimBlendLower(EnemyTask& e, int motion, int start, int n) {
    EnemyAnim& a = e.anim;
    a.tick[0] = start;
    a.frame[0] = start / 2;
    a.blendStart[0] = a.counter[0] - 1;
    a.motion[0] = motion;
    a.blendLen[0] = static_cast<int8_t>(n + 1);
    a.flags[0] = static_cast<uint8_t>((a.flags[0] & 0xDF) | 1);
    const bool t1 = a.track1 == 1;
    AnimBlendTrack(e, 9, motion, 0);
    if (t1) a.track1 = 1;
}

// Anim_TrackTick 0x004112E0 (track 1)
void World::Impl::AnimTick1(EnemyTask& e) {
    EnemyAnim& a = e.anim;
    if (a.track1 != 1) return;
    uint8_t& f = a.flags[1];
    auto endTrack = [&] {
        a.track1 = 0;
        AnimSetNodeTracks(e, 0, 0);  // 0x00412200(0, 0)
    };
    if (!(f & 0x10)) {
        if (f & 1) {
            const int d = a.counter[1] - a.blendStart[1];
            const int L = a.blendLen[1] + 1;
            if (L < d || d < 0) {
                f &= 0xFE;
                endTrack();
            } else if (d == L) {
                a.counter[1] = a.tick[1] + 1;
                f &= 0xFE;
            }
        }
    } else {
        if (a.blendLen[1] + 1 <= a.counter[1] - a.blendStart[1]) f &= 0xFE;
        if (!(f & 1) && a.tick[0] == a.tick[1]) {
            f &= 0xEF;
            endTrack();
        }
    }
    if (a.track1 != 1) return;
    const int len = MotionTicks(a.motion[1]);
    if (!(f & 1)) {
        if (!(f & 0x10)) {
            a.tick[1] = a.counter[1] % (len + 1);
            a.frame[1] = a.tick[1] / 2;
        }
        if ((a.tick[1] & 1) == 1) {
            if (a.tick[1] != len) {
                int fr = a.frame[1];
                int next = fr + 1;
                if (fr < 0) fr = len / 2;
                if (len / 2 < next) next = 0;
                AnimLoadSlot(e, 1, a.motion[1], fr, 1);
                AnimLoadSlot(e, 2, a.motion[1], next, 1);
                a.blendLen[1] = 2;
                f |= 0x20;
                a.blendStart[1] = a.counter[1] - 1;
                goto check;
            }
            ++a.frame[1];
        }
        f &= 0xDF;
        AnimLoadSlot(e, 0, a.motion[1], a.frame[1], 1);
    }
check:
    if (len <= a.tick[1] && !(f & 8)) {
        if (a.loops > 0) {
            AnimTrack1Return(e, 1, a.loops);
            return;
        }
        if (!(f & 0x10)) endTrack();
    }
}

// 0x00411C20 (+ 0x00411EC0 / 0x00411D70): load frame data into the current (0) / source (1) / target (2) slot of
// every node of `track`, or copy current -> source (0xC).
void World::Impl::AnimLoadSlot(EnemyTask& e, int which, int motion, int frame, int track) {
    EnemyAnim& a = e.anim;
    const int partial = a.partial[track];
    const int nodes = exe.S16(0x004E0724 + a.kind * 2);
    const Motion* m = which != 0xC ? EnemyMotion(motion, nodes) : nullptr;
    const bool ok = m && !m->data.empty() && frame >= 0 && frame < m->StoredFrames();
    const Skeleton& s = EnemySkeleton(a.kind);
    std::vector<int> list;  // node indices visited (0x00411EC0 / 0x00411D70)
    if (partial != 0 && partial != 9) {
        // partial skeleton (0x00409A00): the nodes below the partial node, then the partial node; no root position
        int found = -1;
        if (!s.nodes.empty())
            for (int c : s.nodes[0].children) {
                found = FindEntry(s, c, partial);
                if (found >= 0) break;
            }
        if (found >= 0)
            for (int c : s.nodes[found].children) SubtreeNodes(s, c, list);
        list.push_back(partial);
    } else {
        float root[3] = {};
        if (which != 0xC && ok) m->RootPosition(frame, root);
        switch (which) {
        case 0: memcpy(a.root, root, 12); break;
        case 1: memcpy(a.rootS1, root, 12); break;
        case 2: memcpy(a.rootS2, root, 12); break;
        case 0xC: memcpy(a.rootS1, a.root, 12); break;
        default: break;
        }
        for (const SkeletonNode& d : s.nodes) list.push_back(d.index);
    }
    for (int idx : list) {
        if (idx < 0 || idx >= 32) continue;
        EnemyNode& n = a.nodes[idx];
        if (n.track != track) continue;
        int16_t r[3] = {};
        if (which != 0xC && ok) m->Rotation(frame, idx, r);
        switch (which) {
        case 0: for (int i = 0; i < 3; ++i) n.cur[i] = r[i]; break;
        case 1: for (int i = 0; i < 3; ++i) n.s1[i] = r[i]; break;
        case 2: for (int i = 0; i < 3; ++i) n.s2[i] = r[i]; break;
        case 0xC: for (int i = 0; i < 3; ++i) n.s1[i] = n.cur[i]; break;
        default: break;
        }
    }
}

// Anim_TrackTick 0x004111A0 (track 0)
void World::Impl::AnimTick0(EnemyTask& e) {
    EnemyAnim& a = e.anim;
    a.ended = a.nearEnd = false;
    if (a.flags[0] & 1) {
        const int d = a.counter[0] - a.blendStart[0];
        const int L = a.blendLen[0] + 1;
        if (d <= L && d >= 0) {
            if (d == L) {
                a.counter[0] = a.tick[0] + 1;
                a.flags[0] &= 0xFE;
            }
        } else {
            a.flags[0] &= 0xFE;
        }
    }
    const int len = MotionTicks(a.motion[0]);
    if (!(a.flags[0] & 1)) {
        a.tick[0] = a.counter[0] % (len + 1);
        int f = a.tick[0] / 2;
        a.frame[0] = f;
        if ((a.tick[0] & 1) == 1) {
            if (a.tick[0] != len) {
                int next = f + 1;
                if (f < 0) f = len / 2;
                if (len / 2 < next) next = 0;
                AnimLoadSlot(e, 1, a.motion[0], f, 0);
                AnimLoadSlot(e, 2, a.motion[0], next, 0);
                a.flags[0] |= 0x20;
                a.blendStart[0] = a.counter[0] - 1;
                a.blendLen[0] = 2;
                goto done;
            }
            a.frame[0] = f + 1;
        }
        a.flags[0] &= 0xDF;
        AnimLoadSlot(e, 0, a.motion[0], a.frame[0], 0);
    }
done:
    if (len <= a.tick[0]) a.ended = true;
    if (len - 1 <= a.tick[0]) a.nearEnd = true;
}

// Enemy_AnimateSkeleton 0x00454860 -> Enemy_EvalSkeleton 0x00411090
void World::Impl::EnemyAnimate(EnemyTask& e) {
    // 0x0041DC70 / 0x0041DCC0: the enemy light (environment block 1) - left to the renderer
    AnimDraw(e);
    EnemyShadow(e);  // 0x0040A590
    if (runner->cfg.gameType == 2) {
        // training: the enemies hold still (plain node draws 0x00453B30) until the lesson runs (flag 0xF1, not 0xF3)
        if (!(e.flags34 & 0x4000) && runner->flags[0xF1] == 1 && runner->flags[0xF3] == 0) {
            ++e.anim.counter[0];
            ++e.anim.counter[1];
            e.nodeFn = nullptr;  // 0x004534A0
        } else {
            e.nodeFn = TrainPlainNode;
        }
        return;
    }
    if (!(e.flags34 & 0x4000)) {
        ++e.anim.counter[0];
        ++e.anim.counter[1];
    }
}

// Enemy_DrawSkeleton 0x004110D0 (+ root transform 0x00410920 / 0x00410C50)
void World::Impl::AnimDraw(EnemyTask& e) {
    NativeModelDrawScope modelScope(*this,e.anim.kind);
    // Renderer state: nearly every caller of 0x00411090 draws between the actor light 0x0041DC70 (env block 1) and
    // 0x0041DCC0 (back to block 0); callers with their own light (0x0041DC10) or none set animLight = 0 first.
    struct LightGuard {
        World::Impl& w;
        bool on;
        LightGuard(World::Impl& w_) : w(w_), on(w_.animLight == 1) { if (on) w.LightChar(); }
        ~LightGuard() { if (on) w.LightScene(); }
    } lightGuard(*this);
    EnemyAnim& a = e.anim;
    AnimTick0(e);
    AnimTick1(e);  // 0x004112E0
    // 0x00410BD0: blend factor of every active track (0, and 1 while +0x36 == 1)
    for (int tr = 0; tr <= (a.track1 == 1 ? 1 : 0); ++tr)
        if (a.flags[tr] & 0x21) {
            a.factor[tr] = static_cast<float>((static_cast<double>(static_cast<float>(a.counter[tr])) - static_cast<float>(a.blendStart[tr])) /
                                              static_cast<double>(static_cast<float>(a.blendLen[tr])));
        }
    const int nodes = exe.S16(0x004E0724 + a.kind * 2);
    const Motion* m = EnemyMotion(a.motion[0], nodes);
    const bool mok = m && !m->data.empty() && a.frame[0] >= 0 && a.frame[0] < m->StoredFrames();
    // 0x00410920: root position and rotation
    float root[3] = {};
    int ang[3] = {};
    EnemyNode& r0 = a.nodes[0];
    if (!(a.flags[0] & 0x21)) {
        if (mok) {
            m->RootPosition(a.frame[0], root);
            int16_t rr[3];
            m->Rotation(a.frame[0], 0, rr);
            for (int i = 0; i < 3; ++i) ang[i] = rr[i];
        }
    } else {
        for (int i = 0; i < 3; ++i)
            root[i] = (a.rootS2[i] - a.rootS1[i]) * a.factor[0] + a.rootS1[i];  // 24-bit FPU: each step rounded to float
        if (!(a.flags[0] & 1)) {
            for (int i = 0; i < 3; ++i) ang[i] = InterpAngle(r0.s1[i], r0.s2[i], a.factor[0]);
        } else {
            Mat4 top;
            mx::BlendRotation(top, RotZYX(r0.s1), RotZYX(r0.s2), a.factor[0]);
            mx::MatrixToAngles(top, &ang[0], &ang[1], &ang[2]);
        }
    }
    memcpy(a.root, root, 12);
    // 0x00410C50: root motion
    {
        int d = a.prevFrame - a.frame[0];
        if (d < 0) d = a.frame[0] - a.prevFrame;
        const int len = MotionTicks(a.motion[0]);
        if (len / 4 < d) {
            for (int i = 0; i < 3; ++i)
                a.prevRoot[i] = (root[i] - a.prevRoot[i]) / static_cast<float>(len) + root[i];  // 24-bit: each step float
        }
        if ((a.flags[0] & 1) && !(a.flags[0] & 0x20)) memcpy(a.prevRoot, root, 12);
        if (a.drawFlags & 2) {
            Mat4 mm = Mat4::Identity();
            mx::Translate(mm, e.pos[0], e.pos[1], e.pos[2]);
            mx::RotZ(mm, e.rot[2]);
            mx::RotY(mm, e.rot[1]);
            mx::RotX(mm, e.rot[0]);
            mx::Scale(mm, a.scale, a.scale, a.scale);
            const float delta[3] = { root[0] - a.prevRoot[0], root[1] - a.prevRoot[1], root[2] - a.prevRoot[2] };
            float out[3];
            mx::Transform(mm, delta, out);
            e.pos[0] = out[0];
            if (a.drawFlags & 0x10) e.pos[1] = out[1];
            e.pos[2] = out[2];
            memcpy(a.prevRoot, root, 12);
        }
        if (e.postRootFn) e.postRootFn(*this, e);
        else if (e.civilian) CivPostRoot(e);  // callback +0x12F0 = 0x0048D070
        else if (e.enemyPost && e.bodyOf < 0 && e.nodeCb == 0) EnemyPostRoot(e);  // +0x12F0 = 0x00454900 (Enemy_Create)
        // (every other actor keeps 0x00410440's no-op 0x00420810: no pushes, no floor snap)
        a.prevFrame = a.frame[0];
    }
    // The chain twice: in world space (draws, shadows, attachments: the port's convention) and, like the original,
    // from the matrix-stack top = the active view (Cam_BuildViews 0x00401F40 loads it), for the view-space hit points
    // and the body point (bit-exact model-view products; riders start from the vehicle ops applied on the view: frameMv)
    Mat4 base = e.framed ? e.frame : Mat4::Identity();  // riders: the vehicle's matrix (world_st3_vehicle.cpp)
    Mat4 baseMv = e.framed ? e.frameMv : cam.view;
    for (Mat4* q : { &base, &baseMv }) {
        mx::Translate(*q, e.pos[0], e.pos[1], e.pos[2]);
        switch (a.rotOrder) {
        case 0: mx::RotX(*q, e.rot[0]); mx::RotY(*q, e.rot[1]); mx::RotZ(*q, e.rot[2]); break;
        case 1: mx::RotX(*q, e.rot[0]); mx::RotZ(*q, e.rot[2]); mx::RotY(*q, e.rot[1]); break;
        case 2: mx::RotY(*q, e.rot[1]); mx::RotX(*q, e.rot[0]); mx::RotZ(*q, e.rot[2]); break;
        case 3: mx::RotY(*q, e.rot[1]); mx::RotZ(*q, e.rot[2]); mx::RotX(*q, e.rot[0]); break;
        case 4: mx::RotZ(*q, e.rot[2]); mx::RotX(*q, e.rot[0]); mx::RotY(*q, e.rot[1]); break;
        default: mx::RotZ(*q, e.rot[2]); mx::RotY(*q, e.rot[1]); mx::RotX(*q, e.rot[0]); break;
        }
        mx::Scale(*q, a.scale, a.scale, a.scale);
        if (!(a.drawFlags & 2)) mx::Translate(*q, root[0], root[1], root[2]);
        else mx::Translate(*q, 0.0f, root[1], 0.0f);
        mx::RotZ(*q, ang[2]);
        mx::RotY(*q, ang[1]);
        mx::RotX(*q, ang[0]);
    }
    for (int i = 0; i < 3; ++i) r0.cur[i] = ang[i];
    r0.world = base;
    r0.mv = baseMv;
    const size_t firstCharacterDraw = draws.size();
    const Skeleton& s = EnemySkeleton(a.kind);
    if (!s.nodes.empty())
        for (int c : s.nodes[0].children) AnimNode(e, s, c, base, baseMv);
    static const uint32_t dbgFn = getenv("HOTD2_ANIMDBG") ? static_cast<uint32_t>(strtoul(getenv("HOTD2_ANIMDBG"), nullptr, 16)) : 0;
    if (dbgFn && e.fn == dbgFn) {  // debug: per-frame animation state of the tasks with this function
        fprintf(stderr, "%d anim fn %X m %d/%d t %d/%d f %X/%X fr %d c %d bs %d bl %d fac %.9g root %.6f %.6f %.6f pos %.6f %.6f %.6f rot %d %d %d\n",
                frame, e.fn, a.motion[0], a.motion[1], a.tick[0], a.tick[1], a.flags[0], a.flags[1], a.frame[0], a.counter[0],
                a.blendStart[0], a.blendLen[0], a.factor[0], a.root[0], a.root[1], a.root[2], e.pos[0], e.pos[1], e.pos[2], e.rot[0],
                e.rot[1], e.rot[2]);
        for (int i = 0; i < 32; ++i)
            if (a.nodes[i].used || i == 0)
                fprintf(stderr, "  n%d %d %d %d s1 %d %d %d s2 %d %d %d hit %.6f %.6f %.6f wt %.6f %.6f %.6f\n", i, a.nodes[i].cur[0],
                        a.nodes[i].cur[1], a.nodes[i].cur[2], a.nodes[i].s1[0], a.nodes[i].s1[1], a.nodes[i].s1[2], a.nodes[i].s2[0],
                        a.nodes[i].s2[1], a.nodes[i].s2[2], a.nodes[i].hit[0], a.nodes[i].hit[1], a.nodes[i].hit[2],
                        a.nodes[i].world.m[12], a.nodes[i].world.m[13], a.nodes[i].world.m[14]);
    }
    // Attachments (0x0052ED08[kind] = {count, defs}): skinned meshes set up by 0x00419520 and drawn by
    // 0x004EDAEC[kind][i] (e.g. 0x00419E90 -> 0x00419E50): vertex groups deformed by up to four node matrices
    // (0x00419980) relative to the base node, then the model drawn with the base node's matrix (0x00419B40).
    const uint32_t at = exe.U32(0x0052ED08 + 4 * a.kind);
    if ((a.drawFlags & 1) && exe.InData(at) && !a.attachHidden) {
        const int count = static_cast<int>(exe.U32(at));
        const uint32_t defs = exe.U32(at + 4);
        uint32_t tab = 0x004ED1E0;
        int skinNodes = 4;
        switch (a.kind) {
        case 0xE: tab = 0x004ED348; break;
        case 0x1F: case 0x46: case 0x4E: tab = 0x004ED4B0; skinNodes = 2; break;
        case 0x41: tab = 0x004ED780; skinNodes = 2; break;
        case 0x4C: tab = 0x004ED618; break;
        case 0x53: tab = 0x004ED8E8; skinNodes = 2; break;
        default: break;
        }
        for (int i = 0; i < count && i < 32 && exe.InData(defs); ++i) {  // (skeleton 0x4C has 18)
            const uint32_t def = exe.U32(defs + 4 * i);
            if (def == 0) continue;
            if (a.kind == 0x17) {  // world_st4_actors.cpp: translucent (+0x138C) skin + the tail chain 0x0041A020
                AnimAttach17(e, i, def, tab);
                continue;
            }
            if (a.kind == 9 || a.kind == 0x12 || a.kind == 0x18 || a.kind == 0x4C ||
                (a.kind >= 0x4D && a.kind <= 0x52)) {
                if (a.kind == 0x4C && AttachDraw4C(e, i, def)) continue;  // world_st5_actors.cpp
                if (a.kind == 0x18 && AttachDraw18(e, i, def)) continue;  // world_st2_actor31.cpp (stage 5)
                if (AttachDrawSt6(e, i, def, tab, skinNodes)) continue;   // kinds 9 / 0x12 / 0x4D..0x52 (world_st6_enemy.cpp)
                Note("enemy attachment draw path of kind " + std::to_string(a.kind) + " (0x00419B40) not ported");
                continue;
            }
            const int model = static_cast<int>(exe.U32(def));
            const int baseNode = static_cast<int>(exe.U32(tab + i * 0x14 + 0x10)) & 15;
            const bool alt = (e.flags38 & 8) && altModels != 0;
            WorldDraw* d = Draw(model, a.nodes[baseNode].mv, 8, -1.0f, alt, baseNode, true);  // node +0x28 (model-view)
            if (!d) continue;
            d->skinCount = skinNodes;
            for (int k = 0; k < skinNodes; ++k) {
                const int sn = static_cast<int>(exe.U32(tab + i * 0x14 + (skinNodes == 2 ? 8 * k : 4 * k))) & 15;
                d->skin[k] = a.nodes[sn].mv;
            }
        }
    }
    if (e.parts) DrawParts(e);  // anim +0x1170 / +0x1174
    RandomizerCharacterDraw(*this, e, firstCharacterDraw);
}

// 0x004114C0 (+ node transform 0x00411700)
void World::Impl::AnimNode(EnemyTask& e, const Skeleton& s, int idx, const Mat4& parent, const Mat4& parentMv) {
    EnemyAnim& a = e.anim;
    const SkeletonNode& d = s.nodes[idx];
    NativeModelDrawScope modelScope(*this,a.kind,d.index);
    EnemyNode& n = a.nodes[d.index & 31];
    Mat4 m = parent, mv = parentMv;
    mx::Translate(m, d.offset[0], d.offset[1], d.offset[2]);
    mx::Translate(mv, d.offset[0], d.offset[1], d.offset[2]);
    const int track = n.track & 1;
    const uint8_t fl = a.flags[track];
    int ang[3] = {};
    if (!(fl & 1) && !(fl & 0x20)) {
        const int nodes = exe.S16(0x004E0724 + a.kind * 2);
        const Motion* mo = EnemyMotion(a.motion[track], nodes);
        if (mo && !mo->data.empty() && a.frame[track] >= 0 && a.frame[track] < mo->StoredFrames()) {
            int16_t r[3];
            mo->Rotation(a.frame[track], d.index, r);
            for (int i = 0; i < 3; ++i) ang[i] = r[i];
        }
    } else if (!(fl & 1) && (!(a.drawFlags & 8) || !(fl & 0x20))) {
        for (int i = 0; i < 3; ++i) ang[i] = InterpAngle(n.s1[i], n.s2[i], a.factor[track]);
    } else {
        Mat4 top;
        mx::BlendRotation(top, RotZYX(n.s1), RotZYX(n.s2), a.factor[track]);
        mx::MatrixToAngles(top, &ang[0], &ang[1], &ang[2]);
    }
    for (Mat4* q : { &m, &mv }) {
        mx::RotZ(*q, ang[2]);
        mx::RotY(*q, ang[1]);
        mx::RotX(*q, ang[0]);
    }
    for (int i = 0; i < 3; ++i) n.cur[i] = ang[i];
    n.world = m;
    n.mv = mv;
    if (n.model != 0 && (a.drawFlags & 1)) {
        // 0x004122E0: nodes not drawn
        bool skip = false;
        if (a.kind == 0x17) skip = d.index > 15;
        else if (d.index == 9) {
            const int mm = a.nodes[9].model;
            skip = mm == 0xE3C || mm == 0xE4D || mm == 0xEA2 || mm == 0xEB6 || mm == 0xEC6 || mm == 0xED6 ||
                   mm == 0xEF6 || mm == 0xF06 || mm == 0xF83 || mm == 0x15B0;
        }
        if (!skip) {
            // Source Meat uses the same enlarged-head / civilian-limb node
            // draw transforms as the native Original items. Keep children on
            // their unscaled skeleton chain and scale the callback's top only.
            Mat4 originalDraw=mv;
            const auto itemScale=TypingOriginalNodeScale(TypingOriginalSharedFlags(*this),a.kind,e.civilian,d.index);
            if (e.nodeFn!=BigHeadNode) mx::Scale(originalDraw,itemScale[0],itemScale[1],itemScale[2]);
            const Mat4& mv=originalDraw;
            const bool savedMv = drawMv;  // the callbacks draw from the model-view chain (like the original's stack top)
            drawMv = true;
            if (e.nodeFn) e.nodeFn(*this, e, d.index, mv);
            else if (e.civilian) CivDrawNode(e, d.index, mv);  // callback +0x12EC = 0x0048D1F0
            else if (e.bodyOf >= 0) PlayerBodyNode(e, d.index, mv);  // 0x00416570
            else if (e.nodeCb == 0x0049D980) Actor22Node(e, d.index, mv);
            else if (e.nodeCb == 0x00449530) {  // actor 0x20: node model, nodes 3/7/11/14 also draw their first child
                Mat4 hm = mv;  // node 2 with item 0x15 in game type 1 (0x009C88A8 == 1): scaled (2, 2, 2)
                if (d.index == 2 && runner->cfg.gameType == 1 && ItemFlag(*this, 0)) mx::Scale(hm, 2.0f, 2.0f, 2.0f);
                Draw(n.model, hm, 8, -1.0f, false, d.index);
                const int k = d.index - 2;
                if (k >= 0 && k <= 12 && (exe.S16(0x00449610 + static_cast<uint32_t>(k)) & 0xFF) == 1 && !d.children.empty()) {
                    const SkeletonNode& ch = s.nodes[d.children[0]];
                    Mat4 q = mv;
                    mx::Translate(q, ch.offset[0], ch.offset[1], ch.offset[2]);
                    Draw(static_cast<int>(ch.modelId), q, 8, -1.0f, false, d.index);
                }
            }
            else if (e.nodeCb == 0x00411050) Draw(e.anim.nodes[d.index].model, mv, 8, -1.0f, false, d.index);  // default
            else if (e.nodeCb == 0x004208F0) {  // stage-3 boss intro body: game mode 6, game type 1, item 0xC (0x009C88AC)
                Mat4 q = mv;
                if (gameMode == 6 && runner->cfg.gameType == 1 && ItemFlag(*this, 4)) {
                    if (d.index == 2) mx::Scale(q, 1.5f, 1.0f, 1.5f);
                    else if (d.index == 5 || d.index == 8 || d.index == 0xC || d.index == 0xF) mx::Scale(q, 2.0f, 2.0f, 2.0f);
                }
                Draw(e.anim.nodes[d.index].model, q, 8, -1.0f, false, d.index);
            }
            else if (e.nodeCb == 0x004835D0) {  // actor 0x24; game type 1 with item 0xC (0x009C88AC): head / hands / feet scaled
                Mat4 q = mv;
                const int k = a.kind, i = d.index;
                if (runner->cfg.gameType == 1 && modes && modes->itemFlags[4] == 1 && k >= 0x20 && k < 0x40 && k != 0x38 &&
                    (i == 2 || i == 5 || i == 8 || i == 12 || i == 15)) {
                    const float s = i == 2 ? 1.5f : 2.0f;
                    mx::Scale(q, s, 1.0f, s);
                }
                Draw(e.anim.nodes[d.index].model, q, 8, -1.0f, false, d.index);
                // the extras of 0x004835D0 after the node model
                const int mo = a.motion[0], tk = a.tick[0];
                if ((k == 0x42 || k == 0x43) && (mo == 0xE4 || mo == 0xE5) && i == 5 && tk >= 1 && tk < 4) {
                    float f;
                    const uint32_t kb = exe.U32(0x004C4D10);
                    memcpy(&f, &kb, 4);
                    Draw(0xE24, mv, 8, 1.0f - static_cast<float>(tk - 1) * f, false, i);  // 0x004185A0
                    if (tk == 1) Note("actor 0x24 flash 0x00485DE0 (kinds 0x42 / 0x43) not ported");
                }
                const uint32_t c = static_cast<uint32_t>(seedCounter);  // 0x009A32A0
                int extra = 0;
                if (k == 0x31 && mo == 0x241 && i == 1) extra = 0xF0E;
                else if (k == 0x53 && i == 1) extra = static_cast<int>(c % 100) + 0x9D5;
                else if (k == 0x54 && i == 0x1B) extra = static_cast<int>(c % 50) + 0x34C;
                else if (k == 0x55 && (i == 4 || i == 6)) extra = static_cast<int>(c % 50) + 0x3D9;
                else if (k == 0x55 && i == 11) extra = static_cast<int>(c % 50) + 0x39A;
                else if (k == 0x55 && i == 13) extra = static_cast<int>(c % 50) + 0x40C;
                if (extra != 0) Draw(extra, mv, 8, -1.0f, false, i);
            }
            else if (e.nodeCb == 0x00485260) Actor25Node(e, d.index, mv);  // actor 0x25 (world_npc.cpp)
            else if (e.nodeCb == 0x00463F90) CreatureItemNode(e, d.index, mv);  // item slot 3 (world_st1b.cpp)
            else EnemyDrawNode(e, d.index, mv);  // callback +0x12EC = 0x004534A0
            drawMv = savedMv;
        }
        bool small = false;
        int body = 1;
        if (a.kind >= 0 && a.kind < 0x15) {
            small = true;
            if (c7310 == 1) {
                if (e.variant != 4 || (e.flags136C & 0x8000000)) body = 9;
            } else if (e.variant == 5) {
                body = 9;
            } else if (e.flags136C & 0x10000000) {
                body = 2;
            }
        }
        if (d.index == body) {
            // +0x10C = translation of the model-view matrix; +0x100 = that times the context's camera matrix
            // (0x009A6040, built before the angle normalisation, so not exactly the inverse of the view)
            mx::GetTranslation(mv, e.bodyView);
            Mat4 q = mv;  // 0x004A9570: top x the camera matrix, then its translation
            mx::MulRight(q, cam.inverse);
            mx::GetTranslation(q, e.body);
            if (e.lock) memcpy(e.lock->point, e.body, 12);  // the lock-on table reads +0x100 live
            if (small && ((c7310 == 1 && !(e.flags136C & 0x2000000)) || e.variant == 5)) e.body[1] -= 3.5f;
        }
        if (!(e.flags34 & 0x8000)) {  // 0x004A8A80 on the model-view top
            // civilians in game type 1 with item 0xC: 0x0048D1F0 scales the stack top itself (no push) for the head
            // (1.5, 1, 1.5) and the hands / feet (2, 2, 2), so the hit point is taken on the scaled top
            const int i = d.index;
            if (e.civilian && runner->cfg.gameType == 1 && ItemFlag(*this, 4) && (i == 2 || i == 5 || i == 8 || i == 0xC || i == 0xF)) {
                Mat4 hm = mv;
                if (i == 2) mx::Scale(hm, 1.5f, 1.0f, 1.5f);
                else mx::Scale(hm, 2.0f, 2.0f, 2.0f);
                mx::Transform(hm, n.local, n.hit);
            } else {
                mx::Transform(mv, n.local, n.hit);
            }
        }
    }
    for (int c : d.children) AnimNode(e, s, c, m, mv);
}

// Node draw callback 0x004534A0 (+ 0x00453AE0): animated faces / mouths and attached props by node model
void World::Impl::EnemyDrawNode(EnemyTask& e, int idx, const Mat4& world) {
    Mat4 m = world;
    if (idx == 2 && !(e.flags34 & 0x40000)) EnemyHeadLook(e, m);
    auto drawM = [&](int model, const Mat4& at) {  // 0x00453AE0
        if ((e.flags136C & 0x20) && altModels != 0) Draw(model, at, 8, -1.0f, true, idx);
        else if (e.flags1368 & 0x20) {
            Draw(model, at, 8, e.alpha138C, false, idx);  // 0x004185A0
        } else Draw(model, at, 8, -1.0f, false, idx);
    };
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    const int base = e.anim.nodes[idx].model;
    const int v = frameCounter5C50 + e.actorSlot * 10;  // 0x009A5C50 + actor slot * 10
    int model = base;
    switch (base) {
    case 0x1D99: {
        drawM(0x1D99, m);
        Mat4 q = m;
        mx::Translate(q, bits(0x3EAFD220), bits(0x3EE7EF9E), bits(0x3F84432D));
        drawM(0xB66, q);
        return;
    }
    case 0x1C71: case 0x1C72: case 0x1C74: case 0x1C75: case 0x1C76: case 0x1C77: case 0x1C78: case 0x1C79:
    case 0x1C7A: case 0x1C7B: case 0x1C7D: case 0x1C7E: case 0x1C7F: case 0x1C80:
        envMapDraw = true;  // 0x00418660: env-map uv pre-pass (renderer)
        drawM(base, m);
        envMapDraw = false;
        return;
    case 0x1C96:
        drawM(base, m);
        drawM(v % 0x78 + 0x1CB9, m);
        return;
    case 0x1C97: model = v % 0x12 + 0x1C97; break;
    case 0x1CA9:
        if (e.texCounter < 0xE) ++e.texCounter;
        model = e.texCounter + 0x1CA9;
        break;
    case 0x1DCD: model = v % 0x32 + 0x1DCE; break;
    case 0x1E00: model = v % 0x32 + 0x1E01; break;
    case 0x1F09: {  // blinking face: 1..59 then one frame of the base model
        int& c = e.texCounter;
        if (c == -1) c = 0;
        if (!(e.flags136C & 0x80000)) {
            if (c != -1) {
                c = (c + 1) % 0x3C;
                if (c == 0) c = -1;
            }
        } else {
            c = 0;
            e.flags136C &= ~0x80000u;
        }
        model = c == -1 ? base : c < 0x1E ? c + 0x1F0A : 0x1F45 - c;
        break;
    }
    case 0x1C7C: case 0x1C6C:
        EnemyFadeNode(e, base);  // world_st6_enemy.cpp
        break;
    case 0x1BEB: case 0x1BED: {
        drawM(base, m);
        const int m2 = v % 0x19 + 0x161B;
        Mat4 q = m;
        mx::Translate(q, 0.0f, bits(0xC0833333), 0.0f);
        mx::Scale(q, 0.5f, 0.5f, 1.0f);
        drawM(m2, q);
        q = m;
        mx::Translate(q, 0.0f, bits(0xC0833333), 0.0f);
        mx::RotY(q, 0x4000);
        mx::Scale(q, 0.5f, 0.5f, 1.0f);
        drawM(m2, q);
        return;
    }
    case 0x1B3D:
        drawM(v % 0x14 + 0x1B3E, m);
        drawM(v % 0x1E + 0x1B52, m);
        return;
    case 0x1B70: case 0x1B71:
        drawM(base, m);
        drawM(v % 0x1E + 0x1B52, m);
        return;
    case 0x1BCC: model = v % 5 + 0x1BCD; break;
    case 0x1BD2: model = v % 5 + 0x1BD3; break;
    default: break;
    }
    drawM(model, m);
}
// Enemy_HeadLook 0x00453BE0 (uses the head node's view-space hit point from the previous evaluation)
void World::Impl::EnemyHeadLook(EnemyTask& e, Mat4& m) {
    EnemyNode& head = e.anim.nodes[2];
    const uint32_t facing = static_cast<uint32_t>(e.rot[1] - 0x8000) & 0xFFFF;
    float p[3];
    mx::Transform(cam.inverse, head.hit, p);
    int pitch = 0, yaw = 0;
    if (route == 2 && e.target != -1) {  // 0x00453D70
        Mat4 t = Mat4::Identity();
        mx::Translate(t, playerPos[0], playerPos[1], playerPos[2]);
        mx::RotY(t, cam.yaw);
        const float local[3] = { static_cast<float>(e.target * -2 + 1) * F32(exe, 0x00565DFC), 15.0f, -1.5f };
        float q[3];
        mx::Transform(t, local, q);
        DirToAngles(q[0] - p[0], q[1] - p[1], q[2] - p[2], &pitch, &yaw);
    } else {
        DirToAngles(playerPos[0] - p[0], static_cast<float>((playerPos[1] + 15.0) - p[1]), playerPos[2] - p[2], &pitch, &yaw);
    }
    const uint32_t lp = static_cast<uint32_t>(pitch) & 0xFFFF, ly = static_cast<uint32_t>(yaw) & 0xFFFF;
    uint32_t v = ApproachAngle(static_cast<uint32_t>(e.headPitch), lp, 0xC0);
    if (AngleInRange(v, 0, 0x4000)) e.headPitch = static_cast<int>(v);
    else e.headPitch = static_cast<int>(ApproachAngle(static_cast<uint32_t>(e.headPitch), 0, 0xC0));
    v = ApproachAngle(static_cast<uint32_t>(e.headYaw), ly, 0xC0);
    if (AngleInRange(v, facing, 0x4000)) e.headYaw = static_cast<int>(v);
    else e.headYaw = static_cast<int>(ApproachAngle(static_cast<uint32_t>(e.headYaw), facing, 0xC0));
    mx::RotY(m, -static_cast<int>(facing));
    mx::RotY(m, e.headYaw);
    mx::RotX(m, e.headPitch);
}

// 0x0040A590 -> 0x0040A620: shadow model 0x10D0 at the floor (pass-1 mode 0xD)
void World::Impl::EnemyShadow(EnemyTask& e) {
    if ((e.flags34 & 0x80000) || !(e.anim.drawFlags & 1)) return;
    const bool big = e.anim.kind == 0x44 || e.anim.kind == 0x47;
    const float sx = big ? 50.0f : 11.0f, sz = big ? 30.0f : 10.0f;
    float y;
    if (cfg.gameMode == 0xD) y = ground;
    else if (!(e.anim.drawFlags & 4)) y = e.pos[1];
    else {
        int mat = 0;
        y = FloorHeight(e.pos[0], e.pos[1] + 3.0f, e.pos[2], &mat);
    }
    Mat4 m = e.framed ? e.frameMv : cam.view;  // the matrix-stack top (riders: the vehicle frame on the view)
    mx::Translate(m, e.pos[0], y + 0.1f, e.pos[2]);  // 0x004C4CC8
    mx::Scale(m, sx, kOneF, sz);
    Draw(0x10D0, m, 0xD, -1.0f, false, -1, true);
}

// 0x00454900: collision callback after the root transform
void World::Impl::EnemyPostRoot(EnemyTask& e) {
    auto sphereCentre = [&] {  // 0x00454AC0
        const float y = e.radius128 + e.pos[1];
        e.sphere[0] = e.pos[0];
        e.sphere[2] = e.pos[2];
        e.sphere[1] = y + ((e.flags136C & 0x2000000) ? 0.5f : 1.0f);
    };
    e.flags136C &= ~0x800000u;
    sphereCentre();
    if (e.flags136C & 0x40000000) {
        if (e.pushed) {  // pushed by another actor's 0x00405B10 since our last update
            float f = e.pushAmount * 0.1f;  // 0x004C4CC8
            if (e.pusherFlags & 0x18000000) f = f * 1.8f;  // 0x0055DD48
            for (int k = 0; k < 3; ++k) e.pos[k] = f * e.pushDir[k] + e.pos[k];
            sphereCentre();
            e.pushed = false;
        }
        if (SpherePush(e)) {
            float f = cac4c * 0.1f;
            if (e.flags34 & 0x18000000) f = f * 1.8f;
            e.pos[0] = cac64[0] * f + e.pos[0];
            e.flags136C |= 0x800000;
            e.pos[2] = cac64[2] * f + e.pos[2];
            sphereCentre();
        }
    }
    if (e.flags136C & 0x20000000) {  // 0x004057F0: collision-mesh objects and level meshes (world_collision.cpp)
        if (MeshPush(e.sphere, e.radius128)) {
            const float f = cac4c;
            e.pos[0] = cac64[0] * f + e.pos[0];
            e.pos[1] = cac64[1] * f + e.pos[1];
            e.flags136C |= 0x800000;
            e.pos[2] = cac64[2] * f + e.pos[2];
            sphereCentre();
        }
    }
    if (!(e.flags34 & 0x20000)) {  // 0x00454B10: snap to the floor
        int mat = 0;
        const float f = FloorHeight(e.pos[0], e.pos[1] + 6.0f, e.pos[2], &mat);
        const double dy = static_cast<double>(e.pos[1]) - f;
        if (!(e.flags136C & 0x4000000) || e.pos[1] <= f || std::sqrt(dy * dy) <= 10.0) {
            e.pos[1] = f;
        } else if (e.behaviour != 0xB) {
            e.behaviour = 0xB;
            e.sub = 0;
        }
    }
    sphereCentre();
    if (--e.timer1338 < 0 && (e.flags136C & 0x800000)) {
        e.timer1338 = 0x3C;
        e.flags136C ^= 0x400000;
    }
}

// 0x00405B10: the collision sphere (e.sphere, e.radius128) against the spheres of the other targets of the copy list
// (non-mesh, centres as registered); the nearest overlap (0x00405760: stable radix sort on ftol(distance * 10))
// gives the push. The other object is told through +0x138..+0x148 and moves when it next updates.
bool World::Impl::SpherePush(EnemyTask& e) {
    struct Hit {
        uint32_t key;
        ActorTask* obj;
        float p1[3], diff[3], d34, d38;
    };
    std::vector<Hit> hits;
    const float c[3] = { e.sphere[0], e.sphere[1], e.sphere[2] };
    const float r = e.radius128;
    for (Target& t : targetCopy) {
        ActorTask* a = t.obj;
        if (!a || a == &e || (a->flags34 & 0x80008000) || (a->flags34 & 0x10)) continue;
        if (a->radius128 == 0.0f) a->radius128 = a->radius;
        const float* oc = t.sphere;
        const float dz = c[2] - oc[2], dy = c[1] - oc[1], dx = c[0] - oc[0];
        const float d = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx));
        if (!(d <= r + a->radius128)) continue;
        int pitch, yaw;
        DirToAngles(oc[0] - c[0], oc[1] - c[1], oc[2] - c[2], &pitch, &yaw);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, c[0], c[1], c[2]);
        mx::RotY(m, yaw);
        mx::RotX(m, pitch);
        Hit h;
        const float l1[3] = { 0, 0, r };
        mx::Transform(m, l1, h.p1);
        DirToAngles(dx, dy, dz, &pitch, &yaw);
        m = Mat4::Identity();
        mx::Translate(m, oc[0], oc[1], oc[2]);
        mx::RotY(m, yaw);
        mx::RotX(m, pitch);
        float p2[3];
        const float l2[3] = { 0, 0, a->radius128 };
        mx::Transform(m, l2, p2);
        h.obj = a;
        h.key = static_cast<uint32_t>(static_cast<int32_t>(d * 10.0f));
        for (int k = 0; k < 3; ++k) h.diff[k] = p2[k] - h.p1[k];
        const float ez = c[2] - p2[2], ey = c[1] - p2[1], ex = c[0] - p2[0];
        h.d34 = static_cast<float>(std::sqrt(static_cast<double>(ex) * ex + static_cast<double>(ey) * ey + static_cast<double>(ez) * ez));
        const float fz = h.p1[2] - p2[2], fy = h.p1[1] - p2[1], fx = h.p1[0] - p2[0];
        h.d38 = static_cast<float>(std::sqrt(static_cast<double>(fy) * fy + static_cast<double>(fz) * fz + static_cast<double>(fx) * fx));
        hits.push_back(h);
    }
    if (hits.empty()) return false;
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return (a.key & 0xFFFF) < (b.key & 0xFFFF); });
    const Hit& h = hits[0];
    cac48 = h.d34;
    cac4c = h.d38;
    for (int k = 0; k < 3; ++k) cac64[k] = h.diff[k];
    if (cac4c <= h.obj->radius128) cac4c = r - cac48;
    else cac4c = cac48 + r;
    if (cac64[2] + cac64[1] + cac64[0] == 0.0f) return false;
    const float len = static_cast<float>(std::sqrt(static_cast<double>(cac64[2]) * cac64[2] + static_cast<double>(cac64[1]) * cac64[1] +
                                                   static_cast<double>(cac64[0]) * cac64[0]));
    for (float& v : cac64) v = v / len;  // 0x004AAA00
    pushObj = h.obj;  // 0x009C71C8
    h.obj->pushed = true;
    h.obj->pusherFlags = e.flags34;
    h.obj->pushAmount = cac4c;
    for (int k = 0; k < 3; ++k) h.obj->pushDir[k] = -cac64[k];
    return true;
}

}  // namespace game
