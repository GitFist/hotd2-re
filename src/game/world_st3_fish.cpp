// game::World - actor type 0x51 (0x00438540 -> task 0x00438730): the fish that leap out of the water at the players
// (stage 3; also seen in stage 2). Verification: notes/agents/stage3.md.
//
// Control block (0x84 bytes, obj +0x1310): +0x00 spawn point, +0x0C float base y (states 4 / 5), +0x10 jump start,
// +0x1C / +0x20 / +0x24 velocity, +0x28 / +0x2C / +0x30 jump delta (or scales), +0x3C / +0x40 spin step / phase,
// +0x44 bob height, +0x48 alpha, +0x50 yaw, +0x54 yaw step, +0x58 pitch, +0x5C pitch step, +0x60 s16 yaw step (dead),
// +0x62 s16 state, +0x64 s16 counter, +0x68 s16 bob phase, +0x6A s16 flags (1 under water, 2 landed, 4 opaque + shadow),
// +0x6C s16 jump kind (P s16 0xE), +0x6E / +0x70 / +0x72 s16 animated model (current, last, loop start), +0x74 s16
// jump tick, +0x76 s16 attack slot (0x009A2C20[4]), +0x78 / +0x7A s16 bob laps (P s16 0x12) / done, +0x7C s16 swim
// frames (P s16 0x10), +0x7E s16 jump frames (P s16 0x14), +0x80 s16 side step sign.
// States: 0 swim in (alpha fade in), 1 bob, 2 jump at the player (0x00438850 picks one of four points in front of the
// camera; a hit when it gets within 8), 3 fall back, 4 shot in the air (spinning fall), 5 floating dead. Record kind 6:
// sets the water level 0x007DCBB0 (P float 0) and clears the attack slots.

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

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// fild i; fmul qword 2pi/65536 (24-bit); fsin
double SinA(int i) {
    const float a = static_cast<float>(static_cast<double>(i) * mx::kToRad);
    return std::sin(static_cast<double>(a));
}

// fpatan(y, x); fmul qword 65536/2pi (24-bit); __ftol; s16
int AngleOf(float y, float x) {
    const float v = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) * mx::kToAngle);
    return static_cast<int16_t>(static_cast<int>(v));
}

struct FishCtl {
    float spawn[3] = {};   // +0x00
    float baseY = 0;       // +0x0C
    float start[3] = {};   // +0x10
    float vel[3] = {};     // +0x1C
    float d[3] = {};       // +0x28
    float spinStep = 0;    // +0x3C
    float spin = 0;        // +0x40
    float bob = 0;         // +0x44
    float alpha = 1.0f;    // +0x48
    int16_t mode = 0;      // +0x4C
    int yaw = 0;           // +0x50
    int yawStep = 0;       // +0x54
    int pitch = 0;         // +0x58
    int pitchStep = 0;     // +0x5C
    int16_t deadYawStep = 0;  // +0x60
    int16_t state = 0;     // +0x62
    int16_t counter = 0;   // +0x64
    int16_t s66 = 0;       // +0x66
    int16_t bobPhase = 0;  // +0x68
    uint16_t flags = 0;    // +0x6A
    int16_t kind = 0;      // +0x6C
    int16_t model = 0x1156, modelLast = 0x1169, modelLoop = 0x1156;  // +0x6E / +0x70 / +0x72
    int16_t jumpTick = 0;  // +0x74
    int16_t slot = -1;     // +0x76
    int16_t laps = 1;      // +0x78
    int16_t lapsDone = 0;  // +0x7A
    int16_t swimFrames = 0;  // +0x7C
    int16_t jumpFrames = 0;  // +0x7E
    int16_t side = 0;      // +0x80
};

struct FishTask : EnemyTask {
    FishCtl c;
    bool escaping = false;  // task function 0x00439C20
};

void NextModel(FishCtl& c) {
    if (c.modelLast < ++c.model) c.model = c.modelLoop;
}

void ReleaseSlot(World::Impl& w, FishCtl& c) {
    if (c.slot != -1) {
        w.fishSlots[c.slot & 3] = 0;
        c.slot = -1;
    }
}

