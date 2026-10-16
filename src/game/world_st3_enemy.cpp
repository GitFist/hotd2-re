// game::World - enemy pieces first reached in stage 3 (water: wakes, ...). Verification: notes/agents/stage3.md.

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

// Wake of an enemy wading in water (0x00408770 -> task 0x004083D0, object 0x13F4): two per enemy (+0x34 bit 2: the
// mirrored one), following the parent's position on the water surface y (+0x137C); a model 0x1A78 + (frame % 50)
// scaled by the parent's movement (+0x1378 / +0x1374) and faded by +0x138C.
struct WakeTask : EnemyTask {
    EnemyTask* parentEnemy = nullptr;  // +0x1390
    float frameF = 0;                  // +0x1370
    float grow = 1.0f;                 // +0x1374
    float scaleX = 1.0f;               // +0x1378
    float waterY = 0;                  // +0x137C
    float last[3] = {};                // +0x1380
    float alpha = 0.05f;               // +0x138C
    int yaw0 = 0;                      // +0x1350
    uint32_t parentFlags = 0;          // +0x1358
};

void TaskWake(Task& t) {
    World::Impl& w = *CurrentWorld();
    WakeTask& k = t.As<WakeTask>();
    EnemyTask& p = *k.parentEnemy;
    const ExeImage& exe = w.exe;
    const float dz = p.pos[2] - k.last[2], dy = p.pos[1] - k.last[1], dx = p.pos[0] - k.last[0];
    if (!(p.flags34 & 1) || p.behaviour == 9) {
        if (k.actorSlot != -1) w.actorSlots[k.actorSlot] = nullptr;
        w.ts.KillCurrent();  // 0x004A7040
        return;
    }
    if (!(k.parentFlags & 0x10000000) && (p.flags34 & 0x10000000)) w.Sound(0x4616A9);
    k.parentFlags = p.flags34;
    const bool mirror = (k.flags34 & 4) != 0;
    if (mirror == ((p.flags34 & 0x20000000) != 0)) {
        k.sub = 0;
        k.rot[1] = p.rot[1] + (mirror ? 0 : 0x8000);
    } else {
        if (k.sub != 1) {
            k.pos[0] = p.pos[0];
            k.pos[2] = p.pos[2];
        }
        k.sub = 1;
    }
    memcpy(k.last, p.pos, 12);
    const float d2 = dz * dz + dy * dy + dx * dx;
    if (k.sub == 1 || std::sqrt(d2) < F32(exe, 0x004C4CC8)) {
        k.alpha = k.alpha - F32(exe, 0x004C4CC4);
        k.scaleX = k.scaleX + F32(exe, 0x004C4CC0);
        k.grow = k.grow - F32(exe, 0x004C4CC0);
    } else {
        k.alpha = k.alpha + F32(exe, 0x004C4C88);
        k.scaleX = 1.0f;
        k.grow = 1.0f;
    }
    if (F32(exe, 0x004C4380) < k.alpha) k.alpha = 1.0f;
    if (k.alpha < F32(exe, 0x004C436C)) k.alpha = 0;
    if (k.grow < F32(exe, 0x004C436C)) k.grow = 0;
    float sz = k.anim.kind == 0 ? k.scaleX * F32(exe, 0x004C4CBC) : k.scaleX + k.scaleX;
    if (p.variant == 6) sz = k.scaleX * F32(exe, 0x004C4CB8);
    Mat4 m = Mat4::Identity();
    if (k.sub == 0) mx::Translate(m, p.pos[0], k.waterY, p.pos[2]);
    else mx::Translate(m, k.pos[0], k.waterY, k.pos[2]);
    mx::RotY(m, k.rot[1]);
    const float ahead[3] = { 0.0f, 0.0f, 2.0f };
    float pt[3];
    mx::Transform(m, ahead, pt);
    if (k.alpha == F32(exe, 0x004C436C)) return;
    k.frameF = k.frameF + k.grow;
    const int idx = static_cast<int>(k.frameF) % 0x32;  // __ftol
    Mat4 d = Mat4::Identity();
    mx::Translate(d, pt[0], pt[1], pt[2]);
    mx::RotY(d, k.rot[1]);
    mx::Scale(d, k.scaleX, 1.0f, sz);
    w.Draw(idx + 0x1A78, d, 8, k.alpha);  // 0x004185A0
}

