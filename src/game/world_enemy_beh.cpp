// game::World - enemy behaviours of the later stage-1 scenes (table 0x00592AE8) and their effects.
// CONFIRMED = code read + frame-exact against build\scene_capture_play2scene1.txt; CODE = read, not exercised.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

// Splash ring 0x00456880 (created by 0x004567C0): model 0xE23 growing and fading for 60 frames
struct SplashRingTask : WTask {
    float pos[3] = {};
    float scale = 0;      // +0x118
    float grow = 0.02f;   // +0x1370
    float alpha = 1.0f;   // +0x1374
    int counter = 0;      // +0x1330
};

void TaskSplashRing(Task& t) {
    World::Impl& w = *CurrentWorld();
    SplashRingTask& s = t.As<SplashRingTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::Scale(m, s.scale, 0.2f, s.scale);  // 0x3E4CCCCD
    w.Draw(0xE23, m, 8, s.alpha);           // 0x004185A0(model, alpha)
    s.scale = s.grow + s.scale;
    ++s.counter;
    float k = 0;
    w.exe.Read(0x0055DCF4, &k, 4);
    s.alpha = s.alpha - k;
    if (s.counter >= 0x3C) w.ts.KillCurrent();
}

// Flying head 0x0040A230 (created by 0x0040A130 on a decapitation, block 0x1A8)
struct HeadTask : WTask {
    float pos[3] = {};         // +0x40
    float vel[3] = {};         // +0x4C
    float gravity = 0;         // +0x5C
    int rx = 0, ry = 0;        // +0x64 / +0x68
    int16_t state = 0;         // +0x194
    int timer = 0;             // +0x198
    int spinX = 0, spinY = 0;  // +0x19C (added to +0x68) / +0x1A0 (added to +0x64)
    int model = 0;             // +0x1A4
    float scale = 1.0f;        // +0x118
};

void TaskHead(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeadTask& h = t.As<HeadTask>();
    auto F = [&](uint32_t va) {
        float f = 0;
        w.exe.Read(va, &f, 4);
        return f;
    };
    bool move = true;
    if (h.state == 0) {
        const uint32_t g = 0xBCA740DA;
        memcpy(&h.gravity, &g, 4);
        h.vel[1] = static_cast<float>(w.Rand(0x0040A266) % 20 + 1) * F(0x004C4CC0) + F(0x004C4D10);
        Mat4 m = Mat4::Identity();
        mx::RotY(m, w.cam.yaw);  // 0x009A60D0[active context]
        const float l[3] = { 0.0f, 0.0f, -0.2f };
        float o[3];
        mx::Transform(m, l, o);
        h.vel[0] = o[0];
        h.vel[2] = o[2];
        const int s = 1 - 2 * (w.Rand(0x0040A2F7) & 1);
        h.spinX = s * ((w.Rand(0x0040A311) & 0x7FF) + 0x800);
        h.spinY = (w.Rand(0x0040A332) & 0x7FF) + 0x800;
        h.state = 1;
    }
    if (h.state == 1) {
        int mat = 0;
        const float floor = w.FloorHeight(h.pos[0], h.pos[1], h.pos[2], &mat);  // 0x00409D40
        if (h.pos[1] + h.vel[1] <= floor) {
            h.vel[1] = h.vel[1] * F(0x004C4D0C);  // -0.25
            h.pos[1] = floor;
            if (h.model != 0x2015 && h.model != 0x1DC1) {
                const float bp[3] = { h.pos[0], floor, h.pos[2] };
                w.SpawnBloodPoolAt(bp, h.ry, 0.25f);  // 0x00408370
                w.Sound((w.cac40 == 0x37 || w.cac40 == 5) ? 0x4416A9u : 0x2616A9u);
            } else {
                w.Sound(0x1116A9);
            }
            float v = h.vel[1];
            if (v < 0.0f) v = v * -1.0f;
            if (v <= F(0x004C4D08)) {  // 0.15: settle
                h.pos[1] = floor;
                const float bp[3] = { h.pos[0], floor, h.pos[2] };
                h.gravity = 0;
                h.vel[0] = h.vel[1] = h.vel[2] = 0;
                w.SpawnBloodPoolAt(bp, h.ry, 0.5f);
                h.state = 2;
                h.timer = 0x78;
            }
        }
    }
    if (h.state == 2 && move) {
        h.pos[1] = h.pos[1] - F(0x004C4D04);  // 0.04
        h.spinX -= h.spinX / 10;
        h.spinY -= h.spinY / 10;
        if (--h.timer <= 0) w.ts.KillCurrent();
    }
    h.vel[1] = h.gravity + h.vel[1];
    h.ry += h.spinX;
    h.rx += h.spinY;
    h.pos[0] = h.vel[0] + h.pos[0];
    h.pos[1] = h.vel[1] + h.pos[1];
    h.pos[2] = h.vel[2] + h.pos[2];
    Mat4 m = Mat4::Identity();
    mx::Translate(m, h.pos[0], h.pos[1], h.pos[2]);
    mx::RotZ(m, 0);
    mx::RotY(m, h.ry);
    mx::RotX(m, h.rx);
    mx::Scale(m, h.scale, h.scale, h.scale);
    w.Draw(h.model, m);
}

}  // namespace

