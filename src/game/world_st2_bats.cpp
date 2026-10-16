// game::World - actor type 0x43: bats (ctor 0x00445DB0, task 0x004460C0 with the sub-behaviours of table 0x00592944,
// draw 0x00447C20, death 0x00448210, particles 0x00448900 / 0x00448A80, floor splat 0x00448C50 / 0x00448CE0 /
// 0x00448DF0, hit flash 0x00430C50 / 0x00430BD0). Variants (record P byte 1, +0x224): 0 a single bat that swoops at
// the players at once; 1 a group circling (index = record state - 1) whose first member follows a spline path
// (0x00564550) before it attacks; 2 / 3 other scenes (CODE only). The death code has per-variant floors / walls of the
// scenes they appear in. Verification: notes/agents/stage2b.md (world_test --stage-start 14659 on
// build\scene_capture_auto_st2.txt: bats at 17260 (variant 1, two) and 18004 (variant 0)).
//
// Precision: the game runs the x87 with 24-bit precision control, so every fadd / fsub / fmul / fdiv / fsqrt result is
// rounded to float (float arithmetic below), fsin / fcos / fpatan results are not (kept in double until the next
// basic operation), qword constants take part in a float-rounded operation (float(double op double)).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

void SpawnBatParticles(World::Impl& w, int n, const float pos[3]);  // 0x00448900

namespace {

constexpr double kToRad = 9.587379924285257e-05;   // double 0x004C4370
constexpr double kToAngle = 10430.378350470453;    // double 0x004C4378

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

int Ftol(float f) { return static_cast<int>(f); }  // __ftol 0x004ACF50 (truncation)

// fild a; fmul qword [0x004C4370] (rounded to float); fsin / fcos (extended)
double Sin(int a) { return std::sin(static_cast<double>(static_cast<float>(static_cast<double>(a) * kToRad))); }
double Cos(int a) { return std::cos(static_cast<double>(static_cast<float>(static_cast<double>(a) * kToRad))); }

// fpatan(y, x); fmul qword k; __ftol
int Atan2Angle(float y, float x, double k = kToAngle) {
    return Ftol(static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) * k));
}

// The bat object (block 0x2A0); EnemyTask supplies +0x34 flags, +0x40 position, +0x64 angles, +0x70 view position,
// +0x100 body point, +0x11C, +0x120 lock-on slot, +0x121 target player, +0x124 radius.
struct BatTask : EnemyTask {
    int f3C = 0;               // +0x3C (read by state 2; never written: 0)
    float home[3] = {};        // +0x194 spawn position
    float v[3] = {};           // +0x1A0 velocity
    float goal[3] = {};        // +0x1AC steering goal
    float goal2[3] = {};       // +0x1B8 second goal (state 2)
    float from[3] = {};        // +0x1C4 start of the current move
    float prev[3] = {};        // +0x1D0 position of the previous frame
    float centre[3] = {};      // +0x1DC circle centre / attack exit point
    float offs[2] = {};        // +0x1E8 / +0x1EC offset beside the players (two players)
    int baseYaw = 0;           // +0x1F0 yaw from the record to the camera at spawn
    int circle = 0;            // +0x1F4 circle angle
    int spin = 0;              // +0x1F8 death spin speed
    int swoop = 0;             // +0x1FC swoop phase
    int swoopStep = 0;         // +0x204
    int wing = 0;              // +0x208 wing roll
    int wingYaw = 0;           // +0x20C
    int wingL = 0, wingR = 0;  // +0x210 / +0x214
    int legs = 0;              // +0x218
    int legFrame = 0;          // +0x21C
    int state = 0;             // +0x220 sub-behaviour (0x00592944)
    int kind = 0;              // +0x224 variant (record P byte 1)
    int idx = 0;               // +0x228 group index (record state - 1)
    int f230 = 0;              // +0x230
    int dir = 0;               // +0x234 (+1 / -1)
    int f238 = 0;              // +0x238
    int flap = 0;              // +0x240 wing frame (model 0xBC1 + n)
    int mode = 0;              // +0x244 attack mode (0 retreat, 1 path end, 2 from a circle)
    int duration = 0;          // +0x248
    int counter = 0;           // +0x24C
    int f250 = 0, f254 = 0;    // +0x250 / +0x254
    int landed = 0;            // +0x26C
    float t = 0, dt = 0;       // +0x270 / +0x274
    float radius290 = 0;       // +0x290
    float radius294 = 0;       // +0x294
    int seg = 0;               // +0x298 spline segment
    float segT = 0;            // +0x29C
};

// Active camera context (0x009A60C0 / 0x009A60D0 + 0x009C6F00 * 0x1A4)
const float* CamPos(World::Impl& w) { return w.cam.pos; }
int CamYaw(World::Impl& w) { return w.cam.yaw; }

// 0x009A6110[0x009C6F00 * 0x69]: tick (+0x110) of the active camera context (context 0: the event camera tick)
int CtxTick(World::Impl& w) {
    if (w.cam.ctx == 0) return w.runner->camera.tick;
    w.Note("bat: tick +0x110 of camera context " + std::to_string(w.cam.ctx) + " (0x009A6458) not modelled (0 used)");
    return 0;
}

// 0x00448890: velocity eased toward (goal - position) by k
void Steer(BatTask& a, float k) {
    a.v[0] = ((a.goal[0] - a.pos[0]) - a.v[0]) * k + a.v[0];
    a.v[1] = ((a.goal[1] - a.pos[1]) - a.v[1]) * k + a.v[1];
    a.v[2] = ((a.goal[2] - a.pos[2]) - a.v[2]) * k + a.v[2];
}

// 0x00447FB0: target player (two players: random, offset beside the camera by distance / 60); else the player in game
void PickTarget(World::Impl& w, BatTask& a) {
    if (w.players == 2) {
        const int r = w.Rand(0x00447FF4) & 1;
        a.target = static_cast<int8_t>(r);
        const float* c = CamPos(w);
        const float dz = a.pos[2] - c[2], dx = a.pos[0] - c[0];
        const int d = Ftol(std::sqrt(dx * dx + dz * dz));
        const float f = static_cast<float>(d) * F32(w.exe, 0x0055CB80);
        const int ang = CamYaw(w) + (r == 0 ? 0xC000 : 0x4000);
        a.offs[0] = static_cast<float>(Sin(ang) * f);
        a.offs[1] = static_cast<float>(Cos(ang) * f);
        return;
    }
    if (w.playersInGame == 0) a.target = 0;
    if (w.playersInGame == 1) a.target = 1;
}

// 0x00448110: goal on the quadratic B-spline of path 0x00564550[variant][index] (4 points of 3 s16 each), segment
// +0x298 at +0x29C (weights 0x0042DFD0)
void SplineGoal(World::Impl& w, BatTask& a) {
    const uint32_t p = 0x00564550 + static_cast<uint32_t>((a.seg + (a.idx + a.kind * 4) * 4) * 3) * 2;
    const float t = a.segT;
    const float u = 1.0f - t;                    // 0x0042DFD0
    const float w0 = u * u * 0.5f;
    const float w1 = u * t + 0.5f;
    const float w2 = t * t * 0.5f;
    auto s = [&](int i) { return static_cast<float>(w.exe.S16(p + static_cast<uint32_t>(i) * 2)); };
    a.goal[0] = (s(3) * w1 + s(6) * w2) + s(0) * w0;
    a.goal[1] = (s(1) * w0 + s(4) * w1) + s(7) * w2;
    a.goal[2] = (s(2) * w0 + s(5) * w1) + s(8) * w2;
}

