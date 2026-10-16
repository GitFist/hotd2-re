// game::World - enemy pieces first reached in stage 4 (st4evtbl.bin). Verification: notes/agents/stage4.md
// (world_test --stage-start 44107 on build\scene_capture_auto_st4.txt).

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

float ParamF(const EnemyTask& e, int off) {
    const int32_t v = e.record.ParamS32(off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

bool Leaper(const EnemyTask& e) { return e.anim.kind > 0x15 && e.anim.kind < 0x1A; }

// 0x0044DA60: the jump motion table of the actor slot (0x009C8AA0 + slot * 0x30; kept per object here)
void SetJumpTable(World::Impl& w, EnemyTask& e, uint32_t va) {
    for (int k = 0; k < 12; ++k) e.jumpTab[k] = static_cast<int>(w.exe.U32(va + 4 * k));
}

// 0x0044D5F0: stretch / squeeze the table to the jump's frame count (+0x1334)
void FitJump(EnemyTask& e) {
    int* j = e.jumpTab;
    const int n = e.counter1334;
    const int d = (j[5] - j[7]) + n;
    if (d == 0) return;
    if (d > 0) {
        while (j[10] + j[6] < d) {
            ++j[6];
            ++j[10];
        }
        if (d < j[10] + j[6]) --j[6];
        if (j[6] > 0x7F) j[6] = 0x7F;
        if (j[10] > 0x7F) j[10] = 0x7F;
    }
    if (d < 0) {
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

// 0x0044DD00: position on the jump parabola (+0x13C0 -> +0x13CC over +0x1334 frames), +0x1330 += step
bool JumpStep(World::Impl& w, EnemyTask& e, int step) {
    const int n = e.counter1334, i = e.counter1330;
    if (i >= n + step) return false;
    const float t = static_cast<float>(i);
    const float* a = e.jumpStart;
    const float* b = e.jumpTarget;
    e.pos[0] = ((b[0] - a[0]) / static_cast<float>(n)) * t + a[0];
    const float dy = b[1] - a[1];
    const float g = static_cast<float>(n * n) * F32(w.exe, 0x00565E1C);
    e.counter1330 = i + step;
    e.pos[1] = (((g + dy + dy) * t) / static_cast<float>(n * 2) -
                static_cast<float>(i * i) * F32(w.exe, 0x00565E1C) * F32(w.exe, 0x004C43AC)) + a[1];
    e.pos[2] = ((b[2] - a[2]) / static_cast<float>(n)) * t + a[2];
    return true;
}

// 0x0044D860: the jump's motion sequence (take-off, flight, landing); 0 once the landing motion reached its tick.
// Generic enemies only (the leaping zombies 0x16..0x19 have their own port in world_st2_actor31.cpp).
int JumpMotion(World::Impl& w, EnemyTask& e, int step) {
    int* j = e.jumpTab;
    switch (e.jump1360) {
    case 0:
        if (Leaper(e)) w.Note("jump motion 0x0044D860 of skeleton 0x16..0x19 on a generic enemy not ported");
        FitJump(e);  // (0x0044E140 for skeleton 0x19)
        w.AnimBlendTo(e, j[0], j[1], j[2]);
        ++e.jump1360;
        [[fallthrough]];
    case 1:
        if (e.anim.tick[0] < j[3]) return 1;
        w.AnimBlendTo(e, j[4], j[5], j[6]);
        ++e.jump1360;
        [[fallthrough]];
    case 2:
        JumpStep(w, e, step);
        if (e.anim.tick[0] < j[7]) return 1;
        w.AnimBlendTo(e, j[8], j[9], j[10]);
        e.counter1330 -= step;
        ++e.jump1360;
        [[fallthrough]];
    case 3:
        if (JumpStep(w, e, step)) return 1;
        // 0x0044D260(0x50): effects only for skeletons 0x16..0x19
        ++e.jump1360;
        [[fallthrough]];
    case 4:
        if (e.anim.tick[0] < j[11]) return 1;
        return 0;
    default:
        return 1;
    }
}

// 0x00458A70 (behaviour 30): wait (P s32 6 frames, or P byte 2 == 0x16: until the camera tick equals it), crouch
// (motion 0x10C / 0x39F by skeleton 0 / other, until tick 0x21 / 0x2C), then jump from the position to P float 1..3
// in P s32 4 frames with the motion table 0x00567898 / 0x00567958 (step P s32 5); landing: dust hook 0x00592BC8 +
// sound 0x2A16A9, or (variant 5) sound 0x1C16A9 and the shake timer 0x20. Then behaviour 3 (attack the player +0x121,
// whose 0x009A2BA0 flag is taken) or 1.
void Beh30(World::Impl& w, EnemyTask& e) {
    switch (e.sub) {
    case 0:
        e.counter1330 = e.record.ParamS32(0x18);
        ++e.sub;
        [[fallthrough]];
    case 1:
        if (e.record.params[2] == 0x16) {
            const int t = w.runner->camera.tick;  // 0x009A6110; context 2's 0x009A6458 not modelled
            if (t != e.counter1330) return;
        } else {
            if (--e.counter1330 > 0) return;
        }
        if (e.anim.kind == 0) {
            w.AnimBlendTo(e, 0x10C, 0, 5);
            e.counter1330 = 0x21;
        } else {
            w.AnimBlendTo(e, 0x39F, 0, 5);
            e.counter1330 = 0x2C;
        }
        ++e.sub;
        e.flags34 |= 0x2000;
        [[fallthrough]];
    case 2:
        if (e.anim.tick[0] < e.counter1330) return;
        ++e.sub;
        [[fallthrough]];
    case 3:
        // 0x0044DC10(from = pos, to = P float 1..3, frames P s32 4)
        memcpy(e.jumpStart, e.pos, 12);
        e.jumpTarget[0] = ParamF(e, 4);
        e.jumpTarget[1] = ParamF(e, 8);
        e.jumpTarget[2] = ParamF(e, 0xC);
        e.counter1330 = 0;
        e.counter1334 = e.record.ParamS32(0x10);
        SetJumpTable(w, e, e.anim.kind == 0 ? 0x00567898 : 0x00567958);
        ++e.sub;
        e.jump1360 = 0;
        e.flags136C &= ~0x10000u;
        break;
    case 4:
        break;
    default:
        return;
    }
    switch (e.jump1360) {
    case 0: case 1: case 4: {
        const uint32_t f = e.flags136C;
        e.flags136C = f & ~0x100000u;
        if (e.jump1360 != 4 || (f & 0x10000)) break;
        if (e.variant != 5) {
            w.EnemyLandingDust(e);  // hook 0x00592BC8
            w.Sound(0x2A16A9);
        } else {
            w.Sound(0x1C16A9);
            w.shakeTimer = 0x20;  // 0x009C8E8C
            e.flags136C |= 0x10000;
        }
        break;
    }
    case 2: case 3:
        e.flags136C |= 0x100000;
        break;
    default:
        break;
    }
    if (JumpMotion(w, e, e.record.ParamS32(0x14)) != 1) {
        e.flags136C &= ~0x10000u;
        e.flags34 &= ~0x2000u;
        if (e.target == -1) {
            e.behaviour = 1;
            e.sub = 0;
            return;
        }
        w.targetTaken[e.target & 1] = 1;  // 0x009A2BA0
        e.behaviour = 3;
        e.sub = 0;
    }
}

// 0x004574D0 (behaviour 17): hold motion P s32 1 (unless shot: +0x34 & 0x40000000) for P s32 2 frames, then behaviour
// P byte 3; to 15 with the walk length (float)(uint32) P s32 3 (fild qword) and sub-state 1
void Beh17(World::Impl& w, EnemyTask& e) {
    if (e.sub == 0) {
        e.counter1330 = e.record.ParamS32(8);
        e.sub = 1;
    } else if (e.sub != 1) {
        return;
    }
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.record.ParamS32(4)) w.AnimSetMotion(e, e.record.ParamS32(4));
    if (--e.counter1330 < 1) {
        const int next = e.record.ParamS8(3);
        e.behaviour = static_cast<int16_t>(next);
        if (next == 0xF) {
            e.walkDistance = static_cast<float>(static_cast<uint32_t>(e.record.ParamS32(0xC)));
            e.sub = 1;
            return;
        }
        e.sub = 0;
    }
}

}  // namespace

// Enemy_SetupKind 0x00452FD0 case 0xE: node 5 holds a collision-mesh weapon (flags | 0x51, mesh P word 4, radius 0)
void World::Impl::EnemySetupKind14(EnemyTask& e) {
    EnemyNode& nd = e.anim.nodes[5];
    nd.flags |= 0x51;
    nd.mesh = e.P(4);
    nd.radius = 0;
}

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Behaviour 43 (0x0045C080): an attacker attached to its civilian (+0x1394, the parent): it follows the civilian's
// position / angles in motion 0x1A4; after the list's loop count and tick it kills the civilian (motion 0x1A8, the
// civilian's +0x34 |= 0x4000000); if the civilian dies first it lets go (0x1AA). Sub 2: at tick 0x2D the motion 0x1B0
// is placed so node 1 stays (0x0045CE70) with root motion; sub 3 turns to yaw 0x2000; event flag 0x1D (with players)
// takes it out of the game (sub 4: actor slot freed, destroyed).

int16_t EvS16St4(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a & ~3u, &w);
    return static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
}

// 0x00409E00: move angle a towards b by at most `step` (16-bit wrap)
uint32_t ApproachAngleSt4(uint32_t a, uint32_t b, int step) {
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

// 0x0045CE70: the new motion's root (frame tick / 2) placed so node 1 stays where it is
void KeepNode1Enemy(World::Impl& w, EnemyTask& e) {
    const int nodes = w.exe.S16(0x004E0724 + e.anim.kind * 2);
    const Motion* m = w.EnemyMotion(e.anim.motion[0], nodes);
    float root[3] = {};
    int16_t r0[3] = {};
    if (m && !m->data.empty() && e.anim.tick[0] / 2 < m->StoredFrames()) {
        m->RootPosition(e.anim.tick[0] / 2, root);
        m->Rotation(e.anim.tick[0] / 2, 0, r0);
    } else {
        w.Note("behaviour 43: motion frame data missing (0x00412F50)");
    }
    float cur[3];
    {
        Mat4 q = w.cam.inverse;  // 0x004A9230(camera), 0x004A92A0(node 1 +0x28)
        mx::MulLeft(q, e.anim.nodes[1].mv);
        mx::GetTranslation(q, cur);
    }
    const uint32_t sk = w.exe.U32(w.exe.U32(0x004E0430 + 4 * static_cast<uint32_t>(e.anim.kind)) + 0x18);
    Mat4 a = Mat4::Identity();
    mx::Translate(a, e.pos[0], e.pos[1], e.pos[2]);
    mx::RotX(a, e.rot[0]);
    mx::RotZ(a, e.rot[2]);
    mx::RotY(a, e.rot[1]);
    mx::Translate(a, root[0], root[1], root[2]);
    mx::RotZ(a, r0[2]);
    mx::RotY(a, r0[1]);
    mx::RotX(a, r0[0]);
    mx::Translate(a, F32(w.exe, sk + 4), F32(w.exe, sk + 8), F32(w.exe, sk + 0xC));
    float nw[3];
    mx::GetTranslation(a, nw);
    e.pos[0] = (cur[0] - nw[0]) + e.pos[0];
    e.pos[1] = (cur[1] - nw[1]) + e.pos[1];
    e.pos[2] = (cur[2] - nw[2]) + e.pos[2];
    memcpy(e.anim.root, root, 12);
}

void Beh43(World::Impl& w, EnemyTask& e) {
    EnemyTask* c = e.parent;  // +0x1394
    auto follow = [&] {
        if (!c) return;
        memcpy(e.pos, c->pos, 12);
        memcpy(e.rot, c->rot, 12);
    };
    if (!c) w.Note("behaviour 43 without its civilian (0x0045C080)");
    switch (e.sub) {
    case 0: {
        const uint32_t l = static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1));  // 0x0045CA10
        e.flags34 |= 0x2400;
        w.AnimBlendTo(e, 0x1A4, 0, 0);
        e.anim.drawFlags &= ~2u;
        e.facing1350 = EvS16St4(w.mem, l);  // +0x1350 loops
        e.f1354 = EvS16St4(w.mem, l + 2);   // +0x1354 tick
        ++e.sub;
        e.flags1368 |= 0x10;
        e.flags34 |= 0x10000000;
        [[fallthrough]];
    }
    case 1:
        follow();
        if (e.anim.tick[0] == w.MotionTicks(e.anim.motion[0]) - 1) --e.facing1350;
        if (e.facing1350 == 0 && e.anim.tick[0] == e.f1354) {
            if (c && (c->flags34 & 0x4000000)) break;
            w.AnimBlendTo(e, 0x1A8, 0, 2);
            e.flags34 |= 0x10100;
            if (c) c->flags34 |= 0x4000000;
        } else {
            if (!c || !(c->flags34 & 0x4000000)) break;
            w.AnimBlendTo(e, 0x1AA, 0, 2);
            e.flags34 |= 0x10100;
        }
        ++e.sub;
        break;
    case 2:
        follow();
        if (e.anim.tick[0] != 0x2D) break;
        e.anim.motion[0] = 0x1B0;
        e.anim.tick[0] = 0;
        KeepNode1Enemy(w, e);
        w.AnimBlendTo(e, e.anim.motion[0], 0, 10);
        e.anim.drawFlags |= 2;
        ++e.sub;
        break;
    case 3:
        e.rot[1] = static_cast<int>(ApproachAngleSt4(static_cast<uint32_t>(e.rot[1]), 0x2000, 0x1A0));
        break;
    case 4:
        if (e.actorSlot != -1) w.actorSlots[e.actorSlot] = nullptr;
        w.EnemyDestroy(e);
        return;
    default:
        break;
    }
    // event flag 0x1D (0x009C721D) with players in the game: out of the game
    if (w.runner->flags[0x1D] != 0 && w.players != 0) {
        e.flags34 |= 0x4000000;
        if (!(e.flags38 & 2)) {  // 0x00456560
            e.flags38 |= 2;
            --w.liveEnemies;
        }
        if (!(e.flags38 & 4)) {  // 0x00456580
            e.flags38 |= 4;
            --w.attackers;
        }
        e.sub = 4;
    }
}

}  // namespace

bool World::Impl::EnemyBehaviourSt4(EnemyTask& e) {
    switch (e.behaviour) {
    case 43: Beh43(*this, e); return true;
    case 17: Beh17(*this, e); return true;
    case 30: Beh30(*this, e); return true;
    default: return false;
    }
}

}  // namespace game
