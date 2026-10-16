// game::World - the generic enemy behaviours (table 0x00592AE8) used by the walking zombies of the stage-1 demo
// (kind 19 "znsam2", variant 2): 15 scripted walk-in 0x00457220, 1 idle/approach 0x004554D0, 2 walk to the player
// 0x00455720, 3 attack 0x00455A40, 4 back off 0x00455C30, 5 wait 0x00455670, 6 die on the spot 0x00454D20, and their
// helpers (motion sets 0x00592CBC, attack tables 0x00592DC0 / 0x00592F18, approach distances 0x009A2BE0).

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// 0x00409E00
uint32_t Approach(uint32_t a, uint32_t b, int step) {
    a &= 0xFFFF;
    b &= 0xFFFF;
    const int d = static_cast<int>(a) - static_cast<int>(b);
    const int ad = d < 0 ? static_cast<int>(b) - static_cast<int>(a) : d;
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

// 0x0040A040
bool InRange(uint32_t v, uint32_t centre, int range) {
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

}  // namespace

// Motion set entry i of the enemy's kind / variant (0x00592CBC[kind][variant][i]).
int World::Impl::EnemyMotionSet(const EnemyTask& e, int i) {
    const uint32_t t = exe.U32(0x00592CBC + 4 * e.anim.kind);
    const uint32_t v = exe.U32(t + 4 * e.variant);
    return static_cast<int>(exe.U32(v + 4 * i));
}

// 0x009A2BE0 + c * 12 + k * 4 (approach distances; defaults 0x004C4CD0 written by op 0x4D)
float World::Impl::ApproachDistance(int c, int k) {
    const uint32_t a = 0x009A2BE0 + static_cast<uint32_t>(c * 12 + k * 4);
    auto it = runner->globals.find(a);
    uint32_t bits = it != runner->globals.end() ? it->second : exe.U32(0x004C4CD0 + static_cast<uint32_t>(c * 12 + k * 4));
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

int World::Impl::GlobalInt(uint32_t addr, int def) {
    auto it = runner->globals.find(addr);
    return it != runner->globals.end() ? static_cast<int>(it->second) : def;
}

// 0x00454770: blend to `motion` from `start` over n frames unless already playing it (or the hit-reaction lock)
void World::Impl::EnemySetMotion(EnemyTask& e, int motion, int start, int n) {
    if (e.flags136C & 0x2000) return;
    if (e.anim.motion[0] == motion) return;
    if (e.flags136C & 0x1000) {  // upper body busy (hit reaction on track 1): blend the lower body only
        AnimBlendLower(e, motion, start, n);  // 0x00411AC0
        return;
    }
    AnimBlendTo(e, motion, start, n);
}

// 0x00456650: distance band to the players' position; sets the attack limit +0x1358
int World::Impl::EnemyApproachBand(EnemyTask& e) {
    const float dz = e.pos[2] - playerPos[2], dx = e.pos[0] - playerPos[0];
    const float d = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx));
    const int c = e.c131F;
    const int n40 = GlobalInt(0x009C8E40, 2), n44 = GlobalInt(0x009C8E44, 3), n48 = GlobalInt(0x009C8E48, 4);
    int r = 0;
    if (ApproachDistance(c, 2) < d) {
        r = 4;
        e.attackLimit = n48 + n44 + n40;
    }
    if (d <= ApproachDistance(c, 2) && ApproachDistance(c, 1) < d) {
        r = 3;
        e.attackLimit = n44 + n40;
    }
    if (d <= ApproachDistance(c, 1) && ApproachDistance(c, 0) < d) {
        r = 2;
        e.attackLimit = n40;
    }
    if (d <= ApproachDistance(c, 0)) {
        e.attackLimit = n40;
        return 1;
    }
    return r;
}

