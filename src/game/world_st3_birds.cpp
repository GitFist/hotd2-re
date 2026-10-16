// game::World - actor type 0x46 (0x0042D9C0): flocks of small flying creatures (skeleton kind 0x1E, motion 0x407; each
// with a companion body 0x0042F660 of kind 0x1F). Variant 1: 25 that fly up and away (0x0042E9D0), variant 2: 8 (6 with
// one player) circling then diving at the players (0x0042ED50); variant 0 (one at a time, 0x0042E230) is not ported.
// Members are listed in 0x007DC918[group * 25 + index] (the companions follow them). Verification: notes/agents/stage3.md.

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

double SinA(int i) {  // fild; fmul qword 2pi/65536 (24-bit); fsin
    return std::sin(static_cast<double>(static_cast<float>(static_cast<double>(i) * mx::kToRad)));
}

double CosA(int i) {
    return std::cos(static_cast<double>(static_cast<float>(static_cast<double>(i) * mx::kToRad)));
}

int AngleOf(float y, float x) {  // fpatan; fmul qword -65536/2pi (0x0055CB20, 24-bit); __ftol (low 16 bits used)
    const float v = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) * -mx::kToAngle);
    return static_cast<int>(v) & 0xFFFF;
}

struct BirdTask : EnemyTask {
    int8_t group = 0;       // +0x1374 (spawner variant)
    int8_t state = 0;       // +0x1376
    int8_t idx = 0;         // +0x1377
    float v[3] = {};        // +0x1318 / +0x131C / +0x1320 velocity (variant 1, falling)
    float centre[3] = {};   // +0x130C / +0x1310 / +0x1314 (variant 2)
    float prev[2] = {};     // +0x1348 / +0x1350 previous x / z (heading)
    int angle = 0;          // +0x135C (variant 2 circle)
    int radiusPhase = 0;    // +0x1360
    int wobble = 0;         // +0x1380
    float t = 0;            // +0x13A4 (variant 2: circle radius, then the dive fraction)
    float sway = 1.0f;      // +0x13A8
    int timer = 0;          // +0x13D0
};

struct BirdBodyTask : EnemyTask {  // 0x0042F660 companion
    int8_t group = 0, idx = 0;
};

struct BirdFxTask : WTask {
    float pos[3] = {};
    int model = 0;
};

void TaskBirdBlood(Task& t) {  // 0x00430BD0 (0x00430C50): models 0x3A..0x52 at the view-space point
    World::Impl& w = *CurrentWorld();
    BirdFxTask& k = t.As<BirdFxTask>();
    const float s = w.GlobalInt(0x009C911E, 0) == 1 ? 0.15f : 0.3f;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::Scale(m, s, s, s);
    w.Draw(k.model++, m, 8, -1.0f, false, -1, true);
    if (0x52 < k.model) w.ts.KillCurrent();
}

void TaskBirdSplash(Task& t) {  // 0x0042F930 (0x0042F980): models 0x1339.. on the water (y -25), 30 frames
    World::Impl& w = *CurrentWorld();
    BirdFxTask& k = t.As<BirdFxTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    w.Draw(k.model + 0x1339, m);
    if (0x1E <= ++k.model) w.ts.KillCurrent();
}

void Splash(World::Impl& w, const float p[3]) {
    BirdFxTask* k = w.NewTask<BirdFxTask>(TaskBirdSplash, 0x0042F930);
    k->pos[0] = p[0];
    k->pos[1] = -25.0f;  // 0xC1C80000
    k->pos[2] = p[2];
    k->model = 0;
}

BirdTask*& Slot(World::Impl& w, int group, int idx) {
    return reinterpret_cast<BirdTask*&>(w.birdTable[(group * 0x19 + idx) % 75]);
}

void Remove(World::Impl& w, BirdTask& b) {
    Slot(w, b.group, b.idx) = nullptr;
    if (b.actorSlot != -1) w.actorSlots[b.actorSlot] = nullptr;
    w.EnemyDestroy(b);
}

