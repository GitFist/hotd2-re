// game::World - actor type 0x31 (0x00449620 -> task 0x00449910): the leaping zombies (skeletons 0x16..0x19; stage 1
// scene 6 on and stage 2). Own behaviour table 0x00592960, node draw 0x00449F90, post-root 0x00449D40, hits 0x004499A0,
// jump / knock-back paths (0x0044DB50 / 0x0044DC70 / 0x0044D860), attack choice 0x0044ADB0.
// Status per function and the verification against build\scene_capture_auto_st2.txt: notes/agents/stage2.md.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// 0x00409E00: angle a moved toward b by at most `step` (16-bit wrap)
uint32_t TurnToward(uint32_t a, uint32_t b, int step) {
    a &= 0xFFFF;
    b &= 0xFFFF;
    int d = static_cast<int>(a) - static_cast<int>(b);
    int ad = d < 0 ? static_cast<int>(b) - static_cast<int>(a) : d;
    int w1 = static_cast<int>(b) - static_cast<int>(a) + 0x10000;
    if (w1 < 0) w1 = d - 0x10000;
    int w2 = d + 0x10000;
    if (w2 < 0) w2 = static_cast<int>(b) - static_cast<int>(a) - 0x10000;
    if (step < ad && step < w1 && step < w2) {
        if (a < b) {
            if (w2 < ad) step = -step;
        } else if (ad <= w1) {
            return a - static_cast<uint32_t>(step);
        }
        return a + static_cast<uint32_t>(step);
    }
    return b;
}

// 0x0040A040: angle v within +-range of centre
bool InRange(uint32_t v, uint32_t centre, int range) {
    uint32_t hi = (centre & 0xFFFF) + 0x10000 + static_cast<uint32_t>(range);
    uint32_t lo = (centre & 0xFFFF) - static_cast<uint32_t>(range) + 0x10000;
    uint32_t a = lo;
    if (hi < lo) {
        a = hi;
        hi = lo;
    }
    v &= 0xFFFF;
    if (v < a) v += ((a - v) + 0xFFFF) & 0xFFFF0000u;
    return v <= hi;
}

}  // namespace

// The object (block 0x13F4); fields beyond EnemyTask named by their offsets
struct Actor31Task : EnemyTask {
    int hitCount = 0;         // +0x1328 (behaviour 2: knock-backs in a row)
    int jumpFrame = 0;        // +0x1330
    int jumpFrames = 0;       // +0x1334
    int fallFrames = 0;       // +0x1338
    int cooldown = 0;         // +0x133C (flag 0x100 cleared when it runs out)
    float pose134C = 0;       // +0x134C (0 / 1 / 2 / 3 by the record's lo bits 0x40 / 0x80 / 0x100)
    int landMaterial = 0;     // +0x1350 (0x009CAC40 at the landing)
    int f1354 = 0;            // +0x1354
    int band = 0;             // +0x135C
    int jumpState = 0;        // +0x1360 (0x0044D860)
    int poseClass = 0;        // +0x1364
    int lastSlot = 0;         // +0x1368
    float floorY = 0;         // +0x1378
    float f1384 = 0;          // +0x1384
    float fallFromY = 0;      // +0x1388 (behaviour 11: the height the fall started at, 0 once the landing motion runs)
    float alpha = 1.0f;       // +0x138C
    uint32_t path = 0;        // +0x1394 (behaviour 26: event-memory jump list)
    float from[3] = {};       // +0x13C0
    float to[3] = {};         // +0x13CC
    float f13D8[3] = {};      // +0x13D8
    float prev[3] = {};       // +0x13E4 (0x0044D260 type 0x5A trail)
    int8_t attackEntry = 0;   // +0x131A
    int throwHand = 0;        // +0x1358 (behaviour 31: 5 / 8)
    int jump[12] = {};        // 0x009C8AA0 + actor slot * 0x30: {motion, start, n} x 4 thresholds (0x0044DA60)
    // stage 5 (skeleton 0x18, notes/agents/stage5.md)
    int f132C = 0;            // +0x132C (setup with +0x136C & 8: the record yaw)
    float f137C = 0, f1380 = 0;  // +0x137C / +0x1380 (behaviour 27: the players' point; z is +0x1384)
    // stage 6 (world_st6_actor31.inl)
    float f1344 = 0;          // +0x1344 (behaviour 34: frames per step)
    float f1348 = 0;          // +0x1348 (behaviour 34: steps left)
    float surf[3] = {};       // last 0x0044C370 result (behaviour 33)
    float Alpha138C() const override { return alpha; }
};

namespace {

Actor31Task* Cur(World::Impl& w) {
    WTask* t = w.CurrentTask();
    return t && t->fn == 0x00449910 ? static_cast<Actor31Task*>(t) : nullptr;
}

// 0x0044CFF0 / 0x0044D020: leave the live-enemy / attacker counts once
void LeaveLive(World::Impl& w, Actor31Task& a) {
    if (!(a.flags136C & 0x800000)) {
        a.flags136C |= 0x800000;
        --w.liveEnemies;
    }
}
void LeaveAttackers(World::Impl& w, Actor31Task& a) {
    if (!(a.flags136C & 0x1000000)) {
        a.flags136C |= 0x1000000;
        --w.attackers;
    }
}

// 0x0044D050: dead (0x4000000) or hp < 1 -> not registered any more (0x10000) unless the last attacker, live count
void DeadCounts(World::Impl& w, Actor31Task& a) {
    const uint32_t f = a.flags34;
    if ((f & 0x4000000) || a.hp < 1) {
        if (!(f & 0x800000) || w.attackers != 1) {
            a.flags34 = f | 0x10000;
            if (a.slot120 != -1) w.lockOn.Release(a.slot120);
        }
        LeaveLive(w, a);
    }
}

// 0x0044CFB0: the attacked player's slot 0x009A2BA0 freed; attack slot 0x009A34F0 freed
void ReleaseTarget(World::Impl& w, Actor31Task& a) {
    if (a.target != -1) {
        w.targetTaken[a.target & 1] = 0;
        a.target = -1;
    }
    if (a.flags136C & 0x8000) {
        a.flags136C &= ~0x8000u;
        w.attackSlot = 0;
    }
}

// 0x0044E120
void StopMoving(Actor31Task& a) {
    for (int k = 0; k < 3; ++k) a.acc[k] = a.vel[k] = 0;
}

// 0x0044DA60: the actor slot's jump motion table
void SetJumpTable(World::Impl& w, Actor31Task& a, uint32_t va) {
    for (int k = 0; k < 12; ++k) a.jump[k] = static_cast<int>(w.exe.U32(va + 4 * k));
}

// 0x0044DB50: jump from -> to; frames = distance / (15 / n) (n = 10 when 0x2000000 and not dead / shot, else 15)
void JumpTo(Actor31Task& a, const float from[3], const float to[3], const ExeImage& exe) {
    const float dx = from[0] - to[0], dz = from[2] - to[2];
    const float d = static_cast<float>(std::sqrt(static_cast<float>(dz * dz + dx * dx)));
    memcpy(a.from, from, 12);
    memcpy(a.to, to, 12);
    a.jumpFrame = 0;
    const int n = ((a.flags136C & 0x2000000) && !(a.flags34 & 0x44000000)) ? 10 : 15;
    const float k = F32(exe, 0x0055CCD4) / static_cast<float>(n);
    int f = static_cast<int>(d / k);
    a.jumpFrames = f < n ? n : f;
}

// 0x0044DC70: frames = ftol(distance * mult), rounded down to a multiple of mult
void JumpToMult(Actor31Task& a, const float from[3], const float to[3], int mult) {
    const float dx = from[0] - to[0], dz = from[2] - to[2];
    const float d = static_cast<float>(std::sqrt(static_cast<float>(dz * dz + dx * dx)));
    memcpy(a.from, from, 12);
    memcpy(a.to, to, 12);
    a.jumpFrame = 0;
    const int f = static_cast<int>(d * static_cast<float>(mult));
    a.jumpFrames = f - f % mult;
}

// 0x0044DDE0: velocity of the next jump frame (vel.y with the gravity term 0x00565E1C / 0x00565E20); false when done
bool JumpVelocity(World::Impl& w, Actor31Task& a) {
    const int i = a.jumpFrame, n = a.jumpFrames;
    a.jumpFrame = i + 1;
    if (i >= n) return false;
    a.vel[0] = (a.to[0] - a.from[0]) / static_cast<float>(n);
    const float dy = a.to[1] - a.from[1];
    a.vel[1] = static_cast<float>(i + 1) * F32(w.exe, 0x00565E20) +
               (static_cast<float>(n * n) * F32(w.exe, 0x00565E1C) + dy + dy) / static_cast<float>(n * 2);
    a.vel[2] = (a.to[2] - a.from[2]) / static_cast<float>(n);
    return true;
}

// 0x0044DD00: position on the jump parabola, step frames
bool JumpStep(World::Impl& w, Actor31Task& a, int step) {
    const int n = a.jumpFrames, i = a.jumpFrame;
    if (i >= n + step) return false;
    const float t = static_cast<float>(i);
    a.pos[0] = ((a.to[0] - a.from[0]) / static_cast<float>(n)) * t + a.from[0];
    const float dy = a.to[1] - a.from[1];
    const float g = static_cast<float>(n * n) * F32(w.exe, 0x00565E1C);
    a.jumpFrame = i + step;
    a.pos[1] = (((g + dy + dy) * t) / static_cast<float>(n * 2) -
                static_cast<float>(i * i) * F32(w.exe, 0x00565E1C) * F32(w.exe, 0x004C43AC)) + a.from[1];
    a.pos[2] = ((a.to[2] - a.from[2]) / static_cast<float>(n)) * t + a.from[2];
    return true;
}

}  // namespace
namespace {

int PoseIdx(uint32_t f136C) {  // (0x100 ? 3) + (0x80 ? 2) + (0x40 ? 1)
    return static_cast<int>(((f136C >> 8) & 1) * 3 + ((f136C >> 7) & 1) * 2 + ((f136C >> 6) & 1));
}
bool Leaper(const Actor31Task& a) { return a.anim.kind > 0x15 && a.anim.kind < 0x1A; }
uint32_t Row(World::Impl& w, uint32_t table, int variant) { return w.exe.U32(table + 4 * static_cast<uint32_t>(variant)); }

// 0x00449E80: collision sphere centre (+0x12C..)
void SphereCentre(World::Impl& w, Actor31Task& a) {
    a.sphere[0] = a.pos[0];
    if (a.flags136C & 0x100) {
        const float f = a.radius128 * F32(w.exe, 0x00565DE4);
        a.sphere[2] = a.pos[2];
        a.sphere[1] = a.pos[1] - f;
    } else if (!(a.flags136C & 0xC0)) {
        const float f = a.radius128 * F32(w.exe, 0x00565DE4);
        a.sphere[2] = a.pos[2];
        a.sphere[1] = f + a.pos[1];
    } else {
        a.sphere[1] = a.pos[1];
        a.sphere[2] = a.pos[2];
    }
}

// 0x00409D80: material under (x, y, z) (segment y - 1000 .. y)
uint32_t MaterialAt(World::Impl& w, float x, float y, float z) {
    w.cac40 = 0;
    const float s[3] = { x, y - F32(w.exe, 0x004C49C4), z }, e[3] = { x, y, z };
    w.CollSegmentAll(s, e);
    return w.cac40;
}

// 0x00409FE0: turn toward (x, y, z) by `step`; true when within `range` afterwards
bool TurnToPoint(Actor31Task& a, float x, float y, float z, int step, int range) {
    int pitch = 0, yaw = 0;
    DirToAngles(a.pos[0] - x, a.pos[1] - y, a.pos[2] - z, &pitch, &yaw);
    const uint32_t v = TurnToward(static_cast<uint32_t>(a.rot[1]), static_cast<uint32_t>(yaw), step);
    a.rot[1] = static_cast<int>(v);
    return InRange(v, static_cast<uint32_t>(yaw), range);
}

// 0x00409F90: turn toward (x, z) by `step` (negative: away)
void TurnToXZ(Actor31Task& a, float x, float z, int step) {
    int pitch = 0, yaw = 0;
    DirToAngles(a.pos[0] - x, 0.0f, a.pos[2] - z, &pitch, &yaw);
    a.rot[1] = static_cast<int>(TurnToward(static_cast<uint32_t>(a.rot[1]), static_cast<uint32_t>(yaw), step));
}

// 0x0044D5F0: stretch / squeeze the jump motion table to the path's frame count
void FitJump(Actor31Task& a) {
    int* j = a.jump;
    const int n = a.jumpFrames;
    const int e = (j[5] - j[7]) + n;
    if (e == 0) return;
    if (e > 0) {
        while (j[10] + j[6] < e) {
            ++j[6];
            ++j[10];
        }
        if (e < j[10] + j[6]) --j[6];
        if (j[6] > 0x7F) j[6] = 0x7F;
        if (j[10] > 0x7F) j[10] = 0x7F;
    }
    if (e < 0) {
        j[6] = 1;
        j[10] = 1;
        for (int g = 0; g < 4096; ++g) {
            if (((j[10] + j[6]) - j[5]) + j[7] <= n && (j[7] - j[5]) < 2) break;
            ++j[5];
            --j[7];
        }
        if (j[7] - j[5] < 1) --j[5];
    }
}

// 0x0044E140 (skeleton 0x19)
void FitJump19(World::Impl& w, Actor31Task& a) {
    int* j = a.jump;
    const int nB = j[6], t2 = j[7];
    int e = (((a.jumpFrames - j[10]) - nB) - t2) + j[5];
    float hf = static_cast<float>(e) * F32(w.exe, 0x004C43AC);
    if (hf < 0.0f) hf = hf * F32(w.exe, 0x004C4C64);
    const int h = static_cast<int>(hf);
    if (e < 1) {
        if (t2 < j[5] + h) j[5] = t2;
        else j[5] += h;
        if (j[11] < h + j[9]) j[9] = j[11];
        else j[9] += h;
        if (e < 0) e = -e;
        int r = (e - j[9]) - j[5];
        if (r > 0) {
            r = j[5] + r;
            j[5] = t2 < r ? t2 : r;
        }
    } else {
        j[6] = nB + h;
        j[10] += h;
        const int r = (e - j[10]) - j[6];
        if (r > 0) j[6] += r;
    }
}

void Effect5A(World::Impl& w, Actor31Task& a, int type);

// 0x0044D860: the jump's motion sequence (take-off, flight, landing); 0 when the landing motion reached its tick
int JumpMotion(World::Impl& w, Actor31Task& a, int step) {
    int* j = a.jump;
    switch (a.jumpState) {
    case 0:
        if (Leaper(a) && a.behaviour != 10) {
            if (a.flags34 & 0x100) a.flags136C |= 0x200;
            a.flags34 |= 0x100;
        }
        if (a.anim.kind == 0x19) FitJump19(w, a);
        else FitJump(a);
        w.AnimBlendTo(a, j[0], j[1], j[2]);
        ++a.jumpState;
        [[fallthrough]];
    case 1:
        if (a.anim.tick[0] < j[3]) return 1;
        w.AnimBlendTo(a, j[4], j[5], j[6]);
        if (Leaper(a)) {
            if (!(a.flags136C & 0x200)) a.flags34 &= ~0x100u;
            a.flags136C &= ~0x200u;
        }
        ++a.jumpState;
        [[fallthrough]];
    case 2:
        JumpStep(w, a, step);
        if (a.anim.tick[0] < j[7]) return 1;
        w.AnimBlendTo(a, j[8], j[9], j[10]);
        a.jumpFrame -= step;
        ++a.jumpState;
        [[fallthrough]];
    case 3:
        if (JumpStep(w, a, step)) return 1;
        Effect5A(w, a, 0x50);
        ++a.jumpState;
        [[fallthrough]];
    case 4:
        if (a.anim.tick[0] < j[11]) return 1;
        if (Leaper(a)) a.flags136C |= 0x180000;
        return 0;
    default:
        return 1;
    }
}

// 0x0044B610: jump table of the attack entry +0x131A and the pose class
void AttackJumpTable(World::Impl& w, Actor31Task& a) {
    const uint32_t row = Row(w, 0x00592A10, a.variant);
    const uint32_t f = a.flags136C;
    const int pose = static_cast<int>(((f >> 8) & 1) * 3 + (((f >> 7) & 1) + ((f >> 0x11) & 1) * 2) * 2 + ((f >> 6) & 1));
    SetJumpTable(w, a, w.exe.U32(row + static_cast<uint32_t>(a.attackEntry + pose * 4) * 0x10));
}

// 0x0044D780: jump to `to` (skeleton 0x19: 0x0044DB50, else 0x0044DC70 with `mult`); table == 0 (0x007DCC70): a random
// attack entry 0x00592A20[variant][rand / 16 % 10 + (lost parts & 7) * 10] and its jump table
void JumpWith(World::Impl& w, Actor31Task& a, const float to[3], uint32_t table, int mult) {
    if (a.anim.kind == 0x19) JumpTo(a, a.pos, to, w.exe);
    else JumpToMult(a, a.pos, to, mult);
    if (table != 0) {
        SetJumpTable(w, a, table);
        a.jumpState = 0;
        return;
    }
    const int r = w.Rand(0);
    const uint32_t idx = static_cast<uint32_t>((r >> 4) % 10 + (a.lostParts & 7) * 10);
    a.attackEntry = static_cast<int8_t>(w.exe.S16(Row(w, 0x00592A20, a.variant) + idx * 4) & 0xFF);
    if (a.anim.kind == 0x18) a.attackEntry = 3;
    AttackJumpTable(w, a);
    a.jumpState = 0;
}

// 0x0044CA40: pick the player to attack (+0x121); true when one was taken
bool PickTarget(World::Impl& w, Actor31Task& a) {
    a.target = -1;
    if (w.attackSlot == 0) {
        if (w.route == 1) {
            int pig = w.playersInGame;
            if (pig == 0) {
                if (w.targetTaken[0] == 0) a.target = 0;
                pig = w.playersInGame;
            }
            if (pig == 1 && w.targetTaken[1] == 0) a.target = 1;
        } else if (w.route == 2) {
            if (w.players == 1) {
                const int s = w.Rand(0x0044CA7D) & 1;
                if (w.targetTaken[s] == 0) a.target = static_cast<int8_t>(s);
            } else if (w.attackers == 1) {
                int s = w.Rand(0x0044CAB2) & 1;
                if (w.targetTaken[s] != 0) s = ~s;
                a.target = static_cast<int8_t>(s);
            } else {
                const float side = w.focal * a.viewPos[0] / a.viewPos[2];  // 0x00409C90
                if (!(0.0f <= side)) {  // 0x00409C90 returned 1: player 2's side (the shared check of the route-1 path)
                    if (w.targetTaken[1] == 0) a.target = 1;
                } else if (w.targetTaken[0] == 0) {
                    a.target = 0;
                }
            }
        }
        // 0x00409DC0: the target must be playing (camera mode 2)
        const bool ok = a.target >= 0 && a.target <= 1 && w.runner->camera.modeCur == 2 &&
                        (w.gameMode == 5 || w.player[a.target].state == 5);
        if (!ok) a.target = -1;
    }
    if (a.target != -1) {
        if (!w.OnScreen(a.viewPos)) {  // 0x00409C10
            a.flags136C |= 0x8000;
            w.attackSlot = 1;
        }
        w.targetTaken[a.target & 1] = 1;
        return true;
    }
    return false;
}

void ChooseNext(World::Impl& w, Actor31Task& a);

// 0x0044BEF0(dir): a wall beside (dir -1 left / +1 right) to jump onto: the players face the same way (+-0x2000) and
// flag 2 clear; the point 0x00564420 * dir to the side at a random height (rand % 20 + 0x00565DF4 above the floor) must
// hit a collision face; the landing point is 0x00565DF0 * dir from the hit
bool WallSide(World::Impl& w, Actor31Task& a, float dir) {
    if (!InRange(static_cast<uint32_t>(w.runner->camera.playerYaw) & 0xFFFF, static_cast<uint32_t>(a.rot[1]) & 0xFFFF, 0x2000) || (a.flags136C & 2))
        return false;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotY(m, a.rot[1]);
    const float l[3] = { dir * F32(w.exe, 0x00564420), 0.0f, 0.0f };
    float p[3];
    mx::Transform(m, l, p);
    int mat = 0;
    const float floor = w.FloorHeight(a.pos[0], a.pos[1] + F32(w.exe, 0x00565DE8), a.pos[2], &mat);
    const int r = w.Rand(0x0044BFC4);
    const float y = static_cast<float>(r % 0x14) + F32(w.exe, 0x00565DF4) + floor;
    const float s[3] = { p[0], y, p[2] }, e[3] = { a.pos[0], y, a.pos[2] };
    if (w.CollSegmentAll(s, e) == 0) return false;
    m = Mat4::Identity();
    mx::Translate(m, w.cac54, w.cac58, w.cac50);
    mx::RotY(m, a.rot[1]);
    const float l2[3] = { dir * F32(w.exe, 0x00565DF0), 0.0f, 0.0f };
    mx::Transform(m, l2, a.f13D8);
    return true;
}

// 0x0044C0B0(1.0): a ceiling above (up 0x004C49C4) to jump onto
bool Ceiling(World::Impl& w, Actor31Task& a) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotY(m, a.rot[1]);
    const float l[3] = { 0.0f, F32(w.exe, 0x004C49C4), 0.0f };
    float p[3];
    mx::Transform(m, l, p);
    if (w.CollSegmentAll(p, a.pos) == 0) return false;
    a.f13D8[0] = w.cac54;
    a.f13D8[1] = w.cac58;
    a.f13D8[2] = w.cac50;
    return true;
}