// 0x00409ED0: turn towards a point 1.5 in front of the players (the original rotates that frame by ftol of the
// player HEIGHT 0x009C71E4, kept as is)
void World::Impl::EnemyTurnToPlayers(EnemyTask& e, int step) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, playerPos[0], playerPos[1], playerPos[2]);
    mx::RotY(m, static_cast<int>(playerPos[1]));
    const float l[3] = { 0, 0, 1.5f };
    float p[3];
    mx::Transform(m, l, p);
    int pitch, yaw;
    DirToAngles(e.pos[0] - p[0], 0.0f, e.pos[2] - p[2], &pitch, &yaw);
    e.rot[1] = static_cast<int>(Approach(static_cast<uint32_t>(e.rot[1]), static_cast<uint32_t>(yaw), step));
}

// 0x00409E80 / 0x00409F90: turn towards (x, z)
void World::Impl::EnemyTurnTo(EnemyTask& e, float x, float z, int step) {
    int pitch, yaw;
    DirToAngles(e.pos[0] - x, 0.0f, e.pos[2] - z, &pitch, &yaw);
    e.rot[1] = static_cast<int>(Approach(static_cast<uint32_t>(e.rot[1]), static_cast<uint32_t>(yaw), step));
}

// 0x00455920: variant from the remaining arm models (kinds 1, 0x13, 0x14)
void World::Impl::EnemyCheckParts(EnemyTask& e) {
    const int k = e.anim.kind;
    const int m5 = e.anim.nodes[5].model, m8 = e.anim.nodes[8].model;
    if (k == 1) {
        int c = m5 == 0x1BA9;
        if (m8 == 0x1BA5) ++c;
        if (c == 0) e.variant = 0;
    } else if (k > 0x12 && k < 0x15 && e.variant != 7 && e.variant != 5) {
        int c = 0;
        if (m5 == 0x1ECE || m5 == 0x1EF9) c = 1;
        else e.lostParts = static_cast<uint8_t>(e.lostParts | (1u << (static_cast<int8_t>(exe.S16(0x004C4D1C) >> 8) & 0x1F)));
        if (m8 == 0x1ECA || m5 == 0x1EF5) ++c;
        else e.lostParts = static_cast<uint8_t>(e.lostParts | (1u << (static_cast<int8_t>(exe.S16(0x004C4D20)) & 0x1F)));
        e.variant = c;
        if ((e.flags136C & 0x40) && e.variant == 0) {
            e.variant = 5;
            e.flags136C &= ~0x40u;
        }
    }
}

// 0x00457220: scripted walk-in until P[1] units from the start
void World::Impl::EnemyBeh15(EnemyTask& e) {
    if (e.sub == 0) {
        const int32_t w = e.P(1);
        memcpy(&e.walkDistance, &w, 4);
        e.sub = 1;
    } else if (e.sub != 1 && e.sub != 2) {
        return;
    }
    if (e.sub == 1) {
        ++e.sub;
        memcpy(e.jumpStart, e.pos, 12);
    }
    const int m = (e.flags34 & 0x20000000) ? EnemyMotionSet(e, 4) : EnemyMotionSet(e, 2 + ((e.flags34 >> 27) & 1));
    const int r = Rand(0x004572C4);
    EnemySetMotion(e, m, r % MotionTicks(m), 5);
    const float dz = e.jumpStart[2] - e.pos[2], dx = e.jumpStart[0] - e.pos[0];
    const float d = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx));
    e.walked = d;
    if (e.walkDistance <= d) {
        memcpy(e.savedPos, e.pos, 12);
        if (!(e.flags34 & 0x20000000)) {
            e.behaviour = 1;
            e.sub = 0;
            return;
        }
        EnemyDespawn(e);
    }
}

// 0x00457160: walk straight on for P[1] frames (the walk motion's root motion carries the enemy, e.g. the chainsaw
// enemy of stage 1 scene 3 out through the door the hanging prop swings open), then behaviour 1 from that position
void World::Impl::EnemyBeh14(EnemyTask& e) {
    if (e.sub == 0) {
        e.counter1330 = e.P(1);
        e.sub = 1;
    } else if (e.sub != 1) {
        return;
    }
    const int m = EnemyMotionSet(e, 2 + ((e.flags34 >> 27) & 1));
    const int r = Rand(0x004571BD);
    EnemySetMotion(e, m, r % MotionTicks(m), 5);
    if (--e.counter1330 < 1) {
        memcpy(e.savedPos, e.pos, 12);  // +0x13D8
        e.behaviour = 1;
        e.sub = 0;
    }
}

