// game::World - actor type 0x46 variant 0 (0x0042D9C0 -> task 0x0042E230): a single flying creature per record that
// follows a two-segment quadratic B-spline (control points 0x00589944) and then dives at the players (stage 4). The
// other variants are in world_st3_birds.cpp, whose slot table (0x007DC918), companion body (0x0042E060 / 0x0042F660)
// and shot effects this reuses. Verification: notes/agents/stage4.md.

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

double SinA(int i) {  // fild; fmul 2pi/65536 (24-bit); fsin
    return std::sin(static_cast<double>(static_cast<float>(static_cast<double>(i) * mx::kToRad)));
}
double CosA(int i) {
    return std::cos(static_cast<double>(static_cast<float>(static_cast<double>(i) * mx::kToRad)));
}

int AngleOf(float y, float x) {  // fpatan; fmul -65536/2pi (0x0055CB20, 24-bit); __ftol (low 16 bits used)
    const float v = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) * -mx::kToAngle);
    return static_cast<int>(v) & 0xFFFF;
}

// The member (block 0x13D8; same field offsets as the other flock members)
struct Bird0Task : EnemyTask {
    int8_t group = 0;       // +0x1374 (spawner variant: 0)
    int8_t row = 0;         // +0x1375 (spawner +0x1F4: path family)
    int8_t state = 0;       // +0x1376
    int8_t idx = 0;         // +0x1377 (record state - 1)
    float v[3] = {};        // +0x1318 / +0x131C / +0x1320 (falling)
    float start[3] = {};    // +0x130C / +0x1310 / +0x1314 dive start
    float prev[2] = {};     // +0x1348 / +0x1350 previous x / z (heading)
    int wobble = 0;         // +0x1380 (falling: frame counter)
    float t = 0;            // +0x13A4 dive fraction
    float sway = 1.0f;      // +0x13A8
    int seg = 0;            // +0x13CC spline segment
    int timer = 0;          // +0x13D0
    float u = 0;            // +0x13D4 spline parameter
};

Bird0Task*& Slot(World::Impl& w, int group, int idx) {
    return reinterpret_cast<Bird0Task*&>(w.birdTable[(group * 0x19 + idx) % 75]);
}

// 0x0042DFD0(seg, u): quadratic B-spline weights
void Weights(const ExeImage& exe, float u, float* a, float* b, float* c) {
    const float half = F32(exe, 0x004C43AC);
    const float m = F32(exe, 0x004C4380) - u;
    *a = m * m * half;
    *b = m * u + half;
    *c = u * u * half;
}

// control point k of the member's path (3 shorts)
int16_t Ctl(const World::Impl& w, const Bird0Task& b, int k, int axis) {
    const int row = b.row * 3 + b.idx % 3;
    return w.exe.S16(0x00589944 + static_cast<uint32_t>((row * 4 + k) * 6 + axis * 2));
}

void Turn(Bird0Task& b, int a) {  // rot y += (-rot y - a, wrapped) / 8
    int d = (static_cast<int>(static_cast<uint16_t>(-static_cast<int16_t>(b.rot[1]))) - a) & 0xFFFF;
    if (0x8000 < d) d -= 0x10000;
    b.rot[1] = (((d + ((d >> 31) & 7)) >> 3) + b.rot[1]) & 0xFFFF;
}

void Remove(World::Impl& w, Bird0Task& b) {
    if (b.actorSlot != -1) w.actorSlots[b.actorSlot] = nullptr;
    Slot(w, b.group, b.idx) = nullptr;
    w.EnemyDestroy(b);  // 0x00409CC0
}