// 0x0044AFB0: try behaviour b (true: taken)
bool TryBehaviour(World::Impl& w, Actor31Task& a, int b) {
    if (b == a.behaviour) return false;
    auto take = [&] {
        a.behaviour = static_cast<int16_t>(b);
        a.sub = 0;
        return true;
    };
    switch (b) {
    case 7: return take();
    case 8: {
        const bool r = !PickTarget(w, a);
        if (r) take();
        return r;
    }
    case 9: {
        const bool r = PickTarget(w, a);
        if (r) take();
        return r;
    }
    case 0xC: case 0xD:
        if (a.anim.kind == 0x18 || !PickTarget(w, a)) return false;
        return take();
    case 0xE: case 0xF:
        if ((a.flags136C & 0x20) || !WallSide(w, a, b == 0xE ? -1.0f : 1.0f)) return false;
        return take();
    case 0x10:
        if ((a.flags136C & 0x20) || !Ceiling(w, a)) return false;
        return take();
    case 0x1D: case 0x1E: {
        if (a.behaviour != 7) return false;
        bool r = false;  // 0x0044F720: an arm model of skeleton 0x16 / 0x18 lost
        const int m5 = a.anim.nodes[5].model, m8 = a.anim.nodes[8].model;
        if (a.anim.kind == 0x16) r = m5 != 0x1FA2 && m8 != 0x1F9E && (m5 == 0x1F9F || m8 == 0x1F9B);
        else if (a.anim.kind == 0x18) r = m5 != 0x1FF3 && m8 != 0x1FEF && (m5 == 0x1FF1 || m8 == 0x1FED);
        if (!r) return false;
        return take();
    }
    case 0x1F: {  // 0x0044F5D0: a throwing weapon still in hand (skeleton 0x16: 0x1FA2 / 0x1F9E, 0x18: 0x1FF3 / 0x1FEF)
        const int m5 = a.anim.nodes[5].model, m8 = a.anim.nodes[8].model;
        bool r = false;
        if (a.anim.kind == 0x16) r = m5 == 0x1FA2 || m8 == 0x1F9E;
        else if (a.anim.kind == 0x18) r = m5 == 0x1FF3 || m8 == 0x1FEF;
        if (!r || !PickTarget(w, a)) return false;
        return take();
    }
    case 0x20:
        if (!PickTarget(w, a)) return false;
        if (MaterialAt(w, a.pos[0], a.pos[1] + F32(w.exe, 0x00565DE8), a.pos[2]) != 0x35) return false;
        return take();
    default:
        return false;  // other values: not taken (0x0044AFB0 default)
    }
}

// 0x0044ADB0: next behaviour by the distance to the players (bands 0x0055D2AC / 0x004ECB7C, near limit 0x0055CCD4 /
// 0x004C4398): walk up (8), or a random attack from 0x00592A60[variant][band][rand / 16 % 10 + (lost parts & 7) * 10]
void ChooseNext(World::Impl& w, Actor31Task& a) {
    int band = 2;
    const float dz = a.pos[2] - w.playerPos[2], dx = a.pos[0] - w.playerPos[0];
    const float d = static_cast<float>(std::sqrt(static_cast<float>(dz * dz + dx * dx)));
    const float far = F32(w.exe, 0x0055D2AC), mid = F32(w.exe, 0x004ECB7C);
    float limit = F32(w.exe, 0x0055CCD4);
    if (a.anim.kind == 0x17) {
        if (MaterialAt(w, a.pos[0], a.pos[1] + F32(w.exe, 0x00565DE8), a.pos[2]) == 0x35) limit = F32(w.exe, 0x004C4398);
        if (d <= far) band = mid < d ? 1 : 2;
        else band = 2;
    } else {
        if (d <= far && mid < d) band = 1;
    }
    if (d <= limit || band == 0) {
        if (a.behaviour != 8) {
            a.behaviour = 8;
            a.sub = 0;
        }
    } else if (!(a.flags136C & 0x2000)) {
        const int r = w.Rand(0);
        const uint32_t list = w.exe.U32(Row(w, 0x00592A60, a.variant) + static_cast<uint32_t>(band) * 4);
        const int b = static_cast<int>(w.exe.U32(list + static_cast<uint32_t>((r >> 4) % 10 + (a.lostParts & 7) * 10) * 4));
        if (TryBehaviour(w, a, b)) {
            a.band = band;
            a.flags136C |= 0x2000;
            return;
        }
        if (a.behaviour != 7) {
            a.behaviour = 7;
            a.sub = 0;
        }
    } else if (band != a.band) {
        a.flags136C &= ~0x2000u;
    }
}

// 0x0044D0A0: dead on the ground -> behaviour 4 (sink)
void Grounded(World::Impl& w, Actor31Task& a) {
    a.flags136C &= ~0x180000u;
    a.flags34 |= 0xC001;
    if (!(a.flags38 & 1)) LeaveAttackers(w, a);
    --a.anim.counter[0];
    a.behaviour = a.anim.kind != 0x18 ? 4 : 5;
    a.sub = 0;
}

// 0x0044D260(0x46 landing dust / 0x50 landing splash / 0x5A trail)
void Effect5A(World::Impl& w, Actor31Task& a, int type) {
    if (type == 0x46) {
        if (a.flags136C & 0x4000) return;
        float blk[3] = { a.pos[0], a.pos[1], a.pos[2] };
        // +0x1350 >= 1: angles of the floor normal 0x009CAC64 (0x00401870) into the block's +0xC / +0x14; dead, since
        // both effects face the camera (face mode 1 / 2), which replaces +0xC / +0x10, and 0x004073B0 never reads +0x14
        const int pitch = 0, yaw = 0;
        const bool water = w.rainFlag == 1 || w.cac40 == 0x37 || w.cac40 == 5;
        w.SpawnEffectAt(blk, pitch, yaw, water ? 0x61 : 0x46, water ? 1 : 2, -1);
        a.flags136C |= 0x4000;
        return;
    }
    if (type == 0x50) {
        if (!(a.flags34 & 0x10000000) && (a.flags34 & 0x20000000) && Leaper(a)) {
            if (w.rainFlag == 1) {
                w.SpawnEffectAt(a.pos, 0, 0, 0x61, 1, -1);
            } else {  // 0x004073A0 block: scale (0.4, 2, 0.2), type 0x4B facing the camera (angles not set)
                EffectSpawn in;
                memcpy(in.pos, a.pos, 12);
                in.scale[0] = 0.4f;
                in.scale[1] = 2.0f;
                in.scale[2] = Bits(0x3E4CCCCD);
                in.type = 0x4B;
                in.faceMode = 1;
                w.SpawnEffectBlock(in);
            }
        }
    } else if (type != 0x5A) {
        return;
    }
    if (a.anim.kind == 0x16 && a.anim.motion[0] == static_cast<int>(w.exe.U32(Row(w, 0x005929F4, 0) + ((a.flags34 >> 0x1B) & 1) * 4 + 8)) &&
        (a.anim.tick[0] == 0x19 || a.anim.tick[0] == 0x32 || a.behaviour != 7)) {
        // skeleton 0x16 footprints: two marks (yaw +0x4000 / +0xC000) between the last step (+0x13E4) and here
        const float px = a.prev[0], py = a.prev[1], pz = a.prev[2];
        const float x = a.pos[0], y = a.pos[1], z = a.pos[2];
        memcpy(a.prev, a.pos, 12);
        EffectSpawn in;
        in.pos[0] = (x + px) * 0.5f;
        in.pos[1] = (y + py) * 0.5f;
        in.pos[2] = (z + pz) * 0.5f;
        int pitch = 0, yaw = 0;
        DirToAngles(x - px, y - py, z - pz, &pitch, &yaw);  // 0x004016B0
        in.pitch = pitch;
        in.yaw = yaw + 0x4000;
        const float dz = pz - z, dy = py - y, dx = px - x;
        in.scale[0] = std::sqrt((dx * dx + dy * dy) + dz * dz) * F32(w.exe, 0x00565E18);
        in.scale[1] = Bits(0x3F4CCCCD);
        in.scale[2] = Bits(0x3E4CCCCD);
        const uint32_t mat = MaterialAt(w, in.pos[0], static_cast<float>(static_cast<double>(in.pos[1]) + 5.0), in.pos[2]);  // qword 0x00565E10
        in.type = (mat == 0x37 || mat == 5) ? 0x61 : 0x4B;
        in.faceMode = 0;
        w.SpawnEffectBlock(in);  // 0x004073A0
        in.yaw = yaw + 0x4000 + 0x8000;
        w.SpawnEffectBlock(in);
    }
}

// 0x0044D120: knock-back target 15 / view distance units further away from the camera (0.75 when dead)
void KnockBack(World::Impl& w, Actor31Task& a) {
    const float len = static_cast<float>(std::sqrt(static_cast<float>(a.viewPos[2] * a.viewPos[2] + a.viewPos[1] * a.viewPos[1] + a.viewPos[0] * a.viewPos[0])));
    float k = (F32(w.exe, 0x004C4398) / len) * F32(w.exe, 0x004C43A4);
    if (k < 0.0f) k = 0.0f;
    if (a.flags34 & 0x4000000) k = k * F32(w.exe, 0x004C4CB8);
    const float v[3] = { a.viewPos[0], a.viewPos[1], a.viewPos[2] - k };
    float out[3];
    mx::Transform(w.cam.inverse, v, out);
    if (a.anim.kind == 0x18 && (a.flags136C & 0xC0)) out[1] = a.pos[1];
    JumpTo(a, a.pos, out, w.exe);
}

