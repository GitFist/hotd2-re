// game::World - actor type 0x25 (0x004840D0 -> 0x004842A0): scripted characters of the cutscenes and background
// (enemy skeleton P byte 0, a command script P s32 12, removed at camera P s16 2 / tick P s16 4), node callback
// 0x00485260 (talking mouths, held props), draw 0x00484FF0.
// CONFIRMED = code read + frame-exact against build\scene_capture_play2scene1.txt; CODE = read, not exercised.
//
// Script (8-byte commands {s16 op, s16 a, s16 b, s16 c}; ops 4/4, 7 and 8 are 16 bytes): commands run until one
// waits; the waiting command stays current and +0x1320 counts the frames it waited.
//   0 / 1 a: when the condition a holds (0 counter == b, 1 camera b at tick >= c, 2 (op 1 only) motion tick == b
//            (-1: last), 3 event flag b, -1 always): animation frozen (+0x1324) = op
//   2: motion b (0x00411930), counter c (-1: rand() % 10); a 1 / 2: floor shadow off / on
//   3: blend to motion b from tick c over a frames (0x004119A0)
//   4 a: wait for the condition a (0..3 as above, 4: farther from point than before)
//   5: turn by c over b frames (a 0: right, 1: left); 6 a: 0 stop turning, 1 face the players
//   7: face point (x, z); 8: set position (a 1: y, else x / z); 9: voice slot; 10: skip by players in game
//   11: follow camera path b; 12: head look; 13: sound; 14 a: mouth mode +0x1330; 15: jump; 16: node blood
//   17: switch task function; 18: end; -1: script done (0x00484D40)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

namespace game {

namespace {

struct Actor25Task : EnemyTask {
    uint32_t script = 0;   // +0x1394 current command (event memory)
    int waited = 0;        // +0x1320
    int frozen = 0;        // +0x1324
    int turnFrames = 0;    // +0x1328
    int turnMode = 0;      // +0x132C (1 timed turn, 2 face the players)
    int mouth = 0;         // +0x1330 (2 talking, 1 blinking)
    int mouthTick = 0;     // +0x1334
    int turnTarget = 0;    // +0x1350
    int turnStep = 0;      // +0x1354
    int pathMode = 0;      // +0x1358
    int path = 0, pathOffset = 0;  // +0x135C / +0x1360
    int headLook = 0;      // +0x1364
    float lastPos[3] = {}; // +0x13C0
};

}  // namespace

static int16_t EvS16(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a & ~3u, &w);
    return static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
}

static float Bits32(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float F32(const ExeImage& exe, uint32_t va) { return Bits32(exe.U32(va)); }

static uint32_t EvU32(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a, &w);
    return w;
}

// 0x00484FF0: skeleton (0x00411090), extras by P s16 6, animation counter
static void Actor25Draw(World::Impl& w, Actor25Task& a) {
    if (!(w.flags5900 & 0x20)) {
        w.AnimDraw(a);  // light 0x0041DC70 / 0x0041DCC0: renderer
        w.EnemyShadow(a);
        const int extra = a.record.ParamS16(6);
        if (extra != 0 && !w.Actor25Extra(a, extra, a.path)) w.Note("actor 0x25 extra draw " + std::to_string(extra) + " (0x00484FF0) not ported");
    }
    if (a.frozen == 0) ++a.anim.counter[0];
}

// 0x00401AE0: Euler angles from the matrix's y axis (0x00401800: x = atan2(y.z, y.y), z = -atan2(y.x, d)) and the
// z axis brought back by RotZ(-z) RotX(-x) (y = atan2(z.x, z.z)); x87 atan2 / cos / sin, __ftol to s16
void MatrixToAnglesYAxis(const Mat4& top, int* ax, int* ay, int* az) {
    const double k2pi = 6.283185307179586 / 65536.0;  // double 0x004C4370
    const float yAxis[3] = { 0, 1, 0 }, zAxis[3] = { 0, 0, 1 };
    float d1[3], d2[3], d3[3];
    mx::TransformDir(top, yAxis, d1);
    const int x = TruncToAngle(std::atan2(static_cast<double>(d1[2]), static_cast<double>(d1[1])));
    double d;
    if (((x + 0x2000) & 0x4000) == 0) d = static_cast<double>(static_cast<float>(d1[1] / std::cos(x * k2pi)));
    else d = static_cast<double>(static_cast<float>(d1[2] / std::sin(x * k2pi)));
    *ax = x;
    *az = -TruncToAngle(std::atan2(static_cast<double>(d1[0]), d));
    mx::TransformDir(top, zAxis, d2);
    Mat4 m = Mat4::Identity();
    mx::RotZ(m, -*az);
    mx::RotX(m, -*ax);
    mx::TransformDir(m, d2, d3);
    *ay = TruncToAngle(std::atan2(static_cast<double>(d3[0]), static_cast<double>(d3[2])));
}

