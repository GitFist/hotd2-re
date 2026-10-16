// game::World - actor type 0x42 (ctor 0x0042F9B0): a school of jumping fish (stage 2 scene 21 and others). Variants 0 /
// 2: 6 / 8 (10 / 15) fish (task 0x0042FCA0, block 0x230) circling or waiting under the water, then jumping out
// (squash and stretch tables 0x0055D358 / 0x0055D530), one at a time (0x007DCA48) leaping at the players' camera
// (path table 0x0055D440); shot: blood, +80, splash, then the dead task 0x00430C80 (halves of motions 0xBF / 0xC0 or a
// fall into the water). Variant 1: one fish falling (0x00431000). notes/agents/routes.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/world_internal.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

float F64f(const ExeImage& exe, uint32_t va) {  // (float) of a double constant
    double d = 0;
    exe.Read(va, &d, 8);
    return static_cast<float>(d);
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

constexpr double kToRad = 9.587379924285257e-05;  // double 0x004C4370

double Rad(int a) { return static_cast<double>(static_cast<float>(static_cast<double>(a) * kToRad)); }

struct FishTask : EnemyTask {
    int8_t state = 0;         // +0x192
    int8_t idx = 0;           // +0x193
    int8_t count = 0;         // +0x194
    float v198[3] = {};       // +0x198 / +0x19C / +0x1A0 (fall: +0x19C vertical speed; leap off: velocity)
    float start[3] = {};      // +0x1A4
    float prevPos[3] = {};    // +0x1B0
    float scl[3] = { 1, 1, 1 };  // +0x1BC
    float saved[3] = {};      // +0x1C8
    int yawOff = 0;           // +0x1D8
    int f1E0 = 0;             // +0x1E0
    int circle = 0;           // +0x1E4
    int16_t cnt = 0;          // +0x1E8
    int timer = 0;            // +0x1EC
    int f1F0 = 0;             // +0x1F0
    float f1F4[3] = {};       // +0x1F4
    float halfY[2] = {};      // +0x214 / +0x218
    float sink = 0;           // +0x21C
    float dist = 0;           // +0x220
    float water = 0;          // +0x224
    int16_t half[2] = {};     // +0x228 / +0x22A
    uint8_t halfIn[2] = {};   // +0x22C / +0x22D
};

// the school (0x007DCA48 current leaper, 0x007DCA49 live count, 0x007DCA4C[16] members)
struct FishSchool : TaskData {
    int8_t cur = 0;
    int8_t alive = 0;
    FishTask* fish[16] = {};
};

FishSchool& School(World::Impl& w) {
    if (!w.fishSchool) w.fishSchool = std::make_shared<FishSchool>();
    return *static_cast<FishSchool*>(w.fishSchool.get());
}

// 0x00412FB0(motion, frame): the motion's frame record {x, y, z, s16 rx, ry, rz} (data + 4 + frame * 0x14)
struct FrameRec {
    float p[3] = {};
    int rx = 0, ry = 0, rz = 0;
};

FrameRec MotionFrame(World::Impl& w, int motion, int frame) {
    FrameRec r;
    const Motion* mo = w.PropMotion(motion);
    const size_t off = 4 + static_cast<size_t>(frame) * 0x14;
    if (!mo || off + 0x12 > mo->data.size()) {
        w.Note("actor 0x42 motion frame outside the data");
        return r;
    }
    memcpy(r.p, &mo->data[off], 12);
    int16_t s[3];
    memcpy(s, &mo->data[off + 12], 6);
    r.rx = s[0];
    r.ry = s[1];
    r.rz = s[2];
    return r;
}

int16_t S16(const ExeImage& exe, uint32_t a) { return exe.S16(a); }
uint8_t U8(const ExeImage& exe, uint32_t a) {
    uint8_t v = 0;
    exe.Read(a, &v, 1);
    return v;
}

void NextLeaper(FishSchool& s, int from, int n) {  // the next live member after `from`
    if (n <= 0) return;
    s.cur = static_cast<int8_t>(from % n);
    int guard = 0;
    while (!s.fish[s.cur & 15] && guard++ < 64) s.cur = static_cast<int8_t>((s.cur + 1) % n);
}

void ReleaseSlot(World::Impl& w, FishTask& f) {
    if (f.slot120 != -1) w.lockOn.Release(f.slot120);  // 0x009A5EC0[+0x120 * 8] = 0
}

int CamYaw(World::Impl& w, const FishTask& f) {  // fpatan(-(cam x - x), -(cam z - z))
    const float* c = w.cam.pos;
    return TruncToAngle(std::atan2(static_cast<double>(-(c[0] - f.pos[0])), static_cast<double>(-(c[2] - f.pos[2]))));
}

// 0x00430C80: the shot fish (flag 0x1000000: two halves of motions 0xBF / 0xC0 sinking; else the body falling into the
// water), removed after 0x3D frames
void TaskFishDead(Task& t) {
    World::Impl& w = *CurrentWorld();
    FishTask& f = t.As<FishTask>();
    const ExeImage& exe = w.exe;
    const float s6 = Bits(0x3F19999A);
    if (!(f.flags34 & 0x1000000)) {
        if (f.pos[1] <= f.water) {
            f.rot[0] = 0;
            f.pos[1] = f.pos[1] - F32(exe, 0x004C4CB0);
        } else {
            f.pos[1] = f.pos[1] + f.vel[1];
            f.vel[1] = f.vel[1] - F32(exe, 0x0055CB10);
        }
        Mat4 m = Mat4::Identity();
        mx::Translate(m, f.pos[0], f.pos[1] + 1.0f, f.pos[2]);
        mx::RotY(m, f.rot[1]);
        mx::RotX(m, f.rot[0]);
        mx::Scale(m, s6, s6, s6);
        w.Draw(f.cnt + 0x87A, m);
        if (f.cnt < 0x15) ++f.cnt;
    } else {
        for (int h = 0; h < 2; ++h) {
            const FrameRec e = MotionFrame(w, h == 0 ? 0xBF : 0xC0, f.half[h]);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, f.rot[1]);
            if (f.halfIn[h] == 0) {
                mx::Translate(m, e.p[0], e.p[1], e.p[2]);
            } else {
                mx::Translate(m, 0.0f, -f.pos[1], 0.0f);
                mx::Translate(m, e.p[0], f.halfY[h], e.p[2]);
            }
            mx::RotZ(m, e.rz);
            mx::RotY(m, e.ry);
            mx::RotX(m, e.rx);
            mx::Scale(m, s6, s6, s6);
            w.Draw(h + 0x875, m);
            w.Draw(0x877, m);
            if (f.halfIn[h] == 0 && f.half[h] < 0x3B) {
                ++f.half[h];
            } else if (h == 0 && f.half[0] == 0x3B) {
                if (f.sink == 0.0f) {
                    f.sink = Bits(0xBFA9F8A1);
                } else {
                    f.sink = f.sink - F32(exe, 0x0055D7D8);
                    f.pos[1] = f.sink + f.pos[1];
                }
            }
            const float lim = f.water + F64f(exe, 0x0055D7D0);
            if (e.p[1] + f.pos[1] < lim) {
                if (f.halfIn[h] == 0) {
                    Mat4 r = Mat4::Identity();
                    mx::RotY(r, f.rot[1]);
                    const float in[3] = { e.p[0], 0.0f, e.p[2] };
                    float o[3];
                    mx::Transform(r, in, o);
                    f.halfY[h] = lim;
                    const float y = ((static_cast<float>(h) * F32(exe, 0x004E30E0) + static_cast<float>(f.idx) * F32(exe, 0x004C4CC0)) +
                                     w.ground) + 1.0f;
                    w.SpawnSplash43E4C0(f.rot[1], o[0] + f.pos[0], y, o[2] + f.pos[2], 1.0f);
                    f.halfIn[h] = 1;
                }
                f.halfY[h] = f.halfY[h] - F32(exe, 0x004C4CB0);
            }
        }
    }
    if (0x3C < f.timer++) w.EnemyDestroy(f);  // 0x00409CC0
}