// 0x00439F10 (created by 0x00439EA0): splash models 0x1339..0x1356 on the water, facing the camera's yaw
struct FishFxTask : WTask {
    float pos[3] = {};  // +0x38
    int model = 0;      // +0x54
    float scale = 1.0f; // +0x58
    float grow = 0;     // +0x5C
    float alpha = 1.0f; // +0x60
    int16_t kind = 0;   // +0x64
    int16_t counter = 0;  // +0x66
};

void TaskSplash(Task& t) {
    World::Impl& w = *CurrentWorld();
    FishFxTask& s = t.As<FishFxTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::Scale(m, s.scale, s.scale, s.scale);
    mx::RotY(m, w.cam.yaw);  // 0x009A60D0[ctx]
    w.Draw(s.model++, m);
    if (0x1356 < s.model) w.ts.KillCurrent();
}

// 0x00439EA0(fish, scale, kind)
void Splash(World::Impl& w, FishTask& f, float scale, int kind) {
    FishFxTask* s = w.NewTask<FishFxTask>(TaskSplash, 0x00439F10);
    s->pos[0] = f.pos[0];
    s->pos[1] = w.waterLevel - F32(w.exe, 0x0055D1A0);
    s->pos[2] = f.pos[2];
    s->model = 0x1339;
    s->scale = scale;
    s->kind = static_cast<int16_t>(kind);
    w.Sound(kind == 0 ? 0x4216A9u : 0x4116A9u);
}

// 0x0043A000 (created by 0x00439FA0): ring model 0xB71 growing (+0x5C) and fading (0x0055DCF4) for 60 frames
void TaskRing(Task& t) {
    World::Impl& w = *CurrentWorld();
    FishFxTask& s = t.As<FishFxTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::Scale(m, s.scale, Bits(0x3E4CCCCD), s.scale);
    w.Draw(s.model, m, 8, s.alpha);
    ++s.counter;
    s.scale = s.grow + s.scale;
    s.alpha = s.alpha - F32(w.exe, 0x0055DCF4);
    if (s.counter == 0x3C) w.ts.KillCurrent();
}

void Ring(World::Impl& w, FishTask& f, float scale) {  // 0x00439FA0
    FishFxTask* s = w.NewTask<FishFxTask>(TaskRing, 0x0043A000);
    s->pos[0] = f.pos[0];
    s->pos[1] = w.waterLevel - F32(w.exe, 0x004D1D24);
    s->pos[2] = f.pos[2];
    s->model = 0xB71;
    s->scale = scale;
    s->grow = Bits(0x3CA3D70A);
    s->alpha = 1.0f;
}

// 0x00439E00 (created by 0x00439DC0 in game mode 6): hit flash models 0x3A..0x52 at the view-space point
void TaskFlash(Task& t) {
    World::Impl& w = *CurrentWorld();
    FishFxTask& s = t.As<FishFxTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    const float k = w.GlobalInt(0x009C911E, 0) == 1 ? Bits(0x3D4CCCCD) : Bits(0x3DCCCCCD);
    mx::Scale(m, k, k, k);
    w.Draw(s.model++, m, 8, -1.0f, false, -1, true);
    if (0x52 < s.model) w.ts.KillCurrent();
}