// 0x004575A0: hold motion P[1] until the camera tick (context 0 0x009A6110, or context 2 0x009A6458 - not modelled)
// equals P[2], then switch to behaviour (s8) P byte 3
void World::Impl::EnemyBeh18(EnemyTask& e) {
    if (e.sub == 0) {
        if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.P(1)) AnimSetMotion(e, e.P(1));
        if (runner->camera.tick != e.P(2)) return;
        ++e.sub;
    } else if (e.sub != 1) {
        return;
    }
    e.behaviour = static_cast<int16_t>(e.record.ParamS8(3));
    e.sub = 0;
}

// 0x004554D0: idle in place, turning to the players, until close enough to approach
void World::Impl::EnemyBeh1(EnemyTask& e) {
    const int m = EnemyMotionSet(e, 2 + ((e.flags34 >> 27) & 1));
    const int r = Rand(0x00455501);
    EnemySetMotion(e, m, r % MotionTicks(m), 10);
    const int r2 = Rand(0x00455522);
    if ((r2 & 0xFF) < 4 && (e.flags136C & 0x2000000) && (e.flags136C & 0x1000000)) {
        e.flags136C &= ~0x1000000u;
        Note("enemy 0x0045D9F0 not ported");
    }
    const int band = EnemyApproachBand(e);
    const int step = static_cast<int>(((static_cast<float>((e.flags34 >> 27) & 1) * 1.5f) + 1.0f) * 416.0f);  // 0x00567890
    if (band == 1) {
        e.behaviour = 2;
        e.sub = 0;
        return;
    }
    EnemyTurnToPlayers(e, step);
    if (band < 2 || band > 4) return;
    if (e.attackLimit <= e.c131D) {
        const int r3 = Rand(0x004555BB);
        const int bit = static_cast<int>(exe.U32(0x00566124 + ((r3 >> 4) % 10) * 4));
        e.behaviour = 5;
        e.sub = 0;
        e.flags136C |= static_cast<uint32_t>(bit) << 21;
    }
    if (band != 2 && EnemyThrowReady(e)) {  // 0x00458E10 (world_st3_enemy.cpp)
        e.behaviour = 0x21;
        e.sub = 0;
    }
}

// 0x00455720: walk to the players; attack when allowed
void World::Impl::EnemyBeh2(EnemyTask& e) {
    const int c = e.c131F;
    int what = 0;
    EnemyApproachBand(e);
    EnemyCheckParts(e);
    if ((e.flags136C & 0x20000) && !OnScreen(e.viewPos)) {
        e.flags136C &= ~0x20000u;
        attackSlot = 0;
    }
    const float dz = e.pos[2] - playerPos[2], dx = e.pos[0] - playerPos[0];
    const float d = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx));
    if (e.anim.kind == 0 || (e.flags136C & 0x40400) || ApproachDistance(c, 0) - 1.0f <= d) {
        if (!(e.flags1368 & 1)) e.cooldown133C = 0;
        else if ((e.flags136C & 0x40000) && --e.cooldown133C < 1) e.flags1368 &= ~1u;
        if (!(e.flags136C & 0x400) && e.c131D < e.attackLimit && e.c131E < 3 && e.cooldown133C < 1) {
            what = EnemyPickTarget(e) == 1 ? 2 : 1;
        } else {
            what = 1;
        }
    } else {
        e.behaviour = 4;
        e.sub = 0;
    }
    if (what == 1) {
        const int m = EnemyMotionSet(e, 0);
        if (e.anim.motion[0] != m) {
            const int r = Rand(0x004558BB);
            AnimBlendTo(e, m, r % 5, 10);
            Sound(0x1917A9);
        }
        if ((e.flags136C & 0x400) && MotionTicks(e.anim.motion[0]) - 2 <= e.anim.tick[0]) e.flags136C &= ~0x400u;
        EnemyTurnTo(e, playerPos[0], playerPos[2], 0x40);
    } else if (what == 2) {
        e.behaviour = e.variant != 4 ? 3 : 0x34;
        e.sub = 0;
    }
}