// (rand() % 41) * 1e-4 + 0.012: speed of the attack lerp
float AttackSpeed(World::Impl& w, uint32_t site) {
    const int r = w.Rand(site) % 0x29;
    return static_cast<float>(r) * F32(w.exe, 0x0055D7B4) + F32(w.exe, 0x005646D4);
}

// Common "swoop at the players" setup (0x00446C0C.., 0x00447AB7..): state 4 mode 2
void StartSwoop(World::Impl& w, BatTask& a, uint32_t randSite) {
    w.Sound(0x4A17A9);
    a.state = 4;
    a.mode = 2;
    memcpy(a.from, a.pos, 12);
    a.counter = 0;
    a.t = 0;
    a.offs[0] = a.offs[1] = 0;
    a.swoop = 0;
    a.dt = AttackSpeed(w, randSite);
    if (a.dir > 0) a.swoop = 0x8000;
    w.bat43Attacker = a.idx;
    const float* c = CamPos(w);
    const float dz = a.pos[2] - c[2], dx = a.pos[0] - c[0];
    a.radius294 = std::sqrt(dx * dx + dz * dz) * F32(w.exe, 0x0055CB28);
    a.swoopStep = Ftol(a.dt * F32(w.exe, 0x005646D0));
    PickTarget(w, a);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sub-behaviours (0x00592944)
// ---------------------------------------------------------------------------------------------------------------------

// 0: 0x004464C0 wait (0x00592940[index] * 20 frames, wings folding), then circle (index > 0) or follow the path
void State0(World::Impl& w, BatTask& a) {
    const int c = a.counter;
    a.wingYaw = c * 0x444;
    int8_t n = 0;
    w.exe.Read(0x00592940 + static_cast<uint32_t>(a.idx), &n, 1);
    const int wait = n * 20;
    a.counter = c + 1;
    if (c > wait - 0xE) --a.flap;
    if (a.counter <= wait) return;
    if (a.idx > 0) w.Sound(0x4A17A9);
    if (a.idx > 0) {
        memcpy(a.from, a.pos, 12);
        a.state = 2;
        if (a.kind == 1) {
            a.centre[0] = Bits(0xC4200000);
            a.centre[2] = Bits(0xC46D8000);
            a.circle = 0;
            a.radius290 = Bits(0x40C8F5C3);
            a.duration = 0x37;
            a.centre[1] = a.home[1] + F32(w.exe, 0x004C43A4);
        } else {
            a.centre[0] = Bits(0xC3CF8000);
            a.centre[1] = Bits(0x430C0000);
            a.centre[2] = Bits(0xC52BE000);
            a.circle = 0x8000;
            a.radius290 = Bits(0x40C8F5C3);
            a.duration = 0x58;
        }
        a.counter = 0;
        a.flap = 0;
        a.wingYaw = 0;
        a.goal2[1] = a.centre[1];
        a.f230 = 1;
        const int ang = a.circle - 0x2000;
        const float r = F32(w.exe, 0x005646DC);
        a.goal2[0] = static_cast<float>(Sin(ang) * r) + a.centre[0];
        a.goal2[2] = static_cast<float>(Cos(ang) * r) + a.centre[2];
        return;
    }
    if (w.bat43Attacker == -1 && w.runner->camera.modeCur == 2 && w.players > 0) {
        a.counter = 0;
        a.t = Bits(0x3DCCCCCD);
        a.state = 3;
        a.f230 = 0;
        memcpy(a.from, a.pos, 12);
    }
}

// 1: 0x004466C0 circle around +0x1DC until +0x248 frames passed and no other bat attacks, then the path (state 3)
void State1(World::Impl& w, BatTask& a) {
    a.flap = (a.flap + 1) % 0x1E;
    a.rot[0] -= Ftol(static_cast<float>(0x1000 - a.rot[0]) * F32(w.exe, 0x005646E0));
    a.circle += a.kind == 1 ? 0x3E0 : -0x3E0;
    a.goal[1] = a.centre[1];
    a.goal[0] = static_cast<float>(Sin(a.circle) * a.radius290) + a.centre[0];
    a.goal[2] = static_cast<float>(Cos(a.circle) * a.radius290) + a.centre[2];
    Steer(a, Bits(0x3C656042));
    const float x = a.v[0] + a.pos[0];
    a.pos[0] = x;
    const float y = a.v[1] + a.pos[1];
    a.pos[1] = y;
    const float z = a.v[2] + a.pos[2];
    a.pos[2] = z;
    a.legs -= Ftol(static_cast<float>(a.legs) * F32(w.exe, 0x004D1D24));
    ++a.counter;
    if (a.counter > a.duration && w.bat43Attacker == -1 && w.runner->camera.modeCur == 2 && w.players > 0) {
        a.from[0] = x;
        a.counter = 0;
        a.t = 0;
        a.from[1] = y;
        a.from[2] = z;
        a.seg = 0;
        a.segT = 0;
        a.state = 3;
        a.f230 = 0;
    }
}

// 2: 0x00446D20 fly from +0x1C4 to the circle entry +0x1B8 (lerp +0x270), then circle (1) or the path (3)
void State2(World::Impl& w, BatTask& a) {
    a.flap = (a.flap + 1) % 0x1E;
    const int base = a.f3C != 0 ? 0x3000 : 0x1000;
    a.rot[0] -= Ftol(static_cast<float>(base - a.rot[0]) * F32(w.exe, 0x005646F8));
    a.legs -= Ftol(static_cast<float>(0x3000 - a.legs) * F32(w.exe, 0x00564700));
    a.goal[0] = (a.goal2[0] - a.from[0]) * a.t + a.from[0];
    a.goal[1] = (a.goal2[1] - a.from[1]) * a.t + a.from[1];
    a.goal[2] = (a.goal2[2] - a.from[2]) * a.t + a.from[2];
    Steer(a, Bits(0x3B449BA6));
    const int bob = (a.flap + 8) % 0x1E;
    const float x = a.v[0] + a.pos[0];
    a.pos[0] = x;
    const float z = a.v[2] + a.pos[2];
    a.pos[2] = z;
    const int ang = Ftol(static_cast<float>(bob) * F32(w.exe, 0x005646E4));
    const float s = static_cast<float>(Sin(ang) * 0.15);  // fmul qword 0x004C4C50
    const float y = (a.v[1] + a.pos[1]) + s;
    a.pos[1] = y;
    a.t = a.t + F32(w.exe, 0x005646FC);
    if (a.t > 1.0f) a.t = 1.0f;
    const float dz = a.goal2[2] - z, dy = a.goal2[1] - y, dx = a.goal2[0] - x;
    if (std::sqrt((dx * dx + dy * dy) + dz * dz) < 5.0f) {
        if (a.f230 == 0) {
            a.state = 3;
            a.t = 0;
            a.f230 = 0;
            return;
        }
        a.state = 1;
        a.counter = 0;
        a.f230 = 0;
    }
}

// 3: 0x00446860 follow the spline path (two segments), then attack (variant 2: drift and swoop)
void State3(World::Impl& w, BatTask& a) {
    a.flap = (a.flap + 1) % 0x1E;
    a.rot[0] -= Ftol(static_cast<float>(0x3000 - a.rot[0]) * F32(w.exe, 0x005646F8));
    if (a.kind != 2) {
        const int r = Ftol((static_cast<float>(a.seg) + a.segT) * F32(w.exe, 0x005643C0));
        a.rot[2] = Ftol(static_cast<float>(Sin(r) * 8192.0));  // fmul qword 0x005646F0
    }
    if (a.counter == 0) {
        SplineGoal(w, a);
        Steer(a, a.t);
        a.t = a.t + F32(w.exe, 0x005646E8);
        if (a.kind == 2) {
            if (a.t > F32(w.exe, 0x004C4D08)) a.t = Bits(0x3E19999A);
        } else if (a.t > F32(w.exe, 0x004D1D24)) {
            a.t = Bits(0x3E4CCCCD);
        }
        const float lim = F32(w.exe, 0x004C4CB8);
        if (std::sqrt((a.v[0] * a.v[0] + a.v[1] * a.v[1]) + a.v[2] * a.v[2]) > lim) {
            float n[3] = { a.v[0], a.v[1], a.v[2] };
            const float len = std::sqrt((n[0] * n[0] + n[1] * n[1]) + n[2] * n[2]);  // 0x004AA9C0
            for (float& c : n) c = c / len;
            a.v[0] = n[0] * lim;
            a.v[1] = n[1] * lim;
            a.v[2] = n[2] * lim;
        }
        const float dz = a.goal[2] - a.pos[2], dy = a.goal[1] - a.pos[1], dx = a.goal[0] - a.pos[0];
        if (std::sqrt((dx * dx + dy * dy) + dz * dz) < 4.0f) a.segT = w.bat43SegStep + a.segT;
        if (a.segT >= F32(w.exe, 0x005643E0)) {
            ++a.seg;
            a.segT = 0;
            if (a.seg > 1) {
                if (a.kind == 2) {
                    a.counter = 1;
                } else {
                    memcpy(a.from, a.pos, 12);
                    a.state = 4;
                    a.mode = 1;
                    a.counter = 0;
                    a.t = 0;
                    a.dt = 0;
                    a.wingYaw = 0;
                    w.bat43Attacker = a.idx;
                    a.offs[0] = a.offs[1] = 0;
                    if (w.players == 2) {
                        a.target = static_cast<int8_t>(w.Rand(0x00446B27) & 1);
                    } else {
                        if (w.playersInGame == 0) a.target = 0;
                        if (w.playersInGame == 1) a.target = 1;
                    }
                }
            }
        }
    }
    int c = a.counter;
    a.pos[0] = a.v[0] + a.pos[0];
    a.pos[1] = a.pos[1] + a.v[1];
    a.pos[2] = a.v[2] + a.pos[2];
    if (c <= 0) return;
    const int bob = (a.flap + 8) % 0x1E;
    const float k = F32(w.exe, 0x00564404);
    a.v[0] = a.v[0] * k;
    a.v[1] = a.v[1] * k;
    a.v[2] = a.v[2] * k;
    const int ang = Ftol(static_cast<float>(bob) * F32(w.exe, 0x005646E4));
    a.pos[1] = static_cast<float>(Sin(ang) * 0.5) + a.pos[1];  // fmul qword 0x004C4C98
    c = a.seg == 0 ? c - 1 : c + 1;
    a.counter = c;
    if ((a.idx + 1) * 0x14 < c) StartSwoop(w, a, 0x00446C55);
}

// 4: 0x00446F30 attack: lerp to the players (mode 1) or swoop around them (else); within 5 the hit, then retreat (5)
void State4(World::Impl& w, BatTask& a) {
    a.flap = (a.flap + 1) % 0x1E;
    const float* c = CamPos(w);
    int roll = a.rot[2], d;
    if (a.mode == 1) {
        a.goal[0] = ((c[0] + a.offs[0]) - a.from[0]) * a.t + a.from[0];
        a.goal[1] = (c[1] - a.from[1]) * a.t + a.from[1];
        a.goal[2] = ((c[2] + a.offs[1]) - a.from[2]) * a.t + a.from[2];
        Steer(a, a.t * F32(w.exe, 0x005644F8));
        a.t = a.dt + a.t;
        a.dt = a.dt + F32(w.exe, 0x004E30E0);
        if (a.t > 1.0f) a.t = 1.0f;  // fcomp qword 0x004ECB70
        a.pos[0] = a.v[0] + a.pos[0];
        a.pos[1] = a.v[1] + a.pos[1];
        a.pos[2] = a.v[2] + a.pos[2];
        d = Ftol(static_cast<float>(roll) * F32(w.exe, 0x005644F8));
    } else {
        Mat4 m = Mat4::Identity();  // push; identity; RotY(+0x1F0); (sin(+0x1FC), 0, 0) transformed
        mx::RotY(m, a.baseYaw);
        const float in[3] = { static_cast<float>(Sin(a.swoop)), 0.0f, 0.0f };
        float out[3];
        mx::Transform(m, in, out);
        const int sw = a.swoop + a.swoopStep;
        a.swoop = sw;
        a.pos[0] = ((c[0] + a.offs[0]) - a.from[0]) * a.t + a.from[0];
        a.pos[2] = ((c[2] + a.offs[1]) - a.from[2]) * a.t + a.from[2];
        const float ky = (a.kind == 2 || a.kind == 0) ? F32(w.exe, 0x004C4D04) : F32(w.exe, 0x004C4CB0);
        a.pos[1] = (c[1] - a.pos[1]) * ky + a.pos[1];
        a.pos[0] = out[0] * a.radius294 + a.pos[0];
        a.pos[2] = out[2] * a.radius294 + a.pos[2];
        a.t = a.dt + a.t;
        const int bob = (a.flap + 8) % 0x1E;
        const int ang = Ftol(static_cast<float>(bob) * F32(w.exe, 0x005646E4));
        a.pos[1] = static_cast<float>(Sin(ang) * 0.2) + a.pos[1];  // fmul qword 0x0055DD00
        const int r = Ftol(static_cast<float>(Sin(sw + 0x4000) * static_cast<double>(F32(w.exe, 0x0055D22C))));
        d = (roll - r) / 4;
    }
    a.rot[2] = roll - d;
    a.rot[0] -= Ftol(static_cast<float>(0x1E00 - a.rot[0]) * F32(w.exe, 0x005646E0));
    a.wing -= Ftol(static_cast<float>(a.wing + 0x1000) * F32(w.exe, 0x004C4C88));
    a.wingL -= Ftol(static_cast<float>(a.wingL + 0x2800) * F32(w.exe, 0x004C4CC8));
    a.wingR -= Ftol(static_cast<float>(0x2800 - a.wingR) * F32(w.exe, 0x0056470C));
    const int legsOld = a.legs;
    a.legs -= Ftol(static_cast<float>(0x3000 - a.legs) * F32(w.exe, 0x00564700));
    ++a.legFrame;
    const float dz = a.pos[2] - c[2], dy = a.pos[1] - c[1], dx = a.pos[0] - c[0];
    if (std::sqrt((dx * dx + dy * dy) + dz * dz) >= 5.0f && static_cast<float>(a.counter) < 1.0f) return;
    // the hit
    if (a.target >= 0 && a.target < 2 && w.player[a.target].state == 5) w.PlayerTakeHit(a.target, 1, 9);  // 0x00415300
    a.state = 5;
    const float k = (a.kind == 1 && a.mode == 1) ? F32(w.exe, 0x004C4CC8) : F32(w.exe, 0x004C43AC);
    a.v[0] = a.v[0] * k;
    a.v[1] = a.v[1] * k;
    a.v[2] = a.v[2] * k;
    a.dir = 1 - (w.Rand(0x00447472) & 1) * 2;
    int off = 0;
    float rad = 0, yoff = 0;
    switch (a.kind) {  // 0x00447678
    case 0:
        yoff = static_cast<float>(a.idx) * F32(w.exe, 0x005644FC);
        rad = 15.0f;
        off = a.dir > 0 ? 0 : -0xF00;
        break;
    case 1: {
        if (a.dir > 0) off = w.Rand(0x004474AC) % 0x401;
        else off = -0xC00 - w.Rand(0x004474BD) % 0x401;
        rad = 15.0f;
        const int r3 = w.Rand(0x004474D9) % 3;
        yoff = (static_cast<float>(a.idx) * F32(w.exe, 0x004C4CB8) + static_cast<float>(r3)) + F32(w.exe, 0x004E30F0);
        break;
    }
    case 2:
        yoff = static_cast<float>(a.idx) * F32(w.exe, 0x00564708);
        rad = 12.0f;
        off = a.dir > 0 ? 0 : 0x800;
        break;
    case 3:
        yoff = static_cast<float>(a.idx) * F32(w.exe, 0x00564708);
        rad = 15.0f;
        off = a.dir > 0 ? 0 : -0x800;
        break;
    default:
        w.Note("bat variant > 3 (0x00446F30 default case reads the stack)");
        break;
    }
    const int ang = a.baseYaw + off + 0x8000;
    a.centre[0] = static_cast<float>(Sin(ang) * rad) + a.pos[0];
    a.centre[1] = yoff + a.pos[1];
    a.centre[2] = static_cast<float>(Cos(ang) * rad) + a.pos[2];
    a.circle = a.dir > 0 ? 0 : 0xFFFF;
    a.f238 = 1 - a.f238;
    (void)legsOld;
    SpawnBatParticles(w, 8, a.pos);  // 0x00448900
    memcpy(a.from, a.pos, 12);
    a.counter = 0;
    a.t = 0;
    const int r = w.Rand(0x00447639) % 0x15;
    a.dt = static_cast<float>(r) * F32(w.exe, 0x0055D7B4) + F32(w.exe, 0x00564704);
    w.bat43Attacker = -1;
    a.mode = 0;
    a.rot[2] = 0;
}

// 5: 0x00447690 retreat around the exit point +0x1DC, then swoop again (state 4 mode 2)
void State5(World::Impl& w, BatTask& a) {
    a.flap = (a.flap + 1) % 0x1E;
    if (a.counter == 0) a.circle += a.dir * 0x1F0;
    int add = 0;
    float r = 0;
    bool set = true;
    switch (a.kind) {  // 0x00447C04
    case 0: add = 0x4000; r = F32(w.exe, 0x0055D2B4); break;
    case 1: add = 0xE000; r = F32(w.exe, 0x005644FC); break;
    case 2: add = 0x2000; r = F32(w.exe, 0x0055E1C8); break;
    case 3: add = 0x1800; r = F32(w.exe, 0x004C43A0); break;
    default: set = false; break;
    }
    if (set) {
        a.goal[1] = a.centre[1];
        const int ang = a.circle + add;
        a.goal[0] = static_cast<float>(Sin(ang) * r) + a.centre[0];
        a.goal[2] = static_cast<float>(Cos(ang) * r) + a.centre[2];
    }
    Steer(a, a.dt);
    const int bob = (a.flap + 8) % 0x1E;
    const float x = a.pos[0] + a.v[0];
    a.pos[0] = x;
    const float z = a.pos[2] + a.v[2];
    a.pos[2] = z;
    const int ang = Ftol(static_cast<float>(bob) * F32(w.exe, 0x005646E4));
    const double s = Sin(ang);
    const float y = (a.v[1] + a.pos[1]) + static_cast<float>(s * 0.25);  // qword 0x00564720
    a.pos[1] = y;
    const int legs = a.legs + Ftol(static_cast<float>(s * -2048.0));  // qword 0x00564718
    a.legs = legs;
    const int cnt = a.counter;
    if (cnt == 0) {
        const float dz = z - a.prev[2], dx = x - a.prev[0], dy = y - a.prev[1];
        const float h = std::sqrt(dx * dx + dz * dz);
        const int p = static_cast<int16_t>(Atan2Angle(-dy, h, -10430.378350470453));  // qword 0x0055CB20
        int e = static_cast<int>((static_cast<uint32_t>(static_cast<uint16_t>(-static_cast<int16_t>(a.rot[0]))) -
                                  static_cast<uint32_t>(static_cast<uint16_t>(p))) & 0xFFFF);
        if (e > 0x8000) e -= 0x10000;
        a.rot[0] = a.rot[0] + e / 8;
        if (a.rot[0] < 0) a.rot[0] = 0;
    }
    a.wing -= Ftol(static_cast<float>(a.wing) * F32(w.exe, 0x004C4CC0));
    if (cnt == 0) {
        const int v = Ftol(static_cast<float>(a.circle) * F32(w.exe, 0x00564710));
        a.rot[2] = Ftol(static_cast<float>(static_cast<float>(Sin(v) * 8192.0) * static_cast<float>(-a.dir)));
    } else {
        a.rot[2] -= Ftol(static_cast<float>(a.rot[2]) * F32(w.exe, 0x004C4CB0));
    }
    a.legs = legs - Ftol(static_cast<float>(legs) * F32(w.exe, 0x004D1D24));
    if (cnt == 0 && ((a.dir > 0 && a.circle > 0x8800) || (a.dir < 0 && a.circle < 0x7800))) a.counter = 1;
    if (a.counter == 0) return;
    const float k = F32(w.exe, 0x00564404);
    a.v[0] = a.v[0] * k;
    ++a.counter;
    a.v[1] = a.v[1] * k;
    a.v[2] = a.v[2] * k;
    if (a.counter <= 0x1E || a.flap != 0x12) return;
    StartSwoop(w, a, 0x00447B04);
    if (a.kind != 0) {
        const float q = F32(w.exe, 0x004C4C7C);
        a.v[0] = a.v[0] * q;
        a.v[1] = a.v[1] * q;
        a.v[2] = a.v[2] * q;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Draw 0x00447C20
// ---------------------------------------------------------------------------------------------------------------------
void BatDraw(World::Impl& w, BatTask& a) {
    uint32_t face = w.seedCounter % 0x1E;  // 0x009A32A0
    const bool dead = (a.flags34 & 0x1000000) != 0;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotY(m, a.rot[1]);
    mx::RotZ(m, a.rot[2]);
    mx::RotX(m, a.rot[0]);
    {
        Mat4 b = m;
        mx::RotZ(b, 0x4007);
        mx::RotY(b, 0xDEC8);
        mx::RotX(b, 0x3FF9);
        w.Draw(dead ? 0xBC0 : 0xBBF, b);
        w.Draw(a.flap + 0xBC1, b);
        {
            Mat4 h = b;
            mx::Translate(h, Bits(0xBF13FE5D), Bits(0xBED74BC7), Bits(0x3B6BEDFA));
            mx::RotZ(h, 0x8863 - Ftol(static_cast<float>(Sin(a.wingR) * -2500.0)));  // qword 0x00564728
            mx::RotY(h, 0xFFC0);
            mx::RotX(h, 0xFFF2);
            if (face > 0xE) face = 0x1E - face;
            if (a.state == 6) face = 0;
            w.Draw(static_cast<int>(face) + 0xBF8, h);
        }
        if (!dead) {
            Mat4 g = b;
            mx::Translate(g, Bits(0x3FE89D49), 0.0f, 0.0f);
            mx::RotZ(g, a.wing);
            mx::RotY(g, a.wingYaw);
            w.Draw(0xBF1, g);
            {
                Mat4 q = g;
                mx::Translate(q, Bits(0x3DB7B4A2), Bits(0x3EDF212D), Bits(0xBC449BA6));
                mx::RotZ(q, 0);
                mx::RotY(q, 0xC041);
                mx::RotX(q, a.wingL + 0xD494);
                w.Draw(0xBF4, q);
            }
            {
                Mat4 q = g;
                mx::Translate(q, Bits(0x3DBDD97F), Bits(0x3EDF8A09), Bits(0xBC449BA6));
                mx::RotZ(q, 0);
                mx::RotY(q, 0xC041);
                mx::RotX(q, a.wingR + 0x4EE0);
                w.Draw(0xBF2, q);
            }
        }
    }
    for (int side = 0; side < 2; ++side) {  // legs
        Mat4 l = m;
        mx::Translate(l, Bits(side == 0 ? 0x3EAA0903 : 0xBEAA0903), Bits(0xBDAB367A), Bits(0xBBD4FDF4));
        mx::RotZ(l, 0xC000);
        mx::RotY(l, a.legs + 0x3C0);
        mx::RotX(l, 0xC000);
        w.Draw(side == 0 ? 0xBF6 : 0xBF5, l);
        mx::Translate(l, Bits(0x3F2A4A8C), 0.0f, 0.0f);
        mx::RotZ(l, 0xDB13);
        mx::RotY(l, 0);
        mx::RotX(l, 0);
        w.Draw(side == 0 ? 0xBBE : 0xBBD, l);
        mx::Translate(l, Bits(0x3F2E5604), 0.0f, 0.0f);
        w.Draw(a.legFrame % 0xF + (side == 0 ? 0xC08 : 0xC18), l);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------------------------------------------------

// 0x00430BD0 (created by 0x00430C50 from a view-space point): 25 frames of models 0x3A.. at the point, scale 0.3
// (0.15 with language 0x009C911E == 1; 2 here), drawn with the identity (view space)
struct HitFlashTask : WTask {
    float p[3] = {};  // +0x38
    int frame = 0x3A; // +0x54
};
void TaskHitFlash(Task& t) {
    World::Impl& w = *CurrentWorld();
    HitFlashTask& h = t.As<HitFlashTask>();
    const float s = Bits(0x3E99999A);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, h.p[0], h.p[1], h.p[2]);
    mx::Scale(m, s, s, s);
    w.Draw(h.frame++, m, 8, -1.0f, false, -1, true);
    if (0x52 < h.frame) w.ts.KillCurrent();
}

// 0x00448A80 (created by 0x00448900): a falling scrap (model 0xBF0, scale 0.15) swinging toward random points
struct ScrapTask : WTask {
    float p[3] = {};     // +0x34
    float drift = 0;     // +0x44
    float baseY = 0;     // +0x50
    float to[3] = {};    // +0x58 (y unused)
    int yaw = 0;         // +0x68
    int roll = 0;        // +0x6C
    int phase = 0;       // +0x70
    int swing = 0;       // +0x74
    int swings = 0;      // +0x78
    int step = 0;        // +0x7C
};
void TaskScrap(Task& t) {
    World::Impl& w = *CurrentWorld();
    ScrapTask& s = t.As<ScrapTask>();
    const int step = s.step, ph = s.phase, sw = s.swing;
    s.p[0] = (s.to[0] - s.p[0]) / static_cast<float>(0x8000 / step) + s.p[0];
    s.p[2] = (s.to[2] - s.p[2]) * F32(w.exe, 0x005647A0) + s.p[2];
    const double sn = Sin(ph);
    s.p[1] = s.baseY - static_cast<float>(sn * 0.5);  // qword 0x004C4C98
    const int r = Ftol(static_cast<float>(static_cast<double>(sw) * sn));
    s.baseY = s.baseY + s.drift;
    s.roll = sw / 2 - r;
    s.phase = ph + step;
    if (s.phase >= 0x8000) {
        if (8 < ++s.swings) {
            w.ts.KillCurrent();
            return;
        }
        s.phase = 0;
        s.to[0] = (static_cast<float>(w.Rand(0x00448B3C) % 0x3D) * F32(w.exe, 0x0055D230) + s.p[0]) - 3.0f;
        s.baseY = s.p[1];
        s.to[2] = (static_cast<float>(w.Rand(0x00448B63) % 0x3D) * F32(w.exe, 0x0055D230) + s.p[2]) - 3.0f;
        s.drift = (static_cast<float>(w.Rand(0x00448B90) % 7) * F32(w.exe, 0x0055D2B0) - F32(w.exe, 0x004E3100)) -
                  F32(w.exe, 0x0056479C);
        s.step = w.Rand(0x00448BBA) % 0x401 + 0x100;
        s.swing = w.Rand(0x00448BD0) % 0xC01 + 0x2400;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.p[0], s.p[1], s.p[2]);
    mx::RotZ(m, s.roll);
    mx::RotY(m, s.yaw);
    mx::RotX(m, 0x4000);
    const float k = Bits(0x3E19999A);
    mx::Scale(m, k, k, k);
    w.Draw(0xBF0, m);
}

// Splat on the floor 0x00448C50 -> task 0x00448CE0 (120 frames, growing / pulsing) -> 0x00448DF0 (fades out)
struct SplatTask : WTask {
    float pos[3] = {};   // +0x40
    int pitch = 0;       // +0x64
    int yaw = 0;         // +0x68
    int fade = 0;        // +0x1320
    int timer = 0x78;    // +0x1330
    float scale = 0;     // +0x1340
};
void TaskSplat(Task& t) {
    World::Impl& w = *CurrentWorld();
    SplatTask& s = t.As<SplatTask>();
    if (s.fn == 0x00448CE0) {
        if (--s.timer == 0) s.fn = 0x00448DF0;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
        mx::RotY(m, s.yaw);
        mx::RotX(m, s.pitch);
        const int a = Ftol(static_cast<float>(s.timer) * F32(w.exe, 0x004C4CA4));
        const float f = static_cast<float>(Cos(a) * s.scale) * 3.0f;
        Mat4 q = m;
        mx::Scale(q, f, 1.0f, f);
        w.Draw(0x1A38, q);
        const float g = static_cast<float>(Sin(a) * s.scale);
        mx::Scale(m, g, g, g);
        w.Draw(static_cast<int>(w.seedCounter % 0x1E) + 0x15E4, m);
        return;
    }
    if (++s.timer >= 0x1E && ++s.fade >= 0x28) {
        w.ts.KillCurrent();
        return;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::RotY(m, s.yaw);
    mx::RotX(m, s.pitch);
    const float f = static_cast<float>(static_cast<double>(s.scale) * 3.0);  // qword 0x0055CB00
    mx::Scale(m, f, 1.0f, f);
    w.Draw(0x1A38, m, 8, static_cast<float>(0x28 - s.fade) * F32(w.exe, 0x005643F8));  // 0x004185A0 (alpha)
}

void SpawnSplat(World::Impl& w, float x, float y, float z, float scale, int pitch, int yaw) {
    SplatTask* s = w.NewTask<SplatTask>(TaskSplat, 0x00448CE0);
    s->pos[0] = x;
    s->pos[1] = y;
    s->pos[2] = z;
    s->yaw = yaw;
    s->pitch = pitch;
    s->timer = 0x78;
    s->scale = scale;
    s->fade = 0;
    w.Sound(w.cfg.eventMode == 0 ? 0x118A9 : 0x119A9);  // 0x009A1A08 (stage)
}

// 0x004487D0 -> task 0x00448800 (variant 3): 30 frames of models 0x1339.. at (x, -25, z)
struct Splash3Task : WTask {
    float p[3] = {};
    int frame = 0;
};
void TaskSplash3(Task& t) {
    World::Impl& w = *CurrentWorld();
    Splash3Task& s = t.As<Splash3Task>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.p[0], s.p[1], s.p[2]);
    w.Draw(s.frame + 0x1339, m);
    if (++s.frame > 0x1D) w.ts.KillCurrent();
}

// 0x00448850: plane test a x + b y + c z + d <= 0
bool BelowPlane(const BatTask& a, float pa, float pb, float pc, float pd) {
    return ((pa * a.pos[0] + pb * a.pos[1]) + pc * a.pos[2]) + pd <= 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Death 0x00448210: falls spinning, bounces off the scene's floors / walls (per variant), a splat where it lands;
// removed after 120 frames
// ---------------------------------------------------------------------------------------------------------------------
void BatDeath(World::Impl& w, BatTask& a) {
    if (a.landed != 0) {
        a.pos[1] = a.pos[1] - F32(w.exe, 0x005644F8);
    } else {
        const float vy = a.v[1] - F32(w.exe, 0x00564798);
        a.v[1] = vy;
        const float x = a.pos[0] + a.v[0];
        a.pos[0] = x;
        const float z = a.v[2] + a.pos[2];
        a.pos[2] = z;
        const float y = vy + a.pos[1];
        a.pos[1] = y;
        a.rot[0] += a.spin;
        int spin = Ftol(static_cast<float>(a.spin) * F32(w.exe, 0x0055CB40));
        a.spin = spin;
        switch (a.kind) {  // 0x004487B8
        case 0: {
            if (x > F32(w.exe, 0x00564764)) {
                for (int row = 0; row < 2; ++row) {  // two walls of 5 points each (0x005928C8)
                    const uint32_t base = 0x005928C8 + static_cast<uint32_t>(row) * 60;
                    auto P = [&](int i, int c) { return F32(w.exe, base + static_cast<uint32_t>(i * 3 + c) * 4); };
                    int i = 0;
                    if (x < P(1, 0)) {
                        for (;;) {
                            if (i >= 4) break;
                            const bool lt = x < P(i + 2, 0);
                            ++i;
                            if (!lt) break;
                        }
                    }
                    const float cross = (P(i + 1, 2) - P(i, 2)) * (P(i, 0) - x) - (P(i + 1, 0) - P(i, 0)) * (P(i, 2) - a.pos[2]);
                    if ((row == 0 && cross > 0.0f) || (row == 1 && cross < 0.0f)) a.v[2] = a.v[2] * F32(w.exe, 0x004C4C64);
                }
            }
            if (x < F32(w.exe, 0x00564760)) {
                spin += 0x600;
                a.pos[0] = Bits(0xC438C000);
                a.v[0] = 0;
                a.v[2] = 0;
                a.spin = spin;
            }
            if (y < F32(w.exe, 0x0056475C)) {
                a.pos[1] = Bits(0x420FAD1E);
                SpawnSplat(w, a.pos[0], Bits(0x420FC199), a.pos[2], 0.75f, 0, a.rot[1]);
                a.landed = 1;
                w.Sound(0x1E16A9);
                break;
            }
            float f = 0.0f;  // steps: floor 45 - n where x < n * 6 - 671
            if (a.pos[0] < F32(w.exe, 0x00564758)) {
                while (f > F32(w.exe, 0x00564754)) {
                    f = f - 1.0f;
                    if (!(f * F32(w.exe, 0x0055E1C8) - F32(w.exe, 0x00564750) > a.pos[0])) break;
                }
            }
            f = f + F32(w.exe, 0x005308DC);
            if (y <= f) {
                a.pos[1] = f;
                w.Sound(0x1E16A9);
                const float k = F32(w.exe, 0x004C43A8);
                a.spin += -0x400;
                a.v[0] = a.v[0] * k;
                a.v[1] = a.v[1] * F32(w.exe, 0x0056474C);
                a.v[2] = a.v[2] * k;
            }
            break;
        }
        case 1: {
            const float s = static_cast<float>(static_cast<double>(static_cast<float>((-630.6 - static_cast<double>(x)))) * -15.5) -
                            static_cast<float>(static_cast<double>(static_cast<float>(-923.8 - static_cast<double>(z))) * -34.89999999999998);
            if (s <= 0.0f) {
                if (BelowPlane(a, 0.0f, Bits(0x3F7AA1A5), Bits(0x3E509C3D), Bits(0x430C2B30))) {
                    SpawnSplat(w, a.pos[0], a.pos[1] - 1.0f, a.pos[2], 0.75f, 0x1000, 0xDC00);
                    a.landed = 1;
                    w.Sound(0x1E16A9);
                }
            } else if (y < F32(w.exe, 0x00564768)) {
                SpawnSplat(w, x, Bits(0x42453333), z, 0.75f, 0, 0xDC00);
                a.landed = 1;
            }
            break;
        }
        case 2: {
            if (x > F32(w.exe, 0x00564748) && x < F32(w.exe, 0x00564744) && z > F32(w.exe, 0x00564740) &&
                z < F32(w.exe, 0x0056473C)) {
                float f = 0.0f;
                if (z < F32(w.exe, 0x00564738)) {
                    while (f < 8.0f) {
                        f = f + 1.0f;
                        const float edge = (F32(w.exe, 0x0056473C) - f * F32(w.exe, 0x00564734)) - F32(w.exe, 0x00564734);
                        if (!(edge > z)) break;
                    }
                }
                f = f * F32(w.exe, 0x004E1FE4) - F32(w.exe, 0x0055E1B4);
                if (y < f) {
                    a.pos[1] = f;
                    a.v[1] = vy * F32(w.exe, 0x0055D2D8);
                    if (a.f250 == 0) {
                        a.f250 = 1;
                        a.v[0] = a.v[0] * F32(w.exe, 0x0055D2D8);
                        a.v[2] = a.v[2] * F32(w.exe, 0x0055D2D8);
                        w.Sound(0x1E16A9);
                        a.spin += 0x600;
                    }
                }
            }
            if (a.pos[1] <= F32(w.exe, 0x00564730)) {
                a.pos[1] = Bits(0xC2100000);
                SpawnSplat(w, a.pos[0], Bits(0xC20FEB85), a.pos[2], 0.75f, 0, a.rot[1]);
                a.landed = 1;
                w.Sound(0x1E16A9);
            }
            break;
        }
        case 3:
            if (y <= F32(w.exe, 0x0055D2C8)) {
                a.landed = 1;
                Splash3Task* s = w.NewTask<Splash3Task>(TaskSplash3, 0x00448800);  // 0x004487D0
                s->p[0] = x;
                s->p[1] = Bits(0xC1C80000);
                s->p[2] = z;
                w.Sound(0x4616A9);
            }
            break;
        default:
            break;
        }
    }
    if (0x78 < ++a.counter) {
        w.EnemyDestroy(a);  // 0x00409CC0
        return;
    }
    w.LightChar();  // 0x0041DC70 (renderer state)
    BatDraw(w, a);
    w.LightScene();  // 0x0041DCC0
}

// 0x004460C0
void TaskBat(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatTask& a = t.As<BatTask>();
    if (a.lock) a.slot120 = static_cast<int8_t>(a.lock->slot);  // +0x120 written by the table task 0x00408DD0
    if (a.fn == 0x00448210) {
        BatDeath(w, a);
        return;
    }
    if (a.state != 6 && (a.flags34 & 8) &&
        (a.kind != 0 || F32(w.exe, 0x005646D8) <= static_cast<float>(CtxTick(w)))) {
        HitFlashTask* h = w.NewTask<HitFlashTask>(TaskHitFlash, 0x00430BD0);  // 0x00430C50(+0x40): view position
        memcpy(h->p, a.viewPos, 12);
        h->frame = 0x3A;
        w.Sound((w.Rand(0x00446130) & 1) == 0 ? 0x4217A9 : 0x4117A9);
        const uint32_t f = a.flags34;
        int p;
        if ((f & 2) && (f & 4)) p = w.Rand(0x00446165) & 1;
        else p = (f & 2) ? 0 : 1;
        w.AddScore(p, 0x50);
        ++w.player[p].hits;  // 0x009A5C86 + p * 0x130
        if (a.state == 4) w.bat43Attacker = -1;
        a.flags34 |= 0x1000000;
        --w.liveEnemies;
        --w.attackers;
        a.flags34 |= 0x10000;
        if (a.slot120 != -1) w.lockOn.Release(a.slot120);
        if (a.state == 0 || a.state == 2) a.flags34 |= 0x40000000;
        const float k = F32(w.exe, 0x0055D1A0);
        const float vx = a.v[0] * k;
        a.state = 6;
        a.counter = 0;
        a.f250 = 0;
        a.f254 = 0;
        const float vz = a.v[2] * k;
        const int yaw = CamYaw(w) + 0x8000;
        a.v[0] = static_cast<float>(Sin(yaw) + static_cast<double>(vx));
        a.spin = static_cast<int>(0xFFFFFD00u);
        a.v[2] = static_cast<float>(Cos(yaw) + static_cast<double>(vz));
            SpawnBatParticles(w, 0x28, a.pos);  // 0x00448900
        a.fn = 0x00448210;
    }
    switch (a.state) {  // 0x00592944
    case 0: State0(w, a); break;
    case 1: State1(w, a); break;
    case 2: State2(w, a); break;
    case 3: State3(w, a); break;
    case 4: State4(w, a); break;
    case 5: State5(w, a); break;
    default: break;  // 6: 0x00420810 (nothing)
    }
    const int st = a.state;
    if (st != 0) {
        int ang = Atan2Angle(a.pos[0] - a.prev[0], a.pos[2] - a.prev[2]) & 0xFFFF;
        a.rot[1] &= 0xFFFF;
        const int base = a.rot[1];
        if (st == 4 || (st == 3 && a.kind != 2) || (st == 5 && a.counter > 0) || (st == 3 && a.counter > 0)) {
            const float* c = CamPos(w);
            ang = static_cast<int16_t>(Atan2Angle(c[0] - a.pos[0], c[2] - a.pos[2]));
        }
        int d = static_cast<int>(static_cast<uint32_t>(ang - a.rot[1]) & 0xFFFF);
        if (d > 0x8000) d -= 0x10000;
        a.rot[1] = (st == 1 ? d / 4 : d / 10) + base;
    }
    BatDraw(w, a);
    if (a.state == 1) {
        a.body[0] = a.centre[0];
        a.body[1] = a.centre[1] + 4.0f;
        a.body[2] = a.centre[2];
    } else {
        memcpy(a.body, a.pos, 12);
    }
    w.SyncLock(a);
    w.lockOn.Register(a.lock, w.playerPos);  // 0x00408EC0
    mx::Transform(w.cam.view, a.pos, a.viewPos);
    w.RegisterTarget(&a);  // 0x00405160
    memcpy(a.prev, a.pos, 12);
    // 0x008111F0[index] = this (not read elsewhere)
    static const bool trace = getenv("BAT_TRACE") != nullptr;
    if (trace)
        fprintf(stderr, "%d BAT %u st %d k %d i %d c %d pos %.6g %.6g %.6g v %.6g %.6g %.6g rot %d %d %d t %.6g dt %.6g goal %.6g %.6g %.6g\n",
                w.frame, t.serial, a.state, a.kind, a.idx, a.counter, a.pos[0], a.pos[1], a.pos[2], a.v[0], a.v[1], a.v[2],
                a.rot[0], a.rot[1], a.rot[2], a.t, a.dt, a.goal[0], a.goal[1], a.goal[2]);
}

}  // namespace

// 0x00448900: n scraps around pos (task 0x00448A80, 9 rand calls each)
void SpawnBatParticles(World::Impl& w, int n, const float pos[3]) {
    const float k = F32(w.exe, 0x0055D230);
    for (; n > 0; --n) {
        ScrapTask* s = w.NewTask<ScrapTask>(TaskScrap, 0x00448A80);
        s->p[0] = (static_cast<float>(w.Rand(0x00448932) % 0xB) * k + pos[0]) - 0.5f;
        s->p[1] = (static_cast<float>(w.Rand(0x00448959) % 0xB) * k + pos[1]) - 0.5f;
        const int rz = w.Rand(0x00448980) % 0xB;
        s->baseY = s->p[1];
        s->p[2] = (static_cast<float>(rz) * k + pos[2]) - 0.5f;
        s->to[0] = (static_cast<float>(w.Rand(0x004489AD) % 0x3D) * k + s->p[0]) - 3.0f;
        s->to[2] = (static_cast<float>(w.Rand(0x004489D4) % 0x3D) * k + s->p[2]) - 3.0f;
        s->drift = (static_cast<float>(w.Rand(0x004489FB) & 3) * F32(w.exe, 0x0055D2B0) - F32(w.exe, 0x004E3100)) -
                   F32(w.exe, 0x0056479C);
        s->step = w.Rand(0x00448A29) % 0x201 + 0x200;
        s->swing = w.Rand(0x00448A3F) % 0x801 + 0x2800;
        s->yaw = w.Rand(0x00448A55) & 0xFFFF;
    }
}

// 0x00445DB0 (first run of the spawned actor: the bat task is created, the spawner killed)
void SpawnBat(World::Impl& w, const SpawnRecord& r) {
    const int variant = r.ParamS8(1);  // +0x130C
    const int state = static_cast<int16_t>(r.state);  // +0x11C
    if (w.players < 2 && ((variant == 0 && state > 1) || (variant != 0 && state > 2))) return;
    BatTask* a = w.NewTask<BatTask>(TaskBat, 0x004460C0);
    a->flags34 = 1;
    a->hp = 1;
    a->idx = state - 1;
    a->kind = variant;
    memcpy(a->pos, r.pos, 12);
    memcpy(a->rot, r.rot, 12);
    a->prev[0] = a->pos[0];
    a->prev[2] = a->pos[2];
    memcpy(a->home, a->pos, 12);
    a->radius = 1.5f;
    a->state = 0;
    a->slot120 = -1;
    a->flap = 0x2C;
    a->counter = 0;
    a->t = 0;
    a->wingL = 0;
    a->mode = 0;
    const float* c = CamPos(w);
    a->baseYaw = static_cast<int16_t>(Atan2Angle(c[0] - r.pos[0], c[2] - r.pos[2]));
    a->f230 = 0;
    a->lock = &w.lockTargets[w.nextLockTarget++];
    a->target = 0;  // +0x121: cleared task memory (only variant 0 / two players pick one)
    w.SyncLock(*a);
    w.lockOn.AddDirect(a->lock);  // 0x00408E80
    ++w.attackers;
    ++w.liveEnemies;
    w.bat43Attacker = -1;
    // 0x008111F0[index] = bat (not read elsewhere)
    if (a->kind == 0) {
        a->state = 4;
        a->baseYaw = 0x4000;
        memcpy(a->from, a->pos, 12);
        a->wingYaw = 0;
        a->t = 0;
        a->mode = 2;
        a->dt = AttackSpeed(w, 0x00445F75);
        w.bat43Attacker = 0;  // (s8) spawner +0x131B: cleared task memory
        a->offs[0] = a->offs[1] = 0;
        a->swoop = 0;
        a->radius294 = 3.0f;
        a->swoopStep = Ftol(a->dt * F32(w.exe, 0x005646D0));
        if (w.players == 2) {
            a->target = static_cast<int8_t>(w.Rand(0x00446016) & 1);
        } else {
            if (w.playersInGame == 0) a->target = 0;
            if (w.playersInGame == 1) a->target = 1;
        }
    } else if (a->kind == 2) {
        a->state = 3;
        a->baseYaw = 0x12FB;
        memcpy(a->from, a->pos, 12);
        a->counter = 0;  // spawner +0x131B * 20
        a->flap = (a->idx * 10) % 0x1E;
    }
    w.bat43SegStep = Bits(0x3D75C28F);  // 0x007DCC68 (0x007DCC64 = 0.03, 0x007DCC50 / 54 = 0: not read by the bats)
    w.SyncLock(*a);
}

// ---------------------------------------------------------------- bat group 2 water surface (actor 0x40, scene 0x19)
namespace {

struct BatWaterTask : WTask {
    uint32_t flags = 0;     // +0x34
    float pos[3] = {};      // +0x194
    int8_t area = 0;        // +0x1A4 (0x009A2BB0 at creation)
    int8_t areaCount = 0;   // +0x1A5
};

// 0x0043F010: counts area changes (removed after the fourth; flag 0x4000000 after the second), deforms the mesh of
// model 0x10CF under the bats (renderer), draws model 0x10CF at +0x194
void TaskBatWater(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatWaterTask& k = t.As<BatWaterTask>();
    const int8_t area = static_cast<int8_t>(w.runner->variant);
    if (area != k.area) {
        if (3 < ++k.areaCount) {
            w.ts.KillCurrent();
            return;
        }
        k.area = area;
        if (1 < k.areaCount) k.flags |= 0x4000000u;
    }
    if (!(k.flags & 0x4000000u)) {
        // 0x009B739C & 0x8000 (assumed set, see 0x0043EFE0), camera 0x009A2D78 != 0x47 and not 0x40 in ticks
        // 0x10C..0x168: vertices of the mesh 0x0041A320(0x009B7394, 0x0055E580) are pushed down near the bats of
        // 0x007DCC20[0..2] (0x0041A440 / 0x0041A480), then 0x0043F2E0 / 0x0043F3E0. No game state; not ported.
        w.Note("bat water surface deformation (0x0043F010 mesh loop, 0x0043F2E0 / 0x0043F3E0) not ported (renderer)");
    }
    w.LightChar();  // 0x0041DC70 (renderer state)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    w.Draw(0x10CF, m);
    w.LightScene();  // 0x0041DCC0
}

// 0x0043EFE0: first run. The original continues only when 0x009B739C & 0x8000 (a renderer setting; set in every
// capture: the task runs as 0x0043F010) and looks the mesh up (+0x1A8)
void TaskBatWaterInit(Task& t) {
    BatWaterTask& k = t.As<BatWaterTask>();
    k.fn = 0x0043F010;
    t.fn = TaskBatWater;
}

}  // namespace

// 0x0043EF70(bat): task 0x0043EFE0 (0x1AC) at the bat's record position, y + 0x004C43AC
void SpawnBatWater(World::Impl& w, const float pos[3]) {
    BatWaterTask* k = w.NewTask<BatWaterTask>(TaskBatWaterInit, 0x0043EFE0);
    k->pos[0] = pos[0];
    k->pos[1] = pos[1] + F32(w.exe, 0x004C43AC);
    k->pos[2] = pos[2];
    k->flags = 1;
    k->area = static_cast<int8_t>(w.runner->variant);
    k->areaCount = 0;
}

}  // namespace game