// 0x00438850: an attack slot (0..3, 0x009A2C20) and the jump towards its point in front of the camera; -1: none free
int StartJump(World::Impl& w, FishTask& f) {
    FishCtl& c = f.c;
    int u = 0;
    auto scan = [&]() {
        for (u = 0; u < 4; ++u)
            if (w.fishSlots[u] == 0) return true;
        return false;
    };
    if (w.players == 1) {
        u = w.Rand(0x004388DA) & 3;
        if (w.fishSlots[u] != 0 && !scan()) return -1;
        w.fishSlots[u] = 1;
        f.target = w.playersInGame == 0 ? 0 : 1;
    } else if (w.players == 2) {
        u = w.Rand(0x00438873) & 3;
        if (w.fishSlots[u] != 0 && !scan()) return -1;
        w.fishSlots[u] = 1;
        f.target = u < 2 ? 0 : 1;
    } else {
        return -1;
    }
    c.slot = static_cast<int16_t>(u);
    const float up = c.kind == 2 ? F32(w.exe, 0x004C4380) : 0.0f;
    float local[3] = { u < 2 ? -1.5f : 1.5f, (u & 1) ? up - F32(w.exe, 0x004E30F0) : up, -6.0f };
    float tg[3];
    mx::Transform(w.cam.inverse, local, tg);  // 0x004A9230(camera matrix 0x009A6040)
    float dx = tg[0] - f.pos[0];
    const float dy = tg[1] - f.pos[1];
    const float dz = tg[2] - f.pos[2];
    if (dx == 0.0f) dx = 1.0f;
    c.yaw = AngleOf(dx, dz);
    c.pitch = AngleOf(dz, dy);
    c.d[0] = dx;
    c.d[1] = dy;
    c.d[2] = dz;
    if (c.kind == 2) c.side = static_cast<int16_t>(w.Rand(0x00438B67) % 10 < 5 ? -2 : 2);
    const float n = static_cast<float>(c.jumpFrames);
    c.state = 2;
    c.counter = 0;
    c.jumpTick = 0;
    c.model = c.modelLoop = 0x1156;
    c.modelLast = 0x1169;
    c.vel[0] = dx / n;
    c.vel[1] = dy / n;
    c.vel[2] = dz / n;
    memcpy(c.start, f.pos, 12);
    c.flags = 0;
    c.alpha = 1.0f;
    return 0;
}

void FallBack(World::Impl& w, FishTask& f) {  // 0x00439330
    f.c.state = 3;
    f.c.counter = 0;
    if (f.slot120 != -1) w.lockOn.Release(f.slot120);
    if (f.target != -1) f.target = -1;
}

void Dead(World::Impl& w, FishTask& f) {  // floating on the water (state 5 setup shared by 0x00438C10 / 0x00439450)
    FishCtl& c = f.c;
    c.spinStep = 1024.0f;  // 0x44800000
    c.spin = 0;
    c.baseY = w.waterLevel + F32(w.exe, 0x004C4CC8);
    c.vel[0] = Bits(0x3C23D70A);
    c.vel[2] = Bits(0xBC23D70A);
}

void Hits(World::Impl& w, Task& t, FishTask& f) {  // 0x00438C10
    FishCtl& c = f.c;
    if (c.state > 3 || !(f.flags34 & 8)) return;
    const uint32_t g = f.flags34;
    int p;
    if ((g & 2) && (g & 4)) p = w.Rand(0x00438C5D) & 1;
    else p = (g & 2) ? 0 : 1;
    w.AddScore(p, 0x50);
    if (c.kind == 0) ++w.player[p].hits;
    w.Sound(0x716A9);
    if (!(c.flags & 1)) {  // in the air: spin and fall
        c.state = 4;
        c.model = static_cast<int16_t>(w.gameMode == 6 ? 0xB6F : 0x1156);
        c.vel[1] = Bits(0x3ECCCCCD);
        const float fx = f.pos[0] - w.cam.pos[0], fz = f.pos[2] - w.cam.pos[2];
        float d = std::sqrt(fx * fx + fz * fz);
        d = d + d;
        if (d == 0.0f) {
            c.vel[0] = c.vel[2] = 0;
        } else {
            c.vel[0] = fx / d;
            c.vel[2] = fz / d;
        }
        c.yaw = 0;
        c.pitch = 0;
        if (w.Rand(0x00438E3B) % 100 < 0x32) {
            c.yawStep = (w.Rand(0x00438E4D) & 0xFFF) + 0x4FF;
            c.pitchStep = w.Rand(0x00438E5F) & 0xFF;
        } else {
            c.yawStep = -0x4FF - (w.Rand(0x00438E6B) & 0xFFF);
            c.pitchStep = -(w.Rand(0x00438E7F) & 0xFF);
        }
        c.spinStep = 819.0f;   // 0x444CC000
        c.spin = 32768.0f;     // 0x47000000
        c.d[1] = 5.0f;         // +0x2C
        c.counter = 0;
        c.baseY = f.pos[1];
    } else {  // under water: dead at once
        f.pos[1] = w.waterLevel + F32(w.exe, 0x004C4D04);
        Splash(w, f, Bits(0x3F4CCCCD), 1);
        --w.attackers;
        c.state = 5;
        c.model = static_cast<int16_t>(w.gameMode == 6 ? 0xB70 : 0x1156);
        Dead(w, f);
        c.d[1] = Bits(0x3E4CCCCD);
        c.deadYawStep = 0x40;
        c.yaw = w.Rand(0x00438D5B) & 0xFFFF;
        c.counter = 0;
        const float at[3] = { f.pos[0], w.waterLevel + F32(w.exe, 0x004E3100), f.pos[2] };
        w.SpawnBloodPoolAt(at, 0, Bits(0x3F4CCCCD));  // 0x00408370 (yaw: a stack word past the point)
    }
    f.flags34 &= ~8u;
    if (w.gameMode == 6) {  // 0x00439DC0
        FishFxTask* s = w.NewTask<FishFxTask>(TaskFlash, 0x00439E00);
        memcpy(s->pos, f.viewPos, 12);
        s->model = 0x3A;
    }
    f.flags34 |= 0x10000;
    --w.liveEnemies;
    if (f.slot120 != -1) w.lockOn.Release(f.slot120);
    if (f.target != -1) f.target = -1;
    ReleaseSlot(w, c);
    (void)t;
}