// 0x00455A40: attack (entry 0x00592F18[kind][variant][+0x131A]: motion, approach motion, range, hit tick ...)
void World::Impl::EnemyBeh3(EnemyTask& e) {
    auto entry = [&](int idx) { return exe.U32(exe.U32(0x00592F18 + 4 * e.anim.kind) + 4 * e.variant) + idx * 0x10; };
    uint32_t ent = entry(e.attack131A);
    if (e.sub == 0) {
        e.flags34 = (e.flags34 & ~0x100u) | 0x10000000;
        const int r = Rand(0x00455AAA);
        const uint32_t tab = exe.U32(exe.U32(0x00592DC0 + 4 * e.anim.kind) + 4 * e.variant);
        const int8_t pick = static_cast<int8_t>(exe.S16(tab + ((((r >> 4) % 10) + (e.lostParts & 7) * 10) * 4)) & 0xFF);
        e.attack131A = pick;
        ent = entry(pick);
        ++e.sub;
    } else if (e.sub != 1 && e.sub != 2) {
        return;
    }
    const int motion = exe.S16(ent), approachMotion = exe.S16(ent + 2), hitTick = exe.S16(ent + 8);
    if (e.sub == 1) {
        EnemyFaceTarget(e);
        const float dz = e.pos[2] - e.faceTarget[2], dx = e.pos[0] - e.faceTarget[0];
        const float d = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx));
        if (F32(exe, ent + 4) < d && !(e.flags1368 & 1)) {
            if (e.anim.motion[0] != approachMotion) AnimBlendTo(e, approachMotion, 0, 10);  // 0x0044D230
            return;
        }
        AnimBlendTo(e, motion, 0, 5);
        EnemyAttackSplash(e, false);  // hook 0x00592BCC
        e.flags136C = (e.flags136C & ~0x10000u) | 0x80000;
        EnemyVoice(e, 3);
        if (!(e.flags136C & 0x40000)) {
            memcpy(e.savedPos, e.pos, 12);
            e.flags136C |= 0x40000;
        }
        ++e.sub;
    }
    // state 2
    EnemyFaceTarget(e);
    if (e.anim.tick[0] == hitTick) {  // 0x00456490
        const int t = e.target;
        const uint16_t mask = static_cast<uint16_t>(exe.S16(ent + 0xC));
        if ((t == 0 || t == 1) && ((static_cast<int16_t>(static_cast<int8_t>(e.lostParts & 7)) & mask) != mask)) {
            // Player_TakeHit 0x00415300(target, 0 / 1, entry +0xA): no effect in game mode 5 (0x009C8E98)
            const int kind = exe.S16(ent + 0xA);
            if (e.flags34 & 0x2000000) {
                PlayerTakeHit(t, 0, kind);
                EnemyDespawn(e);
                return;
            }
            PlayerTakeHit(t, 1, kind);
        }
    }
    EnemyAttackSplash(e, true);  // hook 0x00592BD0
    if (MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
        e.behaviour = 4;
        e.sub = 0;
    }
}

// 0x00455C30: back off towards the saved position
void World::Impl::EnemyBeh4(EnemyTask& e) {
    const float dz = e.pos[2] - e.faceTarget[2], dx = e.pos[0] - e.faceTarget[0];
    const float d = static_cast<float>(std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx));
    const int c = e.c131F;
    const int m = EnemyMotionSet(e, 4);
    if (e.sub == 0) {
        e.timer1338 = 0x3C;
        e.counter1334 = 0;
        e.flags34 = (e.flags34 & 0xEFFFFFFFu) | 0x20000000;
        e.flags136C &= ~0x400000u;
        e.sub = 1;
    } else if (e.sub != 1) {
        return;
    }
    const int r = Rand(0x00455CDB);
    EnemySetMotion(e, m, r % 5, 10);
    EnemyTurnTo(e, e.savedPos[0], e.savedPos[2], (e.flags136C & 0x400000) ? 0x40 : -0x40);
    ++e.counter1334;
    const float near = ApproachDistance(c, 0);
    if ((e.counter1334 > 0xF0 || near < d || (e.variant == 4 && near * F32(exe, 0x005644F4) < d)) &&
        (e.anim.motion[0] != 0x100 || exe.S16(0x004E09D0) - 1 <= e.anim.tick[0])) {
        if (!(e.flags1368 & 1)) e.cooldown133C = 0;
        e.flags34 &= ~0x20000000u;
        EnemyReleaseTarget(e);
        e.behaviour = 2;
        e.sub = 0;
    }
}

