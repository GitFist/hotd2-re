// game::World - objects of the alternate routes the auto-play captures never visit (notes/agents/routes.md).
// Stage 1, no-rescue route (scene 1 -> 10 -> 3): actor type 0x11 (the frogs of scene 3), komono kind 6 (looping model
// cycler) and komono kind 0 (the stacked barrel groups 0x00593CF0).
// CONFIRMED = code read + frame-exact against build\scene_capture_norescue_st1.txt; CODE = read and ported, not exercised.

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

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// s16 at an event-memory address
int16_t MemS16(const World::Impl& w, uint32_t addr) {
    uint32_t v = 0;
    if (!w.mem.Read(addr & ~3u, &v)) return -1;
    return static_cast<int16_t>((addr & 2) ? (v >> 16) : (v & 0xFFFF));
}

// fild a; fmul qword 0x004C4370 (24-bit precision: the angle rounded to float)
double AngleRad(int a) { return static_cast<double>(static_cast<float>(static_cast<double>(a) * mx::kToRad)); }

// fpatan(y, x) * qword k (rounded to float by the 24-bit precision), __ftol, movsx (16 bits)
int AtanAngle(double y, double x, double k) {
    return static_cast<int16_t>(static_cast<int>(static_cast<float>(std::atan2(y, x) * k)));
}

// Prop_ScoreShot 0x004650F0(flags, score): the shooter (rand when both gun bits), +10 when score, a hit
int PropShooter(World::Impl& w, uint32_t flags, bool score) {
    int p;
    if ((flags & 2) && (flags & 4)) p = w.Rand(0x00465106) & 1;
    else p = (flags & 2) ? 0 : 1;
    const bool train = w.runner && w.runner->cfg.gameType == 2;  // 0x009CA08C == 2: no score, hits always counted
    if (score && !train) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0 || train) ++w.player[p].hits;
    return p;
}

}  // namespace

// =====================================================================================================================
// Actor type 0x11 (ctor 0x0043A080, task 0x0043A1E0): a frog (skeleton P s16 0) that hops toward the players along a
// command list (P s16 5..), leaps at a player when near, is killed by one shot. Control block 0x38 bytes at +0x1310.
// =====================================================================================================================

namespace {

struct Actor11Ctl {
    uint32_t flags = 0;       // +0x00: 1 state done, 2 throat pulse running, 4 pulse at its end, 8 no gravity
    int8_t state = 0;         // +0x04 (0x00592660[state])
    int8_t sub = 0;           // +0x05
    int16_t throat = 0;       // +0x08 throat frame (node 2 model 0xB94 + it)
    int16_t throatCount = 0;  // +0x0A
    uint32_t cmd = 0;         // +0x0C next command (event memory, s16 list ending with -1)
    int16_t edge = 0;         // +0x10 screen-edge angle (P s16 4, or -0x200 + atan(320 / focal))
    float dist = 0;           // +0x14 distance to the players' point (x / z)
    int32_t a18 = 0, a1C = 0, a20 = 0;  // +0x18 / +0x1C / +0x20 (camera / tick / motion, turn, timers)
    float p28[3] = {};        // +0x28 leap target (state 6)
};

struct Actor11Task : EnemyTask {
    Actor11Ctl c;
};

void Blend(World::Impl& w, EnemyTask& a, int motion, int start = 0) { w.AnimBlendTo(a, motion, start, 2); }  // 0x004119A0

// Shot_PlayerOrder 0x004093C0 (the order is not used here; the rand call is)
void ShotPlayerOrder(World::Impl& w) {
    w.shotOrder[0] = w.shotOrder[1] = -1;
    if (w.playersInGame == 0) w.shotOrder[0] = 0;
    else if (w.playersInGame == 1) w.shotOrder[0] = 1;
    else if (w.playersInGame == 2) {
        const int r = w.Rand(0x004093E2);
        w.shotOrder[0] = (r & 1) ? 0 : 1;
        w.shotOrder[1] = (r & 1) ? 1 : 0;
    }
}

// The actor's angles undone on (pos - players' point): Identity, RotY(-yaw), RotZ(-rz), RotX(-rx), 0x004A8A80
void LocalToPlayers(World::Impl& w, const EnemyTask& a, const float from[3], float out[3]) {
    Mat4 m = Mat4::Identity();
    mx::RotY(m, -a.rot[1]);
    mx::RotZ(m, -a.rot[2]);
    mx::RotX(m, -a.rot[0]);
    const float d[3] = { a.pos[0] - from[0], a.pos[1] - from[1], a.pos[2] - from[2] };
    mx::Transform(m, d, out);
}

// After a turn motion (0x142 / 0x143) the yaw moves by +-0x2000 and node 1 is counter-rotated (0x004019E0)
void FinishTurn(EnemyTask& a, Actor11Ctl& c) {
    const int d = a.anim.motion[0] == 0x142 ? 0x2000 : -0x2000;
    a.rot[1] += d;
    c.a18 -= d;
    EnemyNode& n0 = a.anim.nodes[0];
    EnemyNode& n1 = a.anim.nodes[1];
    Mat4 m = Mat4::Identity();
    mx::RotX(m, -n0.cur[0]);
    mx::RotY(m, -n0.cur[1]);
    mx::RotZ(m, -n0.cur[2]);
    mx::RotY(m, -d);
    mx::RotZ(m, n0.cur[2]);
    mx::RotY(m, n0.cur[1]);
    mx::RotX(m, n0.cur[0]);
    mx::RotZ(m, n1.cur[2]);
    mx::RotY(m, n1.cur[1]);
    mx::RotX(m, n1.cur[0]);
    mx::MatrixToAngles(m, &n1.cur[0], &n1.cur[1], &n1.cur[2]);
}

// 0x0043A670: command list, first state
void Actor11Init(World::Impl& w, Actor11Task& a, uint32_t params) {
    Actor11Ctl& c = a.c;
    c.state = 0;
    c.a18 = MemS16(w, params + 4);
    c.a1C = MemS16(w, params + 6);
    c.a20 = 0x141;
    c.sub = 0;
    const int16_t e = MemS16(w, params + 8);
    if (e != 0) c.edge = e;
    else c.edge = static_cast<int16_t>(-0x200 - AtanAngle(320.0, w.focal, F64(w.exe, 0x0055CB20)));
    c.cmd = params + 10;
}

// 0x0043A2E0: shot -> dead (state 8)
void Actor11Shot(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    if (!(a.flags34 & 8)) return;
    a.hp = 0;
    if (a.flags34 & 0x4000000) return;
    a.flags34 |= 0x4000000;
    c.state = 8;
    c.sub = 0;
    c.flags &= ~1u;
    a.flags34 |= 0x8000;
    ShotPlayerOrder(w);  // 0x004093C0
    a.anim.nodes[1].model = 0xB91;
    a.anim.nodes[2].model = 0;
    a.anim.nodes[2].radius = 0;
    w.SpawnNodeBlood(a, 2);  // 0x00407200
    int p;
    switch (a.flags34 & 6) {
    case 2: p = 0; break;
    case 4: p = 1; break;
    default: p = w.Rand(0x0043A380) & 1; break;
    }
    w.AddScore(p, 0x50);
    ++w.player[p].hits;
    w.Sound(0x716A9);
}

// 0x0043A710: next state from the command list, then by distance
void Actor11Next(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    if (!(c.flags & 1)) return;
    c.flags &= ~1u;
    const int16_t cmd = MemS16(w, c.cmd);
    if (cmd != -1) {
        c.state = static_cast<int8_t>(cmd);
        c.cmd += 2;
        if (c.state == 0) {  // wait: camera, tick, motion
            c.a18 = MemS16(w, c.cmd);
            c.cmd += 2;
            c.a1C = MemS16(w, c.cmd);
            c.cmd += 2;
            c.a20 = MemS16(w, c.cmd);
            c.cmd += 2;
        } else if (c.state == 3) {  // turn by an absolute yaw
            c.a18 = static_cast<uint16_t>(MemS16(w, c.cmd) - static_cast<int16_t>(a.rot[1]));
            c.cmd += 2;
            if (0x7FFF < c.a18) c.a18 -= 0x10000;
        }
        c.sub = 0;
        return;
    }
    if (c.dist < 50.0f) {  // 0x0055D2AC: leap at a player
        c.state = 6;
        if (!w.TypingAttackReady()) {
            c.state = 7;  // TOTD 0x00427C55: wait instead of taking a target
        } else if (w.players == 1) {
            a.target = static_cast<int8_t>(w.playersInGame);
        } else if (w.players == 2) {
            if (w.targetTaken[0] != 0) a.target = 1;
            else if (w.targetTaken[1] != 0) a.target = 0;
            else a.target = static_cast<int8_t>(w.Rand(0x0043A86A) & 1);
        } else {
            c.state = 7;
        }
        if (c.state == 6) {
            const int t = a.target;
            if (t >= 0 && t < 2 && w.targetTaken[t] == 0) {
                w.targetTaken[t] = 1;
            } else {
                c.state = 7;
                a.target = -1;
            }
        }
        c.sub = 0;
        return;
    }
    switch (c.state) {
    case 0: case 4: case 7: c.state = 1; break;
    case 1: case 3: {
        const int r = w.Rand(0x0043A8EF) % 6;
        c.state = r == 0 ? 7 : r <= 2 ? 4 : 1;
        break;
    }
    default: break;
    }
    if (c.state == 1) {
        if (c.dist < 53.0f) c.state = 5;        // 0x0055E1E4
        else if (c.dist < 76.0f) c.state = 2;   // 0x0055E1E0
    }
    c.sub = 0;
}

// State 0 (0x0043A9A0): motion +0x20, wait for camera +0x18 at tick >= +0x1C
void Actor11Wait(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    if (c.sub == 0) {
        if (a.anim.motion[0] != c.a20) Blend(w, a, c.a20);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    if (w.runner->camera.camId == c.a18 && c.a1C <= w.runner->camera.tick) c.flags |= 1;
}

// States 1 / 2 / 3 (0x0043AA10): turn (0x142 / 0x143), hop (0x140) toward / along the screen
void Actor11Hop(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    const ExeImage& exe = w.exe;
    const double K = F64(exe, 0x004C4378), KN = F64(exe, 0x0055CB20);
    switch (c.sub) {
    case 0: {
        if (c.state != 3) {
            const float nvz = -a.viewPos[2];
            float o[3];
            LocalToPlayers(w, a, w.playerPos, o);
            int edi, esi;
            const int ang = AtanAngle(o[0], o[2], K);
            if (c.state == 1) {
                if (nvz * 0.25f < a.viewPos[0]) {
                    edi = 0x1800;
                    esi = ang - 0x1800;
                } else if (a.viewPos[0] < nvz * -0.25f) {
                    edi = 0x1800;
                    esi = ang;
                } else {
                    edi = 0x3000;
                    esi = ang - 0x1800;
                }
            } else if (0.0f < a.viewPos[0]) {
                edi = 0x1800;
                esi = ang - 0x1800;
            } else {
                edi = 0x1800;
                esi = ang;
            }
            const int cy = w.runner->camera.ctx[2].yaw;  // 0x009A6418
            const int a1 = static_cast<int16_t>(c.edge + cy - 0x8000);
            const int a2 = static_cast<int16_t>(cy - c.edge - 0x8000);
            const float dx = a.pos[0] - w.playerPos[0], dz = a.pos[2] - w.playerPos[2];
            auto side = [&](int an) {
                const double r = AngleRad(an);
                float v = static_cast<float>(std::cos(r) * dx) - static_cast<float>(std::sin(r) * dz);
                if (v < 0.0f) v = v * -1.0f;
                return v;
            };
            const float d1 = side(a1), d2 = side(a2);
            const float kk = F32(exe, 0x0055E1F0);
            const double hf = std::atan2(320.0, static_cast<double>(w.focal));
            if (d1 < 26.0f) {  // 0x0055E1F4; 0x004AD0B0 = _CIacos
                const int t1 = static_cast<int>(static_cast<float>(std::acos(static_cast<double>(d1 * kk)) * KN));
                const int t2 = static_cast<int>(static_cast<float>(hf * K));
                const int v = static_cast<int16_t>(t2 - t1 - a.rot[1] + cy + 0x4000);
                if (esi < v) {
                    esi = v;
                    if (edi <= 0) edi = 0;
                }
            }
            if (d2 < 26.0f) {
                const int t1 = static_cast<int>(static_cast<float>(std::acos(static_cast<double>(d2 * kk)) * K));
                const int t2 = static_cast<int>(static_cast<float>(hf * KN));
                const int v = static_cast<int16_t>(t2 - t1 - a.rot[1] + cy - 0x4000);
                if (v < esi + edi) {
                    if (v < esi) {
                        esi = v;
                        edi = 0;
                    } else {
                        edi = v - esi;
                    }
                }
            }
            if (0x2000 < esi) {
                c.a18 = esi;
            } else {
                const int e = esi + edi;
                if (0x2000 < e) c.a18 = w.Rand(0x0043AD0E) % (0x2001 - esi) + esi;
                else if (e < -0x2000) c.a18 = e;
                else if (esi < -0x2000) c.a18 = w.Rand(0x0043AD44) % (e + 0x2001) - 0x2000;
                else c.a18 = w.Rand(0x0043AD62) % (edi + 1) + esi;
            }
        }
        if (0x1555 < c.a18) {
            if (a.anim.motion[0] != 0x142) Blend(w, a, 0x142);
            c.sub = 1;
        } else if (c.a18 < -0x1555) {
            if (a.anim.motion[0] != 0x143) Blend(w, a, 0x143);
            c.sub = 1;
        } else {
            if (a.anim.motion[0] != 0x140) Blend(w, a, 0x140);
            c.sub = 2;
        }
        return;
    }
    case 1:
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0])) return;
        FinishTurn(a, c);
        if (0x2000 < std::abs(c.a18)) return;
        if (a.anim.motion[0] != 0x140) Blend(w, a, 0x140);
        c.sub = 2;
        return;
    case 2: {
        if (a.anim.tick[0] != 0x12) return;
        const double r = AngleRad(a.rot[1] + c.a18);
        const float s = static_cast<float>(std::sin(r)), co = static_cast<float>(std::cos(r));
        float sp = -1.0f;  // 0x004C4C64
        if (c.state == 2) {
            const float z = (a.pos[2] - co * 26.0f) - w.playerPos[2];
            const float x = (a.pos[0] - s * 26.0f) - w.playerPos[0];
            const float d = static_cast<float>(std::sqrt(static_cast<double>(x * x + z * z)));
            if (d < 40.0f) sp = (c.dist - 40.0f) * F32(exe, 0x0055E1E8);  // 0x004ECB7C
        } else if (c.state != 3) {
            sp = static_cast<float>(10 - w.Rand(0x0043AF9D) % 5) * F32(exe, 0x0055E1EC);
        }
        a.vel[0] = sp * s;
        a.vel[2] = sp * co;
        ++c.sub;
    }
        // fall through
    case 3:
        if (c.a18 != 0) {
            a.rot[1] += c.a18 / 2;
            c.a18 = c.a18 / 2;
        }
        if (a.anim.tick[0] == 0x2C) {
            a.vel[0] = 0;
            a.vel[2] = 0;
            ++c.sub;
        }
        return;
    case 4:
        if (w.MotionTicks(a.anim.motion[0]) <= a.anim.tick[0]) c.flags |= 1;
        return;
    default:
        return;
    }
}