// 0x0042FC00: the leap's start point; the target player (two players: random, the leap tilted -+0x400)
void LeapStart(World::Impl& w, FishTask& f) {
    memcpy(f.start, f.pos, 12);
    f.timer = 0;
    f.cnt = 0;
    f.yawOff = 0;
    if (w.players == 2) {
        const int r = w.Rand(0x0042FC6B) & 1;
        f.target = static_cast<int8_t>(r);
        f.yawOff = r * 0x800 - 0x400;
    } else {
        if (w.playersInGame == 0) f.target = 0;
        if (w.playersInGame == 1) f.target = 1;
    }
}

// 0x0042FCA0
void TaskFish(Task& t) {
    World::Impl& w = *CurrentWorld();
    FishTask& f = t.As<FishTask>();
    if (f.fn == 0x00430C80) {
        TaskFishDead(t);
        return;
    }
    const ExeImage& exe = w.exe;
    FishSchool& sc = School(w);
    if (f.lock) f.slot120 = static_cast<int8_t>(f.lock->slot);
    sc.fish[f.idx & 15] = &f;
    const int n = f.count;
    if (f.state != 7 && (f.flags34 & 8)) {
        w.SpawnBlood430BD0(f.viewPos);  // 0x00430C50
        --w.liveEnemies;
        --w.attackers;
        sc.fish[f.idx & 15] = nullptr;
        w.Sound((w.Rand(0x0042FD00) & 1) == 0 ? 0x3719A9u : 0x3619A9u);
        const uint32_t g = f.flags34 & 2;
        int p;
        if (g == 0 || !(f.flags34 & 4)) p = g == 0 ? 1 : 0;
        else p = w.Rand(0x0042FD35) & 1;
        w.AddScore(p, 0x50);  // 0x004156C0
        ++w.player[p].hits;
        f.cnt = 0;
        f.water = w.ground;
        if (f.state == 0 || f.state == 1) {
            f.flags34 |= 0x1000000;
            f.halfY[0] = MotionFrame(w, 0xBF, 0).p[1] + f.pos[1];
            f.halfY[1] = MotionFrame(w, 0xC0, 0).p[1] + f.pos[1];
        } else {
            if (f.state == 5) f.flags34 |= 0x10000000;
            w.SpawnSplash43E4C0(f.rot[1], f.pos[0], (static_cast<float>(f.idx) * F32(exe, 0x004C4CC0) + f.water) + 1.0f, f.pos[2], 1.0f);
        }
        f.state = 7;
        f.timer = 0;
        f.half[0] = f.half[1] = 0;
        f.vel[1] = 0.0f;
        f.vel[2] = Bits(0xBE19999A);
        f.rot[0] = 0;
        f.f1F4[0] = 0.0f;
        f.f1F4[1] = Bits(0x3E99999A);
        f.f1F4[2] = 0.0f;
        --sc.alive;
        f.flags34 |= 0x10000;
        ReleaseSlot(w, f);
        if (0 < sc.alive) {
            sc.cur = static_cast<int8_t>(w.Rand(0x0042FEAD) % n);
            NextLeaper(sc, sc.cur, n);
        }
        f.fn = 0x00430C80;
    }
    switch (f.state) {
    case 0:
        if (w.runner->scene == 0x15) {
            const double a = Rad(f.circle);
            f.circle -= 0x80;
            const float r = static_cast<float>(f.idx) + F32(exe, 0x0055D2B4);
            f.pos[0] = static_cast<float>(r * std::sin(a)) - F32(exe, 0x0055D7C8);
            f.pos[2] = static_cast<float>(r * std::cos(a)) - F32(exe, 0x0055D7C4);
        }
        if (static_cast<int>(U8(exe, 0x0055D6E8 + static_cast<uint32_t>(f.idx))) < ++f.timer) {
            f.timer = 0;
            f.state = 1;
        }
        break;
    case 1: {
        const float vy = f.v198[1] - F32(exe, 0x0055D7C0);
        f.v198[1] = vy;
        f.pos[1] = vy + f.pos[1];
        if (f.pos[1] < f.water + F64f(exe, 0x0055D7B8)) {
            f.pos[1] = f.water + F32(exe, 0x004C43A8);
            f.rot[0] = 0;
            f.timer = 0;
            f.state = 2;
            if (w.cfg.eventMode == 0) w.Sound((w.Rand(0x00430003) & 1) == 0 ? 0x1E18A9u : 0x1D18A9u);
            else w.Sound((w.Rand(0x00430199) & 1) == 0 ? 0x2119A9u : 0x2019A9u);
        }
        break;
    }
    case 2:
        if (0x18 < ++f.timer) {
            f.cnt = 8;
            f.state = 3;
            f.f1F0 = 0;
        }
        break;
    case 3: {
        int16_t c = f.cnt;
        const float k = w.runner->scene == 0x15 ? Bits(0x3F4CCCCD) : Bits(0x3E99999A);
        if (c < 0x3B) {
            const float a = static_cast<float>(S16(exe, 0x0055D2E2 + 2 * static_cast<uint32_t>(c))) * F32(exe, 0x004D5464);
            const float b = static_cast<float>(S16(exe, 0x0055D2E0 + 2 * static_cast<uint32_t>(c))) * F32(exe, 0x004D5464);
            Mat4 m = Mat4::Identity();
            mx::RotY(m, f.rot[1]);
            const float in[3] = { 0.0f, 0.0f, (a - b) * F64f(exe, 0x004E3108) };
            float o[3];
            mx::Transform(m, in, o);
            f.pos[0] = o[0] * k + f.pos[0];
            f.pos[2] = o[2] * k + f.pos[2];
            c = f.cnt;
            const uint32_t e = 0x0055D358 + 6 * static_cast<uint32_t>(c);
            const float u = F32(exe, 0x0055D7B4), one = 1.0f;
            f.scl[0] = (static_cast<float>(S16(exe, e)) * u - one) * k + one;
            f.scl[1] = (static_cast<float>(S16(exe, e + 2)) * u - one) * k + one;
            f.scl[2] = (static_cast<float>(S16(exe, e + 4)) * u - one) * k + one;
        }
        f.cnt = static_cast<int16_t>(c + 1);
        if (0x3A < f.cnt) {
            if (w.runner->scene == 0x15 && f.f1F0 == 0) {
                f.f1F0 = 1;
                f.cnt = 0;
            } else {
                f.timer = 0;
                f.state = 4;
                memcpy(f.saved, f.scl, 12);
            }
        }
        break;
    }
    case 4: {
        f.f1F0 += 0x800;
        const double x = Rad(f.f1F0);
        const float s = static_cast<float>(std::sin(x) * 0.5) + F32(exe, 0x0055D7B0);
        const float q = F32(exe, 0x004C4C58);
        f.scl[0] = (s - f.saved[0]) * q + f.saved[0];
        const float c = static_cast<float>(std::cos(x) * 0.5) + 1.0f;
        f.scl[1] = (c - f.saved[1]) * q + f.saved[1];
        f.scl[2] = (s - f.saved[2]) * q + f.saved[2];
        {
            const int a = CamYaw(w, f);
            const uint32_t u = static_cast<uint32_t>(f.rot[1]) & 0xFFFF;
            int d = static_cast<uint16_t>(static_cast<int16_t>(a) + static_cast<int16_t>(f.yawOff) - static_cast<int16_t>(f.rot[1]));
            if (0x8000 < d) d -= 0x10000;
            f.rot[1] = d / 10 + static_cast<int>(u);
        }
        if (sc.cur == f.idx && w.runner->camera.modeCur == 2 && 0 < w.players) {
            const float tf = static_cast<float>(f.timer);
            const float u = F32(exe, 0x0055D7B4), z1 = F32(exe, 0x0055D230);
            f.scl[0] = (static_cast<float>(S16(exe, 0x0055D530)) * u - f.saved[0]) * tf * z1 + f.saved[0];
            f.scl[1] = (static_cast<float>(S16(exe, 0x0055D532)) * u - f.saved[1]) * tf * z1 + f.saved[1];
            ++f.timer;
            f.scl[2] = (static_cast<float>(S16(exe, 0x0055D534)) * u - f.saved[2]) * tf * z1 + f.saved[2];
            if (9 < f.timer) {
                LeapStart(w, f);  // 0x0042FC00
                f.timer = 0;
                const int a = CamYaw(w, f);
                f.state = 5;
                f.rot[1] = a + f.yawOff;
                const float* cp = w.cam.pos;
                const float dx = cp[0] - f.pos[0], dz = cp[2] - f.pos[2];
                f.dist = std::sqrt(dx * dx + dz * dz);
            }
        }
        break;
    }
    case 5: {
        const int i = f.cnt;
        const float c01 = F32(exe, 0x004D5464);
        auto tab = [&](int k, int s) { return static_cast<float>(S16(exe, 0x0055D440 + 4 * static_cast<uint32_t>(k) + 2 * static_cast<uint32_t>(s))) * c01; };
        if (0x1F < i) {
            const double yr = Rad(f.rot[1]);
            const float kd = F32(exe, 0x0055D7A8);
            f.pos[0] = static_cast<float>(static_cast<float>(static_cast<float>(tab(i, 1) * std::sin(yr)) * f.dist) * kd) + f.start[0];
            float h = (w.cam.pos[1] + F32(exe, 0x004C4CA0)) - f.start[1];
            if (h < 0.0f) h = h * -1.0f;
            f.pos[1] = tab(i, 0) * h * F32(exe, 0x0055D7A0) * F64f(exe, 0x004E3108) + f.start[1];
            f.pos[2] = static_cast<float>(static_cast<float>(static_cast<float>(tab(i, 1) * std::cos(yr)) * f.dist) * kd) + f.start[2];
            if (f.cnt < 0x3A) {
                const int old = f.rot[0];
                const int a = TruncToAngle(std::atan2(static_cast<double>(tab(i + 1, 0) - tab(i, 0)), static_cast<double>(-(tab(i + 1, 1) - tab(i, 1)))));
                const int d = a - old;
                f.rot[0] = ((d + ((d >> 31) & 3)) >> 2) + old;
            }
            f.water = w.ground;
        }
        const uint32_t e = 0x0055D530 + 6 * static_cast<uint32_t>(f.cnt);
        const float u = F32(exe, 0x0055D7B4);
        f.scl[0] = static_cast<float>(S16(exe, e)) * u;
        f.scl[1] = static_cast<float>(S16(exe, e + 2)) * u;
        const int16_t sz = S16(exe, e + 4);
        ++f.cnt;
        f.scl[2] = static_cast<float>(sz) * u;
        if (0x3B < f.cnt) {
            const int tp = f.target;
            if (tp >= 0 && tp < 2 && w.player[tp].state == 5) w.PlayerTakeHit(tp, 1, 9);  // 0x00415300
            f.v198[0] = (f.pos[0] - f.prevPos[0]) * F32(exe, 0x0055D79C);
            f.v198[1] = (f.pos[1] - f.prevPos[1]) * F32(exe, 0x004C43A4);
            f.state = 6;
            f.v198[2] = (f.pos[2] - f.prevPos[2]) * F32(exe, 0x0055D79C);
            f.flags34 |= 0x10000;
            ReleaseSlot(w, f);
            if (0 < sc.alive) NextLeaper(sc, f.idx + 1, n);
        }
        break;
    }
    case 6: {
        const float vy = f.v198[1] - F32(exe, 0x0055CB10);
        f.rot[0] += 0x600;
        f.v198[1] = vy;
        f.pos[0] = f.v198[0] + f.pos[0];
        f.pos[1] = vy + f.pos[1];
        f.pos[2] = f.v198[2] + f.pos[2];
        if (f.pos[1] < f.water) {
            f.flags34 |= 0x10000;
            ReleaseSlot(w, f);
            --w.liveEnemies;
            --w.attackers;
            sc.fish[f.idx & 15] = nullptr;
            if (0 < sc.alive) --sc.alive;
            w.EnemyDestroy(f);  // 0x00409CC0
            return;
        }
        break;
    }
    default:
        break;
    }
    if (!(f.state == 7 && (f.timer & 1))) {
        const float k6 = F64f(exe, 0x004E3108);
        Mat4 m = Mat4::Identity();
        if (f.state == 2) mx::Translate(m, 0.0f, 0.0f, static_cast<float>(-f.f1E0));
        mx::RotY(m, f.rot[1]);
        mx::RotX(m, f.rot[0]);
        const float sx = f.scl[0] * k6, sy = f.scl[1] * k6, sz = f.scl[2] * k6;
        mx::Scale(m, sx, sy, sz);
        if (f.state == 2) mx::Translate(m, 0.0f, 0.0f, static_cast<float>(-f.f1E0));
        Mat4 d = Mat4::Identity();
        mx::Translate(d, f.pos[0], f.pos[1] + 1.0f, f.pos[2]);
        mx::MulLeft(d, m);
        w.Draw(f.state == 2 ? f.timer + 0x85C : 0x85A, d);
        Mat4 sh = Mat4::Identity();
        mx::Translate(sh, f.pos[0], f.water + F32(exe, 0x004E30F0), f.pos[2]);
        mx::Scale(sh, 1.0f, Bits(0x3DCCCCCD), 1.0f);
        mx::MulLeft(sh, m);
        if (w.runner->scene == 0x1A) w.Note("actor 0x42 fish shadow in scene 0x1A (0x00418620) drawn as 0x004185A0");
        w.Draw(0x85B, sh, 8, 0.5f);  // 0x00430BA0 -> 0x004185A0(model, 0.5)
    }
    if (sc.cur == f.idx) f.flags34 &= ~0x10000u;
    else f.flags34 |= 0x10000;
    const float p[3] = { f.pos[0], f.pos[1] + F32(exe, 0x0055D7B0), f.pos[2] };
    memcpy(f.prevPos, f.pos, 12);
    memcpy(f.body, f.pos, 12);
    mx::Transform(w.cam.view, p, f.viewPos);
    w.RegisterTarget(&f);  // 0x00405160
    w.SyncLock(f);
    if (f.lock) w.lockOn.Register(f.lock, w.playerPos);  // 0x00408EC0
    if (0 < sc.alive && !sc.fish[sc.cur & 15]) NextLeaper(sc, f.idx + 1, n);
}