// 0x0044CBA0: landing point in front of the camera (screen offset by target side) for the attack jump
void LandingPoint(World::Impl& w, Actor31Task& a, float out[3]) {
    float z = -15.5f, y = F32(w.exe, 0x00565E08);
    if (a.behaviour != 0x16) {
        if (a.behaviour == 0x17) z = -6.0f;
        else y = a.anim.kind == 0x16 ? F32(w.exe, 0x00565E0C) : F32(w.exe, 0x004C49CC);
    }
    float x = F32(w.exe, 0x00565E00);
    if (a.target != 0) x = a.target == 1 ? F32(w.exe, 0x00565E04) : 0.0f;
    if (w.route == 1) x = 0.0f;
    const float v[3] = { (x * z) / w.focal, (y * z) / w.focal, z };
    mx::Transform(w.cam.inverse, v, out);
    if (a.anim.kind == 0x18) w.Note("actor 0x31 skeleton 0x18 landing point (0x0044CBA0) not ported");
}

// 0x0044CE60: the attack lands at its tick: Player_TakeHit (and the attacker leaves when 0x2000000)
void AttackHit(World::Impl& w, Actor31Task& a, Task& t) {
    if (a.flags136C & 0x400) {  // the entries of 0x00592A00 {s16 motion, .., +8 s16 tick, +0xA s16 kind, +0xC u16 parts}
        const uint32_t e = Row(w, 0x00592A00, a.variant) + static_cast<uint32_t>(a.attackEntry) * 0x10;
        const int16_t mask = w.exe.S16(e + 0xC);
        if (a.anim.tick[0] == w.exe.S16(e + 8) && (static_cast<int16_t>(a.lostParts & 7) & mask) != mask) {
            const int kind = w.exe.S16(e + 0xA);
            if (a.flags34 & 0x2000000) {
                w.PlayerTakeHit(a.target, 0, kind);
                LeaveLive(w, a);  // 0x0044AD60
                LeaveAttackers(w, a);
                ReleaseTarget(w, a);
                if (a.slot120 != -1) w.lockOn.Release(a.slot120);
                a.flags34 &= ~1u;
                w.EnemyDestroy(a);
                return;
            }
            w.PlayerTakeHit(a.target, 1, kind);
        }
        return;
    }
    const uint32_t row = Row(w, 0x00592A10, a.variant);
    const uint32_t e = row + static_cast<uint32_t>(a.attackEntry + a.poseClass * 4) * 0x10;
    const int tick = static_cast<int>(w.exe.U32(e + 4));
    const uint32_t mask = w.exe.U32(e + 0xC);
    if ((a.anim.tick[0] == tick || (tick == -1 && a.jumpState == 4)) && ((a.lostParts & mask & 7) != mask)) {
        const int kind = static_cast<int>(w.exe.U32(e + 8));
        if (a.flags34 & 0x2000000) {
            w.PlayerTakeHit(a.target, 0, kind);
            a.flags136C |= 0x800;
            // 0x0044AD60: leaves the counts, frees its target and lock-on slot, Actor_Destroy
            LeaveLive(w, a);
            LeaveAttackers(w, a);
            ReleaseTarget(w, a);
            if (a.slot120 != -1) w.lockOn.Release(a.slot120);
            a.flags34 &= ~1u;
            w.EnemyDestroy(a);
            return;
        }
        w.PlayerTakeHit(a.target, 1, kind);
        a.flags136C |= 0x800;
    }
    (void)t;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Behaviours (0x00592960)
// ---------------------------------------------------------------------------------------------------------------------
namespace {

// 0x0044F630: the hand that throws (5 / 8, 0 none) for skeletons 0x16 (0x1FA2 / 0x1F9E) and 0x18 (0x1FF3 / 0x1FEF)
int ThrowHand(World::Impl& w, Actor31Task& a) {
    int m5w, m8w;
    uint32_t site;
    if (a.anim.kind == 0x16) {
        m5w = 0x1FA2;
        m8w = 0x1F9E;
        site = 0x0044F6B7;  // (the rand of the skeleton-0x16 block; 0x0044F652 is skeleton 0x18's)
    } else if (a.anim.kind == 0x18) {
        m5w = 0x1FF3;
        m8w = 0x1FEF;
        site = 0x0044F652;
    } else {
        return 0;
    }
    const bool h5 = a.anim.nodes[5].model == m5w, h8 = a.anim.nodes[8].model == m8w;
    int r = 0;
    if ((w.Rand(site) & 1) == 0) {
        if (h8) r = 8;
        if (h5) r = 5;
    } else {
        if (h5) r = 5;
        if (h8) return 8;
    }
    return r;
}

void SpawnWeapon31(World::Impl& w, Actor31Task& a);  // 0x004504E0 (end of this file)

// 31: 0x0044FAF0 throw a weapon at the players (motion 0x00592A00[variant][hand != 5], the weapon leaves at the
// entry's tick: 0x004504E0), then walk (7)
void Beh31(World::Impl& w, Actor31Task& a) {
    auto entry = [&](int i) { return Row(w, 0x00592A00, a.variant) + static_cast<uint32_t>(i) * 0x10; };
    uint32_t ent = entry(a.attackEntry);
    if (a.sub == 0) {
        if (a.target == -1 && !PickTarget(w, a)) a.target = 0;
        const int hand = ThrowHand(w, a);
        a.attackEntry = hand != 5 ? 1 : 0;
        a.throwHand = hand;  // +0x1358
        ent = entry(a.attackEntry);
        int motion = w.exe.S16(ent), start = 0x1A;
        if (a.anim.kind == 0x18) {
            start = 0;
            static const int kMotion[4][2] = { { 0x1F7, 0x1F6 }, { 0x1FC, 0x1FB }, { 0x1F2, 0x1F1 }, { 0x204, 0x203 } };
            const int pi = PoseIdx(a.flags136C);
            if (pi <= 3) motion = kMotion[pi][a.attackEntry];
            else w.Note("actor 0x31 skeleton 0x18 throw with pose index > 3 (0x0044FAF0 default case)");
            a.landMaterial = 0x19;  // +0x1350: throw tick
        }
        w.AnimBlendTo(a, motion, start, 4);
        ++a.sub;
    } else if (a.sub != 1 && a.sub != 2) {
        return;
    }
    if (a.sub == 1) {
        if (a.anim.kind == 0x18 ? a.anim.tick[0] != a.landMaterial : a.anim.tick[0] != w.exe.S16(ent + 8)) return;
        SpawnWeapon31(w, a);
        ++a.sub;
    }
    if (w.MotionTicks(a.anim.motion[0]) - 1 <= a.anim.tick[0]) {
        w.Sound(0x2916A9);
        a.behaviour = 7;
        a.sub = 0;
    }
}

// 2: 0x0044A450 shot: knocked back (jump path), falls, lands, recovers or dies
void Beh2(World::Impl& w, Actor31Task& a) {
    const int motion = static_cast<int>(w.exe.U32(Row(w, 0x005929F0, a.variant) + 0x14));
    auto bodyOff = [&](int t16, int t17, int t18) {
        const int k = a.anim.kind;
        const int lim = k == 0x16 ? t16 : k == 0x17 ? t17 : t18;
        if (k >= 0x16 && k <= 0x19 && a.anim.tick[0] > lim) a.flags34 |= 0x4000;
    };
    switch (a.sub) {
    case 0: {
        const uint32_t f = a.flags136C;
        a.flags136C = (f & 0xFFFFFE1Fu) | 0x180000;
        a.flags34 = (a.flags34 & ~0x4000u) | 0x200000;
        if (!(f & 0x400000)) {
            StopMoving(a);
            w.AnimBlendTo(a, motion, 0, 5);
            a.hitCount = 0;
        } else {
            if (a.anim.kind != 0x16) w.AnimSetMotion(a, motion);
            ++a.hitCount;
        }
        if (!(a.flags34 & 0x2000) && a.hitCount < 2) KnockBack(w, a);
        else a.flags34 |= 0x2000;
        DeadCounts(w, a);
        ++a.sub;
    }
        [[fallthrough]];
    case 1:
        bodyOff(0x22, 0x13, 0x2B);
        if (JumpVelocity(w, a)) return;
        if (a.anim.kind == 0x16) StopMoving(a);
        else if (a.anim.kind > 0x16 && a.anim.kind < 0x1A) w.AnimBlendTo(a, motion, 0, 5);
        {  // +0x5C = 0xBD5F0123 (gravity)
            const uint32_t bits = 0xBD5F0123u;
            memcpy(&a.acc[1], &bits, 4);
        }
        ++a.sub;
        a.flags34 &= ~0x4000u;
        a.fallFrames = 0;
        [[fallthrough]];
    case 2: {
        bodyOff(0x2F, 0x13, 0x2B);
        const float y = a.pos[1] + F32(w.exe, 0x00565DE8);
        ++a.fallFrames;
        int mat = 0;
        const float f = w.FloorHeight(a.pos[0], y, a.pos[2], &mat);
        a.floorY = f;
        if (!(a.pos[1] + a.vel[1] <= f) && a.fallFrames <= 0x77) return;
        a.pos[1] = f;
        a.landMaterial = static_cast<int>(w.cac40);
        a.vel[1] = a.vel[1] * F32(w.exe, 0x0055D2D8);
        a.vel[0] = a.vel[0] * F32(w.exe, 0x004C43AC);
        a.vel[2] = a.vel[2] * F32(w.exe, 0x004C43AC);
        Effect5A(w, a, 0x46);
        w.Sound(0x2716A9);
        float vy = a.vel[1];
        if (vy < 0.0f) vy = vy * F32(w.exe, 0x004C4C64);
        if (F32(w.exe, 0x004C4D08) < vy && a.fallFrames < 0x78 && a.anim.kind != 0x16) {
            a.flags34 &= ~0x4000u;
            return;
        }
        StopMoving(a);
        a.flags136C &= ~0x4000u;
        a.flags34 = (a.flags34 & 0xFFFF9FFFu) | 0x100;
        a.jumpFrame = (w.Rand(0) % 10 + 1) * 3;  // +0x1330
        ++a.sub;
    }
        [[fallthrough]];
    case 3:
        if (!(a.flags34 & 0x4000000) && a.landMaterial != 0x5A) {
            if (a.anim.kind == 0x16) {
                if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 1) return;
            } else if (0 < --a.jumpFrame) {
                return;
            }
            if (a.anim.kind != 0x17) w.AnimBlendTo(a, 0x11B, 0, 5);
            ++a.sub;
        } else {
            if (!a.anim.ended) return;
            if (a.landMaterial == 0x5A) {
                a.flags34 |= 0x4000000;
                DeadCounts(w, a);
                w.Note("actor 0x31 landing in material 0x5A (stage 4 scene 4 splash) reached");
            }
            if (a.anim.kind != 0x16) {
                Grounded(w, a);
                return;
            }
            a.sub = 0;
            a.behaviour = 3;
            return;
        }
        [[fallthrough]];
    case 4: {
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 2) return;
        const uint32_t f = a.flags136C;
        const uint32_t g = a.flags34 & 0xBFDFFEFFu;
        a.flags34 = g;
        a.flags136C = f & 0xFFBFDFFFu;
        if (a.anim.kind == 0x16) {
            a.cooldown = 0x1E;
            a.flags34 = g | 0x100;
            a.behaviour = 7;
            a.sub = 0;
        }
        if (f & 0x4000000) {
            a.behaviour = 0x11;
            a.sub = 0;
            return;
        }
        ChooseNext(w, a);
        return;
    }
    default:
        return;
    }
}

// 3: 0x0044A930 dying on the ground (skeleton death motion), then 0x0044D0A0
void Beh3(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        int m = 0;
        switch (a.anim.kind) {
        case 0x16: m = 0x11E; break;
        case 0x17: m = 0x1BC; break;
        case 0x18: case 0x19: m = 0x11D; break;
        default: break;
        }
        w.AnimSetMotion(a, m);
        ReleaseTarget(w, a);
        DeadCounts(w, a);
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    if (w.MotionTicks(a.anim.motion[0]) - 1 <= a.anim.tick[0]) Grounded(w, a);
}

// 4: 0x0044A9D0 blood pool, sinks for 120 frames, random death-pose frame each frame, then Actor_Destroy
void Beh4(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        if (a.anim.motion[0] == 0x3A6) {
            a.pos[1] = a.pos[1] + F32(w.exe, 0x00565DEC);
            w.SpawnBloodPool(a);
            a.pos[1] = a.pos[1] - F32(w.exe, 0x00565DEC);
        } else {
            w.SpawnBloodPool(a);
        }
        a.jumpFrame = 0x78;
        ++a.sub;
        a.flags34 |= 0x20000;
    } else if (a.sub != 1) {
        return;
    }
    a.pos[1] = a.pos[1] - F32(w.exe, 0x004C4D04);
    if (--a.jumpFrame == 0) {
        if ((a.flags34 & 0x800000) && w.attackers == 0) {
            a.flags34 |= 0x10000;
            if (a.slot120 != -1) w.lockOn.Release(a.slot120);
        }
        if (a.actorSlot != -1 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;
        w.EnemyDestroy(a);
        return;
    }
    const int m = a.anim.motion[0];
    uint32_t tab;
    if (m == 0x1BC) tab = 0x00592AC8;
    else if (m == 0x11D) tab = 0x00592AD0;
    else if (m == 0x11E) tab = 0x00592AD8;
    else if (m == 0x3A6) tab = 0x00592AC0;
    else tab = 0x00592A80 + static_cast<uint32_t>(m * 2 - 0x7B2) * 4;
    const int r = w.Rand(0);
    a.anim.counter[0] = static_cast<int>(w.exe.U32(tab + static_cast<uint32_t>((r % 0x11) >> 4) * 4));
}

// 7: 0x0044B180 walk toward the players (or hold a pose), then the attack choice
void Beh7(World::Impl& w, Actor31Task& a) {
    int motion = static_cast<int>(w.exe.U32(Row(w, 0x005929F0, a.variant) + ((a.flags34 >> 0x1B) & 1) * 4 + 8));
    int start = w.Rand(0) % 10;
    auto turn = [&] {
        return !(a.flags136C & 0x20) && TurnToPoint(a, w.playerPos[0], 0.0f, w.playerPos[2], 0x200, 0x200);
    };
    if (a.sub == 0) {
        switch (PoseIdx(a.flags136C)) {
        case 0:
            if (a.anim.kind == 0x16) memcpy(a.prev, a.pos, 12);
            else start = w.Rand(0) % w.MotionTicks(motion);
            break;
        case 1: motion = a.anim.kind != 0x18 ? 0x138 : 0x20F; break;
        case 2: motion = a.anim.kind != 0x18 ? 0x137 : 0x20C; break;
        case 3: motion = a.anim.kind != 0x18 ? 0x131 : 0x212; break;
        default: break;
        }
        w.AnimBlendTo(a, motion, start, 5);
        ++a.sub;
    }
    if (a.sub == 1) {
        bool check = true;
        if (a.anim.kind == 0x17 && MaterialAt(w, a.pos[0], a.pos[1] + F32(w.exe, 0x00565DE8), a.pos[2]) == 0x35) check = false;
        if (check || a.anim.kind != 0x17) {
            if ((a.anim.kind == 0x16 || a.anim.kind == 0x18 || a.anim.kind == 0x19 || a.anim.kind == 0x17) && turn()) ++a.sub;
        }
    }
    if (a.sub == 1 || a.sub == 2) {
        const int k = a.anim.kind;
        if ((k == 0x16 || (k > 0x17 && k < 0x1A)) && !(a.flags136C & 0x20) &&
            !TurnToPoint(a, w.playerPos[0], 0.0f, w.playerPos[2], 0x200, 0x200))
            a.sub = 0;
    }
    if (!TryBehaviour(w, a, a.anim.kind == 0x18 ? 0x1E : 0x1D) && a.behaviour != 0xB) ChooseNext(w, a);
    Effect5A(w, a, 0x5A);
}