// 0x00408770(parent, water y, yaw, mirror)
void NewWake(World::Impl& w, EnemyTask& e, float waterY, int yaw, bool mirror) {
    WakeTask* k = w.NewTask<WakeTask>(TaskWake, 0x004083D0);
    if (mirror) k->flags34 |= 4;
    k->parentEnemy = &e;
    k->grow = 1.0f;
    k->scaleX = 1.0f;
    k->alpha = 0.05f;  // 0x3D4CCCCD
    memcpy(k->last, e.pos, 12);
    k->waterY = waterY;
    k->yaw0 = yaw;
    k->parentFlags = e.flags34;
    k->anim.kind = e.anim.kind;  // +0x1F4
    k->actorSlot = -1;           // Actor_RegisterSlot 0x00409270
    for (int i = 0; i < 14; ++i)
        if (!w.actorSlots[i]) {
            k->flags38 |= 0x40;
            w.actorSlots[i] = k;
            k->actorSlot = i;
            break;
        }
}

float ParamF(const EnemyTask& e, int off) {
    const int32_t v = e.record.ParamS32(off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// 0x00409DC0: the target is valid only in the shooting camera (mode 2) and (demo or player state 5)
bool TargetValid(World::Impl& w, int t) {
    return w.runner->camera.modeCur == 2 && (w.cfg.gameMode == 5 || (t >= 0 && w.player[t & 1].state == 5));
}

// 0x00457CE0 (behaviour 24): glide to the point P float 1..3 in P s16 0x10 frames (motion P word 5), wait P s16 0x12,
// then attack (motion P word 6, hit at tick P u16 0x1C) the player P s8 0x1E (-1: random, else the free one); repeat.
// From the arrival on, the position is held at +0x13C0.
void Beh24(World::Impl& w, EnemyTask& e) {
    bool tail = false;
    switch (e.sub) {
    case 0: {
        w.AnimSetMotion(e, e.P(5));
        e.flags34 |= 0x2000;
        e.flags136C |= 0x100000;
        const int n = e.record.ParamS16(0x10);
        e.counter1330 = n;
        e.vel[0] = (ParamF(e, 4) - e.pos[0]) / static_cast<float>(n);
        e.vel[1] = (ParamF(e, 8) - e.pos[1]) / static_cast<float>(e.counter1330);
        ++e.sub;
        e.vel[2] = (ParamF(e, 0xC) - e.pos[2]) / static_cast<float>(e.counter1330);
    }
        [[fallthrough]];
    case 1:
        if (0 < --e.counter1330) break;
        memcpy(e.jumpStart, e.pos, 12);  // +0x13C0
        for (int i = 0; i < 3; ++i) e.vel[i] = e.acc[i] = 0;  // 0x0044E120
        ++e.sub;
        e.counter1330 = e.record.ParamS16(0x12);
        [[fallthrough]];
    case 2:
        if (w.players == 0) break;
        ++e.sub;
        [[fallthrough]];
    case 3: {
        if (0 < --e.counter1330 || w.players == 0) break;
        int t = e.record.ParamS8(0x1E);
        if (t == -1) t = w.Rand(0x00457E05) & 1;
        if (!TargetValid(w, t) || w.targetTaken[t & 1] == 1) {
            if (t == 0) t = (TargetValid(w, 1) && w.targetTaken[1] == 0) ? 1 : -1;
            else if (t == 1) t = (TargetValid(w, 0) && w.targetTaken[0] == 0) ? 0 : -1;
        }
        e.target = static_cast<int8_t>(t);
        if (t == -1) break;
        w.targetTaken[t & 1] = 1;
        w.EnemyFaceTarget(e);  // 0x00455F40
        e.flags34 &= ~0x4000u;
        w.AnimBlendTo(e, e.P(6), 0, 5);  // 0x004119A0
        ++e.sub;
    }
        [[fallthrough]];
    case 4:
        if (static_cast<uint32_t>(e.anim.tick[0]) != static_cast<uint16_t>(e.record.ParamS16(0x1C))) break;
        w.PlayerTakeHit(e.target, 1, 7);  // 0x00415300
        ++e.sub;
        [[fallthrough]];
    case 5:
        tail = true;
        break;
    default:
        break;
    }
    if (tail && w.MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
        w.EnemyReleaseTarget(e);  // 0x00456520
        e.counter1330 = e.record.ParamS16(0x12) * 2;
        const int r = w.Rand(0x00457F3D);
        w.AnimBlendTo(e, e.P(5), r % 10, 5);
        e.sub = 2;
    }
    if (e.sub > 1) memcpy(e.pos, e.jumpStart, 12);
}

int16_t EvS16(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a & ~3u, &w);
    return static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
}

uint32_t EvU32(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a, &w);
    return w;
}