void States(World::Impl& w, FishTask& f) {
    FishCtl& c = f.c;
    const ExeImage& exe = w.exe;
    switch (c.state) {
    case 0:  // 0x00438F90
        f.pos[0] = c.vel[0] + f.pos[0];
        f.pos[2] = c.vel[2] + f.pos[2];
        if (w.waterLevel <= f.pos[1]) c.flags &= ~1;
        else c.flags |= 1;
        NextModel(c);
        ++c.counter;
        if (c.alpha < F32(exe, 0x004C4380)) c.alpha = c.alpha + F32(exe, 0x004E3100);
        if (c.swimFrames <= c.counter) {
            c.state = 1;
            c.counter = 0;
            c.alpha = 1.0f;
        }
        return;
    case 1: {  // 0x00439020
        const float y = static_cast<float>(SinA(c.bobPhase) * c.bob) + c.spawn[1];
        f.pos[1] = y < c.spawn[1] ? c.spawn[1] : y;
        f.pos[0] = c.vel[0] + f.pos[0];
        f.pos[2] = c.vel[2] + f.pos[2];
        NextModel(c);
        c.bobPhase = static_cast<int16_t>(c.bobPhase + 0x333);
        if (++c.counter == 0x50) {
            ++c.lapsDone;
            c.counter = 0;
            c.bobPhase = 0;
        }
        if (w.waterLevel <= f.pos[1]) {
            if (c.flags & 1) Splash(w, f, Bits(0x3E99999A), 0);
            c.flags &= ~1;
        } else {
            if (!(c.flags & 1)) Splash(w, f, 0.5f, 0);
            c.flags |= 1;
        }
        if (c.counter == 1) c.pitch = 0xE000;
        c.pitch += 0xCC;
        if (0xFFFE < c.pitch) c.pitch = 0xFFFF;
        c.pitch &= 0xFFFF;
        if (c.lapsDone == c.laps && StartJump(w, f) == -1) {
            --w.attackers;
            --w.liveEnemies;
            if (f.slot120 != -1) w.lockOn.Release(f.slot120);
            if (f.target != -1) f.target = -1;
            ReleaseSlot(w, c);
            c.counter = 0;  // 0x00439BF0(fish, 0x3C)
            c.swimFrames = 0x3C;
            c.d[0] = Bits(0x3ECCCCCD);
            f.escaping = true;
            f.fn = 0x00439C20;
        }
        return;
    }
    case 2: {  // 0x00439190 + 0x004397C0
        const int a14 = (static_cast<int>(c.jumpTick) << 14) / c.jumpFrames;
        const int a15 = (static_cast<int>(c.jumpTick) << 15) / c.jumpFrames;
        if (c.kind == 1) {
            f.pos[0] = c.vel[0] + f.pos[0];
            f.pos[2] = c.vel[2] + f.pos[2];
            const double s1 = SinA(a14), s2 = SinA(a15);
            const float y = static_cast<float>(s2 * F32(exe, 0x0055E1C8)) + static_cast<float>(s1 * c.d[1]);
            f.pos[1] = y + c.start[1];
        } else if (c.kind == 2) {
            f.pos[2] = c.vel[2] + f.pos[2];
            f.pos[1] = c.vel[1] + f.pos[1];
            const double s2 = SinA(a15), s1 = SinA(a14);
            const float x = static_cast<float>(s1 * c.d[0]) + static_cast<float>(s2 * static_cast<double>(static_cast<float>(c.side)));
            f.pos[0] = x + c.start[0];
        } else {
            f.pos[0] = c.vel[0] + f.pos[0];
            f.pos[2] = c.vel[2] + f.pos[2];
            f.pos[1] = static_cast<float>(SinA(a14) * c.d[1]) + c.start[1];
        }
        ++c.counter;
        NextModel(c);
        if (c.jumpFrames - 1 < ++c.jumpTick) c.jumpTick = static_cast<int16_t>(c.jumpFrames - 1);
        if (c.jumpFrames * 2 - 1 < c.counter) FallBack(w, f);
        if (c.counter < 0x10 && c.counter == 2) Splash(w, f, Bits(0x3F19999A), 0);
        const float dz = f.pos[2] - w.cam.pos[2], dy = f.pos[1] - w.cam.pos[1], dx = f.pos[0] - w.cam.pos[0];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (F32(exe, 0x004C43A0) <= d) {
            if (d <= F32(exe, 0x0055E1D0)) return;
        } else if (f.target != -1) {
            w.PlayerTakeHit(f.target, 1, 9);  // 0x00415300
        }
        FallBack(w, f);
        return;
    }
    case 3:  // 0x00439370
        if (c.counter == 0x20) {
            if (f.slot120 != -1) w.lockOn.Release(f.slot120);
            ReleaseSlot(w, c);
        }
        if (0x1F < c.counter && c.counter < 0x40) {
            f.pos[1] = f.pos[1] - F32(exe, 0x0055D1A0);
            f.pos[0] = c.vel[0] * F32(exe, 0x004C43AC) + f.pos[0];
            f.pos[2] = c.vel[2] * F32(exe, 0x004C43AC) + f.pos[2];
        }
        if (0x3F < c.counter) {
            --w.liveEnemies;
            --w.attackers;
            if (f.actorSlot != -1) w.actorSlots[f.actorSlot] = nullptr;
            if (f.target != -1) f.target = -1;
            w.EnemyDestroy(f);
            return;
        }
        NextModel(c);
        ++c.counter;
        return;
    case 4: {  // 0x00439450
        const float fy = f.pos[1] + c.vel[1];
        c.vel[1] = c.vel[1] - F32(exe, 0x0055E1CC);
        f.pos[0] = f.pos[0] + c.vel[0];
        f.pos[2] = c.vel[2] + f.pos[2];
        c.yaw = (c.yawStep + c.yaw) & 0xFFFF;
        c.spin = c.spinStep + c.spin;
        ++c.counter;
        c.pitch = (c.pitchStep + c.pitch) & 0xFFFF;
        auto toDead = [&](const float at[3]) {
            --w.attackers;
            c.d[1] = Bits(0x3E4CCCCD);
            c.state = 5;
            c.model = static_cast<int16_t>(w.gameMode == 6 ? 0xB6F : 0x1156);
            c.counter = 0;
            w.SpawnBloodPoolAt(at, 0, Bits(0x3F4CCCCD));  // 0x00408370
        };
        if (c.kind == 1) {
            int mat = 0;
            const float fl = w.FloorHeight(f.pos[0], f.pos[1] + F32(exe, 0x004C43A4), f.pos[2], &mat);  // 0x00409D40
            if (fy <= fl) {
                float at[3];
                if (mat == 0) {
                    Dead(w, f);
                    c.deadYawStep = 0x40;
                    c.yaw = w.Rand(0x00439657) & 0xFFFF;
                    at[0] = f.pos[0];
                    at[1] = w.waterLevel + F32(exe, 0x004E3100);
                    at[2] = f.pos[2];
                    f.pos[1] = fy;
                    Splash(w, f, Bits(0x3F4CCCCD), 1);
                } else {
                    f.pos[1] = fl;
                    c.spinStep = 0;
                    c.spin = 0;
                    c.flags |= 2;
                    c.baseY = f.pos[1];
                    c.vel[0] = c.vel[2] = 0;
                    c.yaw = 0;
                    c.deadYawStep = 0;
                    at[0] = f.pos[0];
                    at[1] = f.pos[1] + F32(exe, 0x004D1D24);
                    at[2] = f.pos[2];
                    w.Sound(0x2616A9);
                }
                toDead(at);
            }
            f.pos[1] = fy;
            return;
        }
        if (c.counter < 0x1900 && w.waterLevel < fy) {
            f.pos[1] = fy;
            return;
        }
        f.pos[1] = fy;
        c.state = 5;
        c.model = static_cast<int16_t>(w.gameMode == 6 ? 0xB6F : 0x1156);
        Dead(w, f);
        c.d[1] = Bits(0x3E4CCCCD);
        c.deadYawStep = 0x40;
        c.yaw = w.Rand(0x00439533) & 0xFFFF;
        c.counter = 0;
        const float at[3] = { f.pos[0], w.waterLevel + F32(exe, 0x004E3100), f.pos[2] };
        w.SpawnBloodPoolAt(at, 0, Bits(0x3F4CCCCD));  // 0x00408370
        Splash(w, f, Bits(0x3F4CCCCD), 1);
        --w.attackers;
        f.pos[1] = fy;
        return;
    }
    case 5: {  // 0x00439700
        f.pos[0] = c.vel[0] + f.pos[0];
        const int a = static_cast<int>(c.spin) & 0xFFFF;  // __ftol & 0xFFFF
        f.pos[1] = static_cast<float>(SinA(a) * c.d[1]) + c.baseY;
        c.baseY = c.baseY - F32(exe, 0x004E30E0);
        f.pos[2] = c.vel[2] + f.pos[2];
        if (c.counter == 1 && !(c.flags & 2)) {
            Ring(w, f, Bits(0x3F19999A));
            Ring(w, f, Bits(0x3E99999A));
        }
        ++c.counter;
        c.yaw += c.deadYawStep;
        c.spin = c.spinStep + c.spin;
        if (299 < c.counter) {
            if (f.actorSlot != -1) w.actorSlots[f.actorSlot] = nullptr;
            w.EnemyDestroy(f);
        }
        return;
    }
    default:
        return;
    }
}