// States 4 / 5 (0x0043B0E0): look around (0x144); state 5 turns toward the players
void Actor11Look(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    if (c.sub == 0) {
        if (c.state == 5) {
            float o[3];
            LocalToPlayers(w, a, w.playerPos, o);
            c.a18 = AtanAngle(o[0], o[2], F64(w.exe, 0x004C4378));
        } else {
            c.a18 = 0;
        }
        if (a.anim.motion[0] != 0x144) Blend(w, a, 0x144);
        ++c.sub;
        return;
    }
    if (c.sub != 1) return;
    if (c.a18 != 0) {
        if (0x40 < c.a18) {
            a.rot[1] += 0x40;
            c.a18 -= 0x40;
        } else if (c.a18 < -0x40) {
            a.rot[1] -= 0x40;
            c.a18 += 0x40;
        } else {
            a.rot[1] += c.a18;
            c.a18 = 0;
        }
    }
    if (w.MotionTicks(a.anim.motion[0]) <= a.anim.tick[0]) c.flags |= 1;
}

// State 6 (0x0043B270): leap at the target player, hit, fall away and leave
void Actor11Leap(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    const ExeImage& exe = w.exe;
    switch (c.sub) {
    case 0: {
        if (w.route == 1) c.p28[0] = 0;
        else c.p28[0] = static_cast<float>(a.target * 2 - 1) * 3.0f;
        c.p28[1] = static_cast<float>(w.Rand(0x0043B2FC) % 3 - 1) * 3.0f - 3.0f;
        c.p28[2] = -12.5f;
        float o[3];
        mx::Transform(w.cam.inverse, c.p28, o);
        memcpy(c.p28, o, 12);
        LocalToPlayers(w, a, c.p28, o);
        c.a18 = AtanAngle(o[0], o[2], F64(exe, 0x004C4378));
        {
            const double r = AngleRad(a.rot[1] + c.a18);
            c.p28[0] = static_cast<float>(std::sin(r) * 3.0) + c.p28[0];
            c.p28[2] = static_cast<float>(std::cos(r) * 3.0) + c.p28[2];
        }
        if (0x1000 < c.a18) {
            if (a.anim.motion[0] != 0x142) Blend(w, a, 0x142);
            c.sub = 1;
        } else if (c.a18 < -0x1000) {
            if (a.anim.motion[0] != 0x143) Blend(w, a, 0x143);
            c.sub = 1;
        } else {
            if (a.anim.motion[0] != 0x13E) Blend(w, a, 0x13E);
            c.sub = 2;
        }
        return;
    }
    case 1:
        if (a.anim.tick[0] < w.MotionTicks(a.anim.motion[0])) return;
        FinishTurn(a, c);
        if (0x1000 < std::abs(c.a18)) return;
        if (a.anim.motion[0] != 0x13E) Blend(w, a, 0x13E);
        c.sub = 2;
        return;
    case 2: {
        if (a.anim.tick[0] != 0x1E) return;
        const float k = F32(exe, 0x0055E1FC);
        a.vel[0] = (c.p28[0] - a.pos[0]) * k;
        a.vel[2] = (c.p28[2] - a.pos[2]) * k;
        a.vel[1] = (c.p28[1] - a.pos[1]) * k - a.acc[1] * 14.5f;  // 0x0055E1F8
        c.flags |= 2;
        ++c.sub;
    }
        // fall through
    case 3:
        if (c.a18 != 0) {
            a.rot[1] += c.a18 / 2;
            c.a18 = c.a18 / 2;
        }
        if (a.anim.tick[0] == 0x3C) {
            a.vel[2] = 0;
            a.vel[1] = 0;
            a.vel[0] = 0;
            c.flags |= 8;
            if (a.anim.motion[0] != 0x141) Blend(w, a, 0x141);
            w.PlayerTakeHit(a.target, 1, 9);  // 0x00415300
            ++c.sub;
        }
        return;
    case 4:
        if (c.flags & 4) {
            c.flags &= ~0xEu;
            if (a.anim.motion[0] != 0x13E) Blend(w, a, 0x13E, 0x3D);
            ++c.sub;
        }
        return;
    case 5:
        if (w.ground < a.pos[1]) return;
        if (a.target >= 0 && a.target < 2) w.targetTaken[a.target] = 0;
        a.target = -1;
        --w.liveEnemies;
        --w.attackers;
        w.EnemyDestroy(a);  // Actor_Destroy 0x00409CC0 (+ the actor slot 0x009C88C0[+0x3C])
        return;
    default:
        return;
    }
}

// State 7 (0x0043B880): sit (0x141) for 64..89 frames, one throat pulse with a croak
void Actor11Sit(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    if (c.sub == 0) {
        if (a.anim.motion[0] != 0x141) Blend(w, a, 0x141);
        c.a18 = w.Rand(0x0043B8B8) % 0x1A + 0x40;
        c.a1C = c.a18 - w.Rand(0x0043B8D0) % 0x1A;
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    if (!(c.flags & 2)) {
        if (c.a18 == c.a1C) {
            c.flags |= 2;
            if (a.anim.motion[0] != 0x141) Blend(w, a, 0x141);
            w.Sound(0x3716A9);
        }
    } else if (c.flags & 4) {
        c.flags &= ~6u;
        if (a.anim.motion[0] != 0x141) Blend(w, a, 0x141);
    }
    if (c.a18-- == 0) c.flags |= 1;
}

// State 8 (0x0043B990): knocked back away from the camera, bounces, blood pool, sinks, removed after 180 frames
void Actor11Die(World::Impl& w, Actor11Task& a) {
    Actor11Ctl& c = a.c;
    const ExeImage& exe = w.exe;
    const float g = w.ground;
    switch (c.sub) {
    case 0: {
        a.anim.motion[0] = 0x13F;  // (0x00412F50: motion load request)
        Blend(w, a, 0x13F);
        uint32_t f = a.flags34 & ~0x4000u;
        a.flags34 = f;
        if (!(f & 0x800000) || w.liveEnemies != 1) a.flags34 = f | 0x10000;
        --w.liveEnemies;
        Mat4 m = w.cam.inverse;  // 0x004A9ED0: the camera matrix's rotation, translation 0
        m.m[12] = m.m[13] = m.m[14] = 0.0f;
        const float back[3] = { 0.0f, 0.0f, -0.5f };
        mx::Transform(m, back, a.vel);
        c.flags = 0;
        if (a.target != -1 && a.target >= 0 && a.target < 2) w.targetTaken[a.target] = 0;
        ++c.sub;
    }
        // fall through
    case 1:
        if (!(g < a.pos[1])) {
            a.pos[1] = g;
            if (a.vel[1] != 0.0f) {
                a.vel[1] = a.vel[1] * F32(exe, 0x0055D2C4);
                a.vel[0] = a.vel[0] * F32(exe, 0x0055E1D4);
                a.vel[2] = a.vel[2] * F32(exe, 0x0055E1D4);
                if (a.vel[1] < F32(exe, 0x004C4C88)) a.vel[1] = 0;
            } else {
                a.vel[0] = a.vel[0] * F32(exe, 0x004C43A8);
                a.vel[2] = a.vel[2] * F32(exe, 0x004C43A8);
            }
            float vx = a.vel[0];
            if (vx < 0.0f) vx = vx * -1.0f;
            if (vx < F32(exe, 0x004C4C88)) a.vel[0] = 0;
            float vz = a.vel[2];
            if (vz < 0.0f) vz = vz * -1.0f;
            if (vz < F32(exe, 0x004C4C88)) a.vel[2] = 0;
            if (a.vel[0] == 0.0f && a.vel[1] == 0.0f && a.vel[2] == 0.0f &&
                a.anim.tick[0] == w.MotionTicks(a.anim.motion[0])) {
                w.SpawnBloodPool(a);  // 0x00407DA0
                a.vel[1] = Bits(0xBD0F5C29);
                c.a18 = 0xB4;
                ++c.sub;
            }
        }
        if (a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) - 1) a.flags34 |= 0x4000;
        return;
    case 2:
        if (c.a18-- != 0) return;
        --w.attackers;
        w.EnemyDestroy(a);  // Actor_Destroy 0x00409CC0 (+ the actor slot)
        return;
    default:
        return;
    }
}

// 0x0043A1E0
void TaskActor11(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor11Task& a = t.As<Actor11Task>();
    Actor11Ctl& c = a.c;
    {
        const float dz = a.pos[2] - w.playerPos[2], dx = a.pos[0] - w.playerPos[0];
        c.dist = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dz * dz)));
    }
    // node 1's world point from its last model-view (0x007DCBB8): camera matrix, x node matrix (0x004A92A0)
    float prev[3];
    {
        Mat4 q = w.cam.inverse;
        mx::MulLeft(q, a.anim.nodes[1].mv);
        mx::GetTranslation(q, prev);
    }
    Actor11Shot(w, a);  // 0x0043A2E0
    Actor11Next(w, a);  // 0x0043A710
    switch (c.state) {  // 0x00592660
    case 0: Actor11Wait(w, a); break;
    case 1: case 2: case 3: Actor11Hop(w, a); break;
    case 4: case 5: Actor11Look(w, a); break;
    case 6: Actor11Leap(w, a); break;
    case 7: Actor11Sit(w, a); break;
    case 8: Actor11Die(w, a); break;
    default: break;  // 9: 0x0041EBB0
    }
    // 0x0043A3D0: move; gravity above the ground unless flag 8
    {
        const float g = w.ground;
        a.pos[0] = a.vel[0] + a.pos[0];
        a.pos[2] = a.vel[2] + a.pos[2];
        a.pos[1] = a.vel[1] + a.pos[1];
        if (g < a.pos[1] && !(c.flags & 8)) a.vel[1] = a.acc[1] + a.vel[1];
    }
    // 0x0043A440: throat pulse, skeleton + shadow (0x00411090), node 2's throat model, animation counter
    if (c.flags & 2) {
        ++c.throatCount;
        const int v = c.throatCount % 0x3B;
        if (v == 0x3A) {
            c.flags |= 4;
            c.throat = static_cast<int16_t>(0x3A - v);
        } else {
            c.flags &= ~4u;
            c.throat = static_cast<int16_t>(v < 0x1E ? v : 0x3A - v);
        }
    }
    w.AnimDraw(a);  // light 0x0041DC70 / 0x0041DCC0: renderer
    w.EnemyShadow(a);
    if (!(a.flags34 & 0x4000000)) a.anim.nodes[2].model = c.throat + 0xB94;
    if (!(a.flags34 & 0x4000)) ++a.anim.counter[0];
    // 0x00409B70(1.0): view position, hit target, lock-on candidate
    mx::Transform(w.cam.view, a.body, a.viewPos);
    w.RegisterTarget(&a);
    a.body[1] = 1.0f + a.body[1];
    w.SyncLock(a);
    w.lockOn.Register(a.lock, w.playerPos);
    // 0x0043A500: collision sphere at node 1, pushed by the other targets
    float s[3];
    {
        Mat4 q = w.cam.inverse;
        mx::MulLeft(q, a.anim.nodes[1].mv);
        mx::GetTranslation(q, s);
    }
    if (c.state != 6) {
        memcpy(a.sphere, s, 12);
        if (w.SpherePush(a)) {  // 0x00405B10(s, +0x128)
            const float dz = s[2] - prev[2], dx = s[0] - prev[0];
            const float d = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dz * dz)));
            float f;
            if (d < 0.1f) {
                f = w.cac4c * 0.05f;
            } else {
                f = w.cac4c;
                if (!(0.6f < d)) f = f * d;
                f = f * 0.3f;
            }
            a.pos[0] = w.cac64[0] * f + a.pos[0];
            a.pos[2] = w.cac64[2] * f + a.pos[2];
            s[0] = w.cac64[0] * f + s[0];
            s[2] = w.cac64[2] * f + s[2];
        }
    }
    memcpy(a.sphere, s, 12);
}

// 0x0043A080 (the spawn task becomes the actor; 0x0043A1E0 from the next frame)
void SpawnActor11(World::Impl& w, Task& t, const SpawnRecord& r) {
    auto d = std::make_unique<Actor11Task>();
    Actor11Task& a = *d;
    a.fn = 0x0043A1E0;
    w.SetTypingSpawnOrigin(a, r);
    a.flags34 = r.flags | 1;  // 0x00408BC0 -> 0x00408970
    memcpy(a.pos, r.pos, 12);
    memcpy(a.rot, r.rot, 12);
    a.hp = a.hpMax = static_cast<int16_t>(r.state);
    a.record = r;
    a.lock = &w.lockTargets[w.nextLockTarget++];
    const uint32_t params = r.addr + 0x24;  // +0x130C
    a.c.flags = 0;
    Actor11Init(w, a, params);
    a.c.throatCount = 0;
    a.c.throat = 0;
    if (w.ground != a.pos[1]) a.pos[1] = w.ground;  // 0x0043A990
    a.acc[1] = Bits(0xBCDF0123);
    a.anim.kind = MemS16(w, params);
    a.anim.motion[0] = MemS16(w, params + 2);
    a.anim.counter[0] = 0;
    a.anim.tick[0] = 0;
    w.EnemyInitPose(a);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
    a.anim.drawFlags |= 4;
    a.anim.rotOrder = 1;
    a.nodeCb = 0x00411050;  // plain node draw; post-root 0x00420810 (ret)
    memcpy(a.sphere, a.pos, 12);
    a.radius = F32(w.exe, 0x004C4D28 + 4 * static_cast<uint32_t>(a.anim.kind));
    a.radius128 = 3.0f;
    ++w.attackers;
    ++w.liveEnemies;
    a.target = -1;
    t.data = std::move(d);
    t.fn = TaskActor11;
}

// =====================================================================================================================
// Komono kind 6 (generic ctor 0x00461CF0 case 6 -> task 0x004668A0): a looping model sequence 0x1032..0x1062 (or
// 0x10C4..0x10CC) at the record, life 1 area change
// =====================================================================================================================

struct Komono6Task : ActorTask {
    float pos[3] = {};          // +0x19C
    int rot[3] = {};            // +0x1CC / +0x1D0 / +0x1D4
    int16_t model = 0x1032;     // +0x28C
    int16_t life = 1;           // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
};

// 0x004668A0
void TaskKomono6(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono6Task& k = t.As<Komono6Task>();
    // Prop_CheckLifetime 0x00466640
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
            w.ts.KillCurrent();
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (k.model == 0x1063) k.model = 0x1032;
    if (k.model == 0x10CD) k.model = 0x10C4;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    w.Draw(k.model++, m);
}

// Komono kind 5 (generic ctor 0x00461CF0, no kind case -> task 0x00466820): the record-state model at the record,
// removed (Task_Kill) once event flag 0x13 is set (stage 2 scene 14)
struct Komono5Task : ActorTask {
    float pos[3] = {};      // +0x19C
    int rot[3] = {};        // +0x1CC / +0x1D0 / +0x1D4
    int16_t model = 0;      // +0x28C
};

// 0x00466820
void TaskKomono5(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono5Task& k = t.As<Komono5Task>();
    if (w.runner->flags[0x13] != 0) w.ts.KillCurrent();  // 0x009C7213, Task_Kill 0x004A7040
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m);
}

// Komono kinds 27 (generic ctor case 0x1B -> task 0x00469E60: prop skeleton 7 / motion 0x1D5) and 28 (case 0x1C ->
// 0x00469F50: skeleton 10 / motion 0x1C9), stage 2 scenes 22 / 23. Both draw through Mdl_SelectAlt (0x0040DFA0).
struct Komono27Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int rot64[3] = {};               // +0x64 / +0x68 / +0x6C
    const int* Rot64() const override { return rot64; }
    int speed = 0;                   // +0x1D8
    PropAnim anim;                   // +0x324
};