// Command 11 (0x00484B18): position (and, mode 1, angles) from camera path +0x135C at the camera tick 0x009A6110
// (Cam_EvaluateAngles 0x004042D0), plus the offset 0x00596B18[+0x1360] rotated by those angles
static void Actor25FollowPath(World::Impl& w, Actor25Task& a) {
    float pos[3] = { a.pos[0], a.pos[1], a.pos[2] }, tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(a.path, static_cast<float>(w.runner->camera.tick), pos, tg, &roll, false)) {
        w.Note("actor 0x25 path " + std::to_string(a.path) + " not evaluated");
        return;
    }
    int ang[3];
    for (int k = 0; k < 3; ++k) ang[k] = static_cast<int>(tg[k]);  // __ftol
    memcpy(a.pos, pos, 12);
    if (a.pathMode != 2) memcpy(a.rot, ang, 12);
    if (a.pathOffset == 0) return;
    const uint32_t e = 0x00596B18 + static_cast<uint32_t>(a.pathOffset) * 0x18;
    float v[3], o[3];
    w.exe.Read(e, v, 12);
    Mat4 m = Mat4::Identity();  // push, identity, RotZ, RotY, RotX, 0x004A8A80, pop
    mx::RotZ(m, ang[2]);
    mx::RotY(m, ang[1]);
    mx::RotX(m, ang[0]);
    mx::Transform(m, v, o);
    a.pos[0] = o[0] + a.pos[0];
    const float y = o[1] + a.pos[1];
    a.pos[2] = o[2] + a.pos[2];
    a.pos[1] = (a.path >= 0x156 && a.path <= 0x15C) ? static_cast<float>(static_cast<double>(y) + 2.0) : y;  // 0x0055CAF8
    if (w.exe.U32(e + 0x10) != 0) {  // angles of RotZ RotY RotX re-derived (0x00401AE0), yaw + the entry's angle
        Mat4 r = Mat4::Identity();
        mx::RotZ(r, a.rot[2]);
        mx::RotY(r, a.rot[1]);
        mx::RotX(r, a.rot[0]);
        MatrixToAnglesYAxis(r, &a.rot[0], &a.rot[1], &a.rot[2]);
        a.rot[1] += static_cast<int>(w.exe.U32(e + 0x10));
    }
}

// 0x00409E00: angle a moved toward b by at most `step` (16-bit wrap)
static uint32_t TurnToward(uint32_t a, uint32_t b, int step) {
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
static bool InRange(uint32_t v, uint32_t centre, int range) {
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

// The players' eye point 0x009C71E0 (+ 0x00565DD8 on y) as seen from p: Cam_DirToAngles 0x004016B0 (pitch, yaw - 0x8000)
static void LookAtPlayers(World::Impl& w, const float p[3], uint32_t* pitch, uint32_t* yaw) {
    double dy;
    w.exe.Read(0x00565DD8, &dy, 8);
    int pi = 0, ya = 0;
    mx::DirToAngles(w.playerPos[0] - p[0], static_cast<float>(static_cast<double>(w.playerPos[1]) + dy) - p[1], w.playerPos[2] - p[2], &pi, &ya);
    *pitch = static_cast<uint32_t>(pi) & 0xFFFF;
    *yaw = static_cast<uint32_t>(ya - 0x8000) & 0xFFFF;
}

// 0x00485D70: head look starts facing the players from the actor's position (+0x1368 pitch, +0x136C yaw: the
// original keeps them in those words of the object)
static void Actor25HeadLookInit(World::Impl& w, EnemyTask& a) {
    uint32_t pitch, yaw;
    LookAtPlayers(w, a.pos, &pitch, &yaw);
    a.flags1368 = pitch;
    a.flags136C = yaw;
}

// 0x00485BA0 (node 2 of a head-looking actor 0x25): the head turns toward the players by 0xC0 per frame (pitch within
// +-0x4000 of 0, yaw within +-0x4000 of the body's yaw - 0x8000) and replaces the node's rotation (0x004A9F70 on the
// model-view: the angles are view-space ones)
static Mat4 Actor25HeadLook(World::Impl& w, EnemyTask& a, const Mat4& node) {
    const uint32_t bodyYaw = static_cast<uint32_t>(a.rot[1] - 0x8000) & 0xFFFF;
    float p[3];
    mx::Transform(w.cam.inverse, a.anim.nodes[2].hit, p);  // camera matrix x the node's view-space point
    uint32_t pitch, yaw;
    LookAtPlayers(w, p, &pitch, &yaw);
    uint32_t v = TurnToward(a.flags1368, pitch, 0xC0);
    a.flags1368 = InRange(v, 0, 0x4000) ? v : TurnToward(a.flags1368, 0, 0xC0);
    v = TurnToward(a.flags136C, yaw, 0xC0);
    a.flags136C = InRange(v, bodyYaw, 0x4000) ? v : TurnToward(a.flags136C, bodyYaw, 0xC0);
    Mat4 mv = node;  // node callbacks get the model-view matrix
    mx::ClearRotation(mv);
    mx::RotY(mv, static_cast<int>(a.flags136C));
    mx::RotX(mv, static_cast<int>(a.flags1368));
    return mv;
}

// 0x004124B0: the part models (ids > 0x11) are released (queue type 2 = op 0x51) when the actor goes
static void ReleaseParts(World::Impl& w, const Actor25Task& a) {
    for (uint32_t p = a.parts; p; p += 2) {
        const int16_t id = EvS16(w.mem, p);
        if (id < 0) break;
        if (id > 0x11 && w.residency) w.resQueue.push_back({ 0x51, w.exe.U32(w.exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id)) + 4) });
    }
}