// 0x0040A130(enemy, model of node 2): the head flies from node 2's view-space point (through the camera matrix)
// +0x118: 1, or with item 0x15 in game type 1 (0x009C88A8 == 1) 2 (kind 0xE: 1.8)
static float FlyingHeadScale(World::Impl& w, const EnemyTask& e) {
    if (w.runner->cfg.gameType != 1 || !ItemFlag(w, 0)) return 1.0f;
    if (e.anim.kind != 0xE) return 2.0f;
    const uint32_t u = 0x3FE66666;
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void World::Impl::SpawnFlyingHead(EnemyTask& e) {
    HeadTask* h = NewTask<HeadTask>(TaskHead, 0x0040A230);
    const EnemyNode& n = e.anim.nodes[2];
    mx::Transform(cam.inverse, n.hit, h->pos);
    h->rx = n.cur[0];
    h->ry = n.cur[1];
    h->model = n.model;
    h->scale = FlyingHeadScale(*this, e);
}

// 0x0040A130(enemy, model): as SpawnFlyingHead with the given model
void World::Impl::SpawnFlyingHeadModel(EnemyTask& e, int model) {
    HeadTask* h = NewTask<HeadTask>(TaskHead, 0x0040A230);
    const EnemyNode& n = e.anim.nodes[2];
    mx::Transform(cam.inverse, n.hit, h->pos);
    h->rx = n.cur[0];
    h->ry = n.cur[1];
    h->model = model;
    h->scale = FlyingHeadScale(*this, e);
}

// 0x004567C0(pos, base scale)
void SpawnSplashRing(World::Impl& w, const float pos[3], float base) {
    SplashRingTask* s = w.NewTask<SplashRingTask>(TaskSplashRing, 0x00456880);
    memcpy(s->pos, pos, 12);
    const int sign = w.Rand(0x00456812) & 1;
    const int r = w.Rand(0x00456838);
    s->scale = static_cast<float>(r % 3) * 0.1f * static_cast<float>(1 - 2 * sign) + base;  // 0x004C4CC8
}
void World::Impl::SpawnSplashRing(const float pos[3], float base) { game::SpawnSplashRing(*this, pos, base); }

static float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

static int16_t EvS16(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a & ~3u, &w);
    return static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
}

// 0x0045CA10: the motion list of a scripted behaviour (P[8] while running behaviour P byte 3, else P[4])
static uint32_t ScriptList(const EnemyTask& e) {
    return static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1));
}