// Komono lifetime 0x00466640 (Actor_Destroy when the area changed more than +0x11C times)
void KomonoLife(World::Impl& w, Komono27Task& k) {
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
}

void DrawAltPose(World::Impl& w, PropAnim& anim, const Mat4& m) {  // 0x0040DFA0
    const Motion* mo = w.PropMotion(anim.motion);
    if (!mo) return;
    for (const PropDraw& d : PropPose(w.PropSkel(anim.skeleton), *mo, anim, m, -1, true)) {
        if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
        w.Draw(d.model, d.world, 8, -1.0f, d.alt);
    }
}

// 0x004650F0(flags, score): the shooter (rand when both gun bits), +10 when score, a hit
int PropShotBy(World::Impl& w, uint32_t f, bool score) {
    int p;
    if ((f & 2) && (f & 4)) p = w.Rand(0x00465106) & 1;
    else p = (f & 2) ? 0 : 1;
    if (score) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

// 0x00469AE0: komono kind 25 (generic ctor case 0x19: route 0x009C88A4 = 0, radius 12, an attacker; prop skeleton 10 /
// motion 0x1C9): shot in stage-2 scene 23 (0x17) within 0xFE frames -> route 1 (scene 26), +0x2F16A9, it plays its
// motion to ticks - 2 and blinks for 30 frames
void TaskKomono25(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono27Task& k = t.As<Komono27Task>();
    KomonoLife(w, k);
    if (++k.speed == 0xFE && !(k.flags34 & 0x40000000)) {  // (+0x2A0 frame counter)
        --w.attackers;
        k.flags34 |= 0x40000000;
    }
    const uint32_t f = k.flags34;
    if ((f & 8) && !(f & 0x40000000) && w.runner->scene == 0x17) {
        PropShotBy(w, f, false);
        w.runner->camera.routeBranch = 1;  // 0x009C88A4
        w.Sound(0x2F16A9);                 // (+ 0x00420810(2, 0x14): no-op)
        --w.attackers;
        k.flags34 |= 0x44000000;
    }
    const int last = w.MotionTicks(k.anim.motion) - 2;
    if (!(k.flags34 & 0x4000000) || last <= k.anim.frame) {
        if (k.anim.frame == last) ++k.rot64[2];  // (+0x2A4 blink counter)
    } else {
        ++k.anim.frame;
    }
    const int b = k.rot64[2];
    if (b < 0x1E && !(b > 0 && (b & 1)) && w.MotionResident(k.anim.motion)) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        DrawAltPose(w, k.anim, m);
    }
    if (!(k.flags34 & 0x4000000)) {
        const float p[3] = { k.pos[0], k.pos[1] + F32(w.exe, 0x004D1D20), k.pos[2] };
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);  // 0x00405160
    }
}

// 0x00467C80: komono kind 11 (generic ctor case 0xB, radius 2): a bird / bat (models 0x1CF / 0x1D0 by the frame
// counter's parity) circling the record (random phase steps); shot (+10): it falls to the ground 0x009C8E58
struct Komono11Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    float vy = 0;                    // +0x1C4
    int ax = 0, yaw = 0;             // +0x1CC / +0x1D0
    int phase = 0;                   // +0x1DC
    float amp[2] = {};               // +0x2C0 / +0x2C4
};

void TaskKomono11(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono11Task& k = t.As<Komono11Task>();
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    const uint32_t f = k.flags34;
    if ((f & 8) && !(f & 0x40000000)) {
        PropShotBy(w, f, true);
        k.flags34 |= 0x40000000;
    }
    const double toRad = 9.587379924285257e-05;  // double 0x004C4370
    auto sinA = [&](int a) { return std::sin(static_cast<double>(static_cast<float>(static_cast<double>(a) * toRad))); };
    if (!(k.flags34 & 0x40000000)) {
        k.yaw += 0x400;
        k.ax += 0x1000;
        k.phase = k.phase + w.Rand(0x00467D22) % 0x501 + 0x900;
        const double s = sinA(k.phase);
        k.amp[0] = static_cast<float>(static_cast<double>(static_cast<float>(s * F32(w.exe, 0x004C43AC))) + F32(w.exe, 0x004C49C0));
        k.amp[1] = static_cast<float>(s * F32(w.exe, 0x004C4CB8));
    } else {
        k.vy = k.vy - F32(w.exe, 0x0055CB10);
        k.pos[1] = k.vy + k.pos[1];
        if (k.pos[1] < w.ground) k.pos[1] = w.ground;
    }
    const double a = static_cast<double>(static_cast<float>(static_cast<double>(k.yaw) * toRad));
    const double c = std::cos(a), s2 = sinA(k.ax), s = std::sin(a);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, static_cast<float>(static_cast<double>(static_cast<float>(s * k.amp[0])) + k.pos[0]),
                  static_cast<float>(static_cast<double>(static_cast<float>(s2 * k.amp[1])) + k.pos[1]),
                  static_cast<float>(static_cast<double>(static_cast<float>(c * k.amp[0])) + k.pos[2]));
    mx::RotY(m, -k.yaw);
    w.Draw(static_cast<int>(w.seedCounter & 1u) + 0x1CF, m);
    mx::Transform(w.cam.view, k.pos, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

// 0x00467F50: komono kind 13 (generic ctor case 0xD, model 0x1A4A): a lamp blinking 0x1A49 / 0x1A4A every 0x28 frames
// that drops (event flag 0x6D: sounds 0x19A9 / 0x3517A9) to y -6 and bounces (z offset 0.4 x -0.925) with its base
// 0x1A43; its light model is drawn with the view matrix (stage 2 scene 21)
struct Komono13Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t phase = 0;                // +0x192
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    float vy = 0, zoff = 0;          // +0x1C4 / +0x1C8
    int16_t model = 0x1A4A;          // +0x28C
    int moved = 0;                   // +0x2A0 (area changed once)
    int toggle = 0;                  // +0x2A4
};

void TaskKomono13(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono13Task& k = t.As<Komono13Task>();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) w.ts.KillCurrent();  // Task_Kill
        k.moved = 1;
        k.area = static_cast<int8_t>(w.runner->variant);
        k.model = 0x1A4A;
    }
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
    if (w.runner->flags[0x6D] == 1 && w.GlobalInt(0x009A2230, 0) != 0) {
        k.phase = 2;
        k.pos[1] = -6.0f;
    }
    if (k.phase == 0) {
        if (k.moved == 0 && w.frameCounter2BAC % 0x28 == 0) {
            k.toggle = 1 - k.toggle;
            k.model = static_cast<int16_t>(k.toggle + 0x1A49);
        }
        if (w.runner->flags[0x6D] == 1) {
            w.Sound(0x19A9);
            w.Sound(0x3517A9);  // (+ 0x00420810(1, 500): no-op)
            k.phase = 1;
            k.model = 0x1A49;
        }
    } else if (k.phase == 1) {
        k.vy = k.vy - F32(w.exe, 0x004E3100);
        k.pos[1] = k.vy + k.pos[1];
        if (k.pos[1] < F32(w.exe, 0x0055CB4C)) {
            k.pos[1] = -6.0f;
            k.phase = 2;
            k.zoff = Bits(0x3ECCCCCD);
            w.Sound(0x1916A9);  // (+ 0x00420810(0, 1): no-op)
        }
    } else if (k.phase == 2) {
        float z = k.zoff * F32(w.exe, 0x0056908C);
        k.zoff = z;
        if (z < 0.0f) z = z * -1.0f;
        if (z < F32(w.exe, 0x004C4C88)) {
            k.zoff = 0.0f;
            k.phase = 3;
        }
    }
    w.Draw(k.model, Mat4::Identity());  // (with the matrix in effect: the view)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.zoff + k.pos[2]);
    w.Draw(0x1A43, m);
}

// 0x0046A030: komono kind 29 (ctor 0x00463270: nine pieces at table 0x00593D38): models 0x1A3A + index (3 and 7 not
// drawn) turning about x (pieces 4 / 5 about y) by 0x80 a frame
void TaskKomono29(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono27Task& k = t.As<Komono27Task>();
    KomonoLife(w, k);
    const int i = k.speed;  // (+0x290 piece index)
    if (i == 4 || i == 5) k.rot[1] -= 0x80;
    else k.rot[0] -= 0x80;
    if (i == 3 || i == 7) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0] + F32(w.exe, 0x005690DC), k.pos[1] - F32(w.exe, 0x0055D2B4), k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(i + 0x1A3A, m);
}

// Komono kinds 42 (ctor 0x004639D0 -> 0x0046CE80: model 0x1823 with the view matrix) and 47 (ctor 0x00463AE0 ->
// 0x0046DD40: model 0x1384 at the record, x 0xC000, scale 0.4, translucent sin(+0x44) * 0.2 + 0.8), both gone in area 2
// (stage 2 scene 35); kind 47 also with event flag 0x11
struct Komono47Task : WTask {
    float pos[3] = {};  // +0x34
    int phase = 0;      // +0x44 (never changed)
};

void TaskKomono42(Task& t) {
    World::Impl& w = *CurrentWorld();
    if (w.runner->variant == 2) w.ts.KillCurrent();
    (void)t;
    w.Draw(0x1823, Mat4::Identity());  // (with the matrix in effect: the view)
}

void TaskKomono47(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono47Task& k = t.As<Komono47Task>();
    if (w.runner->flags[0x11] == 1 || w.runner->variant == 2) w.ts.KillCurrent();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotX(m, 0xC000);
    const float s = Bits(0x3ECCCCCD);
    mx::Scale(m, s, s, s);
    const double sn = std::sin(static_cast<double>(static_cast<float>(static_cast<double>(k.phase) * 9.587379924285257e-05)));
    const float a = static_cast<float>(static_cast<double>(static_cast<float>(sn * F32(w.exe, 0x004D1D24))) + F32(w.exe, 0x004C43A8));
    w.Draw(0x1384, m, 8, a);  // 0x004185A0
}

// 0x0046A360: komono kind 32: a gate (0x197A) with two double doors (0x197B pairs) opening with event flags 0x6B /
// 0x6C and a bar (0x1981) lifted 0x28 frames later; flag 0x37 puts it 15 below the camera (stage 2 scene 17)
struct Komono32Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    int bar = 0, door1 = 0;          // +0x1CC / +0x1D0
    int door2 = 0;                   // +0x1E8
    int counter = 0;                 // +0x2A0
};

void TaskKomono32(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono32Task& k = t.As<Komono32Task>();
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    const uint8_t* fl = w.runner->flags;
    if (fl[0x37] == 1) k.pos[1] = w.cam.pos[1] - F32(w.exe, 0x004C4398);  // 0x009A60C4[ctx]
    if (fl[0x6B] == 1) {
        if (k.door1 < 0x8001) {
            if (k.door1 == 0x4000) w.Sound(0x2216A9);
            k.door1 += 0x200;
        }
        ++k.counter;
    }
    if (fl[0x6C] == 1 && k.door2 < 0xC001) {
        if (k.door2 == 0x8000) w.Sound(0x2216A9);
        k.door2 += 0x200;
    }
    if (0x27 < k.counter && k.bar < 0x1001) {
        if (k.bar == 0) w.Sound(0x2516A9);
        k.bar += 0x200;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    w.Draw(0x197A, m);
    Mat4 d = m;
    mx::Translate(d, Bits(0x4119E76D), Bits(0x3D38BAC7), Bits(0xC1069A6B));
    mx::RotY(d, k.door1);
    w.Draw(0x197B, d);
    mx::Translate(d, -6.5f, 0.0f, 0.0f);
    mx::RotY(d, (-0x4000 - k.door1) * 2);
    w.Draw(0x197B, d);
    d = m;
    mx::Translate(d, Bits(0xC07F3B64), Bits(0x3D38BAC7), Bits(0xC11A212D));
    mx::RotY(d, k.door2);
    w.Draw(0x197B, d);
    mx::Translate(d, -6.5f, 0.0f, 0.0f);
    mx::RotY(d, k.door2 * -2);
    w.Draw(0x197B, d);
    mx::Translate(m, Bits(0xC04DA858), 13.0f, -2.0f);
    mx::RotX(m, k.bar);
    mx::Translate(m, 0.0f, 1.0f, 0.0f);
    w.Draw(0x1981, m);
}

// 0x0046B480: komono kind 36 (generic ctor case 0x24): three flames (models 0x161B + frame counter 0x009A2BAC % 0x18,
// scale (0.2, s * 0.2, 0.2)) at random spots, each shown for 12 frames after its countdown (stage 3 scene 1)
struct Komono36Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float spot[3][3] = {};           // +0x22C (x, y, -) per flame
    int count[3] = {};               // +0x2A0
    float scale[3] = {};             // +0x2C0
};

void TaskKomono36(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono36Task& k = t.As<Komono36Task>();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) w.ts.KillCurrent();  // Task_Kill
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    for (int i = 0; i < 3; ++i) {
        if (0 < --k.count[i]) continue;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.spot[i][0] - F32(w.exe, 0x005690E8), k.spot[i][1] - F32(w.exe, 0x004D1D20), Bits(0xC539AB33));
        const float s = Bits(0x3E4CCCCD);
        mx::Scale(m, s, k.scale[i] * F32(w.exe, 0x004D1D24), s);
        w.Draw(static_cast<int>(static_cast<uint32_t>(w.frameCounter2BAC) % 0x18u) + 0x161B, m);
        if (k.count[i] < -0xB) {
            k.count[i] = w.Rand(0x0046B57F) % 0x1F;
            k.spot[i][0] = static_cast<float>(w.Rand(0x0046B58E) % 7);
            k.spot[i][1] = static_cast<float>(w.Rand(0x0046B5A6) % 5);
            k.scale[i] = static_cast<float>(w.Rand(0x0046B5BD) % 0x29) * F32(w.exe, 0x004D5464) + F32(w.exe, 0x004C43A8);
        }
    }
}

// 0x0046FBE0: komono kind 64 (generic ctor case 0x40): a hanging lamp (0x1A39, scale 0.7) with a part (0xC27) 18 up,
// swinging about z (step +0x2A0 bouncing between +-0x3C, +0x2A4 = -2) (stage 2 scene 17)
struct Komono64Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int step = -32, accel = -2;      // +0x2A0 / +0x2A4
};

void TaskKomono64(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono64Task& k = t.As<Komono64Task>();
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    const int v = k.step;
    if (0x3C < (v < 0 ? -v : v)) k.accel = -k.accel;
    k.rot[2] += v;
    k.step = k.accel + v;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    const float s = Bits(0x3F333333);
    mx::Scale(m, s, s, s);
    w.Draw(0x1A39, m);
    mx::Translate(m, 0.0f, 18.0f, 0.0f);
    mx::RotZ(m, k.rot[2]);
    w.Draw(0xC27, m);
}