// 0x00431000 (variant 1): one fish falling (P float 0x19C gravity) until y 0x0055CCD4, or shot: two halves to frame 0x3A
void TaskFish1(Task& t) {
    World::Impl& w = *CurrentWorld();
    FishTask& f = t.As<FishTask>();
    const ExeImage& exe = w.exe;
    bool halves = f.state == 7;
    if (!halves) {
        if (f.flags34 & 8) {
            f.state = 7;
            f.halfY[0] = MotionFrame(w, 0xBF, 0).p[1] + f.pos[1];
            f.halfY[1] = MotionFrame(w, 0xC0, 0).p[1] + f.pos[1];
            halves = true;
        } else {
            const float vy = f.v198[1] - F32(exe, 0x0055CB10);
            f.v198[1] = vy;
            f.pos[1] = vy + f.pos[1];
            if (f.pos[1] < F32(exe, 0x0055CCD4)) w.EnemyDestroy(f);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, f.rot[1]);
            w.Draw(0x85C, m);
        }
    }
    if (halves) {
        const float s6 = Bits(0x3F19999A);
        for (int h = 0; h < 2; ++h) {
            const FrameRec e = MotionFrame(w, h == 0 ? 0xBF : 0xC0, f.half[h]);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, f.rot[1]);
            mx::Translate(m, e.p[0], e.p[1], e.p[2]);
            mx::RotZ(m, e.rz);
            mx::RotY(m, e.ry);
            mx::RotX(m, e.rx);
            mx::Scale(m, s6, s6, s6);
            w.Draw(h + 0x875, m);
            w.Draw(0x877, m);
            if (0x3A < f.half[h]) w.EnemyDestroy(f);
            ++f.half[h];
        }
    }
    mx::Transform(w.cam.view, f.pos, f.viewPos);
    w.RegisterTarget(&f);  // 0x00405160
}

}  // namespace