// shot: sound (rand), +80 / hit for the shooter, blood flash at the view point
void Shot(World::Impl& w, BirdTask& b, uint32_t siteSound, uint32_t siteShooter) {
    w.Sound((w.Rand(siteSound) & 1) == 0 ? 0x4C17A9u : 0x4B17A9u);
    const uint32_t f = b.flags34;
    int p;
    if ((f & 2) && (f & 4)) p = w.Rand(siteShooter) & 1;
    else p = (f & 2) ? 0 : 1;
    w.AddScore(p, 0x50);
    ++w.player[p].hits;
    BirdFxTask* k = w.NewTask<BirdFxTask>(TaskBirdBlood, 0x00430BD0);
    memcpy(k->pos, b.viewPos, 12);
    k->model = 0x3A;
}

void Turn(BirdTask& b, int a, int shift, bool half) {
    int d = (static_cast<int>(static_cast<uint16_t>(-static_cast<int16_t>(b.rot[1]))) - a) & 0xFFFF;
    if (0x8000 < d) d -= 0x10000;
    const int step = half ? d / 2 : (d + ((d >> 31) & ((1 << shift) - 1))) >> shift;
    b.rot[1] = (step + b.rot[1]) & 0xFFFF;
}

bool Fall(World::Impl& w, BirdTask& b, float g, bool soundAlways) {  // state 2; true: removed
    b.v[1] = b.v[1] - g;
    b.pos[0] = b.v[0] + b.pos[0];
    b.pos[2] = b.v[2] + b.pos[2];
    b.pos[1] = b.v[1] + b.pos[1];
    if (b.rot[0] < 0x8000) b.rot[0] += 0x200;
    b.rot[1] += 0x800;
    if (b.pos[1] < F32(w.exe, 0x0055D2C8)) {
        Slot(w, b.group, b.idx) = nullptr;
        if (soundAlways) {  // variant 1: splash, slot, sound
            Splash(w, b.pos);
            if (b.actorSlot != -1) w.actorSlots[b.actorSlot] = nullptr;
            w.Sound(0x4616A9);
        } else {
            if (b.actorSlot != -1) w.actorSlots[b.actorSlot] = nullptr;
            Splash(w, b.pos);
            if (w.cfg.eventMode == 2) w.Sound(0x4616A9);
        }
        w.EnemyDestroy(b);
        return true;
    }
    return false;
}

void DrawBird(World::Impl& w, BirdTask& b) {
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);  // +0x120 written by the table task 0x00408DD0
    Slot(w, b.group, b.idx) = &b;
    w.AnimDraw(b);  // 0x00411090
    w.EnemyShadow(b);
    ++b.anim.counter[0];
}

void TaskFlyAway(Task& t) {  // 0x0042E9D0 (variant 1)
    World::Impl& w = *CurrentWorld();
    BirdTask& b = t.As<BirdTask>();
    const ExeImage& exe = w.exe;
    DrawBird(w, b);
    if (b.state == 0) {
        if (b.timer-- < 1) b.state = 1;
        return;
    }
    if (b.state == 1) {
        if (b.flags34 & 8) {
            b.flags34 &= ~8u;
            Shot(w, b, 0x0042EB0E, 0x0042EB44);
            b.state = 2;
            b.wobble = 0;
            b.v[0] = b.v[0] * F32(exe, 0x0055D2C4);
            b.v[2] = b.v[2] * F32(exe, 0x0055D2C4);
        }
        b.v[0] = b.v[0] * F32(exe, 0x0055D2C0);
        b.v[1] = b.v[1] * F32(exe, 0x0055D2BC);
        b.pos[0] = b.v[0] + b.pos[0];
        b.pos[1] = b.v[1] + b.pos[1];
        b.pos[2] = b.v[2] + b.pos[2];
        Turn(b, AngleOf(b.prev[0] - b.pos[0], b.prev[1] - b.pos[2]), 3, false);
        if (F32(exe, 0x0055D2B8) < b.pos[2]) {
            Remove(w, b);
            return;
        }
        b.prev[0] = b.pos[0];
        b.prev[1] = b.pos[2];
        const float p[3] = { b.pos[0], static_cast<float>(b.pos[1] + F64(exe, 0x004ECB70)), b.pos[2] };
        mx::Transform(w.cam.view, p, b.viewPos);
        w.RegisterTarget(&b);  // 0x00405160
        return;
    }
    if (b.state == 2) Fall(w, b, F32(exe, 0x0055D2CC), true);
}