// 0x0046F750: komono kind 59 (generic ctor case 0x3B: radius P s8 0 * 0.1): an invisible shootable point (spark and
// sound 0xE16A9 when shot) (stage 3 scene 3)
void TaskKomono59(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono27Task& k = t.As<Komono27Task>();
    KomonoLife(w, k);
    if ((k.flags34 & 8) && k.speed == 0) {  // (+0x192 is never set: 0)
        k.flags34 &= ~8u;
        w.Sound(0xE16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.0f, k.pos[2]);  // 0x004666B0(obj, p, 1.0)
    }
    k.flags34 &= ~6u;
    mx::Transform(w.cam.view, k.pos, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

// 0x00469E60: a gate (alt model 0x17A9) that opens with event flag 0x61 (motion to ticks - 2); gone once the route
// 0x009C88A4 is 1
void TaskKomono27(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono27Task& k = t.As<Komono27Task>();
    KomonoLife(w, k);
    if (w.runner->camera.routeBranch == 1) w.ts.KillCurrent();
    const uint8_t f = w.runner->flags[0x61];  // 0x009C7261
    bool anim = f == 1;
    if (f == 1 && k.anim.frame < w.MotionTicks(k.anim.motion) - 2) ++k.anim.frame;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot64[1]);
    if (!anim) {
        w.Draw(0x17A9, m, 8, -1.0f, true);  // 0x004185E0
        return;
    }
    if (!w.MotionResident(k.anim.motion)) return;
    DrawAltPose(w, k.anim, m);
}

// 0x00469F50: a flap swinging down (x angle +0x64 to 0x4000, accelerating) once event flag 0x60 is set
void TaskKomono28(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono27Task& k = t.As<Komono27Task>();
    KomonoLife(w, k);
    if (w.runner->flags[0x60] == 1 && k.rot64[0] == 0) k.speed = 0x10;
    if (0 < k.speed && k.rot64[0] < 0x4000) {
        k.speed += 0x20;
        k.rot64[0] += k.speed;
        if (0x4000 < k.rot64[0]) {
            w.Sound(0x1A16A9);
            k.rot64[0] = 0x4000;  // (+ 0x00420810(3, 0x14): no-op)
        }
    }
    if (!w.MotionResident(k.anim.motion)) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot64[0]);
    mx::Translate(m, 0.0f, 0.0f, -6.5f);
    DrawAltPose(w, k.anim, m);
}

// =====================================================================================================================
// Komono kind 0 (ctor 0x00462A80 -> task 0x00464620): a group of stacked barrels 0x00593CF0[group] (count
// 0x00593D14[group]); entries {s16 x*10, s16 z*10, s8 item slot, s8 +0x2A0, s8 level, s8 supports, s8 support[2]}.
// First shot cracks (0x19E6) and shakes, second breaks it (prop animation 0x1D9, or 15 pieces for an upper barrel);
// an upper barrel whose support is gone falls (0x00465590 corner test) and rocks on the floor.
// =====================================================================================================================

struct BarrelTask : ActorTask {
    int8_t group = 0;          // +0x194
    int8_t slot = 0;           // +0x195 item slot
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int8_t corner = 0;         // +0x198
    int8_t life = 0;           // +0x199
    int8_t state = 0;          // +0x192 (1 falling, 2 rocking on a corner, 3 broken)
    int16_t hp = 2;            // +0x11C
    float p40[3] = {};         // +0x40 (never written for kind 0; the fall test compares it)
    float pos[3] = {};         // +0x19C
    float pivot[3] = {};       // +0x1A8
    float vel[3] = {};         // +0x1C0 / +0x1C4 / +0x1C8
    int rot[3] = {};           // +0x1CC / +0x1D0 / +0x1D4
    int spin = 0;              // +0x1D8
    int goal = 0;              // +0x1E4
    int16_t yaw0 = 0;          // +0x1FE
    int16_t model = 0x19E8;    // +0x28C
    int16_t idx = 0;           // +0x290
    int f294 = 0;              // +0x294
    float f29C = 0;            // +0x29C
    int f2A0 = 0;              // +0x2A0
    float shake = 0;           // +0x2C0
    Mat4 mv = Mat4::Identity();  // +0x2E4 last model-view
    PropAnim anim;             // +0x324 (skeleton 0, motion 0x1D9)
    uint32_t tableIdx = 0;
};

// 15 pieces of a broken upper barrel (0x00465170 -> task 0x004653B0)
struct BarrelPiecesTask : WTask {
    int counter = 0;           // +0x2B0
    int8_t idx = 0, group = 0, skel = 0;  // +0x34 / +0x35 / +0x36
    float pos[15][3] = {};     // +0x38
    int16_t ang[15][3] = {};   // +0xEC
    float vel[15][3] = {};     // +0x144
    int spinX[15] = {}, spinY[15] = {}, spinZ[15] = {};  // +0x1FC / +0x238 / +0x274
};

// The barrel pointer table 0x007DCDD4[group * 9 + index] (never cleared by the original: entries of removed barrels
// keep pointing at the freed block, whose fields stay readable; kept here as ghost copies)
struct ChainLinkTask;
struct RoutesState : TaskData {
    BarrelTask* barrels[9 * 9] = {};
    std::vector<std::unique_ptr<BarrelTask>> ghosts;
    ChainLinkTask* chain[4 * 20] = {};  // 0x007DCD18[group * 20 + link] (komono kind 24)
};