// 0x0045CA30(&flags, tick): -3 -> 0x100; -2 -> (flags & ~0x2000) | 0x400; else 0x2400
static void ScriptFlags(uint32_t& f, int v) {
    if (v == -3) f |= 0x100;
    else if (v != -2) f |= 0x2400;
    else f = (f & ~0x2000u) | 0x400;
}

// 0x0045C8D0(enemy, params): the scripted part ends: behaviour 1 when it was P byte 3, else P byte 3
void World::Impl::EnemyScriptEnd(EnemyTask& e) {
    if (e.behaviour == e.record.ParamS8(3)) e.behaviour = 1;
    else e.behaviour = e.record.ParamS8(3);
    switch (e.behaviour) {
    case 0x22:
        if (!EnemyParentDead(e)) e.sub = 1;
        break;
    case 0x23:
    case 0x24:
        e.list1398 = ScriptList(e);
        e.sub = 1;
        break;
    case 0x28: {
        const uint32_t l = ScriptList(e);
        float pt[3];
        for (int i = 0; i < 3; ++i) {
            uint32_t u = 0;
            mem.Read(l + 4 * i, &u);
            memcpy(&pt[i], &u, 4);
        }
        if (EnemyPointBehind(e, pt)) {
            e.behaviour = 1;
            e.sub = 0;
        } else {
            e.sub = 1;
        }
        break;
    }
    case 0x29:
        e.sub = 1;
        if (e.P(3) != -1) e.flags34 |= 0x10000;
        break;
    default:
        e.sub = 0;
        break;
    }
    if (e.behaviour == 1 && e.P(3) != -1) {
        Note("enemy script end with P[12] != -1 (behaviour 0x2A) not ported");
        e.behaviour = 0x2A;
        e.flags34 |= 0x10000;
    }
    if (e.behaviour != 0x22) e.flags34 &= ~0x2500u;  // 0xFFFFDAFF
}

// 0x0045C8A0(enemy, civilian): the civilian is dead (+0x34 & 0x4000000) -> 0x0045C8D0, true
bool World::Impl::EnemyParentDead(EnemyTask& e) {
    if (e.parent && (e.parent->flags34 & 0x4000000)) {
        EnemyScriptEnd(e);
        return true;
    }
    return false;
}

// Behaviour 34 (0x0045A890): walk to the civilian until within list distance (float P list +0), then behaviour 35
void World::Impl::EnemyBeh34(EnemyTask& e) {
    EnemyTask* civ = e.parent;
    bool setup = false;
    if (e.sub == 0) {
        setup = true;
        e.flags34 |= 0x400;
    } else if (e.sub == 1) {
        setup = true;
        e.flags34 |= 0x400;
    } else if (e.sub == 2 && civ) {
        const float dz = e.pos[2] - civ->pos[2], dx = e.pos[0] - civ->pos[0];
        if (std::sqrt(dz * dz + dx * dx) <= e.walkDistance) {
            if (EvS16(mem, e.list1398) < 1) {
                EnemyScriptEnd(e);
            } else {
                e.behaviour = 0x23;
                e.sub = 1;
                if (e.facing1350 != 0 && civ->civilian) static_cast<CivTask*>(civ)->c.flags |= 0x800;
            }
        } else {
            EnemyTurnTo(e, civ->pos[0], civ->pos[2], 0x1A0);  // 0x00409F90
        }
    }
    if (setup) {
        const uint32_t l = ScriptList(e);
        uint32_t d = 0;
        mem.Read(l, &d);
        memcpy(&e.walkDistance, &d, 4);                                // +0x1370
        e.facing1350 = static_cast<uint16_t>(EvS16(mem, l + 4));       // +0x1350
        const int m = static_cast<uint16_t>(EvS16(mem, l + 6));
        if (e.sub == 0) AnimBlendTo(e, m, EvS16(mem, l + 8), 0);
        else if (e.anim.motion[0] != m) AnimBlendTo(e, m, EvS16(mem, l + 8), 10);
        e.headPitch = e.anim.motion[0];  // +0x1320
        e.list1398 = l + 10;
        e.sub = 2;
    }
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.headPitch) AnimBlendTo(e, e.headPitch, 0, 10);
    if (EnemyParentDead(e) && e.behaviour != 0x28) {
        e.f1354 = e.behaviour;
        e.attackLimit = e.sub;  // +0x1358
        e.behaviour = 0x2D;
        e.sub = 0;
    }
}