void TaskDive(Task& t) {  // 0x0042ED50 (variant 2)
    World::Impl& w = *CurrentWorld();
    BirdTask& b = t.As<BirdTask>();
    const ExeImage& exe = w.exe;
    DrawBird(w, b);
    if ((b.flags34 & 8) && b.state != 2) {
        b.flags34 &= ~8u;
        --w.liveEnemies;
        --w.attackers;
        Shot(w, b, 0x0042EDD6, 0x0042EE0C);
        b.state = 2;
        b.wobble = 0;
        b.flags34 |= 0x10000;
        if (b.slot120 != -1) w.lockOn.Release(b.slot120);
        b.v[0] = b.v[0] * F32(exe, 0x0055D2D8);
        b.v[2] = b.v[2] * F32(exe, 0x0055D2D8);
    }
    if (b.state == 0) {
        b.angle += 0x400;
        const int r = w.Rand(0x0042F278);
        b.prev[0] = b.pos[0];
        const int timer = b.timer;
        b.prev[1] = b.pos[2];
        b.radiusPhase = b.radiusPhase + r % 0x201 + 0xE00;
        b.timer = timer - 1;
        const float rad = static_cast<float>(SinA(b.radiusPhase) * F32(exe, 0x0055D2B4)) + F32(exe, 0x004D1D20);
        b.t = rad;
        const float x = static_cast<float>(SinA(b.angle) * rad) + b.centre[0];
        b.pos[0] = x;
        const float y = static_cast<float>(b.anim.root[1] * F64(exe, 0x0055D2D0)) + b.centre[1];
        b.pos[1] = y;
        const float z = static_cast<float>(CosA(b.angle) * rad) + b.centre[2];
        b.pos[2] = z;
        if (timer < 1) {
            b.centre[2] = z;
            b.state = 1;
            b.t = 0;
            b.centre[0] = x;
            b.centre[1] = y;
            b.sway = 1.0f;
        }
        Turn(b, AngleOf(b.prev[0] - b.pos[0], b.prev[1] - b.pos[2]), 0, true);
    } else if (b.state == 1) {
        if (F32(exe, 0x004C43A8) < b.t) b.sway = (F32(exe, 0x004C4380) - b.t) * F32(exe, 0x0055D2B4);
        b.pos[0] = (w.cam.pos[0] - b.centre[0]) * b.t + b.centre[0];
        b.pos[1] = b.anim.root[1] * b.sway * F32(exe, 0x0055D2B4) + ((w.cam.pos[1] - b.centre[1]) * b.t + b.centre[1]);
        b.prev[0] = b.pos[0];
        b.wobble += 0x800;
        b.pos[2] = (w.cam.pos[2] - b.centre[2]) * b.t + b.centre[2];
        b.prev[1] = b.pos[2];
        b.t = b.t + F32(exe, 0x004C4CC0);
        Mat4 m = Mat4::Identity();
        mx::RotY(m, w.cam.yaw + 0x8000);
        const float in[3] = { static_cast<float>(SinA(b.wobble)), 0.0f, 0.0f };
        float o[3];
        mx::Transform(m, in, o);
        b.pos[0] = o[0] * b.sway * F32(exe, 0x004C49C0) + b.pos[0];
        b.pos[2] = o[2] * b.sway * F32(exe, 0x004C49C0) + b.pos[2];
        Turn(b, AngleOf(b.prev[0] - w.runner->camera.ctx[0].pos[0], b.prev[1] - w.runner->camera.ctx[0].pos[2]), 2, false);  // context 0 (0x009A60C0)
        if (F32(exe, 0x004C4380) < b.t) {
            if (w.player[0].state == 5 || w.player[1].state == 5) {
                if (w.players == 2) b.target = static_cast<int8_t>(w.Rand(0x0042F1F8) & 1);
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
        if (Fall(w, b, F32(exe, 0x0055CB10), false)) return;
    }
    memcpy(b.body, b.pos, 12);
    w.SyncLock(b);
    w.lockOn.Register(b.lock, w.playerPos);  // 0x00408EC0
    const float p[3] = { b.pos[0], static_cast<float>(b.pos[1] + F64(exe, 0x004ECB70)), b.pos[2] };
    mx::Transform(w.cam.view, p, b.viewPos);
    w.RegisterTarget(&b);
}

void TaskBirdBody(Task& t) {  // 0x0042F660
    World::Impl& w = *CurrentWorld();
    BirdBodyTask& c = t.As<BirdBodyTask>();
    BirdTask* m = Slot(w, c.group, c.idx);
    if (!m) {
        if (c.actorSlot != -1) w.actorSlots[c.actorSlot] = nullptr;
        w.EnemyDestroy(c);
        return;
    }
    {  // camera matrix * member node 1 model-view, translated (0, 1, 2): the world point (approximated through the
       // native world node matrix)
        Mat4 q = m->anim.nodes[1].world;
        mx::Translate(q, 0.0f, 1.0f, 2.0f);
        float wv[3], vv[3];
        mx::GetTranslation(q, wv);
        mx::Transform(w.cam.view, wv, vv);
        mx::Transform(w.cam.inverse, vv, c.pos);
    }
    c.rot[0] = 0xE800;
    c.rot[1] = m->rot[1] + 0x8000;
    for (int i = 0; i < 5; ++i)
        if (w.exe.S16(0x0058992C + 2 * static_cast<uint32_t>(i)) == m->anim.motion[0]) {
            c.anim.motion[0] = w.exe.S16(0x00589938 + 2 * static_cast<uint32_t>(i));
            break;
        }
    c.anim.counter[0] = m->anim.counter[0];
    w.AnimDraw(c);
    w.EnemyShadow(c);
}

void Common(World::Impl& w, EnemyTask& e, int kind, int motion) {
    e.anim.kind = static_cast<int16_t>(kind);
    e.anim.motion[0] = motion;
    e.flags34 = 1;
    e.actorSlot = -1;
    e.target = 0;  // +0x121 is never set by 0x0042D9C0 (cleared task memory): the lock-on table puts them in slots 0 / 1
    w.EnemyInitPose(e);  // Anim_InitEnemy 0x00410440 (+ actor slot of 0x009A26A0 = this object)
    e.anim.rotOrder = 5;
    e.nodeCb = 0x00411050;  // 0x0042E020: the node's model
}

void SpawnBody(World::Impl& w, const BirdTask& b) {  // 0x0042E060
    BirdBodyTask* c = w.NewTask<BirdBodyTask>(TaskBirdBody, 0x0042F660);
    Common(w, *c, 0x1F, 0x406);
    c->flags34 = 0x80001;
    c->anim.counter[0] = 0;
    c->group = b.group;
    c->idx = b.idx;
}

}  // namespace

void World::Impl::SpawnBirds(Task& t, SpawnTask& s) {  // 0x0042D9C0
    const SpawnRecord& r = s.rec;
    const ExeImage& x = exe;
    const int variant = s.op == 0x09 ? r.ParamS8(1) : 0;  // spawn object +0x130C
    if (variant == 1) {
        for (int i = 0; i < 0x19; ++i) {
            BirdTask* b = NewTask<BirdTask>(TaskFlyAway, 0x0042E9D0);
            Common(*this, *b, 0x1E, 0x407);
            b->anim.counter[0] = i;
            b->idx = static_cast<int8_t>(i);
            b->flags34 = (b->flags34 & ~0x80u) | 0x80000;
            b->group = 1;
            b->pos[0] = (static_cast<float>(Rand(0x0042DC40) % 0x15) + r.pos[0]) - F32(x, 0x004ECB7C);
            b->pos[1] = static_cast<float>(Rand(0x0042DC61) % 0xB) * F32(x, 0x004D5464) + r.pos[1];
            b->pos[2] = r.pos[2];
            b->prev[0] = r.pos[0];
            b->prev[1] = r.pos[2];
            b->rot[0] = r.rot[0];
            b->rot[1] = r.rot[1];
            b->rot[2] = 0;
            b->v[0] = (static_cast<float>(Rand(0x0042DCAD) % 0x65) * F32(x, 0x0055D2B0) - F32(x, 0x004C4C88)) * F32(x, 0x004C43A8);
            const int r4 = Rand(0x0042DCDA);
            b->v[2] = 2.0f;
            b->radius = 4.0f;
            b->state = 0;
            b->slot120 = -1;
            b->v[1] = static_cast<float>(r4 % 0xB) * F32(x, 0x0055D2B0) + F32(x, 0x004C4CC0);
            b->timer = b->idx * 2;
            Slot(*this, b->group, b->idx) = b;
            SpawnBody(*this, *b);
        }
    } else if (variant == 2) {
        const int n = players > 1 ? 8 : 6;
        for (int i = 0; i < n; ++i) {
            BirdTask* b = NewTask<BirdTask>(TaskDive, 0x0042ED50);
            Common(*this, *b, 0x1E, 0x407);
            b->anim.counter[0] = i * 10;
            b->idx = static_cast<int8_t>(i);
            b->flags34 = (b->flags34 & ~0x80u) | 0x80000;
            b->group = 2;
            b->pos[1] = r.pos[1];
            b->centre[0] = (static_cast<float>(Rand(0x0042DA9F) % 0x191) * F32(x, 0x004D5464) + r.pos[0]) - F32(x, 0x004E30F0);
            b->centre[1] = (static_cast<float>(Rand(0x0042DAC9) % 0x191) * F32(x, 0x004D5464) + r.pos[1]) - F32(x, 0x004E30F0);
            b->centre[2] = (static_cast<float>(Rand(0x0042DAF3) % 0x191) * F32(x, 0x004D5464) + r.pos[2]) - F32(x, 0x004E30F0);
            b->prev[0] = r.pos[0];
            b->prev[1] = r.pos[2];
            b->radius = 4.0f;
            b->state = 0;
            b->slot120 = -1;
            b->timer = 0x3C + i * 0x14;
            b->angle = i << 13;
            b->lock = &lockTargets[nextLockTarget++];
            SyncLock(*b);
            lockOn.AddDirect(b->lock);  // 0x00408E80
            b->slot120 = static_cast<int8_t>(b->lock->slot);
            ++attackers;
            ++liveEnemies;
            Slot(*this, b->group, b->idx) = b;
            SpawnBody(*this, *b);
        }
    } else if (variant == 0) {
        SpawnBird0(r);  // 0x0042E230, world_st4_birds.cpp
    } else {
        Note("actor 0x46 variant " + std::to_string(variant) + " not ported");
    }
    ts.KillCurrent();  // 0x004A7040
    (void)t;
}

// Exports for the variant-0 flock member (world_st4_birds.cpp): its companion body and the shot effects
void World::Impl::BirdSpawnBody(EnemyTask& member, int group, int idx) {
    BirdTask tmp;
    tmp.group = static_cast<int8_t>(group);
    tmp.idx = static_cast<int8_t>(idx);
    SpawnBody(*this, tmp);
    (void)member;
}
void World::Impl::BirdShotFx(EnemyTask& b, uint32_t siteSound, uint32_t siteShooter) {
    BirdTask tmp;
    tmp.flags34 = b.flags34;
    memcpy(tmp.viewPos, b.viewPos, sizeof(tmp.viewPos));
    Shot(*this, tmp, siteSound, siteShooter);
}

}  // namespace game