RoutesState& Routes(World::Impl& w) {
    if (!w.routes) w.routes = std::make_shared<RoutesState>();
    return *static_cast<RoutesState*>(w.routes.get());
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 16 (ctor 0x00462FE0): six floating objects (task 0x00468640, model 0xA50, hp 2, radius 8) at the table
// 0x00593D20 {s16 x, z} x 0x0055D230, y -1.9, a random yaw. Shot at hp 2: model 0xA51 turned to the camera (shake 1.0);
// at hp 1: model 0xA56 thrown away from the camera (+10) with its stand 0xA55 left at the start, and when it drops
// below 0x0055D79C it floats (hp 0, splash 0x00468C50); a floating one rocks (phase step 0x800) and dips when shot.
// Stage 2 scene 24. Komono kind 26 (ctor 0x00463230, task 0x00469C80): model 0x197F translucent (0.5) whose vertices
// the original waves in place (renderer side, not modelled).
// ---------------------------------------------------------------------------------------------------------------------
struct Komono16Task : ActorTask {
    int16_t hp = 2;                  // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int8_t life = 0;                 // +0x199
    float pos[3] = {};               // +0x19C
    float baseY = 0;                 // +0x1AC
    float start[3] = {};             // +0x1B4
    float vel[3] = {};               // +0x1C0
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int spinX = 0;                   // +0x1D8
    int phaseStep = 0;               // +0x1DC
    int spinZ = 0;                   // +0x1E0
    int phase = 0;                   // +0x1E8
    int16_t model = 0xA50;           // +0x28C
    float amp = 0;                   // +0x2C0
    float shake = 0;                 // +0x2C4
};

struct Splash468Task : WTask {
    float pos[3] = {};  // +0x34
    int yaw = 0;        // +0x40
    int model = 0x1339; // +0x48
};

// 0x00468CA0: models 0x133A..0x1356 at the position, scale 1.5
void TaskSplash468(Task& t) {
    World::Impl& w = *CurrentWorld();
    Splash468Task& k = t.As<Splash468Task>();
    if (0x1356 < ++k.model) {
        w.ts.KillCurrent();
        return;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.yaw);
    mx::Scale(m, 1.5f, 1.5f, 1.5f);
    w.Draw(k.model, m);  // (light scale 1.5, 0x0041EBB0)
}

int SwaySpin(World::Impl& w, uint32_t siteA, uint32_t siteB) {  // ((rand & 1) * -0xA0 + 0x60) - rand % 0x21
    const int a = w.Rand(siteA) & 1;
    const int b = w.Rand(siteB);
    return (a * -0xA0 + 0x60) - b % 0x21;
}

// 0x00468640
void TaskKomono16(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono16Task& k = t.As<Komono16Task>();
    const ExeImage& exe = w.exe;
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        for (World::Impl::Target& tg : w.targetCopy)
            if (tg.obj == &k) tg.obj = nullptr;
        w.ts.KillCurrent();
    };
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            destroy();
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        destroy();
        return;
    }
    const int camYaw = w.cam.yaw;  // 0x009A60D0[ctx]
    if (k.flags34 & 8) {
        if (k.hp == 0) {
            w.Sound(0xE16A9);
            k.phaseStep = 0x800;
            k.phase = 0;
            k.amp = 0.5f;
            k.spinX = SwaySpin(w, 0x00468896, 0x004688B4);
            k.spinZ = SwaySpin(w, 0x004688C9, 0x004688E7);
        } else if (k.hp == 1) {
            PropShotBy(w, k.flags34, true);  // 0x004650F0(flags, 1)
            w.Sound(0x1616A9);
            k.model = 0xA56;
            const double a = static_cast<double>(camYaw + 0x8000) * mx::kToRad;
            k.vel[0] = static_cast<float>(std::sin(a) * F64(exe, 0x005643D8));  // double 0.1
            k.vel[1] = static_cast<float>(w.Rand(0x004687A1) & 3) * F32(exe, 0x0055D230) + F32(exe, 0x004C43AC);
            memcpy(k.start, k.pos, 12);
            k.vel[2] = static_cast<float>(std::cos(a) * F64(exe, 0x005643D8));
            k.spinX = SwaySpin(w, 0x0046882E, 0x0046884C);
            k.spinZ = SwaySpin(w, 0x00468861, 0x004688E7);
        } else if (k.hp == 2) {
            PropShotBy(w, k.flags34, false);
            w.Sound(0xE16A9);
            k.model = 0xA51;
            --k.hp;
            k.rot[1] = camYaw;
            k.shake = 1.0f;
        }
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
        k.flags34 &= ~8u;
    }
    k.flags34 &= ~6u;
    if (k.model == 0xA56 && k.hp != 0) {  // thrown
        const float vy = k.vel[1] - F32(exe, 0x00569098);
        k.rot[0] += k.spinX;
        k.vel[1] = vy;
        k.rot[2] += k.spinZ;
        k.pos[0] = k.vel[0] + k.pos[0];
        const float y = vy + k.pos[1];
        k.pos[1] = y;
        k.pos[2] = k.vel[2] + k.pos[2];
        if (y < F32(exe, 0x0055D79C)) {
            k.hp = 0;
            k.baseY = -5.0f;
            k.phaseStep = 0x800;
            k.phase = 0;
            k.amp = 1.5f;
            Splash468Task* s = w.NewTask<Splash468Task>(TaskSplash468, 0x00468CA0);  // 0x00468C50
            memcpy(s->pos, k.pos, 12);
            s->yaw = w.Rand(0x00468C86) & 0xFFFF;
            w.Sound(0x4616A9);
        }
    }
    if (k.hp == 0) {  // floating
        const int old = k.phase;
        k.phase = old + k.phaseStep;
        k.pos[1] = static_cast<float>(std::sin(static_cast<double>(old) * mx::kToRad) * k.amp + k.baseY);
        if (k.phase % 0x10000 == 0) k.amp = k.amp * F32(exe, 0x004E30F8);
        k.spinX = k.spinX - (k.spinX + k.rot[0]) / 0x30;
        k.spinZ = k.spinZ - (k.rot[2] + k.spinZ) / 0x30;
        k.rot[0] = k.spinX / 2 + k.rot[0];
        k.rot[2] = k.spinZ / 2 + k.rot[2];
    }
    float dx = 0.0f, dz = 0.0f;
    if (F32(exe, 0x004C4CC0) < k.shake) {
        const float c = F32(exe, 0x004C43B0), s = F32(exe, 0x004D5464);
        dx = ((static_cast<float>(w.Rand(0x00468AE2) % 0xC9) - c) * k.shake) * s;
        dz = ((static_cast<float>(w.Rand(0x00468B0D) % 0xC9) - c) * k.shake) * s;
        k.shake = k.shake * F32(exe, 0x00564404);
    }
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, dx + k.pos[0], k.pos[1], dz + k.pos[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        w.Draw(k.model, m, 8, -1.0f, true);  // 0x004185E0
    }
    if (k.model == 0xA56) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.start[0], -7.5f, k.start[2]);
        mx::RotY(m, k.rot[1]);
        w.Draw(0xA55, m, 8, -1.0f, true);
    }
    mx::Transform(w.cam.view, k.pos, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

struct Komono26Task : WTask {
    int8_t life = 0, area = 0, areaCount = 0;  // +0x35 / +0x36 / +0x37
    int counter = 0;                           // +0x3C
};

// 0x00469C80
void TaskKomono26(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono26Task& k = t.As<Komono26Task>();
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        w.ts.KillCurrent();  // 0x00409CC0 (no target)
        return;
    }
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            w.ts.KillCurrent();
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    w.Draw(0x197F, Mat4::Identity(), 8, 0.5f, true);  // 0x00418620(0x197F, 0.5); the vertex waves are not modelled
    k.counter += 0x200;
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 24 (ctor 0x00463160): a hanging chain of 20 links (tasks 0x00469510, alt model 0x1234, each turned a
// quarter more about y); a link's matrix continues from the previous link's stored model-view +0x1C0 (moved 1.5
// down). A shot link swings the chain from two links above it down (angular velocities scaled by the distance).
// Stage 2 scene 22.
// ---------------------------------------------------------------------------------------------------------------------
struct ChainLinkTask : ActorTask {
    int16_t life = 0;                // +0x11C
    float p194[3] = {};              // +0x194 world position of the link's lower end
    float base[3] = {};              // +0x1A0
    int8_t group = 0, idx = 0;       // +0x1AC / +0x1AD
    int8_t area = 0, areaCount = 0;  // +0x1AE / +0x1AF
    uint8_t item = 0;                // +0x1B0 (game type 1: 1 = drop the item 0x004699C0)
    int16_t ax = 0, yaw = 0, az = 0; // +0x1B2 / +0x1B4 / +0x1B6
    int16_t vx = 0, vz = 0;          // +0x1B8 / +0x1BC
    Mat4 mv = Mat4::Identity();      // +0x1C0
};

// 0x00469510
void TaskChainLink(Task& t) {
    World::Impl& w = *CurrentWorld();
    ChainLinkTask& k = t.As<ChainLinkTask>();
    RoutesState& rs = Routes(w);
    auto link = [&](int i) -> ChainLinkTask* {
        const int e = k.group * 20 + i;
        return e >= 0 && e < 80 ? rs.chain[e] : nullptr;
    };
    if (k.group * 20 + k.idx >= 0 && k.group * 20 + k.idx < 80) rs.chain[k.group * 20 + k.idx] = &k;
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        for (World::Impl::Target& tg : w.targetCopy)
            if (tg.obj == &k) tg.obj = nullptr;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (k.flags34 & 8) {
        const int c = k.idx;
        const uint32_t f0 = k.flags34;
        int p = (f0 & 2) && (f0 & 4) ? (w.Rand(0x00465106) & 1) : ((f0 & 2) ? 0 : 1);  // 0x004650F0(flags, 0)
        if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
        k.flags34 &= ~8u;
        w.Sound(0xF16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.25f, k.p194[2]);  // 0x004674C0
        ChainLinkTask* first1 = rs.chain[20];  // 0x007DCD68: group 1, link 0
        if (w.runner->cfg.gameType == 1 && k.group == 1 && first1 && first1->item == 0 && w.runner->scene == 0x16) {
            first1->item = 1;
            w.runner->camera.routeBranch = 2;  // 0x009C88A4
        }
        const int r0 = w.Rand(0x00469642) & 1;
        const int vx = w.Rand(0x0046965B) % 0x51 - r0 * 0x180 + 0x98;
        const int r1 = w.Rand(0x00469674) & 1;
        const int vz = w.Rand(0x0046968D) % 0x51 - r1 * 0x180 + 0x98;
        for (int i = c - 2 < 0 ? 0 : c - 2; i < 20; ++i) {
            float f;
            if (i < c) {
                f = 1.0f - static_cast<float>(c - i) * 0.25f;  // 0x004C4380 - d * 0x004C4C58
            } else {
                const int d = i - c;
                const float a = static_cast<float>(d) * 0.25f + 1.0f;
                f = a - static_cast<float>(d * d) * F32(w.exe, 0x005690B8);
            }
            ChainLinkTask* l = link(i);
            if (!l) continue;
            l->vx = static_cast<int16_t>(static_cast<int>(static_cast<float>(vx) * f));  // __ftol
            l->vz = static_cast<int16_t>(static_cast<int>(static_cast<float>(vz) * f));
        }
    }
    k.flags34 &= ~6u;
    if (k.idx == 0 && k.item == 1) {
        k.item = 2;
        SpawnChainItem(w, 18.0f, 4, k.p194, k.area, k.areaCount, k.life);  // 0x004699C0(0x41900000, 4, link)
    }
    const int16_t sx = static_cast<int16_t>(k.vx - static_cast<int16_t>((k.ax + k.vx) / 0x30));
    k.vx = sx;
    const int16_t sz = static_cast<int16_t>(k.vz - static_cast<int16_t>((k.az + k.vz) / 0x30));
    k.vz = sz;
    k.ax = static_cast<int16_t>(sx + k.ax);
    k.az = static_cast<int16_t>(sz + k.az);
    Mat4 mv = w.cam.view;  // push (the view)
    if (k.idx == 0) {
        mx::Translate(mv, k.base[0], k.base[1], k.base[2]);
    } else if (ChainLinkTask* prev = link(k.idx - 1)) {
        mv = prev->mv;  // 0x004A9230(previous link +0x1C0)
    }
    mx::RotZ(mv, k.az);
    mx::RotX(mv, k.ax);
    Mat4 d = mv;
    mx::RotY(d, k.yaw);
    w.Draw(0x1234, d, 8, -1.0f, true, -1, true);  // 0x004185E0 (model-view)
    mx::Translate(mv, 0.0f, -1.5f, 0.0f);
    k.mv = mv;  // 0x004A8CA0(+0x1C0)
    Mat4 top = w.cam.inverse;  // 0x004A9230(camera matrix 0x009A6040), 0x004A92A0(+0x1C0)
    mx::MulLeft(top, k.mv);
    mx::GetTranslation(top, k.p194);
    mx::Transform(w.cam.view, k.p194, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

uint32_t GroupEntry(const World::Impl& w, int group, int i) { return w.exe.U32(0x00593CF0 + 4 * static_cast<uint32_t>(group)) + 10 * static_cast<uint32_t>(i); }
int8_t GroupByte(const World::Impl& w, int group, int i, int off) { return static_cast<int8_t>(w.exe.S16(GroupEntry(w, group, i) + off) & 0xFF); }
int GroupCount(const World::Impl& w, int group) { return static_cast<int8_t>(w.exe.S16(0x00593D14 + group) & 0xFF); }

// Actor_Destroy 0x00409CC0 of a barrel: the table keeps a ghost of it
void BarrelDestroy(World::Impl& w, BarrelTask& k) {
    k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &k) tg.obj = nullptr;  // 0x00405220
    RoutesState& rs = Routes(w);
    BarrelTask*& e = rs.barrels[k.tableIdx];
    if (e == &k) {
        rs.ghosts.push_back(std::make_unique<BarrelTask>(k));
        e = rs.ghosts.back().get();
    }
    w.ts.KillCurrent();
}

// 0x00465590: corner test against the floor (ground - 0.1); state 1 records the corner that lands, state 2 the lowest
// other corner (the barrel rocks over it) and places the barrel from that pivot. True when a corner is below the floor.
bool BarrelCorners(World::Impl& w, BarrelTask& k) {
    const ExeImage& exe = w.exe;
    bool landed = false;
    float best = 10000.0f;
    Mat4 m = Mat4::Identity();
    mx::RotY(m, k.rot[1]);
    mx::RotZ(m, k.rot[2]);
    mx::RotX(m, k.rot[0]);
    const float ks = F32(exe, 0x0055D2B0), mid = F32(exe, 0x00569038), lim = w.ground - F32(exe, 0x004C4CC8);
    for (int i = 0; i < 96; ++i) {
        const uint32_t e = 0x005937F8 + static_cast<uint32_t>(i) * 6;
        const float c[3] = { static_cast<float>(exe.S16(e)) * ks, static_cast<float>(exe.S16(e + 2)) * ks - mid,
                             static_cast<float>(exe.S16(e + 4)) * ks };
        float t[3];
        mx::Transform(m, c, t);
        if (t[1] + k.pos[1] < lim) {
            landed = true;
            if (k.state == 1) {
                k.corner = static_cast<int8_t>(i);
                k.pivot[0] = t[0] + k.pos[0];
                k.pivot[1] = w.ground;
                k.pivot[2] = t[2] + k.pos[2];
            } else if (k.state == 2 && i != k.corner && t[1] < best) {
                k.corner = static_cast<int8_t>(i);
                best = t[1];
                k.pivot[0] = t[0] + k.pos[0];
                k.pivot[1] = w.ground;
                k.pivot[2] = t[2] + k.pos[2];
            }
        }
        if (w.cfg.eventMode == 1) {  // stage 2: kept inside the corridor z -1325 .. -1265.8
            float f = t[2] + k.pos[2];
            if (f < F32(exe, 0x0056905C)) k.pos[2] = k.pos[2] - (f + F32(exe, 0x00569058));
            f = t[2] + k.pos[2];
            if (static_cast<float>(F64(exe, 0x00569050)) < f) k.pos[2] = k.pos[2] - (f + F32(exe, 0x0056904C));
        }
    }
    if (k.state == 2) {
        Mat4 q = Mat4::Identity();
        mx::Translate(q, k.pivot[0], k.pivot[1], k.pivot[2]);
        mx::RotY(q, k.rot[1]);
        mx::RotZ(q, k.rot[2]);
        mx::RotX(q, k.rot[0]);
        const uint32_t e = 0x005937F8 + static_cast<uint32_t>(k.corner) * 6;
        const float kc = F32(exe, 0x0056903C);
        mx::Translate(q, static_cast<float>(exe.S16(e)) * kc, -(static_cast<float>(exe.S16(e + 2)) * ks - mid),
                      static_cast<float>(exe.S16(e + 4)) * kc);
        mx::GetTranslation(q, k.pos);
    }
    return landed;
}

// 0x004653B0
void TaskBarrelPieces(Task& t) {
    World::Impl& w = *CurrentWorld();
    BarrelPiecesTask& k = t.As<BarrelPiecesTask>();
    const ExeImage& exe = w.exe;
    if (0x48 < k.counter++) {
        w.ts.KillCurrent();
        return;
    }
    const float floor = w.ground + 1.0f;  // 0x004C4380
    for (int i = 0; i < 15; ++i) {
        k.vel[i][1] = k.vel[i][1] - F32(exe, 0x00569048);
        k.pos[i][0] = k.vel[i][0] + k.pos[i][0];
        k.pos[i][1] = k.vel[i][1] + k.pos[i][1];
        k.pos[i][2] = k.vel[i][2] + k.pos[i][2];
        k.ang[i][0] = static_cast<int16_t>(k.ang[i][0] + k.spinX[i]);
        k.ang[i][1] = static_cast<int16_t>(k.ang[i][1] + k.spinY[i]);
        k.ang[i][2] = static_cast<int16_t>(k.ang[i][2] + k.spinZ[i]);
        if (k.pos[i][1] < w.ground + 1.0f && k.group != 'c') {
            k.pos[i][1] = floor;
            k.vel[i][1] = k.vel[i][1] * F32(exe, 0x00569044);
        }
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[i][0], k.pos[i][1], k.pos[i][2]);
        mx::RotZ(m, k.ang[i][2]);
        mx::RotY(m, k.ang[i][1]);
        mx::RotX(m, k.ang[i][0]);
        w.Draw(exe.S16((k.skel == 0 ? 0x00593A38 : 0x00593A58) + 2 * static_cast<uint32_t>(i)), m);
    }
}

}  // namespace

// 0x00465170: 15 pieces placed from the barrel's last model-view (x the inverse of the view); for any barrel block:
// +0x290 index, +0x194 group, +0x324 skeleton, +0x2E4 the last model-view
void SpawnBarrelPiecesFrom(World::Impl& w, int idx, int group, int skel, const Mat4& mv) {
    const ExeImage& exe = w.exe;
    BarrelPiecesTask* k = w.NewTask<BarrelPiecesTask>(TaskBarrelPieces, 0x004653B0);
    k->counter = 0;
    k->idx = static_cast<int8_t>(idx);
    k->group = static_cast<int8_t>(group);
    k->skel = static_cast<int8_t>(skel);
    Mat4 top = mx::Inverse(w.cam.view);  // 0x004A8D20(0)
    mx::MulLeft(top, mv);                // 0x004A92A0(+0x2E4)
    const float ks = F32(exe, 0x0055D2B0), k5464 = F32(exe, 0x004D5464), k4cc8 = F32(exe, 0x004C4CC8);
    for (int i = 0; i < 15; ++i) {
        const uint32_t pe = 0x00593A78 + static_cast<uint32_t>(i) * 6, ae = 0x00593AD4 + static_cast<uint32_t>(i) * 6;
        Mat4 m = top;
        mx::Translate(m, static_cast<float>(exe.S16(pe)) * ks, static_cast<float>(exe.S16(pe + 2)) * ks,
                      static_cast<float>(exe.S16(pe + 4)) * ks);
        mx::RotZ(m, exe.S16(ae + 4));
        mx::RotY(m, exe.S16(ae + 2));
        mx::RotX(m, exe.S16(ae));
        mx::GetTranslation(m, k->pos[i]);
        int ax = 0, ay = 0, az = 0;
        mx::MatrixToAngles(m, &ax, &ay, &az);
        k->ang[i][0] = static_cast<int16_t>(ax);
        k->ang[i][1] = static_cast<int16_t>(ay);
        k->ang[i][2] = static_cast<int16_t>(az);
        const double r = AngleRad(i * 0x1000);
        const float f1 = static_cast<float>(w.Rand(0x004652C6) % 0x15) * k5464 + k4cc8;
        k->vel[i][0] = static_cast<float>(std::sin(r) * f1);
        const float f2 = static_cast<float>(w.Rand(0x004652F9) % 0x15) * k5464 + k4cc8;
        k->vel[i][2] = static_cast<float>(std::cos(r) * f2);
        k->vel[i][1] = 1.0f;
        k->spinX[i] = w.Rand(0x00465336) % 0x801 - 0x400;
        k->spinY[i] = w.Rand(0x0046534C) % 0x801 - 0x400;
        k->spinZ[i] = w.Rand(0x00465361) % 0x801 - 0x400;
    }
}

namespace {

void SpawnBarrelPieces(World::Impl& w, BarrelTask& b) { SpawnBarrelPiecesFrom(w, b.idx, b.group, b.anim.skeleton, b.mv); }

// 0x00465500: the broken barrel's prop animation (skeleton +0x324, motion 0x1D9) for 72 frames
void TaskBarrelBreak(Task& t) {
    World::Impl& w = *CurrentWorld();
    BarrelTask& k = t.As<BarrelTask>();
    if (0x47 < k.anim.frame) {
        w.ts.KillCurrent();
        return;
    }
    ++k.anim.frame;
    if (!w.MotionResident(k.anim.motion)) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotZ(m, k.rot[2]);
    mx::RotX(m, k.rot[0]);
    if (const Motion* mo = w.PropMotion(k.anim.motion)) {
        for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m)) {  // 0x0040DD90
            if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
            if (d.model != 0) w.Draw(d.model, d.world, 8, -1.0f, d.alt);
        }
    }
}