// shadow on the water (opaque fish below the surface): 0x004AA0A0(0.1, 0.1, 0.1) is a renderer colour setting
void Shadow(World::Impl& w, FishTask& f, float sx) {
    FishCtl& c = f.c;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, f.pos[0], w.waterLevel + F32(w.exe, 0x0055CC44), f.pos[2]);
    mx::RotY(m, c.yaw);
    mx::Scale(m, sx, Bits(0x3C23D70A), sx);
    w.Draw(c.model, m);
}

void DrawFish(World::Impl& w, FishTask& f) {  // 0x00439860
    FishCtl& c = f.c;
    const bool shadow = (c.flags & 4) && f.pos[1] < w.waterLevel;
    if (shadow && c.state != 4 && c.state != 5) {
        w.LightChar();                                                    // 0x0041DC70
        w.LightColor(Bits(0x3DCCCCCD), Bits(0x3DCCCCCD), Bits(0x3DCCCCCD));  // 0x004AA0A0(0.1, 0.1, 0.1)
        Shadow(w, f, Bits(0x3ECCCCCD));
        w.LightScene();  // 0x0041DCC0
    }
    auto ops = [&](Mat4& m) {
        switch (c.state) {
        case 2:
        case 3:
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, c.yaw);
            mx::RotX(m, c.kind == 2 ? -0x1000 - c.pitch : ((static_cast<int>(c.jumpTick) << 14) / c.jumpFrames) - 0x3D00);
            break;
        case 4:
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, c.yaw);
            mx::RotX(m, c.pitch);
            break;
        case 5:
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, c.yaw);
            mx::RotZ(m, 0x4000);
            mx::RotX(m, c.yaw & 0xFFFF);
            break;
        default:
            mx::Translate(m, f.pos[0], f.pos[1] - F32(w.exe, 0x0055D1A0), f.pos[2]);
            mx::RotY(m, c.yaw);
            mx::RotX(m, c.pitch);
            break;
        }
        mx::Scale(m, Bits(0x3E99999A), Bits(0x3E99999A), Bits(0x3E99999A));
    };
    Mat4 m = Mat4::Identity();
    ops(m);
    w.LightChar();  // 0x0041DC70 (0x00439AB5)
    // Flag 4 makes this physical body opaque; its flattened shadow is a
    // separate submission above (and in TaskEscape). Both body paths keep the
    // specular normal unit length despite their common 0.3 model scale.
    auto* body = (c.flags & 4) ? w.Draw(c.model, m) : w.Draw(c.model, m, 8, c.alpha);
    if (body) body->unitSpecular = true;
    w.LightScene();  // 0x0041DCC0
    Mat4 mv = w.cam.view;  // +0x70 = the model-view translation (0x004A8CC0)
    ops(mv);
    mx::GetTranslation(mv, f.viewPos);
}
void TaskEscape(World::Impl& w, FishTask& f) {  // 0x00439C20: swims off and fades
    FishCtl& c = f.c;
    f.pos[1] = c.spawn[1];
    f.pos[0] = c.vel[0] + f.pos[0];
    f.pos[2] = c.vel[2] + f.pos[2];
    Mat4 m = Mat4::Identity();
    if (c.flags & 4) {
        mx::Translate(m, f.pos[0], w.waterLevel + F32(w.exe, 0x004C4C88), f.pos[2]);
        mx::RotY(m, c.yaw);
        mx::Scale(m, c.d[0], Bits(0x3C23D70A), c.d[0]);
    } else {
        mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
        mx::RotY(m, c.yaw);
        mx::RotX(m, c.pitch);
        mx::Scale(m, Bits(0x3E99999A), Bits(0x3E99999A), Bits(0x3E99999A));
    }
    w.LightChar();  // 0x0041DC70 (+ 0x004AA0A0(0.1, 0.1, 0.1) for the water shadow)
    if (c.flags & 4) w.LightColor(Bits(0x3DCCCCCD), Bits(0x3DCCCCCD), Bits(0x3DCCCCCD));
    if (auto* draw = w.Draw(c.model, m)) draw->unitSpecular = (c.flags & 4) == 0;
    w.LightScene();  // 0x0041DCC0
    NextModel(c);
    if (c.swimFrames <= ++c.counter) {
        c.d[0] = c.d[0] - F32(w.exe, 0x004C4CC0);
        if (c.d[0] <= 0.0f) w.EnemyDestroy(f);
    }
}