// 9: 0x0044B670 the attack jump at the players (landing point 0x0044CBA0)
void Beh9(World::Impl& w, Actor31Task& a, Task& t) {
    float p[3];
    switch (a.sub) {
    case 0: {
        LandingPoint(w, a, p);
        a.rot[1] = w.runner->camera.playerYaw;  // 0x009C71F0
        JumpWith(w, a, p, 0, 1);
        const uint32_t f = a.flags136C;
        a.poseClass = static_cast<int>(((f >> 8) & 1) * 3 + (((f >> 7) & 1) + ((f >> 0x11) & 1) * 2) * 2 + ((f >> 6) & 1));
        a.flags34 |= 0x10000000;
        if (a.anim.nodes[2].model == 0x2002) w.EnemyVoice(a, 3);
        if (a.anim.kind == 0x18) {
            memcpy(a.f13D8, a.pos, 12);
            a.flags136C &= ~0x800u;
        } else {
            a.flags136C &= 0xFFFFF61Fu;
        }
        ++a.sub;
    }
        [[fallthrough]];
    case 1:
        if (!(a.flags136C & 0x800) && a.anim.kind != 0x18) AttackHit(w, a, t);
        if (JumpMotion(w, a, 1) == 1) return;
        ++a.sub;
        a.flags136C &= ~0x180000u;
        [[fallthrough]];
    case 2:
        if (!(a.flags136C & 0x800)) AttackHit(w, a, t);
        LandingPoint(w, a, p);
        memcpy(a.pos, p, 12);
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 2) return;
        ++a.sub;
        [[fallthrough]];
    case 3:
        a.flags136C &= ~0x800u;
        a.flags34 &= ~0x10000000u;
        a.behaviour = 10;
        a.sub = 0;
        return;
    default:
        return;
    }
}

// 10: 0x0044B880 jump back from the players to a point 50 ahead (floor 0x004053B0), then walk (7)
void Beh10(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        a.flags34 |= 0x20000000;
        a.flags136C |= 0x180000;
        if (a.anim.kind == 0x17) a.flags34 |= 0x2000;
        if (a.anim.kind != 0x18) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, w.playerPos[0], w.playerPos[1], w.playerPos[2]);
            mx::RotY(m, w.runner->camera.playerYaw);
            float l[3] = { 5.0f, 0.0f, 50.0f };
            if (!(a.flags136C & 0x10)) {
                const int s = w.Rand(0) & 1;
                l[0] = static_cast<float>(s * -2 + 1) * F32(w.exe, 0x0055D2B4);
            }
            float q[3];
            mx::Transform(m, l, q);
            const float s0[3] = { q[0], a.body[1] - F32(w.exe, 0x004C49C4), q[2] }, s1[3] = { q[0], a.body[1] + F32(w.exe, 0x004C49C4), q[2] };
            float z = q[2];
            if (w.CollSegmentAll(s0, s1) == 0) {
                a.f13D8[0] = q[0];
                a.f13D8[1] = w.ground;
            } else {
                a.f13D8[0] = w.cac54;
                a.f13D8[1] = w.cac58;
                z = w.cac50;
            }
            a.f13D8[2] = z;
        }
        uint32_t table;
        if (a.attackEntry == 3 && a.anim.kind != 0x18) table = 0x00564AF8;
        else if (a.anim.kind == 0x16) table = 0x005649A8;
        else if (a.anim.kind == 0x17) table = 0;  // (not copied: the jump below uses the previous table)
        else if (a.anim.kind == 0x18) table = 0x00564D08;
        else table = 0x00564AC8;
        if (a.anim.kind == 0x17) w.Note("actor 0x31 skeleton 0x17 jump-back table (0x0044B880) not ported");
        JumpWith(w, a, a.f13D8, table ? table : 0x00564AC8, 1);
        ++a.sub;
    }
    if (a.sub == 1) {
        if (!(a.flags136C & 0x20)) TurnToXZ(a, a.f13D8[0], a.f13D8[2], -0x100);
        if (JumpMotion(w, a, 1) == 1) return;
        a.fallFrames = 0;
        a.flags34 &= ~0x20000000u;
        ReleaseTarget(w, a);
        if (a.anim.kind == 0x18) w.Note("actor 0x31 skeleton 0x18 recovery motion (0x0044B880) not ported");
        else w.AnimBlendTo(a, static_cast<int>(w.exe.U32(Row(w, 0x005929F0, a.variant) + 0x10)), 0, 1);
        ++a.sub;
    }
    if (a.sub == 2) {
        const int c = ++a.fallFrames;
        const float dx = a.pos[0] - w.playerPos[0], dz = a.pos[2] - w.playerPos[2];
        if ((0x59 < c || F32(w.exe, 0x0055D2AC) <= static_cast<float>(std::sqrt(static_cast<float>(dx * dx + dz * dz)))) &&
            w.MotionTicks(a.anim.motion[0]) - 2 <= a.anim.tick[0]) {
            a.behaviour = 7;
            a.sub = 0;
        }
    }
}

// 8: 0x0044B3E0 wait for a free player to attack (another leaper holds the target / attack slot): idle pose of the
// surface the actor clings to (a new rand every frame on the floor), then attack (9; skeleton 0x17: 0x18 or 0x20).
// Without it a waiting leaper kept its walk motion and walked off along a ceiling through the walls; the shooting
// camera (slots 2 + 3 midpoint, 0x00403050) turned after it and stayed turned away.
void Beh8(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        if (!PickTarget(w, a)) {  // 0x0044CA40
            if (a.flags34 & 0x40000000) return;
            int pose = PoseIdx(a.flags136C);
            if (a.anim.kind == 0x18) pose += 10;
            int motion;
            switch (pose) {
            case 0: {
                const uint32_t row = Row(w, 0x005929F0, a.variant);
                if (a.anim.kind == 0x17) motion = static_cast<int>(w.exe.U32(row));
                else motion = static_cast<int>(w.exe.U32(row + static_cast<uint32_t>(w.Rand(0x0044B463) & 1) * 4));
                break;
            }
            case 1: motion = 0x129; break;
            case 2: motion = 0x124; break;
            case 3: motion = 0x134; break;
            case 10: motion = 0x208; break;
            case 11: motion = 0x1FD; break;
            case 12: motion = 0x1F3; break;
            case 13: motion = 0x205; break;
            default: motion = 0x127; break;
            }
            if (a.anim.motion[0] == motion) return;
            w.AnimBlendTo(a, motion, 0, 10);  // 0x0044D230
            return;
        }
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    a.flags136C |= 0x180000;
    a.sub = 0;
    switch (a.anim.kind) {
    case 0x16: case 0x18: case 0x19:
        w.Sound(0x2416A9);
        a.behaviour = 9;
        a.flags136C &= ~0x400u;
        return;
    case 0x17:
        if (MaterialAt(w, a.pos[0], a.pos[1] + F32(w.exe, 0x00565DE8), a.pos[2]) == 0x35 &&
            w.playerPos[1] + F32(w.exe, 0x004C4398) < a.pos[1]) {
            a.behaviour = 0x20;
            a.flags136C |= 0x400;
            return;
        }
        a.flags136C |= 0x400;
        w.Sound(0x2416A9);
        a.behaviour = 0x18;
        return;
    default:
        return;
    }
}

// 11: 0x0044BC70 off the edge of the surface (0x0044C640 found none): fall with gravity to the floor (motion
// 0x3A5 / 0x1BC for skeleton 0x17), land (a drop of 10..15: motion 0x3A9 / 0x1BA, more than 15: 0x3A5 / 0x1BC again),
// then walk (7) or jump back (10); dead, or landed in material 0x5A: dying (3 for skeleton 0x16, else 0x0044D0A0)
void Beh11(World::Impl& w, Actor31Task& a) {
    const bool k17 = a.anim.kind == 0x17;
    if (a.sub == 0) {
        a.flags34 |= 0x2000;
        a.fallFromY = a.pos[1];
        a.flags136C &= 0xFFFFFE1Fu;
        w.AnimBlendTo(a, k17 ? 0x1BC : 0x3A5, 0, 5);
        ++a.sub;
    }
    if (a.sub == 1) {
        const uint32_t bits = 0xBD5F0123u;  // +0x5C gravity
        memcpy(&a.acc[1], &bits, 4);
        if (0x1B < a.anim.tick[0] || w.MotionTicks(a.anim.motion[0]) - 2 <= a.anim.tick[0]) a.flags34 |= 0x4000;
        int mat = 0;
        const float f = w.FloorHeight(a.pos[0], a.pos[1] + F32(w.exe, 0x004C4380), a.pos[2], &mat);  // 0x00409D40
        a.floorY = f;
        if (f < a.vel[1] + a.pos[1]) return;
        StopMoving(a);  // 0x0044E120
        a.pos[1] = a.floorY;
        a.flags34 &= ~0x4000u;
        float drop = a.fallFromY - w.cac58;
        if (drop < 0.0f) drop = drop * -1.0f;
        int motion = 0;
        if (!(drop <= F32(w.exe, 0x004C4398))) motion = k17 ? 0x1BC : 0x3A5;
        else if (F32(w.exe, 0x004C43A4) < drop) motion = k17 ? 0x1BA : 0x3A9;
        if (motion != 0) {
            w.AnimBlendTo(a, motion, 0, 5);
            a.fallFromY = 0.0f;
        }
        ++a.sub;
    } else if (a.sub != 2) {
        return;
    }
    if (a.fallFromY != 0.0f && a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 1) return;
    const float y = a.pos[1] + F32(w.exe, 0x004C4380);
    a.flags34 &= ~0x2000u;
    a.fallFromY = 0.0f;
    a.sub = 0;
    int mat = 0;
    w.FloorHeight(a.pos[0], y, a.pos[2], &mat);  // (0x009CAC40: the material below)
    const uint32_t f34 = a.flags34;
    if (!(f34 & 0x4000000)) {
        if (w.cac40 != 0x5A) {
            a.behaviour = (f34 & 0x20000000) ? 10 : 7;
            return;
        }
        a.flags34 = f34 | 0x4000000;
        DeadCounts(w, a);  // 0x0044D050
    } else if (w.cac40 == 0x5A) {
        a.flags34 = f34 | 0x4000000;
        DeadCounts(w, a);
    }
    if (a.anim.kind == 0x16) {
        a.behaviour = 3;
        a.sub = 0;
        return;
    }
    Grounded(w, a);  // 0x0044D0A0
}

// 17: 0x0044C2E0 shot dead while recovering (+0x136C 0x4000000): not hittable, motion 0x127 with sound 0x1C17A9, then
// the next behaviour (0x0044ADB0)
void Beh17(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        a.flags34 |= 0x100;
        w.AnimBlendTo(a, 0x127, 0, 5);  // 0x0044D230
        w.Sound(0x1C17A9);
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    if (w.MotionTicks(a.anim.motion[0]) - 1 <= a.anim.tick[0]) {
        a.flags34 &= ~0x100u;
        a.flags136C &= ~0x4000000u;
        ChooseNext(w, a);  // 0x0044ADB0
    }
}

// 20: 0x0044E4C0 scripted leap from the record (P words 1..4: to x / y / z, frames)
void Beh20(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        a.flags34 |= 0x100;
        float t[3];
        for (int k = 0; k < 3; ++k) memcpy(&t[k], &a.record.params[4 + 4 * k], 4);
        memcpy(a.from, a.pos, 12);
        memcpy(a.to, t, 12);
        a.jumpFrame = 0;
        a.jumpFrames = a.record.ParamS32(0x10);  // 0x0044DC10
        uint32_t table;
        if (a.anim.kind == 0x17) table = 0x00565E28;
        else table = (w.Rand(0) & 1) == 0 ? 0x00564948 : 0x00564918;
        SetJumpTable(w, a, table);  // 0x0044DA60
        ++a.sub;
        a.jumpState = 0;
        a.flags136C &= ~0x10000000u;
    } else if (a.sub != 1) {
        return;
    }
    if (a.jumpState == 4 && !(a.flags136C & 0x10000000)) {
        a.flags136C |= 0x10000000;
        w.Sound(0x2916A9);
    }
    if (JumpMotion(w, a, 1) != 1) {
        a.behaviour = 7;
        a.sub = 0;
        a.flags34 &= ~0x100u;
    }
}

// 26: 0x0044EE00 a list of leaps (P word 1 delay, then entries of 16 bytes from P + 8: {s16 frames, s16 table, to xyz})
void Beh26(World::Impl& w, Actor31Task& a) {
    auto s16at = [&](uint32_t addr) {
        uint32_t v = 0;
        w.mem.Read(addr & ~3u, &v);
        return static_cast<int16_t>((addr & 2) ? (v >> 16) : (v & 0xFFFF));
    };
    switch (a.sub) {
    case 0:
        a.path = a.record.addr + 0x24 + 8;
        a.jumpFrame = a.record.ParamS32(4);
        a.flags34 |= 0x100;
        ++a.sub;
        [[fallthrough]];
    case 1:
        if (0 < --a.jumpFrame) return;
        ++a.sub;
        [[fallthrough]];
    case 2: {
        const uint32_t p = a.path;
        float to[3];
        for (int k = 0; k < 3; ++k) {
            uint32_t v = 0;
            w.mem.Read(p + 4 + 4 * k, &v);
            memcpy(&to[k], &v, 4);
        }
        uint32_t table;
        if (a.anim.kind == 0x17) table = 0x00565E28;
        else table = s16at(p + 2) == 1 ? 0x00565E58 : s16at(p + 2) == 2 ? 0x00565E88 : 0x00565EB8;
        JumpWith(w, a, to, table, s16at(p));
        ++a.sub;
    }
        [[fallthrough]];
    case 3:
        if (JumpMotion(w, a, s16at(a.path)) == 1) return;
        w.Sound(0x2916A9);
        a.path += 0x10;
        if (s16at(a.path) == -1) {
            a.flags34 &= ~0x100u;
            if (a.anim.kind == 0x17) {
                a.behaviour = 7;
                a.sub = 0;
                return;
            }
            PickTarget(w, a);  // 0x0044CA40
            a.behaviour = 9;
            a.sub = 0;
            return;
        }
        a.sub = 2;
        return;
    default:
        return;
    }
}