// 0x00484D40: script done, still drawn until removed
static void TaskActor25Done(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor25Task& a = t.As<Actor25Task>();
    const SpawnRecord& r = a.record;
    auto remove = [&] {
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;  // 0x004092D0
        ReleaseParts(w, a);  // 0x004124B0 (parts)
        w.ts.KillCurrent();  // Task_Kill
    };
    if (w.skip2230 != 0) remove();
    if (!(a.flags34 & 0x2000000)) {
        if (w.runner->camera.camId == r.ParamS16(2) && w.runner->camera.tick >= r.ParamS16(4)) remove();
    } else if (w.runner->flags[r.ParamS16(2) & 0xFF] == 1) {
        remove();
    }
    Actor25Draw(w, a);
}

// Command 17's task functions (stage 2): 0x00484DF0 sink (+0x50 -= 0.02, frozen at the motion end, splash 0x61 and
// removed at y <= -28), 0x00484EA0 thrown off (+0x4C = x - 231.5, +0x50 = 20, gravity 0x00565E1C, removed below 0),
// 0x00484F90 falling (gravity 0x00565E1C, removed after 200 frames); each draws (0x00484FF0) after a removal too
static void Actor25Remove(World::Impl& w, Actor25Task& a, bool parts) {
    if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;  // 0x004092D0
    if (parts) ReleaseParts(w, a);  // 0x004124B0
    w.ts.KillCurrent();             // Task_Kill 0x004A7040
}

static void TaskActor25Sink(Task& t) {  // 0x00484DF0
    World::Impl& w = *CurrentWorld();
    Actor25Task& a = t.As<Actor25Task>();
    a.vel[1] = static_cast<float>(static_cast<double>(a.vel[1]) - 0.02);  // double 0x0055D188
    a.pos[1] = a.vel[1] + a.pos[1];
    if (a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) - 1) a.frozen = 1;
    double lim;
    w.exe.Read(0x00569480, &lim, 8);
    if (a.pos[1] <= static_cast<float>(lim)) {
        const float at[3] = { a.pos[0], Bits32(0xC1C7FF97), a.pos[2] };
        w.SpawnEffectAt(at, 0, 0, 0x61, 1, -1);  // 0x00407340
        Actor25Remove(w, a, true);
    }
    Actor25Draw(w, a);
}

static void TaskActor25Thrown(Task& t) {  // 0x00484EA0
    World::Impl& w = *CurrentWorld();
    Actor25Task& a = t.As<Actor25Task>();
    float g, x0;
    w.exe.Read(0x00565E1C, &g, 4);
    w.exe.Read(0x00569488, &x0, 4);
    if (a.sub == 0) {
        a.vel[0] = a.pos[0] - x0;
        a.vel[1] = 20.0f;
        a.acc[1] = 0;
        a.acc[0] = 0;
        ++a.sub;
    } else if (a.sub != 1) {
        Actor25Draw(w, a);
        return;
    }
    a.acc[1] = a.acc[1] - g;
    a.vel[1] = a.acc[1] + a.vel[1];
    a.pos[1] = a.vel[1] + a.pos[1];
    a.pos[0] = a.vel[0] + a.pos[0];
    if (a.pos[1] < 0.0f) Actor25Remove(w, a, true);
    Actor25Draw(w, a);
}

static void TaskActor25Fall(Task& t) {  // 0x00484F90
    World::Impl& w = *CurrentWorld();
    Actor25Task& a = t.As<Actor25Task>();
    float g;
    w.exe.Read(0x00565E1C, &g, 4);
    a.vel[1] = a.vel[1] - g;
    a.pos[1] = a.vel[1] + a.pos[1];
    if (200 < ++a.timer1338) Actor25Remove(w, a, false);
    Actor25Draw(w, a);
}