// 0x00464620
void TaskBarrel(Task& t) {
    World::Impl& w = *CurrentWorld();
    BarrelTask& k = t.As<BarrelTask>();
    const ExeImage& exe = w.exe;
    RoutesState& rs = Routes(w);
    float sx = 0, sz = 0, lift = 0;
    rs.barrels[k.tableIdx] = &k;
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            BarrelDestroy(w, k);
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        BarrelDestroy(w, k);
        return;
    }
    const uint32_t f = k.flags34;
    const int level = GroupByte(w, k.group, k.idx, 6);
    if ((f & 8) && k.group != 4 && (w.cfg.eventMode != 1 || w.runner->scene != 0x11 || w.runner->flags[0x28] != 0)) {
        if (k.hp == 1) {
            PropShooter(w, f, true);  // 0x004650F0(flags, 1)
            k.state = 3;
            rs.barrels[k.tableIdx] = nullptr;
            w.Sound(0x1A16A9);
            w.SpawnSmoke(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, k.pos[2]);  // 0x00465860
            if (level != 0) {
                SpawnBarrelPieces(w, k);  // 0x00465170
                w.ts.KillCurrent();
                return;
            }
            k.f294 = 0;
            k.anim.frame = 0;
            k.anim.prevFrame = 0;
            k.fn = 0x00465500;
            t.fn = TaskBarrelBreak;
            k.hp = k.life;
            if (w.runner->cfg.gameType == 1 && ItemFlag(w, 2)) {  // 0x009C88AA: always a life item
                k.slot = 1;
                BreakableProp b;
                memcpy(b.pos, k.pos, 12);
                b.areaSeen = static_cast<uint8_t>(k.area);
                b.areaChanges = static_cast<uint8_t>(k.areaCount);
                b.lifetime = k.hp;
                b.model = k.model;
                w.DropItem(b, 1);  // 0x00471BD0
            } else if (0 < k.slot) {
                uint8_t& c = w.itemCounter[k.slot & 15];
                --c;
                if (c == 0) {
                    if (w.runner->cfg.gameType == 1 && k.f2A0 != -1) {
                        SpawnItemAt(w, k.pos, k.area, k.areaCount, k.hp, k.f2A0);  // 0x00467B90 (slot +0x2A0)
                        SetItemTaken(w, 0);
                    } else if (k.slot == 1 || k.slot == 2 || k.slot == 3 || (k.slot >= 5 && k.slot <= 8)) {
                        BreakableProp b;  // 0x00471BD0 / 0x004723F0 / 0x004722A0 read +0x19C / +0x196 / +0x197 / +0x11C
                        memcpy(b.pos, k.pos, 12);
                        b.areaSeen = static_cast<uint8_t>(k.area);
                        b.areaChanges = static_cast<uint8_t>(k.areaCount);
                        b.lifetime = k.hp;
                        b.model = k.model;
                        w.DropItem(b, k.slot);
                    }
                }
            }
        } else if (k.hp == 2) {
            if (w.runner->cfg.gameType != 2) PropShooter(w, f, false);  // 0x004650F0(flags, 0)
            k.model = 0x19E6;
            if (k.state == 0) k.rot[1] = w.cam.yaw;  // 0x009A60D0[ctx]
            --k.hp;
            w.Sound(0x1D16A9);
            w.SpawnSmoke(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, k.pos[2]);  // 0x00465860
            k.shake = 1.0f;
            if (level < 2) {  // the barrels resting on this one shake too
                const int n = GroupCount(w, k.group);
                for (int j = 0; j < n; ++j) {
                    const int ns = GroupByte(w, k.group, j, 7);
                    for (int s = 0; s < ns; ++s) {
                        BarrelTask* o = rs.barrels[k.group * 9 + j];
                        if (o && GroupByte(w, k.group, j, 8 + s) == k.idx) o->shake = 0.5f;
                    }
                }
            }
        }
    }
    k.flags34 &= ~0xEu;
    if (w.runner->flags[0x65] == 1 && k.group == 4 && k.state == 0) {  // 0x009C7265
        k.state = 3;
        if (level != 0) {
            SpawnBarrelPieces(w, k);
            w.ts.KillCurrent();
            return;
        }
        k.f294 = 0;
        k.anim.skeleton = 0;
        k.anim.motion = 0x1D9;
        k.anim.frame = 0;
        k.anim.prevFrame = 0;
        k.fn = 0x00465500;
        t.fn = TaskBarrelBreak;
    }
    if (F32(exe, 0x004C4CC0) < k.shake) {
        const float c40 = F32(exe, 0x00569040), c64 = F32(exe, 0x004D5464);
        sx = ((static_cast<float>(w.Rand(0x00464985) % 0x97) - c40) * k.shake) * c64;
        sz = ((static_cast<float>(w.Rand(0x004649B0) % 0x97) - c40) * k.shake) * c64;
        k.shake = k.shake * F32(exe, 0x00564534);
    }
    const float mid = F32(exe, 0x00569038);
    if (k.state == 0) {
        lift = 3.770148f;
        const int ns = GroupByte(w, k.group, k.idx, 7);
        if (0 < level) {
            for (int s = 0; s < ns; ++s) {
                BarrelTask* o = rs.barrels[k.group * 9 + GroupByte(w, k.group, k.idx, 8 + s)];
                if (o == nullptr || o->state != 0) {
                    k.pos[1] = k.pos[1] + mid;
                    k.state = 1;
                    k.vel[1] = 0;
                    if (!(k.flags34 & 0x1000000)) {
                        const int sign = w.Rand(0x00464E10) & 1;
                        k.spin = (sign * -2 + 1) * (w.Rand(0x00464E2A) % 0x81 + 0x80);
                    } else {
                        k.rot[1] = k.yaw0;
                    }
                    const float c5464 = F32(exe, 0x004D5464), c4cc8 = F32(exe, 0x004C4CC8);
                    int yaw;
                    if (k.spin < 1) {
                        k.goal = -0x4000;
                        const int r1 = w.Rand(0x00464EC7);
                        yaw = k.rot[1] + 0x8000;
                        k.vel[0] = static_cast<float>(std::sin(AngleRad(yaw)) *
                                                      (static_cast<float>(r1 % 0xB) * c5464 + c4cc8));
                        const int r2 = w.Rand(0x00464F10);
                        k.vel[2] = static_cast<float>(std::cos(AngleRad(yaw)) *
                                                      (static_cast<float>(r2 % 0xB) * c5464 + c4cc8));
                    } else {
                        k.goal = 0x4000;
                        const int r1 = w.Rand(0x00464E5A);
                        yaw = k.rot[1];
                        k.vel[0] = static_cast<float>(std::sin(AngleRad(yaw)) *
                                                      (static_cast<float>(r1 % 0xB) * c5464 + c4cc8));
                        const int r2 = w.Rand(0x00464E95);
                        k.vel[2] = static_cast<float>(std::cos(AngleRad(yaw)) *
                                                      (static_cast<float>(r2 % 0xB) * c5464 + c4cc8));
                    }
                }
            }
        }
        auto build = [&](Mat4 m) {
            mx::Translate(m, sx + k.pos[0], k.pos[1], sz + k.pos[2]);
            mx::RotY(m, k.rot[1]);
            return m;
        };
        w.Draw(k.model, build(Mat4::Identity()));
        k.mv = build(w.cam.view);
    } else if (k.state == 1) {
        const int n = GroupCount(w, k.group);
        bool held = false;
        for (int j = 0; j < n; ++j) {
            BarrelTask* o = rs.barrels[k.group * 9 + j];
            if (j != k.idx && o && o->p40[1] < k.p40[1]) {
                const float dz = k.p40[2] - o->p40[2], dy = k.p40[1] - o->p40[1], dx = k.p40[0] - o->p40[0];
                if (static_cast<float>(std::sqrt(static_cast<double>(dz * dz + dy * dy + dx * dx))) < 6.0f) {  // 0x0055E1C8
                    held = true;
                    k.vel[1] = 0;
                }
            }
        }
        if (!held) {
            k.vel[1] = k.vel[1] - F32(exe, 0x00569034);
            k.pos[1] = k.vel[1] + k.pos[1];
        }
        k.rot[0] += k.spin;
        k.pos[0] = k.vel[0] + k.pos[0];
        k.pos[2] = k.vel[2] + k.pos[2];
        if (BarrelCorners(w, k)) {
            k.state = 2;
            k.f29C = 500.0f;
            w.Sound(0x1C16A9);
        }
        auto build = [&](Mat4 m) {
            mx::Translate(m, sx + k.pos[0], k.pos[1], sz + k.pos[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotZ(m, k.rot[2]);
            mx::RotX(m, k.rot[0]);
            mx::Translate(m, 0.0f, -mid, 0.0f);
            return m;
        };
        w.Draw(k.model, build(Mat4::Identity()));
        k.mv = build(w.cam.view);
    } else if (k.state == 2) {
        const int d = (k.goal - k.rot[0]) - k.spin;
        const int sp = d / 16 + k.spin;
        k.spin = sp;
        if (0x20 < std::abs(sp)) {
            k.rot[0] = sp / 8 + k.rot[0];
            BarrelCorners(w, k);
        }
        const uint32_t e = 0x005937F8 + static_cast<uint32_t>(k.corner) * 6;
        const float kc = F32(exe, 0x0056903C), ks = F32(exe, 0x0055D2B0);
        auto build = [&](Mat4 m) {
            mx::Translate(m, sx + k.pivot[0], k.pivot[1], sz + k.pivot[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotZ(m, k.rot[2]);
            mx::RotX(m, k.rot[0]);
            mx::Translate(m, static_cast<float>(exe.S16(e)) * kc, -(static_cast<float>(exe.S16(e + 2)) * ks - mid),
                          static_cast<float>(exe.S16(e + 4)) * kc);
            mx::Translate(m, 0.0f, -mid, 0.0f);
            return m;
        };
        w.Draw(k.model, build(Mat4::Identity()));
        k.mv = build(w.cam.view);
    }
    if (level == 0 || k.state != 0) {  // floor shadow
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], w.ground + F32(exe, 0x004D1D24), k.pos[2]);
        mx::Scale(m, 10.0f, 1.0f, 10.0f);
        w.Draw(0x10D0, m);
    }
    if (k.state != 3) {
        const float p[3] = { k.pos[0], lift + k.pos[1], k.pos[2] };
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);
    }
}

// 0x00462A80
void SpawnBarrels(World::Impl& w, const SpawnRecord& r) {
    const ExeImage& exe = w.exe;
    RoutesState& rs = Routes(w);
    const int group = static_cast<int16_t>(r.state);
    if (group < 0 || group > 8) {
        w.Note("komono kind 0 group " + std::to_string(group) + " out of the table");
        return;
    }
    const int n = GroupCount(w, group);
    int nSlots = 0, lastSlot = 0;
    for (int i = 0; i < n; ++i) {
        BarrelTask* k = w.NewTask<BarrelTask>(TaskBarrel, 0x00464620);
        k->group = static_cast<int8_t>(group);
        k->slot = GroupByte(w, group, i, 4);
        // TOTD 0x00455340: only authored item barrels (or event mode 7)
        // receive a word post; ordinary stacked scenery stays gun-only.
        if (k->slot != 0 || w.cfg.eventMode == 7) k->typingFlags = 0x00400018u;
        k->hp = 2;
        k->life = r.ParamS8(0);  // spawner +0x1F4
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->idx = static_cast<int16_t>(i);
        const uint32_t e = GroupEntry(w, group, i);
        k->pos[0] = static_cast<float>(exe.S16(e)) * F32(exe, 0x0055D230);
        k->pos[1] = (w.ground - F32(exe, 0x004C4CC8)) + static_cast<float>(GroupByte(w, group, i, 6)) * F32(exe, 0x00569008);
        if (group == 7)
            k->pos[1] = static_cast<float>(GroupByte(w, group, i, 6)) * F32(exe, 0x00569008) - static_cast<float>(F64(exe, 0x00569000));
        k->pos[2] = static_cast<float>(exe.S16(e + 2)) * F32(exe, 0x0055D230);
        const int yaw = w.Rand(0x00462BCA) & 0xFFFF;
        k->rot[1] = yaw;
        k->yaw0 = static_cast<int16_t>(yaw);
        k->radius = 5.0f;
        k->anim.skeleton = 0;
        k->anim.motion = 0x1D9;
        k->flags34 = 0x80000001u;
        k->model = 0x19E8;
        k->f2A0 = GroupByte(w, group, i, 5);
        if (w.runner->cfg.gameType == 2) {  // training: one-shot barrels (skeleton 6, model 0x1A0F) by the difficulty 0x009C9118
            const int d = w.runner->cfg.playerVariant;
            const bool one = d == 0 || d == 3 ? (i == 2 || i == 3 || i == 4 || i == 6)
                           : d == 1           ? (i == 0 || i == 1 || i == 5)
                           : d == 2           ? (i == 0 || i == 6)
                                              : false;
            if (one) {
                k->anim.skeleton = 6;
                k->hp = 1;
                k->model = 0x1A0F;
            }
        }
        k->tableIdx = static_cast<uint32_t>(i + group * 9);
        rs.barrels[k->tableIdx] = k;
        if (group == 0 && i == 3) {
            k->yaw0 = 0;
            k->flags34 |= 0x1000000;
            k->spin = -0x80 - w.Rand(0x00462CED) % 0x81;
        }
        if (group == 7 && i == 5) {
            k->yaw0 = static_cast<int16_t>(0x8000);
            k->flags34 |= 0x1000000;
            k->spin = -0x80 - w.Rand(0x00462D2F) % 0x81;
        }
        if (0 < k->slot) {
            ++nSlots;
            lastSlot = k->slot;
        }
        if (group == 4) {
            k->flags34 |= 0x1000000;
            if (i == 2) k->yaw0 = 0x3000;
            else if (i == 3) k->yaw0 = 0x5000;
            else if (i == 4) k->yaw0 = 0x4000;
        }
    }
    if (1 < nSlots) w.itemCounter[lastSlot & 15] = static_cast<uint8_t>(w.Rand(0x00462DC7) % nSlots + 1);
    if (nSlots == 1) w.itemCounter[lastSlot & 15] = 1;
}

// =====================================================================================================================
// Komono kind 49 (generic ctor 0x00461CF0 case 0x31 -> task 0x0046E6E0): a pot (model 0x1D2) that rolls on its rim
// (13 rim points 0x00594A68) when shot; stage 2 scene 8 (shutter left closed)
// =====================================================================================================================

struct Komono49Task : ActorTask {
    float pos[3] = {};          // +0x19C
    float pivot[3] = {};        // +0x1A8
    int rot[3] = {};            // +0x1CC / +0x1D0 / +0x1D4
    int spinX = 0, spinY = 0, spinZ = 0;  // +0x1D8 / +0x1DC / +0x1E0
    int8_t corner = 0;          // +0x198
    int16_t life = 0;           // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
};