// 14 / 15 / 16: 0x0044C170 jump onto the wall left / right / the ceiling (+0x13D8), then pose 0x80 / 0x40 / 0x100
void Beh14(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        a.flags136C &= ~0xE0u;
        JumpTo(a, a.pos, a.f13D8, w.exe);  // 0x0044DB50
        if (a.behaviour == 0xE) SetJumpTable(w, a, 0x00564A68);
        if (a.behaviour == 0xF) SetJumpTable(w, a, 0x00564A38);
        if (a.behaviour == 0x10) SetJumpTable(w, a, 0x00564A98);
        if (a.anim.nodes[2].model == 0x2002) w.EnemyVoice(a, 3);
        w.Sound(0x2416A9);
        ++a.sub;
        a.jumpState = 0;
    }
    if (a.sub == 1) {
        if (JumpMotion(w, a, 1) == 1) return;
        ++a.sub;
    }
    if (a.sub == 2 && w.MotionTicks(a.anim.motion[0]) - 2 <= a.anim.tick[0]) {
        if (a.behaviour == 0xE) a.flags136C |= 0x80;
        if (a.behaviour == 0xF) a.flags136C |= 0x40;
        if (a.behaviour == 0x10) a.flags136C |= 0x100;
        a.behaviour = 7;
        a.sub = 0;
        a.flags136C |= 0x20;
    }
}

// 17: after the knock-back of a dead one (0x0044C2E0) - not reached so far
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Hits, draw, per frame
// ---------------------------------------------------------------------------------------------------------------------
namespace {

// 0x00449B20: hit effects of player p's shot
void HitEffects(World::Impl& w, Actor31Task& a, int p) {
    if (a.flags34 & 0x100) w.hitType[p] = 5;
    const int type = w.hitType[p], slot = std::clamp(w.hitSlot[p], 0, 31);
    float at[3] = {};
    if (type == 1 || type == 5) mx::Transform(w.cam.inverse, a.anim.nodes[slot].hit, at);
    float scale = 0;
    switch (type) {
    case 1:
        if (w.hitSlot[p] == 2 && a.anim.nodes[2].model == 0x2002) {
            w.EnemyVoice(a, 3);
            w.SpawnFlyingHeadModel(a, 0x2015);  // 0x0040A130(obj, 0x2015)
            a.flags136C |= 0x6000000;
            w.SpawnEffectAt(at, 0, 0, 2, 1, p);
            w.Sound(0x1116A9);
        } else {
            scale = 0.75f;
        }
        break;
    case 2: scale = 0.5f; break;
    case 3: scale = 1.0f; break;
    case 5: w.SpawnEffectAt(at, 0, 0, a.anim.kind == 0x18 ? 0x51 : 3, 1, p); break;
    default: break;
    }
    if (type > 0 && type < 4) {
        w.SpawnHitBlood(a, slot, scale);  // 0x00407310
        w.EnemyVoice(a, 0);
    } else if (type == 5) {
        w.Sound(a.anim.kind == 0x17 ? 0x1516A9u : a.anim.kind != 0x19 ? 0x1116A9u : 0xF16A9u);
    }
}

// 0x004499A0
void Hits(World::Impl& w, Actor31Task& a) {
    if (!(a.flags34 & 8)) return;
    w.EnemyCollectShots(a);  // 0x004092F0
    for (int i = 0; i < 2; ++i) {
        const int p = w.shotOrder[i];
        if (p == -1 || w.hitSlot[p] <= 0) continue;
        a.lastSlot = w.hitSlot[p];
        if (getenv("A31_TRACE")) fprintf(stderr, "%d A31 hit p %d slot %d type %d f34 %X f136C %X\n", w.frame, p, w.hitSlot[p], w.hitType[p], a.flags34, a.flags136C);
        HitEffects(w, a, p);
        if ((a.flags34 & 0x100) || w.hitType[p] == 5 || (a.flags136C & 0x200000)) continue;
        a.flags136C &= 0xFFFDDDFFu;
        a.flags34 &= 0xCFFFFFFFu;
        ReleaseTarget(w, a);
        const uint32_t f = a.flags34;
        if (!(f & 0x4000000)) {
            if (!(f & 0x2000)) {
                if (f & 0x40000000) a.flags136C |= 0x400000;
                a.flags34 = f | 0x40000000;
                a.sub = 0;
                if (a.anim.kind == 0x18) a.behaviour = 0x21;
                else if (a.behaviour == 7 && !(a.flags136C & 0x20)) a.behaviour = 1;
                else a.behaviour = 2;
            }
        } else {
            a.flags136C |= 0x200000;
            a.sub = 0;
            a.behaviour = 2;
            w.EnemyVoice(a, w.hitSlot[p] == 2 ? 2 : 1);
        }
    }
}

}  // namespace

// 0x0044C640: the surface the actor stands / clings on: a segment 10 (ceiling: 15) units along the pose's "down"
// (pitch 0x4000 floor, 0 wall, 0xC000 ceiling; wall yaw by the facing quadrant); hit within reach -> out = contact
// point (walls keep y; floor misses fall back to the ground 0x009C8E58 / ceiling 0x00565DF8); else behaviour 0xB / 8
bool SurfacePoint(World::Impl& w, Actor31Task& a, float out[3]) {
    float reach = 10.0f;
    const uint32_t yaw = static_cast<uint32_t>(a.rot[1]);
    int side1 = 0, side2 = 0;  // pfVar9 / pfStack_2c
    if (InRange(yaw, 0x8000, 0x2000)) { side2 = 0xC000; side1 = 0x4000; }
    if (InRange(yaw, 0, 0x2000)) { side1 = 0xC000; side2 = 0x4000; }
    if (InRange(yaw, 0x4000, 0x2000)) { side1 = 0; side2 = 0x8000; }
    if (InRange(yaw, 0xC000, 0x2000)) { side1 = 0x8000; side2 = 0; }
    const uint32_t f = a.flags136C;
    int pitch = 0x4000, ry = 0;
    if (f & 0x80) { pitch = 0; ry = side1; }
    if (f & 0x40) { pitch = 0; ry = side2; }
    if (f & 0x100) { pitch = 0xC000; ry = 0; }
    auto probe = [&](int yawArg, float z, const float at[3], float res[3]) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, at[0], at[1], at[2]);
        mx::RotX(m, pitch);
        mx::RotY(m, yawArg);
        const float l[3] = { 0.0f, 0.0f, z };
        mx::Transform(m, l, res);
    };
    float pa[3], pb[3];
    probe(ry + 0x8000, 10.0f, a.pos, pa);
    if (f & 0x100) reach = 15.0f;
    probe(ry, reach, a.pos, pb);
    w.cac40 = 0;
    float px, py, pz;
    const bool wall = !(f & 0x100) && (f & 0x20);
    const bool hit = w.CollSegmentAll(pb, pa) != 0;
    if (wall) {
        py = a.pos[1];
        if (hit) { px = w.cac54; pz = w.cac50; }
        else { px = 1000.0f; pz = 1000.0f; }
    } else if (hit) {
        py = w.cac58; px = w.cac54; pz = w.cac50;
    } else {
        px = a.pos[0];
        pz = a.pos[2];
        py = !(f & 0x100) ? w.ground : F32(w.exe, 0x00565DF8);
    }
    if (wall) {
        const float at[3] = { px, py, pz };
        float q[3];
        probe(ry + 0x8000, 6.5f, at, q);
        px = q[0];
        pz = q[2];
        py = a.pos[1];
    }
    if (wall) py = a.pos[1];
    else { px = a.pos[0]; pz = a.pos[2]; }
    const float dz = pz - a.pos[2], dy = py - a.pos[1], dx = px - a.pos[0];
    if (static_cast<float>(std::sqrt(static_cast<float>(dz * dz + dy * dy + dx * dx))) <= reach) {
        out[0] = px;
        out[2] = pz;
        out[1] = py;
        return true;
    }
    if (!(f & 0x20)) {
        const int b = a.behaviour;
        if ((b < 0x13 || (b > 0x16 && b != 0x1A)) && b != 0xB) {
            a.sub = 0;
            a.behaviour = 0xB;
        }
    } else if (a.behaviour != 8) {
        a.behaviour = 8;
        a.sub = 0;
    }
    return false;
}

// Node draw 0x00449F90 (head look on node 2, animated / translucent node models)
void Actor31Node(World::Impl& w, EnemyTask& e, int idx, const Mat4& world) {
    Actor31Task& a = static_cast<Actor31Task&>(e);
    Mat4 m = world;
    if (((a.flags136C & 0x100) || !(a.flags136C & 0x20)) && idx == 2 && !(a.flags34 & 0x40000)) w.EnemyHeadLook(a, m);
    const int model = a.anim.nodes[idx].model;
    auto plain = [&](int mdl) {  // 0x0044A200
        w.Draw(mdl, m, 8, -1.0f, (a.flags136C & 1) && w.altModels != 0, idx);
    };
    auto translucent = [&](int mdl, float alpha) {  // 0x0044A240
        w.Draw(mdl, m, 8, alpha, (a.flags136C & 1) && w.altModels != 0, idx);
    };
    if (a.anim.kind == 0x18) {  // stage 5
        if ((model != 0x1FED && model != 0x1FF1) || !(a.flags136C & 0x8000000)) {
            if (a.flags136C & 4) translucent(model, a.alpha);  // 0x0044A280
            else plain(model);
            return;
        }
        const float s = a.f1384 + F32(w.exe, 0x004C4CB0);
        a.f1384 = s;
        if (1.0f <= s) {
            a.f1384 = 1.0f;
            a.flags136C &= ~0x8000000u;
        }
        plain(model);
        mx::Scale(m, 1.0f, a.f1384, 1.0f);
        plain(model == 0x1FEE ? 0x1FF0 : 0x1FF4);
        return;
    }
    if (!(a.flags34 & 0x4000000)) {
        if (model == 0x1FB9) {
            if (a.flags136C & 4) {
                float f = static_cast<float>(w.frameCounter5C50 % 0x78) * F32(w.exe, 0x0055CB80);
                if (0x3B < w.frameCounter5C50 % 0x78) f = 1.0f - (f - 1.0f);
                a.alpha = f;
                translucent(model, f);
                return;
            }
            plain(0x1FB9);
            return;
        }
        if (model == 0x2016) {
            plain(w.frameCounter5C50 % 0x32 + 0x2017);
            return;
        }
        if (model == 0x204A) {
            plain(w.frameCounter5C50 % 0x32 + 0x204B);
            return;
        }
        if (a.flags136C & 4) {
            translucent(model, a.alpha);
            return;
        }
    }
    plain(model);
}

// Node draw 0x0044A2B0 (game type 2 while the lesson does not run): the node model only (0x0044A200, skeleton 0x18
// 0x0044A280)
void Actor31PlainNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    Actor31Task& a = static_cast<Actor31Task&>(e);
    const int model = a.anim.nodes[idx].model;
    const bool alt = (a.flags136C & 1) && w.altModels != 0;
    if (a.anim.kind == 0x18 && (a.flags136C & 4)) w.Draw(model, m, 8, a.alpha, alt, idx);
    else w.Draw(model, m, 8, -1.0f, alt, idx);
}

// Behaviour 28 (0x0044F510): a motion (P s32 +4) from a random tick, then wait (P s16 +8: 0 frames P +10, 1 camera tick
// >= P +10, 2 event flag P +10) and take the behaviour P byte 3
void Beh28(World::Impl& w, Actor31Task& a) {
    auto s16 = [&](int o) { return static_cast<int>(a.record.ParamS16(o)); };  // params +0x1390
    if (a.sub == 0) {
        const int mo = a.record.ParamS32(4);
        const int r = w.Rand(0x0044F540);
        const int n = w.MotionTicks(mo);
        w.AnimBlendTo(a, mo, n > 0 ? r % n : 0, 4);  // 0x004119A0
        ++a.sub;
        a.jumpFrame = 0;
    } else if (a.sub == 2) {
        a.behaviour = static_cast<int16_t>(a.record.ParamS8(3));
        a.sub = 0;
        return;
    } else if (a.sub != 1) {
        return;
    }
    const int kind = s16(8);
    if (kind == 0) {
        if (++a.jumpFrame < s16(10)) return;
    } else if (kind == 1) {
        if (w.runner->camera.tick < s16(10)) return;
    } else if (kind == 2) {
        if (w.runner->flags[s16(10) & 0xFF] != 1) return;
    } else {
        return;
    }
    a.behaviour = static_cast<int16_t>(a.record.ParamS8(3));
    a.sub = 0;
}

// Post-root 0x00449D40
void Actor31PostRoot(World::Impl& w, EnemyTask& e) {
    Actor31Task& a = static_cast<Actor31Task&>(e);
    SphereCentre(w, a);
    if (a.flags136C & 0x100000) {
        const float keep = a.radius128;
        a.radius128 = keep * F32(w.exe, 0x00565DE0);
        const bool pushed = w.SpherePush(a);  // 0x00405B10
        a.radius128 = keep;
        if (pushed) {
            if (a.anim.kind == 0x18 || !w.pushObj || !(w.pushObj->flags34 & 0x200000)) {
                const float f = w.cac4c * F32(w.exe, 0x004C4CC8);
                a.pos[0] = w.cac64[0] * f + a.pos[0];
                a.pos[2] = w.cac64[2] * f + a.pos[2];
                SphereCentre(w, a);
            } else if (a.behaviour != 2 && (a.flags136C & 0x20)) {
                a.behaviour = 2;
                a.sub = 0;
            }
        }
    }
    if (a.flags136C & 0x80000) {
        if (w.MeshPush(a.sphere, a.radius128)) {  // 0x004057F0
            const float f = w.cac4c;
            a.pos[0] = w.cac64[0] * f + a.pos[0];
            a.pos[1] = w.cac64[1] * f + a.pos[1];
            a.pos[2] = w.cac64[2] * f + a.pos[2];
            SphereCentre(w, a);
        }
    }
    if (a.behaviour == 7 || a.behaviour == 8) {  // 0x0044C600
        float p[3];
        if (SurfacePoint(w, a, p)) memcpy(a.pos, p, 12);
        SphereCentre(w, a);
    }
}

// 19: 0x0044E410 rise (motion P[1]), then 7 (stage 1 auto-play capture, notes/agents/stage1b.md)
void Beh19(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        a.flags136C &= ~0x80000u;
        a.flags34 |= 0x100;
        w.AnimSetMotion(a, a.P(1));
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    if (w.MotionTicks(a.anim.motion[0]) - 2 <= a.anim.tick[0]) {
        w.Sound(0x2916A9);
        a.flags34 &= ~0x100u;
        a.flags136C |= 0x80000;
        a.behaviour = 7;
        a.sub = 0;
    }
}

// 32: 0x00450B20 (skeleton 0x17 on a material-0x35 surface above the players, from 8): drop down (motion 0x1B8),
// then attack from there: a random entry of 0x00592A20 (motion of 0x00592A00, the hit through 0x0044CE60's table
// 0x00592A00), the walk motion for 120 frames, and attack again
void Beh32(World::Impl& w, Actor31Task& a, Task& t) {
    switch (a.sub) {
    case 0:
        w.AnimBlendTo(a, 0x1B8, 0, 5);  // 0x004119A0
        ++a.sub;
        [[fallthrough]];
    case 1:
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 1) return;
        ++a.sub;
        memcpy(a.f13D8, a.pos, 12);
        [[fallthrough]];
    case 2: {
        memcpy(a.pos, a.f13D8, 12);
        if (a.target == -1) PickTarget(w, a);  // 0x0044CA40
        a.flags34 |= 0x10000000;
        const int r = w.Rand(0x00450BE3);
        const uint32_t idx = static_cast<uint32_t>((r >> 4) % 10 + (a.lostParts & 7) * 10);
        a.attackEntry = static_cast<int8_t>(w.exe.S16(Row(w, 0x00592A20, a.variant) + idx * 4) & 0xFF);
        const int motion = w.exe.S16(Row(w, 0x00592A00, a.variant) + static_cast<uint32_t>(a.attackEntry) * 0x10);
        w.AnimBlendTo(a, motion, 0, 5);
        w.EnemyVoice(a, 3);  // 0x0040A6F0
        ++a.sub;
    }
        [[fallthrough]];
    case 3:
        AttackHit(w, a, t);  // 0x0044CE60
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 1) return;
        ++a.sub;
        [[fallthrough]];
    case 4:
        a.flags34 &= ~0x10000000u;
        ReleaseTarget(w, a);  // 0x0044CFB0
        w.AnimBlendTo(a, static_cast<int>(w.exe.U32(Row(w, 0x005929F0, a.variant) + 4)), 0, 5);
        ++a.sub;
        a.jumpFrame = 0x78;  // +0x1330
        [[fallthrough]];
    case 5:
        if (0 < --a.jumpFrame) return;
        a.sub = 2;
        return;
    default:
        return;
    }
}