// 0x004842A0
static void TaskActor25(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor25Task& a = t.As<Actor25Task>();
    const SpawnRecord& r = a.record;
    const EventMemory& mem = w.mem;
    auto remove = [&](bool skip) {
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;  // 0x004092D0
        if (!skip || a.lo == 0) ReleaseParts(w, a);  // 0x004124B0 (parts; on a skip only when +0x1316 == 0)
        w.ts.KillCurrent();  // Task_Kill
    };
    if (w.skip2230 != 0) remove(true);
    if (!(a.flags34 & 0x2000000)) {
        if (w.runner->camera.camId == r.ParamS16(2) && w.runner->camera.tick >= r.ParamS16(4)) remove(false);
    } else if (w.runner->flags[r.ParamS16(2) & 0xFF] == 1) {
        remove(false);
    }
    const int camId = w.runner->camera.camId, tick = w.runner->camera.tick;
    uint32_t p = a.script;
    for (int guard = 0; guard < 256; ++guard) {
        const int op = EvS16(mem, p), sa = EvS16(mem, p + 2), sb = EvS16(mem, p + 4), sc = EvS16(mem, p + 6);
        bool wait = false;
        auto next = [&](uint32_t n) {
            a.waited = 0;
            p += n;
        };
        auto tickIs = [&](int b) {
            return b == -1 ? a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) - 1 : a.anim.tick[0] == b;
        };
        switch (op) {
        case 0:
        case 1: {
            bool ok = false;
            switch (sa) {
            case 0: ok = a.waited == sb; break;
            case 1: ok = camId == sb && tick >= sc; break;
            case 2: ok = op != 0 && tickIs(sb); break;
            case 3: ok = w.runner->flags[sb & 0xFF] == 1; break;
            case -1: ok = true; break;
            default: break;
            }
            if (!ok) {
                wait = true;
                break;
            }
            a.frozen = op;
            next(8);
            break;
        }
        case 2:
            w.AnimSetMotion(a, sb);  // 0x00411930
            a.anim.counter[0] = sc == -1 ? w.Rand(0x00484470) % 10 : sc;
            if (sa == 1) a.anim.drawFlags &= ~4u;
            else if (sa == 2) a.anim.drawFlags |= 4;
            next(8);
            break;
        case 3:
            w.AnimBlendTo(a, sb, sc, sa);  // 0x004119A0(anim, motion, start, n)
            next(8);
            break;
        case 4: {
            bool ok = false;
            uint32_t len = 8;
            switch (sa) {
            case 0: ok = a.waited == sb; break;
            case 1: ok = camId == sb && tick >= sc; break;
            case 2: ok = tickIs(sb); break;
            case 3: ok = w.runner->flags[sb & 0xFF] == 1; break;
            case 4: {  // CONFIRMED: auto_st1 12843 / 12844 (every script step on the original's frame, T +0x1394)
                float pt[2];
                const uint32_t u0 = EvU32(mem, p + 8), u1 = EvU32(mem, p + 12);
                memcpy(&pt[0], &u0, 4);
                memcpy(&pt[1], &u1, 4);
                const float dx = a.pos[0] - pt[0], dz = a.pos[2] - pt[1];
                const float lx = a.lastPos[0] - pt[0], lz = a.lastPos[2] - pt[1];
                ok = !(std::sqrt(dz * dz + dx * dx) <= std::sqrt(lz * lz + lx * lx));
                len = 16;
                break;
            }
            default: break;
            }
            if (!ok) {
                wait = true;
                break;
            }
            next(len);
            break;
        }
        case 5:
            a.turnFrames = sb;
            a.turnMode = 1;
            if (sa == 0) {
                a.turnStep = -(sc / sb);
                a.turnTarget = a.rot[1] - sc;
            } else if (sa == 1) {
                a.turnStep = sc / sb;
                a.turnTarget = a.rot[1] + sc;
            }
            next(8);
            break;
        case 6:
            if (sa == 0) a.turnMode = 0;
            else if (sa == 1) a.turnMode = 2;
            next(8);
            break;
        case 7: {
            float pt[2];
            const uint32_t u0 = EvU32(mem, p + 8), u1 = EvU32(mem, p + 12);
            memcpy(&pt[0], &u0, 4);
            memcpy(&pt[1], &u1, 4);
            int pitch, yaw;
            mx::DirToAngles(a.pos[0] - pt[0], 0.0f, a.pos[2] - pt[1], &pitch, &yaw);  // Cam_DirToAngles 0x004016B0
            a.rot[1] = yaw & 0xFFFF;
            a.turnMode = 0;
            next(16);
            break;
        }
        case 8: {
            const uint32_t u0 = EvU32(mem, p + 8), u1 = EvU32(mem, p + 12);
            if (sa == 1) {
                memcpy(&a.pos[1], &u0, 4);
            } else {
                memcpy(&a.pos[0], &u0, 4);
                memcpy(&a.pos[2], &u1, 4);
            }
            next(16);
            break;
        }
        case 9: {  // gun model on node 5 (0x004EC9E0[b * 3 + a], player b, weapon a; game type 1 (0x00484739): b 0 / 1
                   // are P1's / P2's item character 0x009A2242 + p * 0x14)
            int c = sb;
            if (w.runner->cfg.gameType == 1 && w.modes && (sb == 0 || sb == 1)) c = w.modes->pitems[sb].character;
            a.anim.nodes[5].model = w.exe.S16(0x004EC9E0 + static_cast<uint32_t>(c * 3 + sa) * 2);
            next(8);
            break;
        }
        case 0xA: {  // a 0 / 1 / 2: unless 0x009C7000 (players in game) == a, skip past the next command with a == -2
            const uint32_t base = p;
            next(8);
            if (sa >= 0 && sa <= 2 && w.playersInGame != sa) {
                uint32_t q = base + 16;
                for (int s = EvS16(mem, base + 10), g = 0; s != -2 && g < 256; ++g) {
                    s = EvS16(mem, q + 2);
                    q += 8;
                }
                p = q;
            }
            break;
        }
        case 0xB:
            a.pathMode = (sa == 1 || sa == 2) ? sa : 0;
            if (a.pathMode != 0) {
                a.path = sb;
                a.pathOffset = sc;
            }
            next(8);
            break;
        case 0xC:  // head look off (a 0) / on (a 1: 0x00485D70 starts it facing the players); CODE
            if (sa == 0) {
                a.headLook = 0;
            } else if (sa == 1) {
                a.headLook = 1;
                Actor25HeadLookInit(w, a);
            }
            next(8);
            break;
        case 0x10: {  // blood on node b (0x00407310, scale 0.75) and that node's wound model (0x004C7160[kind], stage c)
            w.SpawnHitBlood(a, sb, 0.75f);
            const uint32_t table = w.exe.U32(0x004C7160 + static_cast<uint32_t>(a.anim.kind) * 4);
            const int m = w.exe.S16(table + static_cast<uint32_t>(sc + sb * 6) * 2) & 0xFFFF;
            if (2 < m && sb >= 0 && sb < static_cast<int>(a.anim.nodes.size())) a.anim.nodes[static_cast<size_t>(sb)].model = m;
            next(8);
            break;
        }
        case 0xD:
            w.TypingSound(EvU32(mem, p + 4));
            next(8);
            break;
        case 0xE:
            if (sa == 0) a.mouth = 0;
            else if (sa == 1) a.mouth = 1;
            else if (sa == 2) {
                a.mouth = 2;
                a.mouthTick = 0;
            }
            next(8);
            break;
        case 0xF:
            a.waited = 0;
            p = EvU32(mem, p + 4);
            break;
        case 0x11:  // switch the task function (a 0 / 1 / 4; from the next frame), or an effect (a 2 / 3)
            if (sa == 0 || sa == 1 || sa == 4) {
                if (sa == 0) {
                    a.fn = 0x00484DF0;
                    t.fn = TaskActor25Sink;
                } else if (sa == 1) {
                    a.fn = 0x00484EA0;
                    t.fn = TaskActor25Thrown;
                } else {
                    a.vel[1] = Bits32(0xBED11111);
                    a.fn = 0x00484F90;
                    t.fn = TaskActor25Fall;
                }
                next(8);
                wait = true;
            } else if (sa == 2) {  // 0x00484F50
                const float at[3] = { Bits32(0xC479E000), Bits32(0x404F5C29), Bits32(0xC4A20666) };
                w.SpawnEffectAt(at, 0, 0xC000, 0x34, 0, -1);
                next(8);
            } else if (sa == 3) {
                const float at[3] = { w.runner->scene == 9 ? -189.3f : -1264.0f, -24.9f, w.runner->scene == 9 ? -1505.0f : -1353.0f };
                w.SpawnEffectAt(at, 0, 0, 0x41, 2, -1);
                next(8);
            } else {
                next(8);
            }
            break;
        case 0x12:  // Task_Kill, then the waiting path (the object stays readable / drawn this frame)
            w.ts.KillCurrent();
            wait = true;
            break;
        case -1:
            a.fn = 0x00484D40;
            t.fn = TaskActor25Done;
            wait = true;
            break;
        default:
            w.Note("actor 0x25 script op " + std::to_string(op) + " (0x004842A0) not ported");
            wait = true;
            break;
        }
        if (wait) break;
    }
    // the waiting command (0x004842A0 default case)
    if (static const bool dbg = getenv("HOTD2_A25DBG") != nullptr; dbg && p != a.script)  // debug: script progress
        fprintf(stderr, "%d %u %X\n", w.frame, w.CurrentSerial(), p);
    a.script = p;
    ++a.waited;
    if (a.turnMode == 1) {
        a.rot[1] += a.turnStep;
        if (--a.turnFrames == 0) {
            a.rot[1] = a.turnTarget;
            a.turnMode = 0;
        }
    } else if (a.turnMode == 2) {
        int pitch, yaw;
        mx::DirToAngles(a.pos[0] - w.playerPos[0], 0.0f, a.pos[2] - w.playerPos[2], &pitch, &yaw);
        a.rot[1] = yaw & 0xFFFF;
    }
    if (a.pathMode != 0) Actor25FollowPath(w, a);
    memcpy(a.lastPos, a.pos, 12);
    Actor25Draw(w, a);
}

