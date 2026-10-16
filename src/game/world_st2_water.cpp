// game::World - the water of stage 2 scene 39 (boss mode boss 2): the wave objects of actor type 0x17 (0x004422D0 ->
// pool 0x005644E4: 0x004420C0 a plane wave, 0x004421B0 a ring wave moving on) summed into the water level by 0x00442390
// over the water block 0x007DCC4C (actor type 0x16, 0x00442290), and the floating objects of actor type 0x15
// (0x00441750 -> 0x004418C0): rafts that follow the water at their four corners, tilt towards the water's normal and
// are pushed down by the stage-2 boss's foot points 0x009A3500. notes/agents/modes.md section 9.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// The water block 0x007DCC4C: count +0, slot mask +4, level +8, wave objects +0xC[8]. The wave objects' fields live
// here (the original's pooled blocks stay readable after their tasks end).
struct Wave {
    int kind = 0;               // 0 plane (0x004420C0 / query 0x00442110), 1 ring (0x004421B0 / query 0x00442210)
    int state = 0;              // +0x38
    float pos[3] = {};          // +0x3C
    int rot[3] = {};            // +0x48
    uint32_t params = 0;        // +0x58 (the record's parameter block)
    float amp = 0, len = 0, speed = 0;  // +0x5C / +0x60 / +0x64
};
struct WaterState : TaskData {
    int count = 0;              // +0
    uint32_t mask = 0;          // +4
    Wave waves[8];
};

WaterState& Water(World::Impl& w) {
    if (!w.st2Waves) w.st2Waves = std::make_shared<WaterState>();
    return *static_cast<WaterState*>(w.st2Waves.get());
}