// 0x0046E6E0
void TaskKomono49(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono49Task& k = t.As<Komono49Task>();
    const ExeImage& exe = w.exe;
    // Prop_CheckLifetime 0x00466640
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
            w.ts.KillCurrent();
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (k.flags34 & 8) {
        PropShooter(w, k.flags34, false);  // 0x004650F0(flags, 0)
        w.Sound(0x1516A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.0f, k.pos[2]);  // 0x004666B0(obj, p, 1.0)
        const int s1 = w.Rand(0x0046E73B) & 1;
        k.spinX = (s1 * -2 + 1) * (w.Rand(0x0046E755) % 0x101 + 0x100);
        const int s2 = w.Rand(0x0046E771) & 1;
        k.spinZ = (s2 * -2 + 1) * (w.Rand(0x0046E78B) % 0x101 + 0x100);
        k.spinY = w.Rand(0x0046E7A7) % 0x201 - 0x300;
        k.flags34 &= ~8u;
    }
    const float k95 = F32(exe, 0x0055CB40), k99 = F32(exe, 0x005643E0);
    {
        const int sx = k.spinX - (k.rot[0] + k.spinX) / 32;
        k.flags34 &= ~6u;
        k.spinX = sx;
        const int sy = static_cast<int>(static_cast<float>(k.spinY) * k95);
        const int sz = k.spinZ - (k.spinZ + k.rot[2]) / 32;
        k.spinY = sy;
        k.rot[1] += sy;
        k.spinZ = sz;
        k.rot[0] = static_cast<int>(static_cast<float>(k.rot[0] + sx) * k99);
        k.rot[2] = static_cast<int>(static_cast<float>(sz + k.rot[2]) * k99);
    }
    // the lowest rim point below the floor (other than the current one) becomes the pivot
    {
        Mat4 m = Mat4::Identity();
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        float best = 10000.0f;
        const float ks = F32(exe, 0x0055D2B0), lim = w.ground - F32(exe, 0x004C4CC8);
        for (int i = 0; i < 13; ++i) {
            const uint32_t e = 0x00594A68 + 4 * static_cast<uint32_t>(i);
            const float c[3] = { static_cast<float>(exe.S16(e)) * ks, 0.0f, static_cast<float>(exe.S16(e + 2)) * ks };
            float o[3];
            mx::Transform(m, c, o);
            if (o[1] + k.pos[1] < lim && i != k.corner && o[1] < best) {
                best = o[1];
                k.pivot[0] = o[0] + k.pos[0];
                k.corner = static_cast<int8_t>(i);
                k.pivot[1] = w.ground;
                k.pivot[2] = o[2] + k.pos[2];
            }
        }
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pivot[0], k.pivot[1], k.pivot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotZ(m, k.rot[2]);
    mx::RotX(m, k.rot[0]);
    {
        const uint32_t e = 0x00594A68 + 4 * static_cast<uint32_t>(k.corner);
        const float kc = F32(exe, 0x0056903C);
        mx::Translate(m, static_cast<float>(exe.S16(e)) * kc, 0.0f, static_cast<float>(exe.S16(e + 2)) * kc);
    }
    mx::GetTranslation(m, k.pos);
    w.Draw(0x1D2, m);
    Mat4 s = Mat4::Identity();
    mx::Translate(s, k.pos[0], w.ground + F32(exe, 0x004C4CC8), k.pos[2]);
    mx::Scale(s, 10.0f, 1.0f, 10.0f);
    w.Draw(0x10D0, s);
    const float p[3] = { k.pos[0], k.pos[1] + 1.0f, k.pos[2] };  // 0x004C4380
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

// Komono kind 33 (generic ctor 0x00461CF0 case 0x21 -> task 0x00472950): the model sequence state + frame, frames
// 0..record angle z, then the task ends (stage 2 scene 11)
struct Komono33Task : ActorTask {
    float pos[3] = {};  // +0x19C
    int rot[3] = {};    // +0x1CC / +0x1D0 / +0x1D4
    int16_t model = 0;  // +0x28C
    int frame = 0;      // +0x2A0
    int last = 0;       // +0x2A4
};

// 0x00472950
void TaskKomono33(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono33Task& k = t.As<Komono33Task>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model + k.frame, m);
    if (k.last < ++k.frame) w.ts.KillCurrent();
}

// Actor type 0x44 state 8 (0x00473260 -> task 0x004748C0, block 0xD14): a prop skeleton 0x10 (73 nodes) that plays
// motion 0x1D3 while event flag P s8 0x10 is set; from frame 0x24 its 72 pieces fly apart (velocities = the motion's
// frame 0x23 - frame 0x22), bounce on the floor and are drawn translucent (0.95) until frame 0x50 (stage 2 scene 11)
struct Prop44s8Task : ActorTask {
    float pos[3] = {};         // +0x194
    float scale[3] = {};       // +0x1A0
    int yaw = 0;               // +0x1B0
    int8_t area = 0, areaCount = 0;  // +0x1BB / +0x1BC
    int flag = 0;              // +0x1C0
    int16_t life = 0;          // +0x11C
    PropAnim anim;             // +0x1E4 {skeleton 0x10, motion 0x1D3, frame, prevFrame}
    float ppos[73][3] = {};    // +0x238 piece positions (entries 1..72 are drawn)
    int16_t pang[73][3] = {};  // +0x910
    float pvel[73][3] = {};    // +0x5A4 (as +0x238 + 0x36C)
    int16_t pdang[73][3] = {}; // +0xAC6
    int16_t models[74] = {};   // +0xC7C [node index]
};

// 0x004748C0
void TaskProp44s8(Task& t) {
    World::Impl& w = *CurrentWorld();
    Prop44s8Task& k = t.As<Prop44s8Task>();
    const ExeImage& exe = w.exe;
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        destroy();
        return;
    }
    if (w.runner->variant != k.area) {
        if (k.life < static_cast<int16_t>(++k.areaCount)) {
            destroy();
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    const bool flagOn = w.runner->flags[k.flag & 0xFF] == 1;
    if (w.runner->scene == 0xB) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0xC4594CCD), 12.0f, Bits(0xC44A8000));
        w.Draw(w.runner->flags[k.flag & 0xFF] == 0 ? 0x173B : 0x173C, m);
    } else if (w.runner->scene == 0x12 && w.runner->flags[k.flag & 0xFF] == 0) {
        w.Draw(0x197E, Mat4::Identity());  // (drawn with the matrix in effect: the view)
    }
    const Motion* mo = w.PropMotion(k.anim.motion);
    const PropSkeleton& sk = w.PropSkel(k.anim.skeleton);
    const size_t stride = sk.FrameStride();
    auto rawF = [&](size_t off) {
        float f = 0;
        if (mo && off + 4 <= mo->data.size()) memcpy(&f, &mo->data[off], 4);
        return f;
    };
    auto rawS = [&](size_t off) {
        int16_t s = 0;
        if (mo && off + 2 <= mo->data.size()) memcpy(&s, &mo->data[off], 2);
        return s;
    };
    auto posOff = [&](int frame, int e) { return 4 + static_cast<size_t>(frame) * stride + static_cast<size_t>(e) * 12; };
    auto angOff = [&](int frame, int e) {
        return 4 + static_cast<size_t>(frame) * stride + static_cast<size_t>(sk.count - 1) * 12 + static_cast<size_t>(e) * 6;
    };
    if (k.anim.frame == 0x22) {  // 0x0040E040 / 0x0040E070
        for (int e = 0; e < 73; ++e) {
            for (int c = 0; c < 3; ++c) k.ppos[e][c] = rawF(posOff(0x22, e) + 4 * c);
            for (int c = 0; c < 3; ++c) k.pang[e][c] = rawS(angOff(0x22, e) + 2 * c);
        }
    }
    if (k.anim.frame == 0x23) {
        for (int e = 0; e < 73; ++e) {
            for (int c = 0; c < 3; ++c) {
                const float p = rawF(posOff(0x23, e) + 4 * c);
                k.pvel[e][c] = p - k.ppos[e][c];
                k.ppos[e][c] = p;
                const int16_t a = rawS(angOff(0x23, e) + 2 * c);
                k.pdang[e][c] = static_cast<int16_t>(a - k.pang[e][c]);
                k.pang[e][c] = a;
            }
        }
    }
    if (flagOn) ++k.anim.frame;
    if (w.MotionResident(k.anim.motion) && flagOn && k.anim.frame < 0x24) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.yaw);
        mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
        for (const PropSkeleton::Node& n : sk.nodes)  // 0x00474E00 records each node's model at +0xC7C[index]
            if (n.index > 0 && n.index < 74) k.models[n.index] = static_cast<int16_t>(n.model);
        if (mo) {
            for (const PropDraw& d : PropPose(sk, *mo, k.anim, m)) {  // 0x00474E00 (0x0040D9D0 per node)
                if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                if (d.model != 0) w.Draw(d.model, d.world, 8, 0.95f);  // 0x004185A0(model, 0.95)
            }
        }
        k.anim.prevFrame = k.anim.frame;
        return;
    }
    if (k.anim.frame >= 0x50) return;
    int floor = static_cast<int>((w.ground - k.pos[1]) + k.scale[1] * 5.0f);  // 0x0055D2B4
    if (w.runner->scene == 0x12) floor = static_cast<int>(static_cast<double>(floor) - 2.0);  // qword 0x0055CAF8
    // On the matrix stack (model-view, the original's order): entry 72's position is read past the motion frame's 72
    // positions (the first angles as floats: ~1e35), so only the stack's own rounding reproduces its matrices.
    Mat4 base = w.cam.view;
    mx::Translate(base, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(base, k.yaw);
    mx::Scale(base, k.scale[0], k.scale[1], k.scale[2]);
    const float g = F32(exe, 0x0055CB10), bounce = F32(exe, 0x00569180);
    for (int e = 1; e <= 72; ++e) {
        float* p = k.ppos[e];
        float* v = k.pvel[e];
        int16_t* a = k.pang[e];
        int16_t* d = k.pdang[e];
        v[1] = v[1] - g;
        p[0] = v[0] + p[0];
        p[1] = p[1] + v[1];
        p[2] = v[2] + p[2];
        a[0] = static_cast<int16_t>(a[0] + d[0]);
        a[1] = static_cast<int16_t>(a[1] + d[1]);
        a[2] = static_cast<int16_t>(a[2] + d[2]);
        if (p[1] < static_cast<float>(floor)) {
            p[1] = static_cast<float>(floor);
            v[1] = v[1] * bounce;
            d[0] = static_cast<int16_t>(d[0] + w.Rand(0x00474D1D) % 0x401 - 0x200);
            d[1] = static_cast<int16_t>(d[1] + w.Rand(0x00474D34) % 0x401 - 0x200);
            d[2] = static_cast<int16_t>(d[2] + w.Rand(0x00474D4A) % 0x401 - 0x200);
        }
        Mat4 m = base;
        mx::Translate(m, p[0], p[1], p[2]);
        mx::RotZ(m, a[2]);
        mx::RotY(m, a[1]);
        mx::RotX(m, a[0]);
        if (k.models[e] != 0) w.Draw(k.models[e], m, 8, 0.95f, false, -1, true);  // (Mdl_Draw of model 0 draws nothing)
    }
}

// Actor type 0x44 state 5 (0x00472F80 -> task 0x00474240): a frame (model 0x17D7) that, once event flag 0x62 is set,
// plays prop skeleton 0xC / motion 0x1CE (one frame per tick to ticks - 3) and hands its hanging part (the record's
// model, state-1 parameters with phase 0) to a new hanging-prop task 0x00473CF0 (stage 2 scene 14)
struct Prop44s5Task : ActorTask {
    float pos[3] = {};      // +0x19C
    int yaw = 0;            // +0x1D0
    int8_t handed = 0;      // +0x192
    HangingProp hang;       // +0x28C model, +0x14C mesh, +0x1DC, +0x290 kind, +0x2A0 / +0x2A4 flags
    PropAnim anim;          // +0x324 {0xC, 0x1CE, +0x32C frame}
};

// 0x00474240
void TaskProp44s5(Task& t) {
    World::Impl& w = *CurrentWorld();
    Prop44s5Task& k = t.As<Prop44s5Task>();
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (w.runner->flags[k.hang.killFlag & 0xFF] == 1) {
        if (k.hang.hitId != -1) destroy();
        w.ts.KillCurrent();
    }
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->flags[0x62] == 1) {  // 0x009C7262
        if (k.anim.frame < w.MotionTicks(k.anim.motion) - 3) ++k.anim.frame;
        if (k.handed == 0) {
            w.SpawnHangingCopy(k.hang);
            k.handed = 1;
        }
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.yaw);
    if (k.anim.frame == 0) {
        w.Draw(0x17D7, m);
        return;
    }
    if (!w.MotionResident(k.anim.motion)) return;  // 0x009A37E4[motion * 4] == 2
    if (k.handed == 0) w.Draw(k.hang.model, m);
    mx::Translate(m, 0.0f, -1.0f, 0.0f);
    const Motion* mo = w.PropMotion(k.anim.motion);
    if (!mo) return;
    for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m)) {  // 0x0040DD90
        if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
        w.Draw(d.model, d.world);
    }
}

// Actor type 0x44 state 6 (0x00473060 -> task 0x00474470): a hanging sign (alt model, collision-mesh target) that
// swings once its trigger flag is set (u16 table 0x005960CC x +0x2C0, 0x3C frames); event flag 0x63 drops it (model
// 0x170.. counting to 0x174, prop skeletons 0xD / 0xE, motions 0x1C7 / 0x1C8, spinning down with 0.85 damping) and
// flag 0x64 swaps to skeleton 0xF / motion 0x1CA (stage 2 scene 23)
struct Prop44s6Task : ActorTask {
    float pos[3] = {};      // +0x19C
    int yaw = 0;            // +0x1D0
    int spin = 0;           // +0x1DC
    int rot64[3] = {};      // +0x64 / +0x68 / +0x6C
    const int* Rot64() const override { return rot64; }
    int16_t model = 0;      // +0x28C
    int16_t kind = 0;       // +0x290
    int trigger = 0;        // +0x2A0
    int kill = 0;           // +0x2A4
    int frame2A8 = 0;       // +0x2A8
    float amp = 1.0f;       // +0x2C0
    PropAnim anim;          // +0x324 {0xD, 0x1C7, +0x32C}
};

// 0x00474470
void TaskProp44s6(Task& t) {
    World::Impl& w = *CurrentWorld();
    Prop44s6Task& k = t.As<Prop44s6Task>();
    uint8_t* fl = w.runner->flags;
    if (fl[k.kill & 0xFF] == 1) {
        if (k.objMesh != -1) {
            k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        }
        w.ts.KillCurrent();
    }
    if (w.cfg.eventMode == 1 && fl[0x77] != 0) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
    bool skipLogic = false;
    if (fl[k.trigger & 0xFF] == 1 && fl[0x63] == 0) {
        const int i = k.frame2A8;
        if (0x3B < i) {
            skipLogic = true;
        } else {
            const uint32_t tab = w.exe.U32(0x005960CC);  // (u16 swing angles per frame)
            const int u = w.exe.S16(tab + 2 * static_cast<uint32_t>(i)) & 0xFFFF;
            const int a = static_cast<int>(static_cast<float>(u) * k.amp);
            k.rot64[1] = k.spin < 1 ? -a : a;  // __ftol
            k.frame2A8 = i + 1;
        }
    }
    if (!skipLogic && fl[0x63] == 1) {
        int f = k.anim.frame;
        if (f == 0) k.model = 0x170;
        if (f < w.MotionTicks(k.anim.motion) - 2) {
            k.anim.frame = ++f;
            const int s = k.spin;
            if (s == 0 && !(k.flags34 & 0x40000000)) {
                k.spin = 0x100;
                k.flags34 |= 0x40000000;
            } else {
                k.yaw += s;
                k.spin = static_cast<int>(static_cast<float>(s) * F32(w.exe, 0x00564534));  // __ftol
            }
        }
        if (fl[0x64] == 1 && !(k.flags34 & 0x4000000)) {
            k.anim.motion = 0x1C7;
            k.anim.frame = 0;
            k.flags34 |= 0x4000000;
        }
    }
    if (w.MotionResident(k.anim.motion)) {
        Mat4 m = w.cam.view;  // (on the matrix stack: the poses and +0x150 are model-view)
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.yaw);
        mx::RotZ(m, k.rot64[2]);
        mx::RotY(m, k.rot64[1]);
        mx::RotX(m, k.rot64[0]);
        auto pose = [&](int skel, int motion) {  // 0x0040DFA0
            k.anim.skeleton = skel;
            k.anim.motion = motion;
            const Motion* mo = w.PropMotion(motion);
            if (!mo) return;
            for (const PropDraw& d : PropPose(w.PropSkel(skel), *mo, k.anim, m, -1, true)) {
                if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                w.Draw(d.model, d.world, 8, -1.0f, d.alt, -1, true);
            }
        };
        if (fl[0x63] == 0) {
            mx::RotY(m, 0x8000);
            mx::Translate(m, Bits(0xC12E07A6), 0.0f, 0.0f);
            w.Draw(k.model, m, 8, -1.0f, true, -1, true);  // 0x004185E0
            k.m150 = m;
        } else if (fl[0x63] == 1) {
            mx::RotY(m, 0x8000);
            if (fl[0x64] == 0) {
                w.Draw(k.model, m, 8, -1.0f, false, -1, true);
                if (k.model < 0x174) ++k.model;
                k.m150 = m;
                pose(0xD, 0x1C7);
                pose(0xE, 0x1C8);
            } else {
                pose(0xF, 0x1CA);
                k.m150 = m;
            }
        }
    }
    if (k.objMesh != -1) w.RegisterTarget(&k);  // 0x00405160 (+0x150 made world)
}

// Actor type 0x44 state 7 (0x00473170 -> task 0x00474770): prop skeleton 0xF / motion 0x1CA with every node drawn as
// the record's model (0x0040E010), played to ticks - 2 while its trigger flag is set; collision-mesh target (stage 2
// scene 17)
struct Prop44s7Task : ActorTask {
    float pos[3] = {};            // +0x19C
    float scale[3] = { 1, 1, 1 }; // +0x1A8
    int yaw = 0;                  // +0x1D0
    int16_t model = 0;            // +0x28C
    int trigger = 0, kill = 0;    // +0x2A0 / +0x2A4
    PropAnim anim;                // +0x324
};

void TaskProp44s7(Task& t) {
    World::Impl& w = *CurrentWorld();
    Prop44s7Task& k = t.As<Prop44s7Task>();
    const uint8_t* fl = w.runner->flags;
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (fl[k.kill & 0xFF] == 1) {
        if (k.objMesh != -1) destroy();
        w.ts.KillCurrent();
    }
    if (w.cfg.eventMode == 1 && fl[0x77] != 0) destroy();
    if (fl[k.trigger & 0xFF] == 1 && k.anim.frame < w.MotionTicks(k.anim.motion) - 2) ++k.anim.frame;
    if (w.MotionResident(k.anim.motion)) {
        Mat4 m = w.cam.view;
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.yaw);
        mx::RotZ(m, 0);
        mx::RotY(m, 0);
        mx::RotX(m, 0);
        mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
        if (const Motion* mo = w.PropMotion(k.anim.motion)) {
            for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m, k.model)) {  // 0x0040E010
                if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                w.Draw(d.model, d.world, 8, -1.0f, d.alt, -1, true);
            }
        }
        k.m150 = m;
    }
    if (k.objMesh != -1) w.RegisterTarget(&k);  // 0x00405160
}

// Actor type 0x44 state 9 (0x00473300 -> task 0x00474EA0): the record's model (P s16 4), rising by 0.8 a frame up to
// y 10 while its trigger flag (P s8 0x20) is set; gone with its kill flag (P s8 0x21) (stage 3 scene 0)
struct Prop44s9Task : ActorTask {
    float pos[3] = {};  // +0x19C
    int16_t model = 0;  // +0x28C
    int trigger = 0;    // +0x2A0
    int kill = 0;       // +0x2A4
};

void TaskProp44s9(Task& t) {
    World::Impl& w = *CurrentWorld();
    Prop44s9Task& k = t.As<Prop44s9Task>();
    if (w.runner->flags[k.kill & 0xFF] == 1) w.ts.KillCurrent();  // Task_Kill
    if (w.runner->flags[k.trigger & 0xFF] == 1 && k.pos[1] < 10.0f) k.pos[1] = k.pos[1] + F32(w.exe, 0x004C43A8);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    w.Draw(k.model, m);
}