// Node callback 0x00485260 (game type 0): node 2 = the face (talking mouth +0x1330 == 2 / blinking == 1 by kind),
// then held props by kind / motion
void World::Impl::Actor25Node(EnemyTask& e, int idx, const Mat4& node) {
    Actor25Task& a = static_cast<Actor25Task&>(e);
    const int kind = a.anim.kind;
    const Mat4 m = (a.headLook != 0 && idx == 2) ? Actor25HeadLook(*this, a, node) : node;  // 0x00485BA0 (CODE)
    // game type 1: item 0xC (0x009C88AC) scales the head (1.5, 1, 1.5) and the hands / feet (2, 1, 2), item 0x15
    // (0x009C88A8) the head of skeleton 0x38 (2, 2, 2): the node's own draws only (0x004852C3 / 0x00485599)
    Mat4 ms = m;
    if (runner->cfg.gameType == 1 && modes && kind >= 0x20 && kind <= 0x40) {
        if (idx == 2 && kind != 0x38 && modes->itemFlags[4] == 1) mx::Scale(ms, 1.5f, 1.0f, 1.5f);
        else if (idx == 2 && kind == 0x38 && modes->itemFlags[0] == 1) mx::Scale(ms, 2.0f, 2.0f, 2.0f);
        else if ((idx == 5 || idx == 8 || idx == 12 || idx == 15) && kind != 0x38 && modes->itemFlags[4] == 1) mx::Scale(ms, 2.0f, 1.0f, 2.0f);
    }
    int model = a.anim.nodes[idx].model;
    if (idx == 2) {
        const int blink = static_cast<int>((seedCounter + static_cast<uint32_t>(a.c131B) * 20u) % 150u);
        if (a.mouth == 2) {
            const int f = static_cast<int8_t>(exe.S16(0x00596C80 + static_cast<uint32_t>(a.mouthTick % 13)) & 0xFF);
            switch (kind) {
            case 0x36: model = (static_cast<int8_t>(exe.S16(0x00596C90 + static_cast<uint32_t>(a.mouthTick % 13)) & 0xFF)) + 0x149D; break;
            case 0x39: model = f + 0x14B3; break;
            case 0x3A: model = f + 0x14C3; break;
            case 0x3B: model = f + 0x14DA; break;
            case 0x3C: model = f + 0x14E1; break;
            case 0x3D: model = f + 0xD28; break;
            case 0x3E: model = f + 0xC3A; break;
            case 0x3F: {
                Mat4 q = ms;
                mx::Scale(q, 0.9f, 0.9f, 0.9f);  // 0x3F666666
                model = -1;
                Draw(f + 0x1273, q, 8, -1.0f, false, idx);
                break;
            }
            default: break;
            }
            ++a.mouthTick;
        } else if (a.mouth == 1 && blink < 13) {
            const int f = static_cast<int8_t>(exe.S16(0x00596C80 + static_cast<uint32_t>(blink)) & 0xFF);
            if (kind == 0x39) model = f + 0x14BB;
            else if (kind == 0x3A) model = f + 0x14CB;
            else if (kind == 0x3B) model = f + 0x14D3;
        }
    }
    if (model >= 0) Draw(model, ms, 8, -1.0f, false, idx);
    // props held by kind / motion (0x00485625..)
    if (kind == 0x3E && a.anim.motion[0] == 0x14B && idx == 8 && a.anim.tick[0] >= 0 && a.anim.tick[0] < 0x6C) {
        Mat4 q = m;
        auto bits = [](uint32_t u) {
            float f;
            memcpy(&f, &u, 4);
            return f;
        };
        mx::Translate(q, bits(0xBE99999A), bits(0xBFB33333), bits(0x3F19999A));
        mx::RotY(q, 0x4800);
        mx::RotX(q, 0x1000);
        mx::Scale(q, bits(0x3E19999A), bits(0x3E19999A), bits(0x3E19999A));
        Draw(0x7ED, q, 8, -1.0f, false, idx);
    } else if (kind == 0x3F && idx == 1) {
        Draw(0x125D, m, 8, -1.0f, false, idx);
    } else if ((kind == 0x3B || kind == 0x3C) && (a.anim.motion[0] == 0x32D || a.anim.motion[0] == 0x355) && idx == 5 &&
               a.anim.tick[0] >= 1 && a.anim.tick[0] < 4) {
        Actor25Flash(a, m, a.anim.tick[0] - 1, a.anim.tick[0] == 1);  // 0x00485725 (CODE)
    }
    // 0x00485790: props of motions 0x32A / 0x34C / 0x35D (kinds 0x21, 0x34, 0x39..0x40)
    if (!(kind == 0x21 || kind == 0x34 || (kind >= 0x39 && kind <= 0x40))) return;
    const int motion = a.anim.motion[0], tick = a.anim.tick[0];
    if (motion == 0x32A) {  // CONFIRMED (auto_st1 4135..): model 0x7ED in the hand (node 5) from tick 0x30
        if (idx != 5 || tick < 0x30) return;
        Mat4 q = m;
        mx::Translate(q, Bits32(0x3F666666), Bits32(0xBFB33333), Bits32(0x3F19999A));
        mx::RotZ(q, 0x8000);
        mx::RotY(q, 0x1000);
        mx::Scale(q, Bits32(0x3E19999A), Bits32(0x3E19999A), Bits32(0x3E19999A));
        Draw(0x7ED, q, 8, -1.0f, false, idx);
    } else if (motion == 0x34C) {  // model 0x10E3 on node 8 from tick 0x32, before that flying in (world space)
        if (idx != 8) return;
        Mat4 q;
        if (tick > 0x31) {
            q = m;
            mx::Translate(q, 0.0f, Bits32(0xBF4CCCCD), Bits32(0x3DCCCCCD));
            mx::RotX(q, 0x7000);
        } else {
            q = cam.view;  // 0x004A9230(view of the active context): model-view from the view
            mx::Translate(q, a.pos[0], a.pos[1], a.pos[2]);
            mx::RotY(q, a.rot[1]);
            mx::Translate(q, Bits32(0xC000A3D7), Bits32(0x4160872B), Bits32(0xC0856873));
            const float f = static_cast<float>(0x32 - tick);
            mx::Translate(q, 0.0f, f * F32(exe, 0x00565E1C), f * F32(exe, 0x00564700));
            mx::RotZ(q, 0x61B0);
            mx::RotY(q, 0xF760);
            mx::RotX(q, 0x8B93);
        }
        mx::Scale(q, Bits32(0x3DCCCCCD), Bits32(0x3DCCCCCD), Bits32(0x3DCCCCCD));
        Draw(0x10E3, q, 8, -1.0f, false, idx, true);
    } else if (motion == 0x35D && idx == 5) {  // three muzzle flashes (CODE)
        for (int start : { 0x26, 0x40, 0x58 })
            if (tick >= start && tick <= start + 2) {
                Actor25Flash(a, m, tick - start, tick == start);
                break;
            }
    }
}