void TaskBird0(Task& t) {  // 0x0042E230
    World::Impl& w = *CurrentWorld();
    Bird0Task& b = t.As<Bird0Task>();
    const ExeImage& exe = w.exe;
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);  // +0x120 written by the table task 0x00408DD0
    Slot(w, b.group, b.idx) = &b;
    w.AnimDraw(b);  // 0x00411090 (between 0x0041DC70 / 0x0041DCC0)
    w.EnemyShadow(b);
    ++b.anim.counter[0];
    if ((b.flags34 & 8) && b.state != 2 && b.state != 0) {
        b.flags34 &= ~8u;
        --w.liveEnemies;
        --w.attackers;
        w.BirdShotFx(b, 0x0042E2BD, 0x0042E2F3);  // sound (rand), +80 and a hit for the shooter, blood 0x00430C50
        b.state = 2;
        b.wobble = 0;
        b.flags34 |= 0x10000;
        if (b.slot120 != -1) w.lockOn.Release(b.slot120);
        const int a = b.rot[1] + 0x8000;
        b.v[0] = static_cast<float>(SinA(a) * F64(exe, 0x004C4C98));
        b.v[2] = static_cast<float>(CosA(a) * F64(exe, 0x004C4C98));
    }
    if (b.state == 0) {
        if (b.timer-- < 1) b.state = 1;
    } else if (b.state == 1) {
        if (b.seg < 2) {
            float fa, fb, fc;
            Weights(exe, b.u, &fa, &fb, &fc);
            const int k = b.seg;
            auto C = [&](int i, int axis) { return static_cast<float>(Ctl(w, b, k + i, axis)); };
            b.pos[0] = C(2, 0) * fc + C(1, 0) * fb + C(0, 0) * fa;
            const float lift = b.anim.root[1] * F32(exe, 0x0055D2B4);  // +0x204
            b.pos[1] = C(2, 1) * fc + C(1, 1) * fb + C(0, 1) * fa + lift;
            b.pos[2] = C(2, 2) * fc + C(1, 2) * fb + C(0, 2) * fa;
            const float y2 = lift + b.pos[1];
            b.pos[1] = y2;
            const int a = AngleOf(b.prev[0] - b.pos[0], b.prev[1] - b.pos[2]);
            const float u = b.u + F32(exe, 0x004C4C88);
            b.u = u;
            Turn(b, a);
            if (F32(exe, 0x004C4380) < u) {
                b.u = 0;
                b.seg = k + 1;
                if (1 < b.seg) {
                    b.start[0] = b.pos[0];
                    b.start[2] = b.pos[2];
                    b.start[1] = y2 - lift;
                    b.t = 0;
                }
            }
        } else {
            if (F32(exe, 0x004C43A8) < b.t) b.sway = (F32(exe, 0x004C4380) - b.t) * F32(exe, 0x0055D2B4);
            const float* cp = w.cam.pos;  // 0x009A60C0[ctx]
            b.pos[0] = (cp[0] - b.start[0]) * b.t + b.start[0];
            b.pos[1] = (cp[1] - b.start[1]) * b.t + b.anim.root[1] * F32(exe, 0x0055D2B4) * b.sway + b.start[1];
            b.pos[2] = (cp[2] - b.start[2]) * b.t + b.start[2];
            b.t = b.t + F32(exe, 0x004C4CC4);
            Turn(b, AngleOf(b.prev[0] - b.pos[0], b.prev[1] - b.pos[2]));
        }
        b.prev[0] = b.pos[0];
        b.prev[1] = b.pos[2];
        b.wobble += 0x800;
        Mat4 m = Mat4::Identity();
        mx::RotY(m, w.cam.yaw + 0x8000);  // 0x009A60D0[ctx]
        const float in[3] = { static_cast<float>(SinA(b.wobble)), 0.0f, 0.0f };
        float o[3];
        mx::Transform(m, in, o);
        b.pos[0] = o[0] * b.sway * F32(exe, 0x004C49C0) + b.pos[0];
        b.pos[2] = o[2] * b.sway * F32(exe, 0x004C49C0) + b.pos[2];
        if (F32(exe, 0x004C4380) < b.t) {
            if (w.player[0].state == 5 || w.player[1].state == 5) {
                if (w.players == 2) b.target = static_cast<int8_t>(w.Rand(0x0042E8CE) & 1);
                else if (w.playersInGame == 0) b.target = 0;
                else if (w.playersInGame == 1) b.target = 1;
                w.PlayerTakeHit(b.target, 1, 9);  // 0x00415300
            }
            --w.liveEnemies;
            --w.attackers;
            Remove(w, b);
            return;
        }
    } else if (b.state == 2) {
        b.v[1] = b.v[1] - F32(exe, 0x0055CB10);
        b.pos[0] = b.v[0] + b.pos[0];
        b.pos[2] = b.v[2] + b.pos[2];
        b.pos[1] = b.v[1] + b.pos[1];
        if (b.rot[0] < 0x8000) b.rot[0] += 0x200;
        b.rot[1] += 0x800;
        if (0x50 < b.wobble++) {
            Remove(w, b);
            return;
        }
    }
    memcpy(b.body, b.pos, 12);  // +0x100
    w.SyncLock(b);
    w.lockOn.Register(b.lock, w.playerPos);  // 0x00408EC0
    const float p[3] = { b.pos[0], static_cast<float>(b.pos[1] + F64(exe, 0x004ECB70)), b.pos[2] };
    mx::Transform(w.cam.view, p, b.viewPos);
    w.RegisterTarget(&b);  // 0x00405160
}

}  // namespace