// Behaviour 41 (0x0045BE30): walk with the list motion to the list point {x, y, z, s16 motion, s16 start} (within 5),
// then the next list entry (behaviour 35) or the end of the script
void EnemyBeh41(World::Impl& w, EnemyTask& e) {
    auto s16 = [&](uint32_t a) { return MemS16(w, a); };
    if (e.sub == 0 || e.sub == 1) {
        const uint32_t l = static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1));  // 0x0045CA10
        for (int i = 0; i < 3; ++i) {
            uint32_t u = 0;
            w.mem.Read(l + 4 * i, &u);
            memcpy(&e.jumpStart[i], &u, 4);  // +0x13C0
        }
        const int m = s16(l + 12);
        if (e.anim.motion[0] != m) w.AnimBlendTo(e, m, s16(l + 14), e.sub == 0 ? 0 : 10);
        e.headPitch = m;  // +0x1320
        e.sub = 2;
        e.list1398 = l + 16;
    } else if (e.sub == 2) {
        const float dz = e.pos[2] - e.jumpStart[2], dx = e.pos[0] - e.jumpStart[0];
        if (static_cast<float>(std::sqrt(static_cast<double>(dz * dz + dx * dx))) <= 5.0f) {  // 0x0055D2B4
            if (s16(e.list1398) < 1) {
                w.EnemyScriptEnd(e);
            } else {
                e.behaviour = 0x23;
                e.sub = 1;
            }
        } else {
            w.EnemyTurnTo(e, e.jumpStart[0], e.jumpStart[2], 0x1A0);  // 0x00409F90
        }
    }
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.headPitch) w.AnimBlendTo(e, e.headPitch, 0, 10);
}

// Behaviour 19 (0x00457620): hidden (P byte 0xC == 0: attachments off, flags 0x90100, not drawn) with motion P word 1
// until the camera tick P s16 8 (context 0, or context 2 0x009A6458: taken as context 0), then shown; with P byte 0xD
// and a target (0x00455DE0): after P s16 0xE frames behaviour 3 (flag 0x1368 | 1, +0x133C = P s16 0x10); else
// behaviour P byte 3
void EnemyBeh19(World::Impl& w, EnemyTask& e) {
    const SpawnRecord& r = e.record;
    switch (e.sub) {
    case 0:
        if (r.ParamS8(0xC) == 0) {
            e.anim.attachHidden = true;  // 0x00409D10(anim, 0)
            e.flags34 |= 0x90100;
            e.anim.drawFlags &= ~1u;
        }
        ++e.sub;
        [[fallthrough]];
    case 1:
        if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.P(1)) w.AnimSetMotion(e, e.P(1));  // 0x00411930
        if (w.runner->camera.tick != r.ParamS16(8)) return;
        ++e.sub;
        [[fallthrough]];
    case 2:
        e.anim.attachHidden = false;  // 0x00409D10(anim, 1)
        if (r.ParamS8(0xC) == 0) {
            e.anim.drawFlags |= 1;
            e.flags34 &= 0xFFF6FEFFu;
        }
        if (r.ParamS8(0xD) == 0 || w.EnemyPickTarget(e) == 0) {
            e.behaviour = r.ParamS8(3);
            e.sub = 0;
            return;
        }
        e.counter1330 = r.ParamS16(0xE);
        ++e.sub;
        [[fallthrough]];
    case 3:
        if (--e.counter1330 < 1) {
            e.flags1368 |= 1;
            e.cooldown133C = r.ParamS16(0x10);
            e.behaviour = 3;
            e.sub = 0;
        }
        return;
    default:
        return;
    }
}

}  // namespace

// Enemy behaviours first met on the alternate routes (false: not ported)
bool World::Impl::EnemyBehaviourRoutes(EnemyTask& e) {
    switch (e.behaviour) {
    case 41: EnemyBeh41(*this, e); return true;
    case 19: EnemyBeh19(*this, e); return true;
    default: return false;
    }
}

// Spawned actors of the alternate routes (called from TaskSpawn before the scripted / scene-1 chain).
bool World::Impl::SpawnRoutes(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    if (SpawnFishSchool(t, s)) return true;  // actor 0x42 (world_routes_fish.cpp)
    if (r.type == 0x11) {  // 0x0043A080
        SpawnActor11(*this, t, r);
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 6) {  // generic ctor 0x00461CF0 case 6 -> 0x004668A0
        Komono6Task* k = NewTask<Komono6Task>(TaskKomono6, 0x004668A0);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->model = 0x1032;
        k->life = 1;
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 10) {  // generic ctor 0x00461CF0 case 10 -> 0x004668A0 (as kind 6)
        Komono6Task* k = NewTask<Komono6Task>(TaskKomono6, 0x004668A0);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->model = 0x10C4;
        k->life = 2;
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 24) {  // 0x00463160: 20 chain links 0x00469510 (none for slot 1 outside type 1)
        const int8_t g = r.ParamS8(0);  // spawn +0x1F4
        if (runner->cfg.gameType == 1 || g != 1) {
            for (int i = 0; i < 20; ++i) {
                ChainLinkTask* k = NewTask<ChainLinkTask>(TaskChainLink, 0x00469510);
                k->group = g;
                if (g * 20 + i >= 0 && g * 20 + i < 80) Routes(*this).chain[g * 20 + i] = k;
                memcpy(k->base, r.pos, 12);
                k->life = static_cast<int16_t>(r.state);
                k->idx = static_cast<int8_t>(i);
                k->area = static_cast<int8_t>(runner->variant);
                k->areaCount = 0;
                k->radius = 2.0f;
                k->yaw = static_cast<int16_t>(i << 14);
            }
        }
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 16) {  // 0x00462FE0: six objects from the table 0x00593D20
        for (uint32_t e = 0x00593D20; e < 0x00593D38; e += 4) {
            Komono16Task* k = NewTask<Komono16Task>(TaskKomono16, 0x00468640);
            k->flags34 = 0x80000001u;
            k->pos[1] = -1.9f;  // 0xBFF33333
            k->pos[0] = static_cast<float>(exe.S16(e)) * F32(exe, 0x0055D230);
            k->pos[2] = static_cast<float>(exe.S16(e + 2)) * F32(exe, 0x0055D230);
            k->rot[1] = Rand(0x0046304B) & 0xFFFF;
            k->hp = 2;
            k->model = 0xA50;
            k->life = static_cast<int8_t>(r.state);
            k->area = static_cast<int8_t>(runner->variant);
            k->areaCount = 0;
            k->radius = 8.0f;
        }
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 26) {  // 0x00463230
        Komono26Task* k = NewTask<Komono26Task>(TaskKomono26, 0x00469C80);
        k->life = static_cast<int8_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 59) {  // 0x00461CF0 case 0x3B -> 0x0046F750
        Komono27Task* k = NewTask<Komono27Task>(TaskKomono59, 0x0046F750);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->radius = static_cast<float>(r.ParamS8(0)) * F32(exe, 0x0055D230);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 32) {  // 0x00461CF0 (default) -> 0x0046A360
        Komono32Task* k = NewTask<Komono32Task>(TaskKomono32, 0x0046A360);
        memcpy(k->pos, r.pos, 12);
        k->bar = 0;  // (case 0x20: +0x1CC = 0, +0x1D0 = 0x4000, +0x1E8 = 0x8000)
        k->door1 = 0x4000;
        k->door2 = 0x8000;
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 36) {  // 0x00461CF0 case 0x24 -> 0x0046B480
        Komono36Task* k = NewTask<Komono36Task>(TaskKomono36, 0x0046B480);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->flags34 = 0x80000001u;
        for (int i = 0; i < 3; ++i) {
            k->spot[i][0] = static_cast<float>(Rand(0x00462182) % 9);
            k->spot[i][1] = static_cast<float>(Rand(0x0046219A) % 0xB);
            k->scale[i] = static_cast<float>(Rand(0x004621B1) % 0x29) * F32(exe, 0x004D5464) + F32(exe, 0x004C43A8);
        }
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 64) {  // 0x00461CF0 case 0x40 -> 0x0046FBE0
        Komono64Task* k = NewTask<Komono64Task>(TaskKomono64, 0x0046FBE0);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->rot[2] = 0x400;
        k->step = -32;
        k->accel = -2;
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 42) {  // 0x004639D0 -> 0x0046CE80
        NewTask<Komono47Task>(TaskKomono42, 0x0046CE80);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 47) {  // 0x00463AE0 -> 0x0046DD40
        Komono47Task* k = NewTask<Komono47Task>(TaskKomono47, 0x0046DD40);
        memcpy(k->pos, r.pos, 12);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 13) {  // 0x00461CF0 case 0xD -> 0x00467F50
        Komono13Task* k = NewTask<Komono13Task>(TaskKomono13, 0x00467F50);
        memcpy(k->pos, r.pos, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->model = 0x1A4A;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 29) {  // 0x00463270: nine pieces -> 0x0046A030
        for (int i = 0; i < 9; ++i) {
            Komono27Task* k = NewTask<Komono27Task>(TaskKomono29, 0x0046A030);
            k->speed = i;
            for (int c = 0; c < 3; ++c) k->pos[c] = F32(exe, 0x00593D38 + 12 * static_cast<uint32_t>(i) + 4 * static_cast<uint32_t>(c));
            k->area = static_cast<int8_t>(runner->variant);
            k->areaCount = 0;
            k->life = static_cast<int16_t>(r.state);
        }
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 11) {  // 0x00461CF0 case 0xB -> 0x00467C80
        Komono11Task* k = NewTask<Komono11Task>(TaskKomono11, 0x00467C80);
        memcpy(k->pos, r.pos, 12);
        k->ax = r.rot[0];
        k->yaw = r.rot[1];
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->radius = 2.0f;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 25) {  // 0x00461CF0 case 0x19 -> 0x00469AE0
        Komono27Task* k = NewTask<Komono27Task>(TaskKomono25, 0x00469AE0);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->radius = 12.0f;
        k->anim.skeleton = 10;
        k->anim.motion = 0x1C9;
        runner->camera.routeBranch = 0;  // 0x009C88A4
        ++attackers;                     // 0x009C7006
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && (r.ParamS8(1) == 27 || r.ParamS8(1) == 28)) {  // 0x00461CF0 cases 0x1B / 0x1C
        const bool k27 = r.ParamS8(1) == 27;
        Komono27Task* k = NewTask<Komono27Task>(k27 ? TaskKomono27 : TaskKomono28, k27 ? 0x00469E60 : 0x00469F50);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->anim.skeleton = k27 ? 7 : 10;
        k->anim.motion = k27 ? 0x1D5 : 0x1C9;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 5) {  // generic ctor 0x00461CF0 (default) -> 0x00466820
        Komono5Task* k = NewTask<Komono5Task>(TaskKomono5, 0x00466820);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->model = static_cast<int16_t>(r.state);
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 49) {  // generic ctor 0x00461CF0 case 0x31 -> 0x0046E6E0
        Komono49Task* k = NewTask<Komono49Task>(TaskKomono49, 0x0046E6E0);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->pivot[0] = static_cast<float>(exe.S16(0x00594A68)) * F32(exe, 0x0055D2B0) + k->pos[0];
        k->pivot[1] = k->pos[1];
        k->pivot[2] = static_cast<float>(exe.S16(0x00594A6A)) * F32(exe, 0x0055D2B0) + k->pos[2];
        k->corner = 0;
        k->radius = 5.0f;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x44 && r.state == 5) {  // 0x00472F80 -> 0x00474240
        Prop44s5Task* k = NewTask<Prop44s5Task>(TaskProp44s5, 0x00474240);
        memcpy(k->pos, r.pos, 12);
        k->yaw = r.rot[1];
        k->flags34 = 0x51;
        HangingProp& h = k->hang;  // the block the hanging task copies (0x00472BD0's fields, phase 0)
        h.Init(r);
        h.phase = 0;
        k->anim.skeleton = 0xC;
        k->anim.motion = 0x1CE;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x44 && r.state == 7) {  // 0x00473170 -> 0x00474770
        Prop44s7Task* k = NewTask<Prop44s7Task>(TaskProp44s7, 0x00474770);
        memcpy(k->pos, r.pos, 12);
        k->yaw = r.rot[1];
        k->flags34 = 0x51;
        k->model = r.ParamS16(4);
        k->objMesh = r.ParamS32(8);
        k->trigger = r.ParamS8(0x10);
        k->kill = r.ParamS8(0x11);
        for (int i = 0; i < 3; ++i) memcpy(&k->scale[i], &r.params[0x14 + 4 * i], 4);
        k->anim.skeleton = 0xF;
        k->anim.motion = 0x1CA;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x44 && r.state == 9) {  // 0x00473300 -> 0x00474EA0
        Prop44s9Task* k = NewTask<Prop44s9Task>(TaskProp44s9, 0x00474EA0);
        memcpy(k->pos, r.pos, 12);
        k->trigger = r.ParamS8(0x20);
        k->kill = r.ParamS8(0x21);
        k->model = r.ParamS16(4);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x44 && r.state == 6) {  // 0x00473060 -> 0x00474470
        Prop44s6Task* k = NewTask<Prop44s6Task>(TaskProp44s6, 0x00474470);
        memcpy(k->pos, r.pos, 12);
        k->yaw = r.rot[1];
        k->flags34 = 0x51;
        k->model = r.ParamS16(4);
        k->objMesh = r.ParamS32(8);
        k->spin = r.ParamS32(0xC);
        k->kind = r.ParamS16(0);
        k->trigger = r.ParamS8(0x10);
        k->kill = r.ParamS8(0x11);
        // +0x2AC = P s8 0x12, scale P 0x14.. (+0x1A8; unused by the task)
        k->amp = 1.0f;
        k->anim.skeleton = 0xD;
        k->anim.motion = 0x1C7;
        runner->flags[0] = 0;  // 0x009C7200[spawn +0x1330 / +0x1334] (0 for an op-0x0B actor): event flag 0
        runner->flags[0x63] = 0;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x44 && r.state == 8) {  // 0x00473260 -> 0x004748C0
        Prop44s8Task* k = NewTask<Prop44s8Task>(TaskProp44s8, 0x004748C0);
        memcpy(k->pos, r.pos, 12);
        k->yaw = r.rot[1];
        k->anim.skeleton = 0x10;
        k->anim.motion = 0x1D3;
        k->anim.frame = 0;
        k->anim.prevFrame = 0;
        k->flag = r.ParamS8(0x10);
        for (int i = 0; i < 3; ++i) memcpy(&k->scale[i], &r.params[0x14 + 4 * i], 4);
        k->life = r.ParamS8(0x11);
        k->area = 0;  // +0x1BB (zeroed block, not the current area)
        k->areaCount = 0;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 33) {  // generic ctor 0x00461CF0 case 0x21 -> 0x00472950
        Komono33Task* k = NewTask<Komono33Task>(TaskKomono33, 0x00472950);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->model = static_cast<int16_t>(r.state);
        k->last = r.rot[2];
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 0) {  // 0x00462A80
        SpawnBarrels(*this, r);
        ts.KillCurrent();
        return true;
    }
    return false;
}

bool TypingDrillFinishFrog(ActorTask& actor) {
    const auto* world = CurrentWorld();
    if (!world || !world->cfg.portTyping || world->cfg.eventMode != 7 || world->cfg.gameType != 2) return false;
    auto* frog = dynamic_cast<Actor11Task*>(&actor);
    if (!frog) return false;
    // PC4A7493: kind1B stores its finish state in the private control block.
    if (frog->c.state != 8) {
        frog->c.state = 8;
        frog->c.sub = 0;
    }
    return true;
}

}  // namespace game