// 24: 0x0044EA50 (from 8: skeleton 0x17 not above the players): walk to the landing point (0x0044CBA0) facing away from
// the players, attack there (entry of 0x00592A20 row 0 for skeleton 0x17: {s16 attack motion, s16 walk motion, float
// reach, s16 hit tick}), then 25
void Beh24(World::Impl& w, Actor31Task& a, Task& t) {
    auto entry = [&] { return Row(w, 0x00592A00, a.variant) + static_cast<uint32_t>(a.attackEntry) * 0x10; };
    switch (a.sub) {
    case 0: {
        a.jumpState = w.players;  // +0x1360 = 0x009C8E80
        LandingPoint(w, a, a.prev);  // +0x13E4
        int pitch = 0;
        mx::DirToAngles(a.pos[0] - w.playerPos[0], 0.0f, a.pos[2] - w.playerPos[2], &pitch, &a.rot[1]);  // 0x004016B0
        const int r = w.Rand(a.anim.kind == 0x17 ? 0x0044EADF : 0);
        const uint32_t idx = static_cast<uint32_t>((r >> 4) % 10 + (a.lostParts & 7) * 10);
        const uint32_t row = a.anim.kind == 0x17 ? w.exe.U32(0x00592A20) : Row(w, 0x00592A20, a.variant);
        a.attackEntry = static_cast<int8_t>(w.exe.S16(row + idx * 4) & 0xFF);
        ++a.sub;
        a.flags34 |= 0x10000000;
        a.flags136C &= 0xFFFFFE1Fu;
        memcpy(a.f13D8, a.pos, 12);
        break;
    }
    case 1:
        break;
    case 2:
        goto attack;
    case 3:
        goto done;
    default:
        return;
    }
    {
        const uint32_t e = entry();
        const float dz = a.pos[2] - a.prev[2], dx = a.pos[0] - a.prev[0];
        float reach;
        const uint32_t rb = w.exe.U32(e + 4);
        memcpy(&reach, &rb, 4);
        if (!(std::sqrt(dz * dz + dx * dx) <= reach)) {
            const int walk = w.exe.S16(e + 2);
            if (a.anim.motion[0] != walk) w.AnimBlendTo(a, walk, 0, 5);
            return;
        }
        w.AnimBlendTo(a, w.exe.S16(e), 0, 5);  // 0x004119A0
        w.EnemyVoice(a, 3);                    // 0x0040A6F0
        ++a.sub;
    }
attack:
    if (a.anim.tick[0] == w.exe.S16(entry() + 8)) AttackHit(w, a, t);  // 0x0044CE60
    if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 1) return;
    ++a.sub;
done:
    a.behaviour = 0x19;
    a.sub = 0;
    a.flags34 &= ~0x10000000u;
}

// 25: 0x0044EC80 after an attack (24): the variant's motion 4 (skeleton 0x17: facing the attack start +0x13D8), until
// 90 frames passed or the players are far (0x0055CCD4; others 0x0055D2AC and the motion's end), then walk (7)
void Beh25(World::Impl& w, Actor31Task& a) {
    if (a.sub == 0) {
        a.flags34 |= 0x20000000;
        a.flags136C |= 0x180000;
        if (a.anim.kind == 0x17) a.flags34 |= 0x2000;
        w.AnimBlendTo(a, static_cast<int>(w.exe.U32(Row(w, 0x005929F0, a.variant) + 0x10)), 0, 1);  // 0x0044D230
        ++a.sub;
        a.fallFrames = 0;  // +0x1338
    } else if (a.sub != 1) {
        return;
    }
    const float dz = a.pos[2] - w.playerPos[2], dx = a.pos[0] - w.playerPos[0];
    const float d = std::sqrt(dz * dz + dx * dx);
    if (a.anim.kind == 0x17) {
        w.EnemyTurnTo(a, a.f13D8[0], a.f13D8[2], -0x100);  // 0x00409F90
        if (++a.fallFrames < 0x5A && d < F32(w.exe, 0x0055CCD4)) return;
    } else {
        if (++a.fallFrames < 0x5A && d < F32(w.exe, 0x0055D2AC)) return;
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0]) - 2) return;
    }
    a.fallFrames = 0;
    ReleaseTarget(w, a);  // 0x0044CFB0
    a.behaviour = 7;
    a.flags34 &= 0xDFFFDFFFu;
    a.sub = 0;
}

// 18: 0x0044E2A0 walk straight on (walk motion of the variant, random start tick) for P float 1 units (xz), then
// 7 with +0x13D8 = the position (first met in stage 4); training lesson 5 (game type 2, 0x009C8FB1 == 5): the walk
// motion by difficulty {0x13C, 0x13B, 0x13A, 0x13A, 0x139}
void Beh18(World::Impl& w, Actor31Task& a) {
    int motion = static_cast<int>(w.exe.U32(Row(w, 0x005929F0, a.variant) + ((a.flags34 >> 0x1B) & 1) * 4 + 8));
    if (w.runner->cfg.gameType == 2 && w.modes && w.modes->lesson == 5) {
        static const int kWalk[5] = { 0x13C, 0x13B, 0x13A, 0x13A, 0x139 };
        const int dv = w.runner->cfg.playerVariant;
        motion = kWalk[dv < 0 || dv > 4 ? 0 : dv];
    }
    if (a.sub == 0) {
        a.flags136C |= 0x1000;
        a.flags34 |= 0x2000;
        const int r = w.Rand(0x0044E344);
        w.AnimBlendTo(a, motion, r % w.MotionTicks(motion), 5);  // 0x0044D230
        memcpy(a.from, a.pos, 12);  // +0x13C0
        const int32_t d = a.record.ParamS32(4);
        memcpy(&a.walkDistance, &d, 4);  // +0x1370
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    const float dz = a.from[2] - a.pos[2], dx = a.from[0] - a.pos[0];
    const float d = static_cast<float>(std::sqrt(static_cast<float>(dz * dz + dx * dx)));
    a.walked = d;  // +0x1374
    if (a.walkDistance <= d) {
        memcpy(a.f13D8, a.pos, 12);
        a.flags136C &= ~0x1000u;
        a.flags34 &= 0xFFFFDEFFu;
        a.behaviour = 7;
        a.sub = 0;
    }
}

// 5: 0x0044AB70 (first met in stage 5): the corpse: blood pool (raised for motion 0x3A6), 120 frames blinking, the
// animation counter re-rolled every frame from the dead motion's pair table, then removed
void Beh5(World::Impl& w, Actor31Task& a) {
    const ExeImage& exe = w.exe;
    if (a.sub == 0) {
        if (a.anim.motion[0] == 0x3A6) {
            a.pos[1] = a.pos[1] + F32(exe, 0x00565DEC);
            w.SpawnBloodPool(a);  // 0x00407DA0
            a.pos[1] = a.pos[1] - F32(exe, 0x00565DEC);
        } else {
            w.SpawnBloodPool(a);
        }
        a.jumpFrame = 0x78;  // +0x1330
        ++a.sub;
        a.flags34 |= 0x20000;
    } else if (a.sub != 1) {
        return;
    }
    if ((a.jumpFrame & 1) == 0) {
        a.alpha = 1.0f;
        a.flags136C &= ~4u;
    } else {
        a.alpha = 0.0f;
        a.flags136C |= 4;
    }
    if (--a.jumpFrame < 1) {
        a.alpha = 0.0f;
        a.flags136C &= ~4u;
        if ((a.flags34 & 0x800000) && w.attackers == 0) {
            a.flags34 |= 0x10000;
            if (a.slot120 != -1) w.lockOn.Release(a.slot120);
        }
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;
        w.EnemyDestroy(a);  // 0x00409CC0
        return;
    }
    const int m = a.anim.motion[0];
    uint32_t site = 0x0044AD11, tab = 0;
    if (m == 0x11E) site = 0x0044ACAA, tab = 0x00592AD8;
    else if (m == 0x11D) site = 0x0044ACCA, tab = 0x00592AD0;
    else if (m == 0x1BC) site = 0x0044ACEA, tab = 0x00592AC8;
    else if (m == 0x3A6) site = 0x0044AD3E, tab = 0x00592AC0;
    const int k = (w.Rand(site) % 0x11) >> 4;
    if (tab != 0) a.anim.counter[0] = static_cast<int>(exe.U32(tab + static_cast<uint32_t>(k) * 4));
    else a.anim.counter[0] = static_cast<int>(exe.U32(0x00592A80 + static_cast<uint32_t>(k - 0x7B2 + m * 2) * 4));
}

// 27: 0x0044EF90 (stage 5, skeleton 0x18): an apparition in the players' frame: hidden at the record offset until
// camera tick P s16 0x10, rises (flickering) to P float 1..3 in P s16 0x12 frames, flickers P s16 0x14 frames, attacks
// the player P s8 0x16 (motion 0x1E7 / 0x1E5, hit at tick 0x1E), retreats (motion 0x1E8) and is removed (0x0044AD60)
void Beh27(World::Impl& w, Actor31Task& a) {
    const float* pp = w.playerPos;  // 0x009C71E0
    auto flicker = [&] {
        if ((w.frameCounter5C50 & 1) == 0) {
            a.alpha = 1.0f;
            a.flags136C &= ~4u;
        } else {
            a.alpha = 0.0f;
            a.flags136C |= 4;
        }
    };
    auto pf = [&](int off) {
        const int32_t v = a.record.ParamS32(off);
        float f;
        memcpy(&f, &v, 4);
        return f;
    };
    switch (a.sub) {
    case 0:
        a.flags34 |= 0x4100;
        w.AnimBlendTo(a, 0x1E9, 0, 5);
        memcpy(a.from, a.pos, 12);  // +0x13C0
        a.to[0] = pf(4);            // +0x13CC
        a.to[1] = pf(8);
        a.to[2] = pf(0xC);
        a.pos[0] = a.pos[0] + pp[0];
        a.pos[1] = a.pos[1] + pp[1];
        a.pos[2] = a.pos[2] + pp[2];
        ++a.sub;
        [[fallthrough]];  // (the switch's break leads to case 1)
    case 1:
        a.pos[0] = pp[0] + a.from[0];
        a.pos[1] = pp[1] + a.from[1];
        a.pos[2] = pp[2] + a.from[2];
        if (w.runner->camera.tick != a.record.ParamS16(0x10)) break;  // (or the context-2 tick 0x009A6458: not modelled)
        a.flags34 &= ~0x4000u;
        a.jumpFrame = a.record.ParamS16(0x12);  // +0x1330
        a.vel[1] = (a.to[1] - a.from[1]) / static_cast<float>(a.jumpFrame);
        w.Sound(0x2516A9);
        ++a.sub;
        [[fallthrough]];
    case 2:
        flicker();
        if (0 < --a.jumpFrame) break;
        StopMoving(a);  // 0x0044E120
        a.flags34 |= 0x2000;
        a.pos[1] = pp[1] + a.to[1];
        w.Sound(0x2916A9);
        w.AnimBlendTo(a, 0x1E9, 0, 5);
        a.jumpFrame = a.record.ParamS16(0x14);
        w.EnemyVoice(a, 3);  // 0x0040A6F0
        w.Sound(0x1F23A9);
        ++a.sub;
        [[fallthrough]];
    case 3: {
        if (a.record.ParamS16(0x14) - 0xF < a.jumpFrame) {
            flicker();
            a.flags34 |= 0x100;
        } else {
            w.Sound(0x2023A9);
            a.flags136C &= ~4u;
            a.alpha = 1.0f;
            a.flags34 &= ~0x100u;
        }
        if (0 < --a.jumpFrame) break;
        int t = a.record.ParamS8(0x16);
        a.target = static_cast<int8_t>(t);
        if (t == -1) {
            t = w.Rand(0x0044F21C) & 1;
            a.target = static_cast<int8_t>(t);
        }
        auto valid = [&](int p) {  // 0x00409DC0
            return w.runner->camera.modeCur == 2 && (w.cfg.gameMode == 5 || (p >= 0 && w.player[p & 1].state == 5));
        };
        if (!valid(t) || w.targetTaken[t & 1] == 1) {
            if (t == 0) t = (valid(1) && w.targetTaken[1] == 0) ? 1 : -1;
            else if (t == 1) t = (valid(0) && w.targetTaken[0] == 0) ? 0 : -1;
            a.target = static_cast<int8_t>(t);
        }
        if (t != -1) {
            w.targetTaken[t & 1] = 1;
            const int r = w.Rand(0x0044F2C8) & 1;
            w.AnimBlendTo(a, r == 0 ? 0x1E7 : 0x1E5, 0, 5);
        }
        ++a.sub;
    }
        [[fallthrough]];
    case 4:
        if (a.target != -1 && a.anim.tick[0] == 0x1E) w.PlayerTakeHit(a.target, 1, a.anim.motion[0] != 0x1E5 ? 1 : 0);
        if (w.MotionTicks(a.anim.motion[0]) - 1 > a.anim.tick[0]) break;
        a.f137C = pp[0];
        a.f1380 = pp[1];
        a.f1384 = pp[2];
        w.AnimBlendTo(a, 0x1E8, 0, 5);
        ++a.sub;
        [[fallthrough]];
    case 5:
        if (a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) / 2) w.Sound(0x2516A9);
        if (w.MotionTicks(a.anim.motion[0]) - 1 <= a.anim.tick[0]) {  // 0x0044AD60
            LeaveLive(w, a);
            LeaveAttackers(w, a);
            ReleaseTarget(w, a);
            if (a.slot120 != -1) w.lockOn.Release(a.slot120);
            a.flags34 &= ~1u;
            w.EnemyDestroy(a);
            return;
        }
        break;
    default:
        break;
    }
    if (1 < a.sub && a.sub < 5) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, pp[0], pp[1], pp[2]);
        mx::RotY(m, w.runner->camera.playerYaw + 0x8000);  // 0x009C71F0
        float o[3];
        mx::Transform(m, a.from, o);
        a.pos[0] = o[0];
        a.pos[2] = o[2];
    }
    if (a.sub == 5) {
        const float dy = pp[1] - a.f1380, dz = pp[2] - a.f1384;
        a.pos[0] = (pp[0] - a.f137C) + a.pos[0];
        a.pos[1] = dy + a.pos[1];
        a.pos[2] = dz + a.pos[2];
        a.f137C = pp[0];
        a.f1380 = pp[1];
        a.f1384 = pp[2];
    }
}