uint32_t Word(World::Impl& w, uint32_t a) {
    uint32_t v = 0;
    w.mem.Read(a, &v);
    return v;
}
float WordF(World::Impl& w, uint32_t a) {
    const uint32_t v = Word(w, a);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

float Rad(int a) { return static_cast<float>(static_cast<double>(a) * mx::kToRad); }

// 0x00442110: a plane wave along its yaw (the larger of the direction's components picks the coordinate)
float PlaneWave(const World::Impl& w, const Wave& o, const float p[3]) {
    const float a = Rad(o.rot[1]);
    const double c = std::cos(static_cast<double>(a));
    double t = c;
    if (c < 0.0) t = static_cast<float>(c * F32(w.exe, 0x004C4C64));
    float d;
    if (F32(w.exe, 0x004C43AC) < t) d = static_cast<float>(c) * p[2];
    else d = static_cast<float>(std::sin(static_cast<double>(a)) * p[0]);
    const float v = ((d + o.pos[0]) * F32(w.exe, 0x004E1FDC)) / o.len;
    return static_cast<float>(std::cos(static_cast<double>(Rad(static_cast<int>(v)))) * o.amp);  // __ftol
}

// 0x00442210: a ring wave around its centre, fading out with the distance
float RingWave(const World::Impl& w, const Wave& o, const float p[3]) {
    const float dz = p[2] - o.pos[2], dx = p[0] - o.pos[0];
    const float d = std::sqrt(dx * dx + dz * dz);
    float f = 1.0f - d * F32(w.exe, 0x004D5464);
    if (f < 0.0f) f = 0.0f;
    const float v = (d * F32(w.exe, 0x004E1FDC)) / o.len;
    const float c = static_cast<float>(std::cos(static_cast<double>(Rad(static_cast<int>(v)))) * o.amp);
    return c * f;
}

// the wave object tasks 0x004420C0 / 0x004421B0
struct WaveTask : WTask {
    int slot = 0;  // +0x54
};

void TaskWave(Task& t) {
    World::Impl& w = *CurrentWorld();
    WaterState& s = Water(w);
    Wave& o = s.waves[t.As<WaveTask>().slot & 7];
    if (o.state == 0) {
        o.amp = WordF(w, o.params);
        o.len = WordF(w, o.params + 4);
        o.speed = WordF(w, o.params + 8);
        ++s.count;
        ++o.state;
    } else if (o.state != 1) {
        return;
    }
    if (o.kind == 0) {
        o.pos[0] = o.speed + o.pos[0];
    } else {
        const double a = Rad(o.rot[1]);
        o.pos[0] = static_cast<float>(std::sin(a) * o.speed) + o.pos[0];
        o.pos[2] = static_cast<float>(std::cos(a) * o.speed) + o.pos[2];
    }
}

// ---- actor type 0x15: the floating objects ----

struct RaftBlock {              // the 0x34-byte block +0x1310
    uint32_t fn = 0;            // +0x00 motion function 0x005926A8[P s16 4]
    int16_t countdown = -1;     // +0x04 (event flag +0x0E set: frames until it sinks)
    int16_t camId = 0, tick = 0;  // +0x06 / +0x08 removed at this camera tick
    int16_t a = 0, b = 0, flag = 0;  // +0x0A / +0x0C / +0x0E
    int tilt = 0;               // +0x10 rotation about +0x14
    float axis[3] = {};         // +0x14
    float sink = 0;             // +0x20
    int feet = 0;               // +0x24
    int tiltFrom = 0;           // +0x28
    float sinkFrom = 0;         // +0x2C
    int phase = 0;              // +0x30 (not initialized by the original; only read after the feet left)
};

struct RaftTask : ActorTask {
    int idx = 0;                // +0x3C
    float pos[3] = {};          // +0x40
    int rot[3] = {};            // +0x64
    const int* Rot64() const override { return rot; }
    int16_t model = 0;          // +0x1F4
    uint32_t params = 0;        // +0x130C
    RaftBlock blk;              // +0x1310
};

void KillRaft(World::Impl& w, RaftTask& k) {
    k.flags34 = (k.flags34 & ~1u) | 0x80008000u;
    w.ts.KillCurrent();
}

// 0x004418C0
void TaskRaft(Task& t) {
    World::Impl& w = *CurrentWorld();
    RaftTask& k = t.As<RaftTask>();
    RaftBlock& b = k.blk;
    const ExeImage& exe = w.exe;
    const CameraDirector& cd = w.runner->camera;
    if (cd.camId == b.camId && cd.tick == b.tick) {
        KillRaft(w, k);
        return;
    }
    const int16_t cnt = b.countdown;
    if (-1 < cnt && w.runner->flags[b.flag & 0xFF] != 0) {
        b.countdown = static_cast<int16_t>(cnt - 1);
        if (cnt == 0) {
            const int n = static_cast<int8_t>(Word(w, k.params + 0x24) & 0xFF);
            if ((n - k.idx) % 3 == 1) {
                const int rot[3] = { 0, w.cam.yaw, 0 };  // 0x009A60D0[ctx]
                w.SpawnExplosionAt(k.pos, rot, 1, 0.7f);  // 0x0043FCA0(pos, 1, 0x3F333333)
                w.shakeTimer = 6;  // 0x009C8E8C
            }
            KillRaft(w, k);
            return;
        }
    }
    if (b.fn != 0x0041EBB0) w.Note("actor 0x15 motion function " + std::to_string(b.fn) + " (0x005926A8) not ported");
    // the water at the four corners (+2.5), the centre at their mean
    const float x = k.pos[0], y = k.pos[1], z = k.pos[2];
    const float k20 = F32(exe, 0x004C4C8C), k5 = F32(exe, 0x0055D2B4);
    float c[4][3] = { { x + k20, y, z + k5 }, { x + k20, y, z - k5 }, { x - k20, y, z - k5 }, { x - k20, y, z + k5 } };
    const float lift = F32(exe, 0x004C4CBC);
    for (auto& q : c) q[1] = WaterLevelAt(w, q) + lift;
    const float cy = (((c[0][1] + c[1][1]) + c[2][1]) + c[3][1]) * F32(exe, 0x004C4C58);
    int hi = 3;
    for (int i = 0; i < 3; ++i)
        if (c[hi][1] < c[i][1]) hi = i;
    const int o = (hi - 2) & 3, e1 = (hi + 1) & 3, e2 = (hi - 1) & 3;
    const float ax = c[e1][0] - c[o][0], ay = c[e1][1] - c[o][1], az = c[e1][2] - c[o][2];
    const float bx = c[e2][0] - c[o][0], by = c[e2][1] - c[o][1], bz = c[e2][2] - c[o][2];
    const float n[3] = { by * az - bz * ay, bz * ax - bx * az, bx * ay - by * ax };
    Mat4 m = Mat4::Identity();
    mx::RotY(m, -k.rot[1]);
    mx::RotZ(m, -k.rot[2]);
    mx::RotX(m, -k.rot[0]);
    float ln[3];
    mx::Transform(m, n, ln);
    auto atan2Angle = [](float s, float c) {  // fpatan(s, c) * 65536 / 2pi, __ftol, as a short
        return static_cast<int>(static_cast<int16_t>(static_cast<int>(std::atan2(static_cast<double>(s), static_cast<double>(c)) * mx::kToAngle)));
    };
    int tilt = atan2Angle(std::sqrt(ln[2] * ln[2] + ln[0] * ln[0]), ln[1]);
    if (tilt != 0) {
        float axis[3] = { 1.0f, 0.0f, 0.0f };
        if (tilt != 0x8000) {
            axis[0] = ln[2];
            axis[2] = -ln[0];
        }
        Mat4 r = Mat4::Identity();
        mx::RotX(r, k.rot[0]);
        mx::RotZ(r, k.rot[2]);
        mx::RotY(r, k.rot[1]);
        if (0x60 < tilt) tilt = 0x60;
        else if (tilt < -0x60) tilt = -0x60;
        mx::RotAxis(r, axis, tilt);
        MatrixToAnglesYAxis(r, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x00401AE0
        k.pos[0] = x;
        k.pos[1] = cy;
        k.pos[2] = z;
    }
    // the boss's foot points (0x009A3500 {strength, x, y, z}) push the raft down and tilt it away
    Mat4 top = Mat4::Identity();
    if (b.tilt != 0) mx::RotAxis(top, b.axis, -b.tilt);
    float acc = 0.0f;
    for (const auto& f : w.st2Feet) {
        if (f[0] == 0.0f || c[0][2] < f[3] || f[3] < c[1][2]) continue;
        const float axis[3] = { f[3] - z, 0.0f, x - f[1] };
        const float dz = f[3] - z, dx = f[1] - x, dy = f[2] - cy;
        const float d = std::sqrt((dy * dy + dx * dx) + dz * dz);
        mx::RotAxis(top, axis, static_cast<int>((d * f[0]) * F32(exe, 0x0056451C)));
        acc = f[0] * F32(exe, 0x00564518) + acc;
    }
    if (acc == 0.0f) {
        if (b.feet != 0) {
            b.tiltFrom = b.tilt;
            b.sinkFrom = b.sink;
            b.phase = 0;
        }
        b.feet = 0;
    } else {
        b.feet = 1;
        b.tiltFrom = 0;
        b.sinkFrom = 0;
    }
    auto upTilt = [&](const Mat4& mm, float out[3]) {
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        mx::Transform(mm, up, out);
        return atan2Angle(std::sqrt(out[2] * out[2] + out[0] * out[0]), 1.0f);  // fld qword 0x004ECB70 = 1.0 (not y)
    };
    if (b.feet != 0) {
        float u[3];
        int s = upTilt(top, u);
        s = (s + ((s >> 31) & 3)) >> 2;
        Mat4 r = Mat4::Identity();
        if (b.tilt != 0) {
            mx::RotAxis(r, b.axis, b.tilt);
            if (s != 0) {
                const float axis2[3] = { u[2] * F32(exe, 0x004C43B0), 0.0f, u[0] * F32(exe, 0x00564514) };
                mx::RotAxis(r, axis2, s);
            }
            const int s2 = upTilt(r, u);
            b.axis[1] = 0;
            b.tilt = s2;
            b.axis[0] = u[2];
            b.axis[2] = -u[0];
        } else {
            b.tilt = s;
            if (s != 0) {
                b.axis[1] = 0;
                b.axis[0] = u[2];
                b.axis[2] = -u[0];
            }
        }
        b.sink = (acc - b.sink) * F32(exe, 0x004C4C58) + b.sink;
    } else {
        b.phase += 0x400;
        const double cph = std::cos(static_cast<double>(Rad(b.phase)));
        if (cph == 0.0) {
            b.tiltFrom = (b.tiltFrom * 2) / 3;
            b.sinkFrom = b.sinkFrom * F32(exe, 0x005644F4);
        }
        if (b.tiltFrom != 0) b.tilt = static_cast<int>(static_cast<float>(static_cast<double>(b.tiltFrom) * cph));
        if (b.sinkFrom != 0.0f) b.sink = static_cast<float>(cph * b.sinkFrom);
    }
    auto build = [&](const Mat4& base) {
        Mat4 mm = base;
        mx::Translate(mm, x, cy - b.sink, z);
        mx::RotX(mm, k.rot[0]);
        mx::RotZ(mm, k.rot[2]);
        mx::RotY(mm, k.rot[1]);
        if (b.tilt != 0) mx::RotAxis(mm, b.axis, b.tilt);
        return mm;
    };
    // drawn on the matrix stack from the view (0x004A9230(0x009A6000[ctx]) .. 0x00418560), +0x150 its model-view
    k.m150 = build(w.cam.view);  // 0x004A8CA0 -> +0x150
    w.Draw(k.model, k.m150, 8, -1.0f, false, -1, true);
    w.RegisterTarget(&k);        // 0x00405160
}

}  // namespace

// 0x00442390: the water level at p (the block's level plus every wave object)
float WaterLevelAt(World::Impl& w, const float p[3]) {
    float acc = w.st2Water;
    if (!w.st2Waves) return acc;
    const WaterState& s = Water(w);
    int done = 0;
    for (int i = 0; i < 8 && done < s.count; ++i) {
        if (!(s.mask & (1u << i))) continue;
        const Wave& o = s.waves[i];
        acc = (o.kind == 0 ? PlaneWave(w, o, p) : RingWave(w, o, p)) + acc;
        ++done;
    }
    return acc;
}

// actor type 0x16 (0x00442290): a new water block
void WaterReset(World::Impl& w) {
    if (w.st2Waves) Water(w) = WaterState{};
}

// actor types 0x15 / 0x17
bool SpawnWater(World::Impl& w, Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    if (r.type == 0x17) {  // 0x004422D0: a wave object in the first free slot (else again next frame)
        WaterState& st = Water(w);
        for (int i = 0; i < 8; ++i) {
            if (st.mask & (1u << i)) continue;
            const int kind = static_cast<int16_t>(r.state);
            if (kind != 0 && kind != 1) w.Note("actor 0x17 wave kind " + std::to_string(kind) + " (0x005644E4) not ported");
            WaveTask* k = w.NewTask<WaveTask>(TaskWave, kind == 1 ? 0x004421B0 : 0x004420C0);
            k->slot = i;
            Wave& o = st.waves[i];
            o = Wave{};
            o.kind = kind == 1 ? 1 : 0;
            memcpy(o.pos, r.pos, 12);
            memcpy(o.rot, r.rot, 12);
            o.params = r.addr + 0x24;  // +0x130C
            st.mask |= 1u << i;
            w.ts.KillCurrent();
            return true;
        }
        return true;  // all slots in use: the spawn task tries again
    }
    if (r.type != 0x15) return false;
    // 0x00441750: P byte 0x24 rafts along the step P float 0x18..0x20 (the first one created is the farthest)
    const uint32_t p = r.addr + 0x24;
    const int n = static_cast<int8_t>(Word(w, p + 0x24) & 0xFF);
    const int start = static_cast<int8_t>((Word(w, p + 0x24) >> 8) & 0xFF);
    const int16_t step = static_cast<int16_t>(Word(w, p + 0x24) >> 16);
    int16_t delay = 0;
    const float sx = WordF(w, p + 0x18), sy = WordF(w, p + 0x1C), sz = WordF(w, p + 0x20);
    for (int left = n, idx = n; left != 0; --left) {
        --idx;
        RaftTask* k = w.NewTask<RaftTask>(TaskRaft, 0x004418C0);
        const float f = static_cast<float>(idx);
        k->flags34 = r.flags | 1;  // the spawner's +0x34
        k->pos[0] = f * sx + r.pos[0];
        k->pos[1] = f * sy + r.pos[1];
        k->pos[2] = f * sz + r.pos[2];
        memcpy(k->rot, r.rot, 12);
        k->idx = idx;
        k->params = p;
        const uint32_t w0 = Word(w, p), w2 = Word(w, p + 8), w3 = Word(w, p + 0xC), w4 = Word(w, p + 0x10);
        k->model = static_cast<int16_t>(w0 & 0xFFFF);
        k->objMesh = static_cast<int32_t>(Word(w, p + 4));
        RaftBlock& b = k->blk;
        b.fn = w.exe.U32(0x005926A8 + 4 * static_cast<uint32_t>(static_cast<int16_t>(w2 & 0xFFFF)));
        b.camId = static_cast<int16_t>(w2 >> 16);
        b.tick = static_cast<int16_t>(w3 & 0xFFFF);
        b.a = static_cast<int16_t>(w3 >> 16);
        b.b = static_cast<int16_t>(w4 & 0xFFFF);
        b.flag = static_cast<int16_t>(w4 >> 16);
        if (start < left) {
            b.countdown = delay;
            delay = static_cast<int16_t>(delay + step);
        } else {
            b.countdown = -1;
        }
    }
    for (auto& f : w.st2Feet) f[0] = 0;  // 0x009A3500 + i * 0x10 cleared
    w.ts.KillCurrent();
    return true;
}

}  // namespace game