void TaskFish(Task& t) {  // 0x00438730
    World::Impl& w = *CurrentWorld();
    FishTask& f = t.As<FishTask>();
    if (f.lock) f.slot120 = static_cast<int8_t>(f.lock->slot);  // +0x120 written by the table task 0x00408DD0
    if (f.escaping) return TaskEscape(w, f);
    Hits(w, t, f);
    States(w, f);  // a destroyed fish (Actor_Destroy) and a switch to 0x00439C20 still run the rest of 0x00438730
    DrawFish(w, f);
    const int s = f.c.state;
    if (s <= 3) {  // 0x00439B50
        mx::Transform(w.cam.view, f.pos, f.viewPos);
        w.RegisterTarget(&f);  // 0x00405160
    }
    if (s <= 2) {  // 0x00439D70
        memcpy(f.body, f.pos, 12);
        w.SyncLock(f);
        w.lockOn.Register(f.lock, w.playerPos);  // 0x00408EC0
    }
}

}  // namespace

// 0x00438540 (actor type 0x51 first run)
void World::Impl::SpawnFish(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    const int16_t kind = r.ParamS16(0xE);
    if (kind == 6) {
        for (int& v : fishSlots) v = 0;
        const int32_t wl = r.ParamS32(0);
        memcpy(&waterLevel, &wl, 4);  // 0x007DCBB0
        ts.KillCurrent();
        return;
    }
    auto d = std::make_unique<FishTask>();
    FishTask& f = *d;
    f.fn = 0x00438730;
    f.record = r;
    f.flags34 = r.flags | 1;
    memcpy(f.pos, r.pos, 12);
    memcpy(f.rot, r.rot, 12);
    f.hp = f.hpMax = static_cast<int16_t>(r.state);
    f.lo = static_cast<int16_t>(r.lo);
    f.actorSlot = -1;
    f.radius = Bits(0x40066666);
    f.lock = &lockTargets[nextLockTarget++];
    FishCtl& c = f.c;
    memcpy(c.spawn, f.pos, 12);
    ++liveEnemies;
    ++attackers;
    c.slot = -1;
    f.target = -1;
    f.slot120 = -1;
    SyncLock(f);
    lockOn.AddDirect(f.lock);  // 0x00408E80
    f.slot120 = static_cast<int8_t>(f.lock->slot);
    auto pf = [&](int off) {
        const int32_t v = r.ParamS32(off);
        float x;
        memcpy(&x, &v, 4);
        return x;
    };
    c.vel[0] = pf(0);
    c.vel[2] = pf(4);
    // 0x00438760
    memcpy(f.pos, c.spawn, 12);
    c.counter = 0;
    const float dx = cam.pos[0] - f.pos[0], dz = cam.pos[2] - f.pos[2];
    const float dd = std::sqrt(dz * dz + dx * dx);
    if (dd != 0.0f) {
        c.vel[0] = (dx * c.vel[0]) / dd;
        c.vel[2] = (dz * c.vel[2]) / dd;
    } else {
        c.vel[0] = c.vel[2] = Bits(0x3A83126F);
    }
    c.vel[1] = 0;
    c.yaw = AngleOf(dx, dz);
    c.yawStep = c.pitch = c.pitchStep = 0;
    c.deadYawStep = c.s66 = c.bobPhase = c.lapsDone = c.state = 0;
    c.d[1] = 0;
    c.flags = 0;
    c.laps = 1;
    c.alpha = 1.0f;
    c.model = c.modelLoop = 0x1156;
    c.modelLast = 0x1169;
    // back in 0x00438540
    c.mode = r.ParamS16(0xC);
    if (c.mode == 0) c.flags |= 4;
    else if (c.mode == 1) c.alpha = 0;
    c.kind = kind;
    c.swimFrames = r.ParamS16(0x10);
    c.laps = r.ParamS16(0x12);
    c.jumpFrames = r.ParamS16(0x14);
    c.bob = pf(8);
    t.data = std::move(d);
    t.fn = TaskFish;
}