// Muzzle flash sprite model 0xE24 (0x004185A0, alpha 1 - k * 0x004C4D10) on the hand node; on its first frame a
// shell casing 0x00485DE0
void World::Impl::Actor25Flash(EnemyTask& e, const Mat4& m, int k, bool casing) {
    Draw(0xE24, m, 8, F32(exe, 0x004C4380) - static_cast<float>(k) * F32(exe, 0x004C4D10), false, 5);
    if (casing) SpawnCasing(e, m);
}

namespace {
// Task 0x00485F00 (block 0x94): a shell casing
struct CasingTask : WTask {
    float pos[3] = {};   // +0x34
    float vel[3] = {};   // +0x40
    float gravity = 0;   // +0x50
    int spin = 0;        // +0x58
    int yaw = 0;         // +0x5C
    float floorY = 0;    // +0x68
};
}  // namespace

// 0x00485F00: falls (+0x50 per frame) until below the actor's height, then removed; drawn spinning (model 0x1AFE)
static void TaskCasing(Task& t) {
    World::Impl& w = *CurrentWorld();
    CasingTask& c = t.As<CasingTask>();
    if (c.floorY < c.pos[1]) {
        const float vy = c.gravity + c.vel[1];
        c.vel[1] = vy;
        c.pos[0] = c.vel[0] + c.pos[0];
        c.pos[1] = vy + c.pos[1];
        c.pos[2] = c.vel[2] + c.pos[2];
    } else {
        w.ts.KillCurrent();  // Task_Kill: still drawn this frame
    }
    Mat4 q = Mat4::Identity();
    mx::Translate(q, c.pos[0], c.pos[1], c.pos[2]);
    mx::RotY(q, c.yaw);
    c.spin += 0x1000;
    mx::RotX(q, c.spin);
    w.Draw(0x1AFE, q);
}