// 1: 0x0044A360 shot while walking (behaviour 7 without pose 0x20): flinch motion 0x00592A70[variant][0x004C84A8[last
// hit slot]] (slots < 9 on the upper-body track 1), then the next behaviour (0x0044ADB0) or 0x11 when dead
void Beh1(World::Impl& w, Actor31Task& a) {
    if (a.lastSlot > 0xF) a.lastSlot = 0xF;
    const int idx = static_cast<uint16_t>(w.exe.S16(0x004C84A8 + static_cast<uint32_t>(a.lastSlot) * 2));
    const int m = static_cast<int>(w.exe.U32(Row(w, 0x00592A70, a.variant) + static_cast<uint32_t>(idx) * 4));
    if (a.sub == 0) {
        StopMoving(a);  // 0x0044E120
        if (a.lastSlot < 9) w.AnimTrack1Start(a, 1, m, 0, 1, 0x14);  // 0x00411B70
        else w.AnimSetMotion(a, m);                                 // 0x00411930
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    if (w.MotionTicks(a.anim.motion[0]) - 1 <= a.anim.tick[0]) {
        const uint32_t f = a.flags136C;
        a.flags34 &= ~0x40000000u;
        a.flags136C = f & ~0x400000u;
        if (f & 0x4000000) {
            a.behaviour = 0x11;
            a.sub = 0;
            return;
        }
        ChooseNext(w, a);  // 0x0044ADB0
    }
}

// 23: 0x0044E830 (stage-2 scene 21) hidden (draw flag 0x10, not shootable) with motion P word 1 for P word 2 frames,
// then the attack leap at the players' point (landing 0x0044CBA0, P word 2 frames, 0x0044DC10), roll back to 0; then 25
void Beh23(World::Impl& w, Actor31Task& a, Task& t) {
    const int motion = a.record.ParamS32(4), frames = a.record.ParamS32(8);
    if (a.sub == 0) {
        a.anim.drawFlags |= 0x10;
        w.AnimBlendTo(a, motion, 0, 5);  // 0x004119A0
        a.flags34 |= 0x100;
        ++a.sub;
        a.jumpFrame = frames;  // (+0x1330 as the countdown)
    } else if (a.sub != 1 && a.sub != 2) {
        return;
    }
    if (a.sub == 1) {
        if (0 < --a.jumpFrame) return;
        a.anim.drawFlags &= ~0x10u;
        a.flags34 &= ~0x100u;
        PickTarget(w, a);  // 0x0044CA40 (-1 when none)
        a.flags34 |= 0x10000000;
        a.flags136C |= 0x20000;
        const int r = w.Rand(0x0044E8FF);
        const uint32_t idx = static_cast<uint32_t>((r >> 4) % 10 + (a.lostParts & 7) * 10);
        a.attackEntry = static_cast<int8_t>(w.exe.S16(Row(w, 0x00592A20, a.variant) + idx * 4) & 0xFF);
        AttackJumpTable(w, a);  // 0x0044B610
        float p[3];
        LandingPoint(w, a, p);  // 0x0044CBA0
        memcpy(a.from, a.pos, 12);  // 0x0044DC10
        a.to[0] = p[0];
        a.to[1] = w.playerPos[1];  // 0x009C71E4
        a.to[2] = p[2];
        a.jumpFrame = 0;
        a.jumpFrames = frames;
        ++a.sub;
        a.jumpState = 0;
    }
    a.rot[2] = static_cast<int>(TurnToward(static_cast<uint32_t>(a.rot[2]), 0, 0xCCC));  // 0x00409E00
    if (a.target != -1) AttackHit(w, a, t);  // 0x0044CE60
    const uint32_t f = a.flags136C;
    const int pose = static_cast<int>(((f >> 8) & 1) * 3 + (((f >> 7) & 1) + ((f >> 0x11) & 1) * 2) * 2 + ((f >> 6) & 1));
    if (static_cast<int32_t>(w.exe.U32(Row(w, 0x00592A10, a.variant) + static_cast<uint32_t>(a.attackEntry + pose * 4) * 0x10 + 4)) <
        a.anim.tick[0])
        a.flags34 |= 0x2000;
    if (JumpMotion(w, a, 1) != 1) {
        a.flags34 &= ~0x10000000u;
        a.flags136C &= ~0x20000u;
        a.behaviour = 0x19;
        a.sub = 0;
    }
}

#include "game/world_st6_actor31.inl"  // skeleton 0x18: behaviours 33 / 34 (stage 6)

// 0x00449910
void TaskActor31(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor31Task& a = t.As<Actor31Task>();
    w.actor8E4C = &a;  // 0x009C8E4C
    if (a.lock) a.slot120 = static_cast<int8_t>(a.lock->slot);  // +0x120 written by the table task 0x00408DD0
    if (a.cooldown > 0 && --a.cooldown == 0) a.flags34 &= ~0x100u;
    Hits(w, a);
    switch (a.behaviour) {
    case 0: break;
    case 1: Beh1(w, a); break;
    case 2: Beh2(w, a); break;
    case 19: Beh19(w, a); break;  // stage 1
    case 3: Beh3(w, a); break;
    case 4: Beh4(w, a); break;
    case 7: Beh7(w, a); break;
    case 8: Beh8(w, a); break;
    case 11: Beh11(w, a); break;
    case 17: Beh17(w, a); break;
    case 9: case 12: case 13: Beh9(w, a, t); break;
    case 14: case 15: case 16: Beh14(w, a); break;
    case 10: Beh10(w, a); break;
    case 20: Beh20(w, a); break;
    case 26: Beh26(w, a); break;
    case 31: Beh31(w, a); break;
    case 32: Beh32(w, a, t); break;  // stage 4 (skeleton 0x17)
    case 24: Beh24(w, a, t); break;
    case 25: Beh25(w, a); break;
    case 18: Beh18(w, a); break;  // stage 4
    case 27: Beh27(w, a); break;  // stage 5
    case 5: Beh5(w, a); break;    // stage 5
    case 33: Beh33(w, a); break;  // stage 6 (world_st6_actor31.inl)
    case 34: Beh34(w, a); break;
    case 28: Beh28(w, a); break;  // training
    case 23: Beh23(w, a, t); break;  // stage-2 scene 21 (routes2a)
    default:
        w.Note("actor 0x31 behaviour " + std::to_string(a.behaviour) + " (0x00592960) not ported");
        break;
    }
    const float vy = a.acc[1] + a.vel[1];
    a.vel[1] = vy;
    const float vx = a.acc[0] + a.vel[0];
    a.vel[0] = vx;
    const float vz = a.acc[2] + a.vel[2];
    a.vel[2] = vz;
    a.pos[0] = vx + a.pos[0];
    a.pos[1] = vy + a.pos[1];
    a.pos[2] = vz + a.pos[2];
    // 0x00449EF0: skeleton (0x00411090 + shadow), animation counters unless frozen (0x4000)
    w.AnimDraw(a);
    w.EnemyShadow(a);
    if (w.runner->cfg.gameType == 2) {  // training: frozen (plain draws 0x0044A2B0) until the lesson runs (0xF1, not 0xF2)
        if (!(a.flags34 & 0x4000) && w.runner->flags[0xF1] == 1 && w.runner->flags[0xF2] == 0) {
            ++a.anim.counter[0];
            ++a.anim.counter[1];
            a.nodeFn = Actor31Node;
        } else {
            a.nodeFn = Actor31PlainNode;
        }
    } else if (!(a.flags34 & 0x4000)) {
        ++a.anim.counter[0];
        ++a.anim.counter[1];
    }
    // 0x00409B70(0): view position, hit target, lock-on candidate
    // (the body point here comes from the world-space skeleton, not the original's model-view chain, so it is ulps
    // off anyway; the port's old summation order fits the captures' knock-back / draws better: st2 +54, st6 +94 frames)
    mx::TransformWorldOrder(w.cam.view, a.body, a.viewPos);
    w.RegisterTarget(&a);
    a.body[1] = 0.0f + a.body[1];
    w.SyncLock(a);
    w.lockOn.Register(a.lock, w.playerPos);
    static const bool trace = getenv("A31_TRACE") != nullptr;
    if (trace)
        fprintf(stderr, "%d A31 %u beh %d/%d f34 %X f136C %X hp %d m %d t %d pos %.4f %.4f %.4f rot %d slot %d tg %d js %d jf %d/%d body %.4f %.4f %.4f\n", w.frame,
                t.serial, a.behaviour, a.sub, a.flags34, a.flags136C, a.hp, a.anim.motion[0], a.anim.tick[0], a.pos[0], a.pos[1], a.pos[2], a.rot[1],
                a.lock ? a.lock->slot : -9, a.target, a.jumpState, a.jumpFrame, a.jumpFrames, a.body[0], a.body[1], a.body[2]);}

// 0x00419B40 case 0x18 (stage 5): the attachment model through the translucent path with +0x138C (0x00418620 with the
// alternative models when +0x38 & 8 and 0x009A2BB4); skin as the default path (0x004ED1E0, 4 nodes)
bool World::Impl::AttachDraw18(EnemyTask& e, int i, uint32_t def) {
    const Actor31Task* a = dynamic_cast<const Actor31Task*>(&e);
    const float alpha = a ? a->alpha : 1.0f;
    const uint32_t tab = 0x004ED1E0 + static_cast<uint32_t>(i) * 0x14;
    const int baseNode = static_cast<int>(exe.U32(tab + 0x10)) & 15;
    WorldDraw* d = Draw(static_cast<int>(exe.U32(def)), e.anim.nodes[baseNode].mv, 8, alpha, (e.flags38 & 8) && altModels != 0, baseNode, true);
    if (!d) return true;
    d->skinCount = 4;
    for (int s = 0; s < 4; ++s) d->skin[s] = e.anim.nodes[static_cast<int>(exe.U32(tab + 4 * s)) & 15].mv;
    return true;
}

// 0x0044A300 (game type 1, item 0x15): node 2 drawn by 0x00449F90 on a (2, 2, 2) scale; nothing without the item flag
void Actor31BigHeadNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& world) {
    if (!ItemFlag(w, 0)) return;
    if (idx != 2) {
        Actor31Node(w, e, idx, world);
        return;
    }
    Mat4 s = world;
    mx::Scale(s, 2.0f, 2.0f, 2.0f);
    Actor31Node(w, e, idx, s);
}

// 0x00449620
void SpawnActor31(World::Impl& w, Task& t, const SpawnRecord& r) {
    auto d = std::make_unique<Actor31Task>();
    Actor31Task& a = *d;
    a.fn = 0x00449910;
    a.record = r;
    a.flags34 = r.flags | 1;
    memcpy(a.pos, r.pos, 12);
    memcpy(a.rot, r.rot, 12);
    a.hp = a.hpMax = static_cast<int16_t>(r.state);
    a.lo = static_cast<int16_t>(r.lo);
    a.anim.kind = r.ParamS8(0);
    // 0x0040A8B0: hp += 0x005776B0[difficulty] (demo: 2), clamped to 1..300
    const int diff = w.gameMode == 5 ? 2 : w.cfg.difficulty;
    int hm = a.hpMax + w.exe.S16(0x005776B0 + static_cast<uint32_t>(diff) * 4);
    if (hm < 1) hm = 1;
    if (hm > 300) hm = 300;
    a.hpMax = a.hp = static_cast<int16_t>(hm);
    a.anim.motion[0] = a.anim.kind == 0x17 ? 0x1BA : 0x3A8;
    a.slot120 = -1;
    a.lock = &w.lockTargets[w.nextLockTarget++];
    w.EnemyInitPose(a);  // Anim_InitEnemy 0x00410440 (+ actor slot)
    a.anim.drawFlags |= 4;
    a.anim.rotOrder = 1;
    a.nodeFn = Actor31Node;          // +0x12EC = 0x00449F90
    a.postRootFn = Actor31PostRoot;  // +0x12F0 = 0x00449D40
    a.anim.counter[0] = a.actorSlot % 10;
    const int k = a.anim.kind;
    a.radius = F32(w.exe, 0x004C4D28 + static_cast<uint32_t>(k) * 4);
    if (k == 0x16) a.radius128 = 5.0f;
    else if (k > 0x16 && k < 0x1A) a.radius128 = 4.0f;
    if (!(a.flags34 & 0x40000)) {
        int pitch = 0, yaw = 0;
        const float dy = static_cast<float>(static_cast<double>(w.playerPos[1]) + F64(w.exe, 0x00565DD8));
        DirToAngles(w.playerPos[0] - a.pos[0], dy - a.pos[1], w.playerPos[2] - a.pos[2], &pitch, &yaw);
        a.headPitch = pitch & 0xFFFF;
        a.headYaw = yaw & 0xFFFF;
    }
    const uint32_t lo = static_cast<uint32_t>(static_cast<int>(a.lo));
    a.flags136C = lo | 0x180000;
    switch (((lo & 0x100) >> 8) * 3 + ((lo & 0x80) >> 7) * 2 + ((lo & 0x40) >> 6)) {
    case 0: a.pose134C = 0; break;
    case 1: a.pose134C = 1.0f; a.flags136C |= 0x20; break;
    case 2: a.pose134C = 2.0f; a.flags136C |= 0x20; break;
    case 3: a.pose134C = 3.0f; a.flags136C |= 0x20; break;
    default: break;
    }
    if (k == 0x17) {
        a.alpha = 1.0f;
        if (a.flags136C & 1) a.flags38 |= 8;
        if (a.flags136C & 4) a.f1354 = 0;
    }
    if (k == 0x18) {  // stage 5
        const uint32_t f = a.flags34;
        a.flags34 = f | 0x400;
        if (r.ParamS8(2) == 0x22) {
            a.alpha = 0.0f;
            a.flags34 = f | 0x80400;
            a.flags136C |= 4;
        } else {
            a.alpha = 1.0f;
        }
        if (a.flags136C & 8) {
            a.f1384 = 0.0f;
            a.f1354 = 0;
            a.f132C = a.rot[1];
        }
    }
    if (k == 0x16) {
        a.anim.nodes[5].model = 0x1FA2;
        a.anim.nodes[8].model = 0x1F9E;
    }
    ++w.attackers;
    ++w.liveEnemies;
    a.target = -1;
    w.SyncLock(a);
    w.lockOn.AddDirect(a.lock);  // 0x00408E80
    a.variant = r.ParamS8(1);
    a.behaviour = r.ParamS8(2);
    a.sub = 0;
    if (w.runner->cfg.gameType == 1 && ItemFlag(w, 0)) {  // item 0x15 (0x009C88A8 == 1): node cb 0x0044A300, head radius x 2
        a.nodeFn = Actor31BigHeadNode;
        a.anim.nodes[2].radius = a.anim.nodes[2].radius + a.anim.nodes[2].radius;
    }
    t.data = std::move(d);
    t.fn = TaskActor31;  // first run next frame
}

// =====================================================================================================================
// The weapon a leaper throws (behaviour 31): 0x004504E0 -> task 0x00450780 (block 0x13F4, states 0x00592AE0: 0 flight
// 0x0044FD40, 1 shot down 0x00450050); skeleton 0x18's weapons leave a trail (0x00450930 -> task 0x00450A30).
// CONFIRMED: build\scene_capture_weapon31_st2.txt (skeleton 0x16: flight, hit, stuck, blink-out; --stage-start 14659)
// and build\scene_capture_weapon31_st6.txt (skeleton 0x18: trail, shot down; --stage-start 70806 --civilians 0,1):
// every logic check exact, weapon / trail draws exact
// =====================================================================================================================