// Behaviour 35 (0x0045AAA0): play the motion list {motion, start, loops, attack tick} (8 bytes, motion < 1 ends);
// at the attack tick the civilian dies (+0x34 | 0x4000000, sound 0x0045CBD0)
void World::Impl::EnemyBeh35(EnemyTask& e) {
    EnemyTask* civ = e.parent;
    auto motionSetup = [&](int m) {
        switch (m) {
        case 0xB2: case 0xB7: {  // a jump 25 units ahead (yaw +0x68) onto the floor there (CODE)
            e.flags136C |= 0x100002;
            // x87 at 24-bit precision: the angle product and the mul / add round to float; fsin / fcos do not
            const double a = static_cast<float>(static_cast<double>(e.rot[1]) * 9.587379924285257e-05);
            const float d = F32(exe, 0x004E30E8);
            e.jumpTarget[0] = static_cast<float>(std::sin(a) * d) + e.pos[0];
            int mat = 0;
            e.jumpTarget[1] = FloorHeight(e.pos[0], e.pos[1] + F32(exe, 0x004C43B0), e.pos[2], &mat);  // 0x00409D40
            e.jumpTarget[2] = static_cast<float>(std::cos(a) * d) + e.pos[2];
            break;
        }
        case 0x1A3: case 0x1A7: e.flags1368 |= 0x10; break;
        case 0x1AB: e.flags1368 |= 8; break;
        default: break;
        }
    };
    if (e.sub == 0 || e.sub == 1) {
        const bool first = e.sub == 0;
        const uint32_t l = first ? ScriptList(e) : e.list1398;
        const int m = EvS16(mem, l), start = EvS16(mem, l + 2), loops = EvS16(mem, l + 4), tick = EvS16(mem, l + 6);
        ScriptFlags(e.flags34, tick);
        if (first) AnimBlendTo(e, m, start, 0);
        else if (e.anim.motion[0] != m) AnimBlendTo(e, m, start, 10);
        e.headPitch = m;     // +0x1320
        e.facing1350 = loops;  // +0x1350
        e.f1354 = tick;      // +0x1354
        e.list1398 = l + 8;
        if (first) {
            motionSetup(e.anim.motion[0]);
            if (e.anim.motion[0] == 0x234) e.flags1368 |= 0x80;
            if (e.anim.motion[0] == 0x277) e.flags1368 |= 0x40;
        } else {
            motionSetup(e.anim.motion[0]);
        }
        if (e.f1354 < 0) {
            e.flags34 &= ~0x10000000u;
        } else {
            EnemyVoice(e, 3);  // 0x0040A6F0
            e.flags34 |= 0x10000000;
        }
        e.sub = first ? 2 : static_cast<int16_t>(e.sub + 1);
    } else if (e.sub == 2) {
        if (civ && e.anim.tick[0] == e.f1354 && !(e.anim.flags[0] & 1) && !(civ->flags34 & 0x4000000)) {
            civ->flags34 |= 0x4000000;  // (+0x1CB = anim +0x37 bit 0: not blending)
            // 0x0045CBD0: kill sound by motion / kind
            const int m = e.anim.motion[0];
            if (m == 0xFE || m == 0xFF || m == 0x3CB) Sound(0x1D16A9);
            else if (m != 0x1A3 && m != 0x1A7 && m != 0x1A9 && m != 0x1AB) {
                const int k = e.anim.kind;
                Sound(k == 2 ? 0x516A9u : k == 0xD ? 0x1B16A9u : (k == 0xE || k == 0x13 || k == 0x14) ? 0x316A9u : 0x1A16A9u);
            }
        }
        const int m = e.anim.motion[0];
        if ((m != 0x1AB && m != 0x1A3 && m != 0x1A7) || !EnemyParentDead(e)) {
            const int m2 = e.anim.motion[0];
            if (e.anim.tick[0] == MotionTicks(m2) - 1) {
                if (m2 == 0xB2 || m2 == 0xB7) e.flags136C &= ~0x100000u;
                if (!EnemyParentDead(e)) {
                    if (--e.facing1350 == 0) {
                        if (EvS16(mem, e.list1398) < 1) EnemyScriptEnd(e);
                        else e.sub = 1;
                    }
                } else if (e.behaviour != 0x28 && EvS16(mem, e.list1398) == -1) {
                    e.f1354 = e.behaviour;
                    e.attackLimit = e.sub;
                    e.behaviour = 0x2D;
                    e.sub = 0;
                }
            } else if (m2 == 0xB2 && e.anim.tick[0] == 0x16) {  // the landing: explosion type 0 (0x0043FCA0), camera yaw (CODE)
                const int rot[3] = { 0, cam.yaw, 0 };
                SpawnExplosionAt(e.pos, rot, 0, 1.0f);
            } else if (m2 == 0xB8 && (e.anim.tick[0] == 0x15 || e.anim.tick[0] == 0x1B)) {
                int mat = 0;
                float p[3] = { e.pos[0], 0.0f, e.pos[2] };
                p[1] = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);
                if (e.anim.tick[0] == 0x1B) {
                    SpawnSplashRing(p, 1.0f);
                    SpawnSplashRing(p, 0.5f);
                }
                Mat4 mm = Mat4::Identity();
                mx::Translate(mm, p[0], p[1], p[2]);
                mx::RotY(mm, e.rot[1]);
                const float l[3] = { 0.0f, 0.0f, -1.5f };
                float o[3];
                mx::Transform(mm, l, o);
                SpawnEffectAt(o, 0, 0, 0x62, 1, -1);
            }
        }
    }
    // 0x0045B013
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.headPitch) {
        if (e.facing1350 < 2) {
            AnimBlendTo(e, e.headPitch, MotionTicks(e.anim.motion[0]) - 1, 1);
            return;
        }
        AnimBlendTo(e, e.headPitch, 0, 10);
        --e.facing1350;
    }
}