// 0x00485DE0: casing task at node 5 (camera matrix x node 5 model-view: its world position), sideways velocity
// RotY(yaw) (0.1, 0, 0.1), upward rand() % 5 * 0x004D5464 + 0x004E30F8, gravity 0xBCDF0123, floor = actor y
void World::Impl::SpawnCasing(EnemyTask& e, const Mat4& node) {
    CasingTask* c = NewTask<CasingTask>(TaskCasing, 0x00485F00);
    const Mat4 wm = node * cam.inverse;  // node callbacks get the model-view matrix
    c->pos[0] = wm.m[12];
    c->pos[1] = wm.m[13];
    c->pos[2] = wm.m[14];
    c->spin = 0;
    c->yaw = e.rot[1];
    Mat4 r = Mat4::Identity();
    mx::RotY(r, e.rot[1]);
    const float v[3] = { Bits32(0x3DCCCCCD), 0.0f, Bits32(0x3DCCCCCD) };
    float o[3];
    mx::Transform(r, v, o);
    c->vel[0] = o[0];
    const int rnd = Rand(0x00485EB8);
    c->gravity = Bits32(0xBCDF0123);
    c->vel[1] = static_cast<float>(rnd % 5) * F32(exe, 0x004D5464) + F32(exe, 0x004E30F8);
    c->vel[2] = o[2];
    c->floorY = e.pos[1];
}