// 0x0045CA60: the body (+0x10C view point, radius +0x124) is on screen
bool BodyOnScreen(World::Impl& w, const EnemyTask& e) {
    const float* v = e.bodyView;
    if (0.0f <= v[2]) return false;
    const float r = e.radius;
    const float ex = v[0] <= 0.0f ? -v[0] - r : r - v[0];
    const float ey = v[1] <= 0.0f ? -v[1] - r : r - v[1];
    const float s = w.focal / v[2];
    const float x1 = s * ex, y1 = s * ey;
    const float x2 = -((w.focal * v[0]) / v[2]);
    const float y2 = -((w.focal * v[1]) / v[2]);
    const float kx = F32(w.exe, 0x004C49CC), kx2 = F32(w.exe, 0x004D1D10), ky = F32(w.exe, 0x004C49C8), ky2 = F32(w.exe, 0x004C4D00);
    if ((x1 < kx || x2 < kx) && (kx2 < x1 || kx2 < x2) && (y1 < ky || y2 < ky)) return !(y1 <= ky2 && y2 <= ky2);
    return false;
}

// 0x0045BA40: the actor leaves the game (flags 0x4008001, killer from the parent civilian's scoring player, counts,
// actor slot), sub-state 4 (destroyed on the next run)
void LeaveScripted(World::Impl& w, EnemyTask& e) {
    e.flags34 |= 0x4008001;
    if (e.parent) {
        const CivTask& c = static_cast<const CivTask&>(*e.parent);
        if (c.c.scorePlayer == -1) {
            if (w.players == 1) e.killer = static_cast<int8_t>(w.playersInGame);
            else e.killer = static_cast<int8_t>(w.Rand(0x0045BA8A) & 1);
        } else {
            e.killer = static_cast<int8_t>(c.c.scorePlayer & 0xFF);
        }
    }
    if (!(e.flags38 & 2)) {  // 0x00456560
        e.flags38 |= 2;
        --w.liveEnemies;
    }
    if (!(e.flags38 & 4)) {  // 0x00456580
        e.flags38 |= 4;
        --w.attackers;
    }
    if (e.actorSlot != -1) w.actorSlots[e.actorSlot] = nullptr;
    e.sub = 4;
}