// 0x0045BC10(&rot, &pos, point): the point in the actor's frame (RotY(-ry), RotZ(-rz), RotX(-rx)) has z > 0
bool World::Impl::EnemyPointBehind(const EnemyTask& e, const float pt[3]) {
    Mat4 m = Mat4::Identity();
    mx::RotY(m, -e.rot[1]);
    mx::RotZ(m, -e.rot[2]);
    mx::RotX(m, -e.rot[0]);
    const float d[3] = { pt[0] - e.pos[0], pt[1] - e.pos[1], pt[2] - e.pos[2] };
    float o[3];
    mx::Transform(m, d, o);
    return 0.0f < o[2];
}

// Behaviour 40 (0x0045BCB0): walk with the list motion until the list point {x, y, z, s16 motion, s16 start} is
// behind, then the next entry (behaviour 35) or the end of the script
void World::Impl::EnemyBeh40(EnemyTask& e) {
    if (e.sub == 0 || e.sub == 1) {
        const bool first = e.sub == 0;
        e.flags34 |= 0x2400;
        const uint32_t l = ScriptList(e);
        for (int i = 0; i < 3; ++i) {
            uint32_t u = 0;
            mem.Read(l + 4 * i, &u);
            memcpy(&e.jumpStart[i], &u, 4);  // +0x13C0
        }
        const int m = EvS16(mem, l + 12), start = EvS16(mem, l + 14);
        if (e.anim.motion[0] != m) AnimBlendTo(e, m, start, first ? 0 : 10);
        e.headPitch = m;  // +0x1320
        e.list1398 = l + 16;
        e.sub = first ? 2 : static_cast<int16_t>(e.sub + 1);
    }
    if (EnemyPointBehind(e, e.jumpStart)) {
        int32_t next = 0;
        mem.Read(e.list1398, reinterpret_cast<uint32_t*>(&next));
        if (next < 1) {
            EnemyScriptEnd(e);
        } else if (!EnemyParentDead(e)) {
            e.behaviour = 0x23;
            e.sub = 1;
        }
    }
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.headPitch) AnimBlendTo(e, e.headPitch, 0, 10);
}