// 0x0042F9B0 (actor type 0x42): the school (variant 0: 6 / 8 fish, 2: 10 / 15 by the player count; 1: one fish)
bool World::Impl::SpawnFishSchool(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    if (r.type != 0x42) return false;
    (void)t;
    FishSchool& sc = School(*this);
    const int variant = static_cast<int8_t>(r.params[1]);  // +0x130C
    int n = 0;
    if (variant == 0) n = players > 1 ? 8 : 6;
    else if (variant == 2) n = players > 1 ? 15 : 10;
    sc.alive = 0;
    const int r0 = Rand(0x0042FA08);
    sc.cur = static_cast<int8_t>(variant == 1 ? r0 : (n > 0 ? r0 % n : 0));
    if (variant == 1) {
        FishTask* f = NewTask<FishTask>(TaskFish1, 0x00431000);
        f->flags34 = 1;
        memcpy(f->pos, r.pos, 12);
        f->rot[1] = r.rot[1];
        f->radius = 3.0f;
        f->state = 1;
        ts.KillCurrent();
        return true;
    }
    for (int i = 0; i < n; ++i) {
        FishTask* f = NewTask<FishTask>(TaskFish, 0x0042FCA0);
        f->flags34 = 1;
        f->hp = 1;
        f->idx = static_cast<int8_t>(i);
        f->count = static_cast<int8_t>(n);
        const uint32_t tab = variant == 0 ? 0x0055D698 : 0x0055D6C0;
        const float k = F32(exe, 0x0055D230);
        f->pos[0] = static_cast<float>(static_cast<int8_t>(U8(exe, tab + 2 * static_cast<uint32_t>(i)))) * k + r.pos[0];
        f->pos[2] = static_cast<float>(static_cast<int8_t>(U8(exe, tab + 1 + 2 * static_cast<uint32_t>(i)))) * k + r.pos[2];
        f->rot[0] = 0x4000;
        f->pos[1] = r.pos[1];
        f->rot[1] = static_cast<int>(exe.U32(0x0055D6FC + 4 * static_cast<uint32_t>(i))) + r.rot[1];
        f->scl[0] = f->scl[1] = f->scl[2] = 1.0f;
        f->radius = Bits(0x400CCCCD);
        f->state = 0;
        f->cnt = static_cast<int16_t>(Rand(0x0042FB73) % 0x3C);
        f->timer = 0;
        f->water = ground;
        f->slot120 = -1;
        f->target = 0;  // (+0x121: the zeroed block)
        f->circle = static_cast<int>(exe.U32(0x0055D74C + 4 * static_cast<uint32_t>(i))) + 0x7800;
        f->lock = &lockTargets[nextLockTarget++];  // 0x00408E80
        memcpy(f->lock->pos, f->pos, 12);
        f->lock->targetPlayer = 0;
        lockOn.AddDirect(f->lock);
        ++attackers;
        ++liveEnemies;
        ++sc.alive;
        sc.fish[i & 15] = f;
    }
    ts.KillCurrent();
    return true;
}

}  // namespace game