// 0x00438640(x, y, z, jump frames, kind): a fish that jumps at the players at once (the stage-2 boss): only with a free
// attack point (1 / 2 players); water level 0x007DCBB0 = -24.9; cleared task memory (Task_Clear 0x004A73D0) and a
// cleared control block, counted, lock-on slot direct, then 0x00438850
void World::Impl::SpawnFishJump(float x, float y, float z, int frames, int kind) {
    if (players == 0) return;
    if (players > 0 && players < 3) {
        bool free = false;
        for (int v : fishSlots) free = free || v == 0;
        if (!free) return;
    }
    waterLevel = Bits(0xC1C73333);
    FishTask* f = NewTask<FishTask>(TaskFish, 0x00438730);
    f->actorSlot = -1;
    f->radius = Bits(0x40066666);
    f->pos[0] = x;
    f->pos[1] = y;
    f->pos[2] = z;
    FishCtl& c = f->c;
    c = FishCtl{};
    c.model = c.modelLast = c.modelLoop = 0;
    c.laps = 0;
    c.alpha = 0;
    c.slot = 0;
    c.kind = static_cast<int16_t>(kind);
    c.jumpFrames = static_cast<int16_t>(frames == 0 ? 0x50 : frames);
    memcpy(c.spawn, f->pos, 12);
    ++liveEnemies;
    ++attackers;
    f->target = -1;
    f->slot120 = -1;
    f->lock = &lockTargets[nextLockTarget++];
    SyncLock(*f);
    lockOn.AddDirect(f->lock);  // 0x00408E80
    f->slot120 = static_cast<int8_t>(f->lock->slot);
    StartJump(*this, *f);  // 0x00438850
}

}  // namespace game