// 0x0042D9C0 variant 0: with two players or record state < 5, one member: index state - 1, path family the spawner's
// +0x1F4 (op 0x09 params byte 0), start at the spline's first point, waits index * 20 frames
void World::Impl::SpawnBird0(const SpawnRecord& r) {
    if (!(1 < players || static_cast<int16_t>(r.state) < 5)) return;
    const ExeImage& x = exe;
    Bird0Task* b = NewTask<Bird0Task>(TaskBird0, 0x0042E230);
    b->anim.kind = 0x1E;
    b->anim.motion[0] = 0x407;
    b->flags34 = 1;
    b->actorSlot = -1;
    b->target = 0;  // +0x121 never set (cleared task memory)
    EnemyInitPose(*b);  // Anim_InitEnemy 0x00410440
    b->anim.rotOrder = 5;
    b->nodeCb = 0x00411050;  // 0x0042E020
    b->anim.counter[0] = static_cast<int16_t>(r.state) * 7;
    b->flags34 = (b->flags34 & ~0x80u) | 0x80000;
    b->idx = static_cast<int8_t>(static_cast<int16_t>(r.state) - 1);
    b->group = 0;
    b->row = static_cast<int8_t>(r.params[0]);
    float fa, fb, fc;
    Weights(x, 0.0f, &fa, &fb, &fc);
    auto C = [&](int i, int axis) { return static_cast<float>(Ctl(*this, *b, i, axis)); };
    b->pos[0] = C(0, 0) * fa + C(2, 0) * fc + C(1, 0) * fb;
    b->pos[1] = C(2, 1) * fc + C(1, 1) * fb + C(0, 1) * fa;
    b->pos[2] = C(2, 2) * fc + C(1, 2) * fb + C(0, 2) * fa;
    const int a = b->rot[1] + 0x8000;  // +0x68 still 0 here
    b->prev[0] = static_cast<float>(SinA(a) + static_cast<double>(b->pos[0]));
    b->prev[1] = static_cast<float>(CosA(a) + static_cast<double>(b->pos[2]));
    b->rot[0] = r.rot[0];
    b->rot[1] = r.rot[1];
    b->rot[2] = 0;
    b->radius = 4.0f;
    b->state = 0;
    b->slot120 = -1;
    b->timer = b->idx * 0x14;
    b->seg = 0;
    b->u = 0;
    b->sway = 1.0f;
    b->lock = &lockTargets[nextLockTarget++];
    SyncLock(*b);
    lockOn.AddDirect(b->lock);  // 0x00408E80
    b->slot120 = static_cast<int8_t>(b->lock->slot);
    ++attackers;
    ++liveEnemies;
    Slot(*this, b->group, b->idx) = b;
    BirdSpawnBody(*b, b->group, b->idx);  // 0x0042E060
}

}  // namespace game