namespace {

struct Weapon31Task : EnemyTask {
    Task* owner = nullptr;   // +0x1390 the thrower
    uint32_t ownerSerial = 0;
    int hand = 0;            // +0x1358 node slot it left (5 / 8)
    int spin = 0;            // +0x135C
    int pitchOff = 0;        // +0x1364 added to the pitch when drawn
    float frames = 0;        // +0x1344 flight frames left
    int counter = 0;         // +0x1330
    int trailTimer = 4;      // +0x1338
    int trailPeriod = 4;     // +0x133C
    int trails = 0;          // +0x1368 live trail copies
    float aim[3] = {};       // +0x13C0
    int model = 0;           // +0x13F0
};

struct Trail31Task : EnemyTask {
    Task* owner = nullptr;   // +0x1390 the weapon
    uint32_t ownerSerial = 0;
    uint32_t ownerFlags = 0; // its +0x34 when last seen (the original reads the freed block)
    int pitchOff = 0;        // +0x1364
    int counter = 0;         // +0x1330
    float scale = 1.0f;      // +0x1384
    float step = 0;          // +0x1388
    int model = 0;           // +0x13F0
};

Weapon31Task* LiveWeapon(Task* t, uint32_t serial) {
    return (t && t->serial == serial && t->data) ? &t->As<Weapon31Task>() : nullptr;
}

// 0x0044CFB0 on any enemy object
void ReleaseTargetE(World::Impl& w, EnemyTask& a) {
    if (a.target != -1) {
        w.targetTaken[a.target & 1] = 0;
        a.target = -1;
    }
    if (a.flags136C & 0x8000) {
        a.flags136C &= ~0x8000u;
        w.attackSlot = 0;
    }
}

// 0x004503D0: the point the weapon flies at / sticks to, in front of the view (camera matrix 0x009A6040[context]):
// (0, 0, -4) with both players on route 1, else x = (1 - 2 * target) * 0x00565EF0 to the target's side
void WeaponAim(World::Impl& w, Weapon31Task& k) {
    float local[3] = { 0.0f, 0.0f, -4.0f };
    if (w.route != 1) local[0] = static_cast<float>(k.target * -2 + 1) * F32(w.exe, 0x00565EF0);
    mx::Transform(w.cam.inverse, local, k.aim);
}

// 0x00450A30: a fading copy of the weapon (15 frames, +0x1384 - 0x3D888889 per frame = the light colour 0x004AA0A0:
// (0, 0, v) for models 0x1FE1 / 0x1FE2, else (v, v, v), kept for the later draws) while the weapon flies (+0x34 & 0x4000
// clear)
void TaskTrail31(Task& t) {
    World::Impl& w = *CurrentWorld();
    Trail31Task& k = t.As<Trail31Task>();
    Weapon31Task* o = LiveWeapon(k.owner, k.ownerSerial);
    if (o) k.ownerFlags = o->flags34;
    if (0 <= --k.counter && !(k.ownerFlags & 0x4000)) {
        k.scale = k.scale - k.step;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.pitchOff + k.rot[0]);
        if (k.model == 0x1FE1 || k.model == 0x1FE2) w.LightColor(0.0f, 0.0f, k.scale);
        else w.LightColor(k.scale, k.scale, k.scale);
        w.Draw(k.model, m);
        return;
    }
    if (!(k.ownerFlags & 0x4000000) && o) --o->trails;
    w.EnemyDestroy(k);  // Actor_Destroy 0x00409CC0
}

// 0x00450930: every 5th frame a trail copy, at most 10 at a time
void WeaponTrail(World::Impl& w, Weapon31Task& k) {
    if (0 <= --k.trailTimer) return;
    if (k.trails < 10) {
        Trail31Task* c = w.NewTask<Trail31Task>(TaskTrail31, 0x00450A30);
        memcpy(c->pos, k.pos, 12);  // +0x40..+0x6F: position, velocity, acceleration, angles
        memcpy(c->vel, k.vel, 12);
        memcpy(c->acc, k.acc, 12);
        memcpy(c->rot, k.rot, 12);
        c->owner = w.ts.Current();
        c->ownerSerial = c->owner ? c->owner->serial : 0;
        c->ownerFlags = k.flags34;
        c->pitchOff = k.pitchOff;
        c->counter = 0xF;
        c->scale = 1.0f;
        c->step = Bits(0x3D888889);
        c->actorSlot = -1;
        c->model = k.model;
        if (k.model == 0x1FE1) {
            c->scale = 0.75f;
            c->model = 0x1FE4;
        }
        if (c->model == 0x1FE2) {
            c->scale = 0.75f;
            c->model = 0x1FE5;
        }
        ++k.trails;
    }
    k.trailTimer = k.trailPeriod;
}

// 0x0044FD40 (state 0): straight at the aim point; on arrival Player_TakeHit(target, 1, 6), then it sticks in front of
// the view facing the players (30 frames) and blinks out (60 frames)
void WeaponFlight(World::Impl& w, Weapon31Task& k) {
    if (k.sub == 0) {
        const float dz = k.aim[2] - k.pos[2], dy = k.aim[1] - k.pos[1], dx = k.aim[0] - k.pos[0];
        const float f = std::sqrt((dx * dx + dy * dy) + dz * dz) * F32(w.exe, 0x00565EE8);
        k.frames = f;
        k.vel[0] = (k.aim[0] - k.pos[0]) / f;
        k.vel[1] = (k.aim[1] - k.pos[1]) / f;
        k.vel[2] = (k.aim[2] - k.pos[2]) / f;
        w.Sound(0x5117A9);
        ++k.sub;
    } else if (k.sub != 1 && (k.sub < 2 || k.sub > 4)) {
        return;
    }
    if (k.sub == 1) {
        if (k.hand == 5) k.rot[1] += k.spin;
        else k.rot[1] -= k.spin;
        for (int c = 0; c < 3; ++c) k.pos[c] = k.vel[c] + k.pos[c];
        k.frames = k.frames - 1.0f;
        if (!(k.frames <= 0.0f)) return;
        w.PlayerTakeHit(k.target, 1, 6);  // Player_TakeHit 0x00415300
        ++k.sub;
    }
    if (k.sub == 2) {
        k.flags34 |= 0x400C000;
        mx::DirToAngles(w.playerPos[0] - k.pos[0], 0.0f, w.playerPos[2] - k.pos[2], &k.rot[0], &k.rot[1]);  // 0x004016B0
        k.rot[0] = 0;
        int u;
        if (k.model == 0x1FE1 || k.model == 0x1FE2) {
            if (k.hand == 5) {
                if (w.route == 2 && k.target == 1) k.rot[1] += 0x8000;
                u = -(w.Rand(0x0044FF2C) & 5);
            } else {
                if (w.route == 2 && k.target == 0) k.rot[1] += 0x8000;
                u = w.Rand(0x0044FF51) & 5;
            }
        } else {
            const int s = w.Rand(0x0044FEBA) & 1;  // (rand() is never negative: the signed % 2 fix-up does nothing)
            const int r = w.Rand(0x0044FED4) & 2;
            k.rot[0] += (1 - s * 2) * r * 0x100;
            u = k.hand == 5 ? -(w.Rand(0x0044FEF5) & 5) : (w.Rand(0x0044FF01) & 5);
        }
        k.counter = 0x1E;
        k.rot[1] += u * 0x100;
        w.Sound(0x5217A9);
        ++k.sub;
    }
    if (k.sub == 3) {
        WeaponAim(w, k);
        memcpy(k.pos, k.aim, 12);
        if (0 < --k.counter) return;
        ++k.sub;
        k.counter = 0x3C;
    }
    if (k.sub == 4) {
        WeaponAim(w, k);
        memcpy(k.pos, k.aim, 12);
        if ((k.counter & 1) == 0) k.anim.drawFlags |= 1;
        else k.anim.drawFlags &= ~1u;
        if (0 < --k.counter) return;
        ReleaseTargetE(w, k);  // 0x0044CFB0
        w.EnemyDestroy(k);     // Actor_Destroy 0x00409CC0
    }
}

// 0x00450050 (state 1): shot down: effect 3 (skeleton 0x16's weapons) / 0x51, then it flies off to a random point beside
// the view (speed 0x00564708, at most 180 frames)
void WeaponShot(World::Impl& w, Weapon31Task& k) {
    if (k.sub == 0) {
        w.SpawnEffectAt(k.pos, 0, 0, (k.model == 0x1F90 || k.model == 0x1F91) ? 3 : 0x51, 1, -1);  // 0x00407340
        k.flags34 |= 0x4008000;
        ReleaseTargetE(w, k);
        ++k.sub;
    }
    if (k.sub == 1) {
        const int s1 = w.Rand(0x00450101) & 1;
        const int r1 = w.Rand(0x00450127);
        float local[3];
        local[0] = static_cast<float>(r1 % 10 + 1) * F32(w.exe, 0x004C43A4) * static_cast<float>(s1 * -2 + 1) + k.viewPos[0];
        const int s2 = w.Rand(0x0045014E) & 1;
        const int r2 = w.Rand(0x00450174);
        local[2] = k.viewPos[2];
        local[1] = static_cast<float>(r2 % 10 + 1) * F32(w.exe, 0x004C43A4) * static_cast<float>(s2 * -2 + 1) + k.viewPos[1];
        mx::Transform(w.cam.inverse, local, k.aim);
        const int s3 = w.Rand(0x004501D6) & 1;
        k.spin = static_cast<int>(static_cast<float>(s3 * -2 + 1) * (static_cast<float>(k.spin) * F32(w.exe, 0x00565EEC)));  // __ftol
        k.counter = 5;
        w.Sound(0x5217A9);
        w.Sound(0x1116A9);
        ++k.sub;
    }
    if (k.sub == 2) {
        if (0 < --k.counter) return;
        ++k.sub;
        k.counter = 0;
    }
    if (k.sub != 3) return;
    float d[3] = { k.aim[0] - k.pos[0], k.aim[1] - k.pos[1], k.aim[2] - k.pos[2] };
    const float len = std::sqrt(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);  // 0x004AAA00
    const float v = F32(w.exe, 0x00564708);
    for (int c = 0; c < 3; ++c) k.vel[c] = d[c] / len * v;
    for (int c = 0; c < 3; ++c) k.pos[c] = k.vel[c] + k.pos[c];
    if (k.hand == 5) k.rot[0] -= k.spin;
    else k.rot[0] += k.spin;
    if (++k.counter < 0xB4) {
        for (float& c : d)
            if (c < 0.0f) c = c * -1.0f;  // 0x004C4C64
        if (v < d[0] || v < d[1] || v < d[2]) return;
    }
    w.EnemyDestroy(k);
}

// 0x00450780
void TaskWeapon31(Task& t) {
    World::Impl& w = *CurrentWorld();
    Weapon31Task& k = t.As<Weapon31Task>();
    if ((k.flags34 & 8) && !(k.flags34 & 0x4000000)) {  // shot: the shooter's hit count (+0x26), state 1
        ++w.player[(k.flags34 & 0x10) ? 1 : 0].hits;
        k.behaviour = 1;
        k.sub = 0;
    }
    if (k.behaviour == 0) WeaponFlight(w, k);
    else if (k.behaviour == 1) WeaponShot(w, k);
    // (Actor_Destroy ends the task function: Task_Kill 0x004A7040 does not return)
    if (k.anim.drawFlags & 1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.pitchOff + k.rot[0]);
        w.Draw(k.model, m);
        mx::Transform(w.cam.view, k.pos, k.viewPos);  // 0x009A6000[context]
        w.RegisterTarget(&k);                          // 0x00405160
        // 0x0040A600(weapon, 5, 5): shadow 0x10D0 on the floor below (drawFlags 4: floor query)
        if (!(k.flags34 & 0x80000)) {
            float y;
            if (w.cfg.gameMode == 0xD) y = w.ground;
            else if (!(k.anim.drawFlags & 4)) y = k.pos[1];
            else {
                int mat = 0;
                y = w.FloorHeight(k.pos[0], k.pos[1] + F32(w.exe, 0x004C49C0), k.pos[2], &mat);
            }
            Mat4 s = Mat4::Identity();
            mx::Translate(s, k.pos[0], y + F32(w.exe, 0x004C4CC8), k.pos[2]);
            mx::Scale(s, 5.0f, 1.0f, 5.0f);
            w.Draw(0x10D0, s, 0xD);
        }
    }
    if (k.anim.kind == 0x18 && ((k.behaviour == 0 && k.sub < 2) || k.behaviour == 1)) WeaponTrail(w, k);
    if (k.behaviour == 0) {  // a lock-on candidate while it flies (0x00408EC0)
        memcpy(k.body, k.pos, 12);
        w.SyncLock(k);
        w.lockOn.Register(k.lock, w.playerPos);
    } else {
        w.SyncLock(k);
    }
}

// 0x004504E0: the weapon in node slot +0x1358 (5 / 8) leaves the thrower's hand (the slot shows the empty hand, its hit
// radius 0, the part bit 0x004C4D18[slot] in +0x1318) at the hand's world point
void SpawnWeapon31(World::Impl& w, Actor31Task& a) {
    Weapon31Task* k = w.NewTask<Weapon31Task>(TaskWeapon31, 0x00450780);
    k->lock = &w.lockTargets[w.nextLockTarget++];
    k->actorSlot = -1;  // Actor_RegisterSlot 0x00409270
    for (int i = 0; i < 14; ++i)
        if (!w.actorSlots[i]) {
            k->flags38 |= 0x40;
            w.actorSlots[i] = k;
            k->actorSlot = i;
            break;
        }
    const int hand = a.throwHand;
    auto emptyHand = [&](int empty, int weapon, int pitch) {
        a.anim.nodes[static_cast<size_t>(hand)].model = empty;
        a.anim.nodes[static_cast<size_t>(hand)].radius = 0.0f;
        k->model = weapon;
        k->pitchOff = pitch;
    };
    if (a.anim.kind == 0x16) {
        if (hand == 5) emptyHand(0x1F9F, 0x1F91, 0x600);
        else if (hand == 8) emptyHand(0x1F9B, 0x1F90, 0x600);
    } else if (a.anim.kind == 0x18) {
        if (hand == 5) emptyHand(0x1FF1, 0x1FE2, 0);
        else if (hand == 8) emptyHand(0x1FED, 0x1FE1, 0);
    }
    a.lostParts = static_cast<uint8_t>(a.lostParts | (1u << (w.exe.U32(0x004C4D18 + static_cast<uint32_t>(hand)) & 0x1F)));
    if (hand >= 0 && hand < static_cast<int>(a.anim.nodes.size()))  // camera matrix x the hand's view-space point
        mx::Transform(w.cam.inverse, a.anim.nodes[static_cast<size_t>(hand)].hit, k->pos);
    k->flags34 = 0x80000001u;
    k->radius = 2.0f;
    k->radius128 = 2.0f;
    k->target = a.target;
    a.target = 0;  // (+0x121 = 0, not -1)
    if (a.flags136C & 0x8000) {
        k->flags136C |= 0x8000;
        a.flags136C &= ~0x8000u;
    }
    k->anim.drawFlags = 5;
    k->anim.kind = a.anim.kind;
    k->owner = w.ts.Current();
    k->ownerSerial = k->owner ? k->owner->serial : 0;
    k->hand = hand;
    k->spin = 0x2400;
    k->trailPeriod = 4;
    k->trailTimer = 4;
    k->behaviour = 0;
    k->sub = 0;
    WeaponAim(w, *k);
    memcpy(k->body, k->pos, 12);
    w.SyncLock(*k);
    w.lockOn.Register(k->lock, w.playerPos);  // 0x00408EC0
}

}  // namespace

}  // namespace game