// 0x0045B7B0 (behaviour 38): motion list (P word 2 while behaviour P byte 3, else P word 1): {x, y, z target} then
// entries {s16 motion, start, loops, mode}; mode 1 / 2 turn to the target (0x00409F90 step 0x1A0), 0 / 1 leave when off
// screen, 3 leave at the end of the loops; a list end (motion < 1) leaves the actor in sub-state 3.
void Beh38(World::Impl& w, EnemyTask& e) {
    const EventMemory& mem = w.mem;
    switch (e.sub) {
    case 0: {
        e.flags34 |= 0x10000;
        const uint32_t list = static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1));  // 0x0045CA10
        for (int k = 0; k < 3; ++k) {
            const uint32_t u = EvU32(mem, list + 4 * static_cast<uint32_t>(k));
            memcpy(&e.jumpStart[k], &u, 4);  // +0x13C0 target
        }
        const int motion = EvS16(mem, list + 12);
        w.AnimBlendTo(e, motion, EvS16(mem, list + 14), e.anim.motion[0] == motion ? 0 : 10);
        e.headPitch = motion;                  // +0x1320
        e.facing1350 = EvS16(mem, list + 16);  // +0x1350 loops
        e.f1354 = EvS16(mem, list + 18);       // +0x1354 mode
        e.list1398 = list + 20;
        e.sub = 2;
        break;
    }
    case 1: {
        const uint32_t p = e.list1398;
        const int motion = EvS16(mem, p);
        if (e.anim.motion[0] != motion) w.AnimBlendTo(e, motion, EvS16(mem, p + 2), 10);
        e.headPitch = motion;
        e.facing1350 = EvS16(mem, p + 4);
        ++e.sub;
        e.f1354 = EvS16(mem, p + 6);
        e.list1398 = p + 8;
        break;
    }
    case 2:
        if (e.anim.tick[0] == w.MotionTicks(e.anim.motion[0]) - 1 && --e.facing1350 == 0) {
            if (EvS16(mem, e.list1398) < 1) ++e.sub;
            else e.sub = 1;
        }
        break;
    case 4:
        w.EnemyDestroy(e);
        return;
    default:
        break;
    }
    switch (e.f1354) {
    case 1:
        w.EnemyTurnTo(e, e.jumpStart[0], e.jumpStart[2], 0x1A0);  // 0x00409F90
        [[fallthrough]];
    case 0:
        if (!BodyOnScreen(w, e) && !(e.flags34 & 0x4000000)) LeaveScripted(w, e);
        break;
    case 2:
        w.EnemyTurnTo(e, e.jumpStart[0], e.jumpStart[2], 0x1A0);
        break;
    case 3:
        if (e.facing1350 == 0) LeaveScripted(w, e);
        break;
    default:
        break;
    }
    if (e.fn == 0x0045CD90 && e.vehicle) {  // the vehicle +0x13B0 flagged 0x400000: leave, a plain enemy (0x004533F0)
        bool live = false;
        for (Task* t : w.ts.Live()) live = live || t->data.get() == e.vehicle;
        if (!live) w.Note("behaviour 38: rider of a removed vehicle (the original reads its memory)");
        else if (e.vehicle->flags34 & 0x400000) {
            LeaveScripted(w, e);  // 0x0045BA40
            e.fn = fn::kEnemy;  // (drawn in the vehicle's frame for the rest of this frame)
        }
    }
    if (!(e.flags34 & 0x40000000)) {
        const int m = e.headPitch;
        if (e.anim.motion[0] != m) {
            if (e.facing1350 < 2) {
                w.AnimBlendTo(e, m, w.MotionTicks(e.anim.motion[0]) - 1, 1);
                return;
            }
            w.AnimBlendTo(e, m, 0, 10);
            --e.facing1350;
        }
    }
}

}  // namespace

// 0x00458E10: a variant-8 thrower facing the camera (yaw within 0x400 of the camera's - 0x8000) with a weapon in hand
// and a target (0x00455DE0): behaviour 0x21 (33)
bool World::Impl::EnemyThrowReady(EnemyTask& e) {
    if (e.variant != 8) return false;
    {  // 0x0040A040(camera yaw - 0x8000, yaw, 0x400)
        uint32_t a = static_cast<uint32_t>(cam.yaw - 0x8000) & 0xFFFF;
        const uint32_t b = static_cast<uint32_t>(e.rot[1]) & 0xFFFF;
        uint32_t hi = b + 0x10000 + 0x400, lo = (b - 0x400) + 0x10000;
        if (hi < lo) std::swap(hi, lo);
        if (a < lo) a = a + (((lo - a) + 0xFFFF) & 0xFFFF0000u);
        if (!(a <= hi)) return false;
    }
    const int kind = e.anim.kind;
    const int m5 = e.anim.nodes[5].model, m8 = e.anim.nodes[8].model;
    if (kind == 1) {
        if (EnemyPickTarget(e) != 1) return false;
        return m5 == 0x1BA9 || m8 == 0x1BA5;
    }
    if (kind == 0x13) {
        if (m5 != 0x1ECE && m8 != 0x1ECA) return false;
    } else if (kind == 0x14) {
        if (m5 != 0x1EF9 && m8 != 0x1EF5) return false;
    } else {
        return false;
    }
    return EnemyPickTarget(e) == 1;
}

bool World::Impl::EnemyBehaviourSt3(EnemyTask& e) {
    switch (e.behaviour) {
    case 24: Beh24(*this, e); return true;
    case 38: Beh38(*this, e); return true;
    default: return EnemyBehaviourVehicle(e);  // world_st3_vehicle.cpp
    }
}

// 0x00408740: the two wakes of an enemy that entered water (hook 0x00456920)
void World::Impl::SpawnWake(EnemyTask& e, float waterY, int yaw) {
    NewWake(*this, e, waterY, yaw, false);
    NewWake(*this, e, waterY, yaw, true);
}

}  // namespace game