// Behaviour 45 (0x0045C7D0): idle rand() % 11 + 10 frames (motion set entry 0), then back to the saved behaviour
void World::Impl::EnemyBeh45(EnemyTask& e) {
    if (e.sub == 0) {
        e.facing1350 = Rand(0x0045C81D) % 0xB + 10;
        const int m = EnemyMotionSet(e, 0);
        if (e.anim.motion[0] != m) AnimBlendTo(e, m, 0, 10);
        ++e.sub;
        e.headPitch = e.anim.motion[0];
    } else if (e.sub == 1 && --e.facing1350 == 0) {
        e.behaviour = static_cast<int16_t>(e.f1354);
        e.sub = static_cast<int16_t>(e.attackLimit);
    }
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.headPitch) AnimBlendTo(e, e.headPitch, 0, 10);
}

// Behaviour 13 (0x00456F50): rise (motion 0xB8 for kinds 0xF..0x11, else 0x3D8) once the camera tick reaches P[1]
// (not shootable before: +0x34 | 0x4100), with water splashes for 0xB8; at the end behaviour 15 with walk length
// (float)(u32) P[2] when P byte 3 is 0xF, else behaviour 1
void World::Impl::EnemyBeh13(EnemyTask& e) {
    if (e.sub == 0) {
        e.flags34 |= 0x4100;
        const int k = e.anim.kind;
        AnimSetMotion(e, (k == 0xF || k == 0x10 || k == 0x11) ? 0xB8 : 0x3D8);
        ++e.sub;
    } else if (e.sub != 1 && e.sub != 2) {
        return;
    }
    if (e.sub == 1) {
        if (static_cast<uint32_t>(runner->camera.tick) < static_cast<uint32_t>(e.P(1))) return;
        ++e.sub;
        e.flags34 &= ~0x4100u;
    }
    if (e.anim.motion[0] == 0xB8 && (e.anim.tick[0] == 0x15 || e.anim.tick[0] == 0x1B)) {
        int mat = 0;
        float p[3] = { e.pos[0], 0.0f, e.pos[2] };
        p[1] = FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);  // 0x004C4C8C
        if (e.anim.tick[0] == 0x1B) {
            SpawnSplashRing(p, 1.0f);
            SpawnSplashRing(p, 0.5f);
        }
        Mat4 m = Mat4::Identity();
        mx::Translate(m, p[0], p[1], p[2]);
        mx::RotY(m, e.rot[1]);
        const float l[3] = { 0.0f, 0.0f, -1.5f };
        float o[3];
        mx::Transform(m, l, o);
        SpawnEffectAt(o, 0, 0, 0x62, 1, -1);  // 0x00407340 (block angles: stack values, faced to the camera)
    }
    if (MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
        if (e.record.ParamS8(3) == 0xF) {
            e.walkDistance = static_cast<float>(static_cast<uint32_t>(e.P(2)));
            e.behaviour = 0xF;
            e.sub = 1;
            return;
        }
        e.behaviour = 1;
        e.sub = 0;
    }
}

}  // namespace game