// 0x00455670: wait (motion 0x00592CBC[.][.][(+0x136C >> 21) & 1]) until the attack slot frees
void World::Impl::EnemyBeh5(EnemyTask& e) {
    const int m = EnemyMotionSet(e, (e.flags136C >> 21) & 1);
    if (e.sub == 0) e.sub = 1;
    else if (e.sub != 1) return;
    if (e.anim.motion[0] != m) {
        const int r = Rand(0x004556C8);
        AnimBlendTo(e, m, r % 5, 10);
    }
    EnemyTurnTo(e, playerPos[0], playerPos[2], 0x40);
    if (e.c131D < e.attackLimit) {
        e.behaviour = 1;
        e.sub = 0;
    }
}

// 0x00454D20: die on the spot
void World::Impl::EnemyBeh6(EnemyTask& e) {
    if (e.sub == 0) {
        EnemyStartDeathMotion(e);
        ++e.sub;
    } else if (e.sub != 1 && e.sub != 2) {
        return;
    }
    if (e.sub == 1) {
        e.flags34 |= 0x22000;
        e.flags136C = (e.flags136C & ~0x200u) | 0x60000000;
        if (e.c131B == 1) {  // 0x00456600
            if (GroupCount() == 1) Sound(e.anim.kind == 2 ? 0x004E17A9 : 0x002025A9);
            e.c131B = 0;
            --GroupCount();
        }
        EnemyOnDeath(e);
        ++e.sub;
        e.flags136C &= ~0x10000u;
    }
    EnemyDeathSplash(e);  // hook 0x00592BC4 = 0x004569B0
    if (MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0] || (e.flags34 & 0x1000000)) {
        e.flags136C &= ~0x200u;
        if (e.flags34 & 0x1000000) {
            e.behaviour = 0xC;
            e.sub = 0;
            return;
        }
        EnemyEnterCorpse(e);
    }
}

// 0x00456220: death motion from the hit direction (camera yaw against the enemy's facing)
void World::Impl::EnemyDeathMotionWalk(EnemyTask& e) {
    const uint32_t u = static_cast<uint32_t>(cam.yaw - e.rot[1]) & 0xFFFF;
    if (InRange(u, 0x4000, 0x2000)) AnimBlendTo(e, 0x3E0, 0, 5);
    if (InRange(u, 0xC000, 0x2000)) AnimBlendTo(e, 0x3DF, 0, 5);
    if (InRange(u, 0, 0x2000)) {
        const int r = Rand(0x004562C8);
        AnimBlendTo(e, static_cast<int>(exe.U32(0x0059309C + (r & 3) * 4)), 0, 5);
    }
    if (InRange(u, 0x8000, 0x2000)) {
        const int r = Rand(0x0045630B);
        AnimBlendTo(e, static_cast<int>(exe.U32(0x00593084 + (r % 6) * 4)), 0, 5);
    }
    if ((e.flags136C & 4) && e.anim.motion[0] > 0x3DE && e.anim.motion[0] < 0x3E1) {
        const int r = Rand(0x0045634C);
        e.anim.motion[0] = static_cast<int>(exe.U32(0x00593084 + (r % 6) * 4));
    }
    if (e.flags136C & 1) {
        int& m = e.anim.motion[0];
        if (m == 0x3D9) m = 0x3DB;
        else if (m == 0x3DA) m = 0x3DC;
        else if (m == 0x3DE) m = 0x3DD;
    }
    if (e.flags136C & 2) {
        int& m = e.anim.motion[0];
        if (m == 0x3DB) m = 0x3D9;
        else if (m == 0x3DC) m = 0x3DA;
        else if (m == 0x3DD) m = 0x3DE;
    }
}