// 0x004840D0 (game type 0)
bool World::Impl::SpawnActor25(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    auto d = std::make_unique<Actor25Task>();
    d->fn = 0x004842A0;
    d->record = r;
    d->flags34 = r.flags | 1;  // 0x00408970
    memcpy(d->pos, r.pos, 12);
    memcpy(d->rot, r.rot, 12);
    d->hp = d->hpMax = static_cast<int16_t>(r.state);
    d->slot120 = -1;
    const uint32_t script = static_cast<uint32_t>(r.ParamS32(12));
    d->anim.kind = r.ParamS8(0);
    if (runner->cfg.gameType == 1 && modes && (d->anim.kind == 0x39 || d->anim.kind == 0x3A)) {
        // 0x00484116: in game type 1 the agents' skeletons 0x39 / 0x3A become P1's / P2's item character (0x009A2242 +
        // p * 0x14: c <= 7 skeleton 0x39 + c, 8 0x21, 9 0x34)
        const int c = modes->pitems[d->anim.kind - 0x39].character;
        if (c <= 7) d->anim.kind = static_cast<int16_t>(0x39 + c);
        else if (c == 8) d->anim.kind = 0x21;
        else if (c == 9) d->anim.kind = 0x34;
    }
    d->anim.motion[0] = EvS16(mem, script + 4);
    EnemyInitPose(*d);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
    if (EvS16(mem, script + 2) == 2) d->anim.drawFlags |= 4;
    d->anim.rotOrder = 1;
    d->nodeCb = 0x00485260;
    d->parts = static_cast<uint32_t>(r.ParamS32(8));
    for (uint32_t a = d->parts; a; a += 2) {  // 0x00412440
        const int16_t id = EvS16(mem, a);
        if (id < 0) break;
        const uint32_t def = exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id));
        if (id < 0x24) d->anim.nodes[static_cast<int>(exe.U32(def)) & 31].model = static_cast<int32_t>(exe.U32(def + 4));
        if (id > 0x11 && residency) resQueue.push_back({ 0x50, exe.U32(def + 4) });  // 0x0041D5D0
    }
    const int c = EvS16(mem, script + 6);
    d->anim.counter[0] = c == -1 ? Rand(0x004841DF) % 10 : c;
    float rad = 0;
    exe.Read(0x004C4D28 + static_cast<uint32_t>(d->anim.kind) * 4, &rad, 4);
    d->radius = rad;
    d->mouth = (d->anim.kind >= 0x39 && d->anim.kind <= 0x3B) ? 1 : 0;
    d->frozen = EvS16(mem, script + 8) == 1 ? 1 : 0;
    d->script = script + 8;
    t.data = std::move(d);
    t.fn = TaskActor25;
    TaskActor25(t);
    // 0x004840D0 sets the task function to 0x004842A0 after the first run: a script that ends (-1) in that run runs
    // 0x004842A0 once more next frame (stage-2 capture 28932)
    if (t.fn == TaskActor25Done) {
        t.As<Actor25Task>().fn = 0x004842A0;
        t.fn = TaskActor25;
    }
    return true;
}

}  // namespace game