// 0x004563F0: splash tick list of the death motion (+0x13A0)
void World::Impl::EnemySplashList(EnemyTask& e) {
    switch (e.anim.motion[0]) {
    case 0x3D9: e.splashList = 0x005930AC; break;
    case 0x3DA: e.splashList = 0x005930B8; break;
    case 0x3DB: e.splashList = 0x005930C4; break;
    case 0x3DC: e.splashList = 0x005930D0; break;
    case 0x3DD: e.splashList = 0x005930DC; break;
    case 0x3DE: e.splashList = 0x005930E8; break;
    case 0x3DF: e.splashList = 0x005930F4; break;
    case 0x3E0: e.splashList = 0x00593100; break;
    default: e.splashList = 0x00593124; break;
    }
}

// hook 0x004569B0: dust / water splash at the listed ticks of the death motion
void World::Impl::EnemyDeathSplash(EnemyTask& e) {
    if (!e.splashList) return;
    if (e.anim.tick[0] != static_cast<int32_t>(exe.U32(e.splashList))) return;
    int mat = 0;
    const float floorY = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);
    float at[3] = { e.pos[0], floorY, e.pos[2] };
    if (rainFlag != 1 && mat != 0x37 && mat != 5) {
        EffectSpawn in;
        if (e.anim.kind != 0xF && e.anim.kind != 0x10 && e.anim.kind != 0x11) {
            memcpy(in.pos, at, 12);
            in.scale[0] = 0.5f;
            in.scale[1] = 1.5f;
            in.scale[2] = 1.5f;
            in.type = 0x46;
            in.faceMode = 2;
        } else {
            at[1] = e.pos[1];
            memcpy(in.pos, at, 12);
            in.scale[0] = 1.0f;
            in.scale[1] = 0.3f;
            in.scale[2] = 1.0f;
            in.type = 0x61;
            in.faceMode = 1;
        }
        SpawnEffectBlock(in);
        e.splashList += 4;
        return;
    }
    at[1] = e.pos[1];
    if ((mat == 0x37 || mat == 5) && !(e.flags136C & 0x10000)) {
        const float ring[3] = { e.pos[0], floorY, e.pos[2] };
        SpawnSplashRing(ring, 1.0f);  // 0x004567C0
        SpawnSplashRing(ring, 0.5f);
        e.flags136C |= 0x10000;
        at[1] = floorY;
    }
    SpawnEffectAt(at, 0, 0, 0x61, 1, -1);
    e.splashList += 4;
}

// hooks 0x00456C50 (attack start) / 0x00456D10 (20 ticks before the attack motion ends): variant 6 splashes when
// standing in water (floor material 0x37 / 5)
void World::Impl::EnemyAttackSplash(EnemyTask& e, bool atTick) {
    if (e.variant != 6) return;
    if (atTick) {
        if (e.flags136C & 0x10000) return;
        if (e.anim.tick[0] != MotionTicks(e.anim.motion[0]) - 0x14) return;
    }
    int mat = 0;
    const float fy = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);
    if (mat != 0x37 && mat != 5) return;
    const float at[3] = { e.pos[0], fy, e.pos[2] };
    if (!atTick) Sound(0x4116A9);  // 0x00456C50: sound first
    SpawnSplashRing(at, 1.0f);    // 0x004567C0
    SpawnSplashRing(at, 0.5f);
    SpawnEffectAt(at, 0, 0, (!atTick && e.behaviour == 0x17) ? 0x62 : 0x61, 1, -1);  // 0x00407340
    if (atTick) {  // 0x00456D10
        e.flags136C |= 0x10000;
        Sound(0x4116A9);
    }
}

// Enemy_Despawn 0x00455490
void World::Impl::EnemyDespawn(EnemyTask& e) {
    if (!(e.flags38 & 2)) {
        e.flags38 |= 2;
        --liveEnemies;
    }
    if (!(e.flags38 & 4)) {
        e.flags38 |= 4;
        --attackers;
    }
    EnemyReleaseTarget(e);
    if (e.slot120 != -1) lockOn.Release(e.slot120);
    EnemyDestroy(e);
}

}  // namespace game
