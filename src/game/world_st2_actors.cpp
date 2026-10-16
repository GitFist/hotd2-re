// game::World - stage-2 actors (st2evtbl.bin). Each function names the original it ports; CONFIRMED / CODE status and
// the verification are in notes/agents/stage2.md (world_test --stage-start 14659 on build\scene_capture_auto_st2.txt).

#include <cmath>
#include <cstdio>
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

// 0x00401EC0(a, b, num, den): a moved toward b by num / (num + den) of the shorter way
int AngleLerp(int a, int b, int num, int den) {
    const uint32_t d = static_cast<uint32_t>(b - a) & 0xFFFF;
    if (d < 0x8000) return static_cast<int>(d * static_cast<uint32_t>(num)) / (den + num) + a;
    return a - static_cast<int>((0x10000 - d) * static_cast<uint32_t>(num)) / (den + num);
}

// Cam_EvaluateAngles 0x004042D0(path, t): position and the three angle channels (__ftol)
bool EvalPath(World::Impl& w, int path, float t, float pos[3], int ang[3]) {
    float p[3] = {}, tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(path, t, p, tg, &roll, false)) {
        w.Note("camera path " + std::to_string(path) + " not evaluated (0x004042D0)");
        return false;
    }
    memcpy(pos, p, 12);
    for (int k = 0; k < 3; ++k) ang[k] = static_cast<int>(tg[k]);
    return true;
}

// 0x004018E0 on RotZ(r[2]) RotY(r[1]) RotX(r[0]): the angles re-derived from the matrix
void NormalizeRot(const int r[3], int out[3]) {
    MtxStack s;
    s.LoadIdentity();
    s.RotateZ(r[2]);
    s.RotateY(r[1]);
    s.RotateX(r[0]);
    NormalizeAngles(s, &out[0], &out[1], &out[2]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x21 (0x00451720): a zombie riding a vehicle along camera paths (stage 2 opening). Skeleton P byte 0,
// node draw 0x00452090 (= the default node model in game type 0), hp 0x00565F0C[rank]; the vehicle is a second task
// (0x00452120 -> 0x00452150).
// ---------------------------------------------------------------------------------------------------------------------
struct Actor21Task : EnemyTask {
    int pathIdx = 0;          // +0x1350 (0x00565EF4[idx]: camera path)
    int slide = 0;            // +0x1334 (death slide frames)
    int sinkTimer = 0;        // +0x1338
    float delta[3] = {};      // +0x13CC (path step of the last frame)
    int count = 0;            // +0x132C (game type 2: the path time)
};

struct VehicleTask : WTask {  // 0x00452150 / 0x004521B0 / 0x004522A0 (object 0x13F4)
    float pos[3] = {};        // +0x40
    int rot[3] = {};          // +0x64
    int wheels = 1;           // +0x1320 (wheels turning)
    int steerCount = 0;       // +0x1324
    int wheelAngle = 0;       // +0x1330
    int steer = 0;            // +0x1334
    int param = 0;            // +0x1350
    int broken = 0;           // +0x13F0 (model row 0x00565F2C[row * 4])
    int count = 0;            // +0x132C (game type 2: the path time)
};

// 0x00451FF0: skeleton drawn while the position is in front of the camera (view z < 0x004C436C = 0)
void Actor21Draw(World::Impl& w, Actor21Task& a) {
    float v[3];
    mx::Transform(w.cam.view, a.pos, v);
    if (v[2] < F32(w.exe, 0x004C436C)) {  // 0x00411090: skeleton + shadow 0x0040A590 (lights 0x0041DC70 / 0x0041DCC0: renderer)
        w.AnimDraw(a);
        w.EnemyShadow(a);
    }
}

// 0x00451E50: position / angles from the path at the camera tick
void Actor21Path(World::Impl& w, Actor21Task& a) {
    float p[3];
    int ang[3];
    if (!EvalPath(w, w.exe.S16(0x00565EF4 + static_cast<uint32_t>(a.pathIdx) * 2), static_cast<float>(w.runner->camera.tick), p, ang)) return;
    memcpy(a.pos, p, 12);
    memcpy(a.rot, ang, 12);
}

// 0x00451EB0: the same, keeping the step (+0x13CC)
void Actor21PathStep(World::Impl& w, Actor21Task& a) {
    float p[3];
    int ang[3];
    if (!EvalPath(w, w.exe.S16(0x00565EF4 + static_cast<uint32_t>(a.pathIdx) * 2), static_cast<float>(w.runner->camera.tick), p, ang)) return;
    for (int k = 0; k < 3; ++k) a.delta[k] = p[k] - a.pos[k];
    memcpy(a.pos, p, 12);
    memcpy(a.rot, ang, 12);
}

// 0x00451F40: slide on after death (step fades out over frames 11..20), fall to the ground, roll back to 0
void Actor21Slide(World::Impl& w, Actor21Task& a) {
    if (a.slide > 10 && a.slide < 0x15) {
        a.delta[0] = Lerp(a.delta[0], 0.0f, 1, 0x14 - a.slide);
        a.delta[2] = Lerp(a.delta[2], 0.0f, 1, 0x14 - a.slide);
    }
    a.pos[0] = a.delta[0] + a.pos[0];
    a.pos[1] = Lerp(a.pos[1], w.ground, 1, 3);
    a.pos[2] = a.delta[2] + a.pos[2];
    a.rot[2] = AngleLerp(a.rot[2], 0, 1, 1);
    ++a.slide;
}

void TaskActor21(Task& t);

// 0x00451D20: escaped (cam 0x39 tick > 0x121): follows the path until cam 0x39 tick 0x181
void Actor21Escaped(World::Impl& w, Task& t, Actor21Task& a) {
    if (w.runner->flags[0] == 1 || (w.runner->camera.camId == 0x39 && w.runner->camera.tick == 0x181)) {
        w.EnemyDestroy(a);
        return;
    }
    Actor21PathStep(w, a);
    Actor21Draw(w, a);
    ++a.anim.counter[0];
}

// 0x00451980: shootable on the vehicle
void Actor21Alive(World::Impl& w, Task& t, Actor21Task& a) {
    if (w.runner->flags[0] == 1) {
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;
        a.actorSlot = -1;
        --w.liveEnemies;
        --w.attackers;
        w.EnemyDestroy(a);
        return;
    }
    if (a.flags34 & 8) {
        for (int i = 0; i < 16; ++i) {
            EnemyNode& n = a.anim.nodes[i];
            if (!(n.flags & 8)) continue;
            --a.hp;
            w.EnemyVoice(a, 0);  // 0x0040A6F0
            n.flags &= ~8u;
            w.SpawnNodeBlood(a, i);  // 0x00407200
            if (i == 2) {  // head: 120 + the streak, streak + 10
                for (int p = 0; p < 2; ++p) {
                    if (!(a.anim.nodes[2].flags & (2u << p))) continue;
                    World::Impl::Player& P = w.player[p];
                    w.AddScore(p, 0x78);
                    w.AddScore(p, P.streak);
                    P.streak = static_cast<int16_t>(P.streak + 10);
                    ++P.hits;
                }
            } else {
                for (int p = 0; p < 2; ++p) {
                    if (!(n.flags & (2u << p))) continue;
                    World::Impl::Player& P = w.player[p];
                    w.AddScore(p, 10);
                    ++P.hits;
                    P.streak = 0;
                }
            }
            n.flags &= ~6u;
        }
    }
    if (a.hp < 1) {  // killed
        w.runner->camera.routeBranch = 1;  // 0x009C88A4
        w.EnemyVoice(a, 1);
        // 0x009CA0EC / 0x009C89C0[stage] / 0x009C8EC0: counted as a rescue of kind 0x36 (the stage clear shows it)
        {
            ++w.rescueTotal;
            const int st = w.cfg.eventMode >= 0 && w.cfg.eventMode < 10 ? w.cfg.eventMode : 0;
            const int n = w.rescueCount[st]++;
            if (n >= 0 && n < 10) w.rescueKinds[st][n] = 0x36;
        }
        int p;
        if ((a.flags34 & 6) == 2) p = 0;
        else if ((a.flags34 & 6) == 4) p = 1;
        else p = w.Rand(0x00451B40) & 1;  // (both players hit it: the rand at 0x00451B3B)
        w.AddScore(p, 0x50);
        w.AddScore(p, 400);
        w.ResultRescue(p);
        int r[3];
        NormalizeRot(a.rot, r);
        memcpy(a.rot, r, 12);
        a.anim.rotOrder = 2;
        a.slide = 0;
        w.AnimBlendTo(a, 0x3CC, 0xF, 5);  // 0x004119A0
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;
        a.actorSlot = -1;
        --w.liveEnemies;
        --w.attackers;
        if (a.slot120 != -1 && a.lock) w.lockOn.Remove(a.lock);  // 0x004092B0
        Actor21Slide(w, a);
        Actor21Draw(w, a);
        ++a.anim.counter[0];
        a.fn = 0x00451D80;
        return;
    }
    Actor21PathStep(w, a);
    Actor21Draw(w, a);
    ++a.anim.counter[0];
    if (w.runner->camera.camId == 0x39 && w.runner->camera.tick > 0x121) {  // escaped
        --w.liveEnemies;
        --w.attackers;
        if (a.slot120 != -1 && a.lock) w.lockOn.Remove(a.lock);
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;  // 0x004092D0
        a.actorSlot = -1;
        a.fn = 0x00451D20;
        return;
    }
    mx::Transform(w.cam.view, a.body, a.viewPos);  // +0x70 = active context view x +0x100
    a.flags34 &= ~8u;
    w.RegisterTarget(&a);  // 0x00405160
}

// Game type 2 (training lesson 7): 0x00452540 / 0x004528B0 wait for camera tick 60 (the count starts there and runs
// while flag 0xF2 is clear) until 0x00565F46[(idx + difficulty * 8)], then the count restarts at 0x00565F66[idx]
bool TrainWait(World::Impl& w, int& count, int idx) {
    if (count != 0 && w.runner->flags[0xF2] == 0) ++count;
    if (w.runner->camera.tick == 0x3C) count = 0x3C;
    const uint32_t k = static_cast<uint32_t>(idx + w.runner->cfg.playerVariant * 8) * 2;
    if (count != w.exe.S16(0x00565F46 + k)) return false;
    count = w.exe.S16(0x00565F66 + static_cast<uint32_t>(idx) * 2);
    return true;
}

void Actor21Release(World::Impl& w, Actor21Task& a) {
    if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;
    a.actorSlot = -1;
    --w.liveEnemies;
    --w.attackers;
    if (a.slot120 != -1 && a.lock) w.lockOn.Remove(a.lock);  // 0x004092B0
}

// 0x004525C0 (game type 2): on path 0x00565EF4[idx] at the count; hits take the hp (no score), a kill counts in
// 0x009A244A; at count 0x00565F96[idx] the lesson is failed (0x009A2234 = 1); removed at the path's end
void Actor21Train(World::Impl& w, Actor21Task& a) {
    if (w.runner->flags[0xF2] != 0) {
        Actor21Draw(w, a);
        return;
    }
    if (a.flags34 & 8) {
        for (int i = 0; i < 16; ++i) {
            EnemyNode& n = a.anim.nodes[i];
            if (!(n.flags & 8)) continue;
            --a.hp;
            w.EnemyVoice(a, 0);  // 0x0040A6F0
            n.flags &= ~8u;
            w.SpawnNodeBlood(a, i);  // 0x00407200
            n.flags &= ~6u;
        }
    }
    const int path = w.exe.S16(0x00565EF4 + static_cast<uint32_t>(a.pathIdx) * 2);
    if (a.hp < 1) {
        w.runner->globals[0x009A244A] = static_cast<uint32_t>(w.GlobalInt(0x009A244A, 0) + 1);
        w.EnemyVoice(a, 1);
        int r[3];
        NormalizeRot(a.rot, r);  // 0x004018E0 of RotZ RotY RotX
        memcpy(a.rot, r, 12);
        a.anim.rotOrder = 2;
        a.slide = 0;
        w.AnimBlendTo(a, 0x3CC, 0xF, 5);  // 0x004119A0
        Actor21Release(w, a);
        Actor21Slide(w, a);
        Actor21Draw(w, a);
        ++a.anim.counter[0];
        a.fn = 0x00451D80;
    } else {
        if (w.exe.S16(0x00565F96 + static_cast<uint32_t>(a.pathIdx) * 2) <= a.count) w.runner->globals[0x009A2234] = 1;
        float p[3];
        int ang[3];
        if (EvalPath(w, path, static_cast<float>(a.count), p, ang)) {  // 0x004042D0
            for (int k = 0; k < 3; ++k) a.delta[k] = p[k] - a.pos[k];
            memcpy(a.pos, p, 12);
            memcpy(a.rot, ang, 12);
        }
        Actor21Draw(w, a);
        ++a.anim.counter[0];
        mx::Transform(w.cam.view, a.body, a.viewPos);
        a.flags34 &= ~8u;
        w.RegisterTarget(&a);  // 0x00405160
        if (static_cast<int32_t>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4)) <= a.count) {
            Actor21Release(w, a);
            w.EnemyDestroy(a);  // 0x00409CC0
            return;
        }
    }
    ++a.count;
}

void TaskActor21(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor21Task& a = t.As<Actor21Task>();
    if (a.fn == 0x00452540) {
        if (!TrainWait(w, a.count, a.pathIdx)) return;
        a.fn = 0x004525C0;
    }
    switch (a.fn) {
    case 0x004525C0:
        Actor21Train(w, a);
        return;
    case 0x00451860: {  // riding in
        if (a.sub == 0) {
            Actor21Path(w, a);
            Mat4 m = Mat4::Identity();  // push, identity, T(pos), RotY(yaw); (0, f, f) with f = 50.0 - tick
            mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
            mx::RotY(m, a.rot[1]);
            const float f = static_cast<float>(F64(w.exe, 0x00565FC0) - static_cast<double>(w.runner->camera.tick));
            const float local[3] = { 0.0f, f, f };
            mx::Transform(m, local, a.pos);
            if (w.runner->camera.tick >= 0x32) ++a.sub;
        } else if (a.sub == 1) {
            Actor21Path(w, a);
            const int tick = w.runner->camera.tick;
            if (tick != 0x5A && tick != 0x8C && tick >= 0xBE) {
                a.pathIdx = 1;
                a.fn = 0x00451980;
            }
        }
        Actor21Draw(w, a);
        ++a.anim.counter[0];
        if (w.skip2230 != 0) {
            a.pathIdx = 1;
            a.fn = 0x00451980;
        }
        return;
    }
    case 0x00451980:
        Actor21Alive(w, t, a);
        return;
    case 0x00451D80:  // dying: slides, then a blood pool and sinks (0x00451DF0)
        if (w.runner->flags[0] == 1) {
            w.EnemyDestroy(a);
            return;
        }
        Actor21Slide(w, a);
        Actor21Draw(w, a);
        if (a.anim.ended) {  // +0x1F1
            a.sinkTimer = 0x78;
            w.SpawnBloodPool(a);  // 0x00407DA0
            a.fn = 0x00451DF0;
            return;
        }
        ++a.anim.counter[0];
        return;
    case 0x00451DF0:
        if (w.runner->flags[0] == 1) {
            w.EnemyDestroy(a);
            return;
        }
        a.pos[1] = a.pos[1] - F32(w.exe, 0x004C4D04);
        Actor21Draw(w, a);
        if (--a.sinkTimer == 0) w.EnemyDestroy(a);
        return;
    case 0x00451D20:
        Actor21Escaped(w, t, a);
        return;
    default:
        w.Note("actor 0x21 function " + std::to_string(a.fn) + " not ported");
        return;
    }
}

// 0x00452320: vehicle body + steered front wheel, then the wheel pair with the roll limited to +-0x3800
void VehicleDraw(World::Impl& w, VehicleTask& v) {
    const uint32_t row = 0x00565F2C + static_cast<uint32_t>(v.broken) * 0x10;
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    Mat4 m = Mat4::Identity();
    mx::Translate(m, v.pos[0], v.pos[1], v.pos[2]);
    mx::RotZ(m, v.rot[2]);
    mx::RotY(m, v.rot[1]);
    mx::RotX(m, v.rot[0]);
    w.Draw(static_cast<int>(w.exe.U32(row)), m);
    Mat4 q = m;
    mx::Translate(q, bits(0x4110EECC), bits(0x40CBC84B), bits(0x410F17C2));
    if (v.steerCount != 0) mx::RotY(q, v.steer);
    w.Draw(static_cast<int>(w.exe.U32(row + 4)), q);
    int n[3];
    NormalizeRot(v.rot, n);
    uint32_t roll = static_cast<uint32_t>(n[2]) & 0xFFFF, r = roll;
    if (roll < 0x4001) r = roll < 0x801 ? 0 : roll - 0x800;
    else if (roll > 0xBFFF) r = roll < 0xE800 ? roll - 0xE800 : 0;
    m = Mat4::Identity();
    mx::Translate(m, v.pos[0], v.pos[1], v.pos[2]);
    mx::RotY(m, n[1]);
    mx::RotX(m, n[0]);
    mx::RotZ(m, static_cast<int>(r));
    q = m;
    mx::Translate(q, 0.0f, bits(0x404AB852), bits(0x415A61E5));
    if (v.wheels != 0) mx::RotX(q, v.wheelAngle);
    w.Draw(static_cast<int>(w.exe.U32(row + 8)), q);
    q = m;
    mx::Translate(q, 0.0f, bits(0x404AB852), bits(0xC117AE14));
    if (v.wheels != 0) mx::RotX(q, v.wheelAngle);
    w.Draw(static_cast<int>(w.exe.U32(row + 12)), q);
}

void TaskVehicle(Task& t) {
    World::Impl& w = *CurrentWorld();
    VehicleTask& v = t.As<VehicleTask>();
    if (v.fn == 0x00452150) {  // first run
        v.broken = 0;
        v.wheelAngle = 0;
        v.steer = 0;
        v.wheels = 1;
        v.steerCount = 0;
        v.fn = 0x004521B0;
        if (w.runner->cfg.gameType == 2) {
            v.count = 0;
            v.fn = 0x004528B0;
        }
    }
    if (v.fn == 0x004528B0) {  // game type 2: the wait (as the rider's 0x00452540)
        if (!TrainWait(w, v.count, v.param)) return;
        v.fn = 0x00452930;
    }
    if (v.fn == 0x00452930) {  // game type 2: on path 0x00565EF4[param] at the count; sound 0x719A9 at 0x00565FA6[param]
        if (w.runner->flags[0xF2] != 0) {
            VehicleDraw(w, v);
            return;
        }
        v.wheelAngle += 0x1000;
        const int path = w.exe.S16(0x00565EF4 + static_cast<uint32_t>(v.param) * 2);
        float p[3];
        int ang[3];
        if (EvalPath(w, path, static_cast<float>(v.count), p, ang)) {
            memcpy(v.pos, p, 12);
            memcpy(v.rot, ang, 12);
        }
        VehicleDraw(w, v);
        if (static_cast<int32_t>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4)) <= v.count) {
            w.ts.KillCurrent();  // Task_Kill 0x004A7040
            return;
        }
        if (v.count == w.exe.S16(0x00565FA6 + static_cast<uint32_t>(v.param) * 2)) w.Sound(0x719A9);
        ++v.count;
        return;
    }
    if (v.fn == 0x004521B0) {  // on the cutscene paths 0x148 / 0x14E / 0x14D of cameras 0x38 / 0x39 / 0x3A
        v.wheelAngle += 0x1000;
        const int cam = w.runner->camera.camId, tick = w.runner->camera.tick;
        int path = cam == 0x38 ? 0x148 : cam == 0x39 ? 0x14E : cam == 0x3A ? 0x14D : -1;
        if (path < 0) w.Note("vehicle 0x004521B0 outside cameras 0x38..0x3A (path from a stale register)");
        const int end = path >= 0 ? static_cast<int>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4)) : 0;
        if (cam == 0x39) {
            if (tick >= end) {
                v.broken = 1;
                v.fn = 0x004522A0;
            }
        } else if (cam == 0x3A) {
            if (tick > 0x4F) v.wheels = 0;
            if (tick >= end) v.fn = 0x004522A0;
        }
        float p[3];
        int ang[3];
        if (path >= 0 && EvalPath(w, path, static_cast<float>(tick), p, ang)) {
            memcpy(v.pos, p, 12);
            memcpy(v.rot, ang, 12);
        }
        VehicleDraw(w, v);
        return;
    }
    // 0x004522A0: stopped; the front wheel turns once (path 0x153 at 100 + n, frames 1..39)
    if (w.runner->flags[0] == 1) {
        w.ts.KillCurrent();
        return;
    }
    const int c = ++v.steerCount;
    if (c > 0 && c < 0x28) {
        float p[3];
        int ang[3];
        if (EvalPath(w, 0x153, static_cast<float>(c) + F32(w.exe, 0x004C43B0), p, ang)) v.steer = 0x4000 - ang[1];
    }
    VehicleDraw(w, v);
}

// 0x00452090 in game type 1 with item 0x15: node 2 scaled (2, 2, 2)
void Actor21BigHeadNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    Mat4 s = m;
    if (idx == 2 && ItemFlag(w, 0)) mx::Scale(s, 2.0f, 2.0f, 2.0f);
    w.Draw(e.anim.nodes[idx].model, s, 8, -1.0f, false, idx);
}

// 0x00451720
void SpawnActor21(World::Impl& w, Task& t, const SpawnRecord& r) {
    auto d = std::make_unique<Actor21Task>();
    d->fn = 0x00451860;
    d->record = r;
    d->flags34 = r.flags | 1;  // 0x00408970
    memcpy(d->pos, r.pos, 12);
    memcpy(d->rot, r.rot, 12);
    d->hp = d->hpMax = static_cast<int16_t>(r.state);
    d->lo = static_cast<int16_t>(r.lo);
    d->anim.kind = r.ParamS8(0);  // op 0x09 (0x004088A0): +0x1F4 = P byte 0, +0x130C = P byte 1
    d->variant = r.ParamS8(1);
    d->slot120 = -1;
    d->anim.motion[0] = 0x3E6;
    w.EnemyInitPose(*d);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
    d->anim.rotOrder = 5;
    d->anim.drawFlags &= ~2u;
    d->anim.counter[0] = w.Rand(0x00451768) % 10;
    d->nodeCb = 0x00411050;  // 0x00452090: the node model (game type 0)
    d->radius = F32(w.exe, 0x004C4D28 + static_cast<uint32_t>(d->anim.kind) * 4);
    if (w.runner->cfg.gameType == 1 && ItemFlag(w, 0)) {  // item 0x15 (0x009C88A8 == 1): head radius x 2, head drawn x 2
        d->anim.nodes[2].radius = d->anim.nodes[2].radius + d->anim.nodes[2].radius;
        d->nodeFn = Actor21BigHeadNode;  // 0x00452090 in game type 1
    }
    ++w.attackers;
    ++w.liveEnemies;
    if (w.runner->cfg.gameType == 2) {  // training lesson 7: path index = the record's state, hp 2, 0x00452540
        d->count = 0;
        d->pathIdx = d->hp;
        d->hp = 2;
        VehicleTask* v = w.NewTask<VehicleTask>(TaskVehicle, 0x00452150);  // 0x00452120(idx)
        v->param = d->pathIdx;
        d->fn = 0x00452540;
        t.data = std::move(d);
        t.fn = TaskActor21;
        TaskActor21(t);
        return;
    }
    d->hp = w.exe.S16(0x00565F0C + static_cast<uint32_t>(w.rank) * 2);  // 0x0040A8A0 = rank
    w.CountCivilian();  // 0x009A21BA / 0x009C9100[stage]
    d->pathIdx = 0;
    // 0x009A21BA / 0x009C9100[stage] spawn statistics: not modelled
    // 0x00452120(0): the vehicle task (first run at the end of this frame)
    VehicleTask* v = w.NewTask<VehicleTask>(TaskVehicle, 0x00452150);
    v->param = 0;
    t.data = std::move(d);
    t.fn = TaskActor21;
    TaskActor21(t);
}

// Actor_RegisterSlot 0x00409270
void RegisterSlot(World::Impl& w, EnemyTask& e) {
    e.actorSlot = -1;
    for (int i = 0; i < 14; ++i)
        if (!w.actorSlots[i]) {
            e.flags38 |= 0x40;
            w.actorSlots[i] = &e;
            e.actorSlot = i;
            return;
        }
}

// Spawned actor object from its record (0x00408A20 / 0x004088A0: +0x34 flags | 1, position, angles, +0x11C state,
// +0x1316 lo, params at +0x1390; op 0x09 also +0x1F4 = P byte 0, +0x130C = P byte 1)
template <class T> std::unique_ptr<T> FromRecord(const SpawnRecord& r, uint32_t fn) {
    auto d = std::make_unique<T>();
    d->fn = fn;
    d->record = r;
    d->flags34 = r.flags | 1;
    memcpy(d->pos, r.pos, 12);
    memcpy(d->rot, r.rot, 12);
    d->hp = d->hpMax = static_cast<int16_t>(r.state);
    d->lo = static_cast<int16_t>(r.lo);
    d->anim.kind = r.ParamS8(0);
    d->variant = r.ParamS8(1);
    return d;
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x33 state 2 (0x00432FF0 -> 0x00433A10): a static model P word 0, removed at camera tick P word 3 or event
// flag P byte 0x11. 0x00432FF0 only sets the function (first run next frame).
// ---------------------------------------------------------------------------------------------------------------------
struct Static33Task : EnemyTask {
    int model = 0;  // +0x13F0
};

void TaskStatic33(Task& t) {
    World::Impl& w = *CurrentWorld();
    Static33Task& a = t.As<Static33Task>();
    if (a.fn == 0x00432FF0) {
        a.fn = 0x00433A10;
        return;
    }
    if (a.sub == 0) {
        a.model = a.record.ParamS32(0);
        a.sub = 1;
    }
    if (a.record.ParamS32(12) != w.runner->camera.tick && w.runner->flags[a.record.params[0x11]] != 1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotZ(m, a.rot[2]);
        mx::RotY(m, a.rot[1]);
        mx::RotX(m, a.rot[0]);
        mx::Scale(m, 1.0f, 1.0f, 1.0f);
        w.Draw(a.model, m);
        return;
    }
    w.EnemyDestroy(a);
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x33 state 1 (0x00432FF0 -> 0x004331D0): model P word 0 flown along camera path P word 3 from the context-0
// tick - 1 to P float 4 (0x00433860); at P float 5 an effect at P +0x24 (block), then from 20 frames on a fire
// billboard there; removed by event flag P byte 0x21 or at camera tick P word 7. Model 0x1B0E (a boat) has parts and
// sounds; the others two flame sprites while flying. State 5 (0x00433B00): effect 0x44 at camera tick P word 0.
// ---------------------------------------------------------------------------------------------------------------------
struct Actor33PathTask : EnemyTask {
    int model = 0;          // +0x13F0
    int path = 0;           // +0x1350
    int flame[2] = {};      // +0x1354 / +0x1358
    int spin = 0;           // +0x135C
    int fire[3] = {};       // +0x1364 frame, +0x1368 first, +0x136C last
    float t = 0;            // +0x1370
    float end = 0;          // +0x1374
    float scale = 1.0f;     // +0x118
};

float ParamF(const SpawnRecord& r, int off) {
    const int32_t v = r.ParamS32(off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

// 0x00433860
void Actor33Move(World::Impl& w, Actor33PathTask& a) {
    const SpawnRecord& r = a.record;
    if (a.sub == 0) {
        a.flags34 |= 0x80000001u;
        if (r.ParamS32(4) == -1) {
            a.radius = a.radius128 = ParamF(r, 8);
        } else {
            a.flags34 |= 0x80000051u;
            a.objMesh = r.ParamS32(4);
            w.Note("actor 0x33 state 1 collision-mesh target (P word 1) not checked");
        }
        a.model = r.ParamS32(0);
        a.path = r.ParamS32(12);
        a.flame[0] = 0x24A;
        a.flame[1] = 0x260;
        a.spin = 0;
        a.t = static_cast<float>(w.runner->camera.tick - 1);  // 0x009A6110
        a.end = ParamF(r, 16);
        if (a.model == 0x1B0E) w.Sound(0x423A9);
        ++a.sub;
    } else if (a.sub != 1) {
        return;
    }
    const float t = a.t + F32(w.exe, 0x004C4380);
    a.t = t;
    if (a.end <= t) {
        ++a.sub;
        return;
    }
    float p[3];
    int ang[3];
    if (!EvalPath(w, a.path, t, p, ang)) return;  // 0x004042D0
    a.pos[0] = p[0];
    a.pos[2] = p[2];
    a.rot[0] = ang[0] & 0xFFFF;
    a.pos[1] = p[1];
    a.rot[2] = ang[2] & 0xFFFF;
    a.rot[1] = ang[1] & 0xFFFF;
    a.scale = 1.0f;
    if (a.model == 0x1A35) {
        a.scale = 2.5f;
    } else if (a.model == 0x1A36) {
        a.scale = 2.5f;
        a.rot[1] = ang[1] + 0x4000;
    } else if (a.model == 0x1B0E) {
        a.pos[0] = p[0] - F32(w.exe, 0x0055D2B4);
    }
}

void TaskActor33Path(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor33PathTask& a = t.As<Actor33PathTask>();
    w.actor33 = &a;  // 0x009A5C34
    if (a.fn == 0x00432FF0) {  // 0x00432FF0 only sets the function
        a.fn = 0x004331D0;
        return;
    }
    const SpawnRecord& r = a.record;
    const uint8_t* flags = w.runner->flags;
    if (flags[r.params[0x20]] == 1 || static_cast<float>(r.ParamS32(0x18)) == a.t) a.flags34 |= 0x10000000;
    const float blk[3] = { ParamF(r, 0x24), ParamF(r, 0x28), ParamF(r, 0x2C) };
    if (r.ParamS32(0x14) != static_cast<int32_t>(0xBF800000u) && a.t == ParamF(r, 0x14)) {
        if (a.model == 0x1B0E) {
            w.Sound(0x523A9);
            w.SpawnEffectAt(blk, r.ParamS32(0x30), r.ParamS32(0x34), 0x45, 2, -1);  // 0x00407340
        } else {
            w.SpawnEffectAt(blk, r.ParamS32(0x30), r.ParamS32(0x34), 0x44, 0, -1);
        }
        a.counter1334 = 0;
        a.flags34 |= 0x40000000;
    }
    if (a.flags34 & 0x40000000) {
        if (++a.counter1334 == 0x14) {
            a.flags34 |= 0x200000;
            a.fire[0] = a.fire[1] = 0x1AAB;
            a.fire[2] = 0x1AD2;
            w.Sound(0x723A9);
        }
        if (a.flags34 & 0x200000) {  // fire billboard facing the camera (pitch 0)
            int pitch, yaw;
            mx::DirToAngles(w.cam.pos[0] - blk[0], w.cam.pos[1] - blk[1], w.cam.pos[2] - blk[2], &pitch, &yaw);  // 0x004016B0
            Mat4 m = Mat4::Identity();
            mx::Translate(m, blk[0], blk[1], blk[2]);
            mx::RotY(m, yaw);
            mx::RotX(m, 0);
            if (a.fire[2] < ++a.fire[0]) a.fire[0] = a.fire[1];
            w.Draw(a.fire[0], m);
        }
    }
    if (flags[r.params[0x21]] == 1 || w.runner->camera.tick == r.ParamS32(0x1C)) {  // (or the context-2 tick 0x009A6458)
        if (a.flags34 & 0x200000) w.Sound(0x823A9);
        w.EnemyDestroy(a);
        w.actor33 = nullptr;  // (the original keeps the stale pointer)
        return;
    }
    Actor33Move(w, a);
    auto frame = [&](Mat4& m) {
        mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotZ(m, a.rot[2]);
        mx::RotY(m, a.rot[1]);
        mx::RotX(m, a.rot[0]);
    };
    Mat4 m = Mat4::Identity();
    frame(m);
    mx::Scale(m, a.scale, a.scale, a.scale);
    w.Draw(a.model, m);  // light scale (0x0041EBB0): renderer
    a.m150 = m;          // 0x004A8CA0: model-view
    mx::MulRight(a.m150, w.cam.view);
    mx::Transform(w.cam.view, a.pos, a.viewPos);
    w.RegisterTarget(&a);  // 0x00405160
    if (a.model == 0x1B0E) {
        auto part = [&](float x, float y, float z, int rx, int model) {
            Mat4 p = Mat4::Identity();
            frame(p);
            mx::Translate(p, x, y, z);
            mx::RotX(p, rx);
            w.Draw(model, p);
        };
        {
            Mat4 p = Mat4::Identity();
            frame(p);
            mx::Translate(p, Bits(0xC0A88659), Bits(0x41055326), Bits(0x40D6F1AA));
            mx::RotZ(p, 0);
            mx::RotY(p, 0);
            mx::RotX(p, static_cast<int>(0xFFFFD2C6u));
            w.Draw(0x899, p);
        }
        a.spin += 0x2000;
        part(0.0f, Bits(0x4062CBFB), Bits(0x4188398C), a.spin, 0x8CB);
        part(0.0f, Bits(0x4062CBFB), Bits(0xC14624DD), a.spin, 0x8CB);
        Mat4 p = Mat4::Identity();
        frame(p);
        mx::Translate(p, 10.0f, Bits(0x40C6E979), Bits(0x40DC1893));
        w.Draw(0x1B0A, p);
        p = Mat4::Identity();
        frame(p);
        mx::Translate(p, -10.0f, Bits(0x40C6E979), Bits(0x40DC1893));
        w.Draw(0x1B0D, p);
        return;
    }
    if (a.sub == 1) {  // two flame sprites 0x24A..0x25F / 0x260..0x275, 25 ahead
        for (int i = 0; i < 2; ++i) {
            Mat4 p = Mat4::Identity();
            frame(p);
            mx::Translate(p, 0.0f, 0.0f, 25.0f);
            mx::Scale(p, Bits(0x3F19999A), 0.5f, Bits(0x3F333333));
            w.Draw(a.flame[i], p);  // light scale 1 (0x0041EBB0)
            if ((i == 0 ? 0x25F : 0x275) < ++a.flame[i]) a.flame[i] = i == 0 ? 0x24A : 0x260;
        }
    }
}

// 0x00433B00 (state 5)
void TaskActor33Burst(Task& t) {
    World::Impl& w = *CurrentWorld();
    EnemyTask& a = t.As<EnemyTask>();
    if (a.fn == 0x00432FF0) {
        a.fn = 0x00433B00;
        return;
    }
    if (w.runner->camera.tick == a.record.ParamS32(0)) {  // (or the context-2 tick 0x009A6458: not modelled)
        w.SpawnEffectAt(a.pos, 0, 0, 0x44, 0, -1);  // 0x00407340 (block angles 0)
        w.EnemyDestroy(a);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x27 (0x004329D0): a model moved along camera path 0x00589AE8[state] at the camera tick; from tick 0xBE
// model 0x33 with a burning-fire billboard (0x00432B10) for states 0 / 1. Removed by event flag 0 (0x00432AF0).
// ---------------------------------------------------------------------------------------------------------------------
struct Actor27Task : EnemyTask {
    int fire = 0;     // +0x1320 fire frame
    int burning = 0;  // +0x1324
    int model = 0;    // +0x13F0
};

void Actor27Draw(World::Impl& w, Actor27Task& a) {  // 0x00432B10
    Mat4 m = Mat4::Identity();
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotZ(m, a.rot[2]);
    mx::RotY(m, a.rot[1]);
    mx::RotX(m, a.rot[0]);
    w.Draw(a.model, m);
    if (a.burning != 0 && a.hp < 2) {
        ++a.fire;
        int pitch = 0, yaw = 0;
        mx::DirToAngles(w.cam.pos[0] - a.pos[0], 0.0f, w.cam.pos[2] - a.pos[2], &pitch, &yaw);  // 0x004016B0
        Mat4 f = Mat4::Identity();
        mx::Translate(f, a.pos[0], a.pos[1] + F32(w.exe, 0x0055D2B4), a.pos[2]);
        mx::RotY(f, yaw);
        w.Draw(a.fire < 0x28 ? a.fire % 0x28 + 0x1433 : a.fire % 0x28 + 0x1AAB, f);
        mx::Translate(f, 0.0f, 0.0f, 5.0f);
        mx::Scale(f, 5.0f, 5.0f, 5.0f);
        w.Draw(a.fire % 8 + 0xB67, f);
    }
}

void TaskActor27(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor27Task& a = t.As<Actor27Task>();
    const uint32_t path = w.exe.U32(0x00589AE8 + static_cast<uint32_t>(a.hp) * 4);
    auto place = [&](float time) {
        float p[3];
        int ang[3];
        if (EvalPath(w, static_cast<int>(path), time, p, ang)) {
            memcpy(a.pos, p, 12);
            memcpy(a.rot, ang, 12);
        }
    };
    if (a.fn == 0x00432AF0) {
        if (w.runner->flags[0] == 1) {
            w.ts.KillCurrent();
            return;
        }
        Actor27Draw(w, a);
        return;
    }
    bool check = false;
    if (a.sub == 0) {
        a.model = 0x2B;
        a.sub = 1;
        check = true;
    } else if (a.sub == 1) {
        check = true;
    }
    if (check && w.runner->camera.tick > 0xBD) {
        a.burning = 1;
        a.model = 0x33;
        a.fn = 0x00432AF0;
    }
    place(static_cast<float>(w.runner->camera.tick));
    Actor27Draw(w, a);
    if (w.skip2230 != 0) {
        a.burning = 1;
        a.model = 0x33;
        a.fn = 0x00432AF0;
        place(190.0f);  // 0x433E0000
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x44 state 17 (0x00473A70 -> task 0x00474F30, block 0x378): a placed model, a collision-mesh target
// (P word 2 = coli mesh; -1: a sphere of radius 8 that ignores shots). Game type 1: Prop17GameType1 (world_modes_items.cpp).
// ---------------------------------------------------------------------------------------------------------------------
struct Prop17Task : ActorTask {
    float pos[3] = {};        // +0x19C
    float scale[3] = {};      // +0x1A8
    int yaw = 0;              // +0x1D0
    int rot[3] = {};          // +0x64
    const int* Rot64() const override { return rot; }
    int16_t model = 0;        // +0x28C
    int flagKill = -1;        // +0x2A4
    Prop17Type1 t1;           // +0x192 / +0x194 / +0x11C / +0x1DC / +0x2A0 / +0x2A8..+0x2B0 / items (game type 1)
};

void TaskProp17(Task& t) {
    World::Impl& w = *CurrentWorld();
    Prop17Task& a = t.As<Prop17Task>();
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
        for (World::Impl::Target& tg : w.targetCopy)
            if (tg.obj == &a) tg.obj = nullptr;
        w.ts.KillCurrent();
    };
    if (a.flagKill >= 0 && w.runner->flags[a.flagKill & 0xFF] == 1) return destroy();
    if (w.cfg.eventMode == 1) {
        if (w.runner->flags[0x77] != 0) return destroy();
    } else if (w.cfg.eventMode == 2 && w.runner->scene == 2 && a.t1.shot == 0) {
        w.runner->flags[0x15] = 1;
    }
    if (w.runner->cfg.gameType == 1 && !Prop17GameType1(w, a, a.t1, a.pos, a.rot)) return destroy();
    bool draw = true;
    if (a.t1.t2A8 > 0x3B) draw = (a.t1.t2AC % 2) == 0;
    if (draw) {
        auto ops = [&](Mat4& m) {
            mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
            mx::RotY(m, a.yaw);
            mx::RotZ(m, a.rot[2]);
            mx::RotY(m, a.rot[1]);
            mx::RotX(m, a.rot[0]);
            mx::Scale(m, a.scale[0], a.scale[1], a.scale[2]);
        };
        Mat4 m = Mat4::Identity();
        ops(m);
        Mat4 mv = w.cam.view;  // the same ops on the view: +0x150 = model-view (0x004A8CA0)
        ops(mv);
        const bool alt = w.altModels != 0 && w.runner->camera.camId != 0x46;
        w.Draw(a.model, m, 8, -1.0f, alt);
        a.m150 = mv;
    }
    w.RegisterTarget(&a);  // 0x00405160
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 66 (0x00464500): decorations from table 0x00595158 (20, P byte 0 == 0) / 0x005953D8 (29); task 0x0046FE00.
// The hanging signs 0x10DC..0x10DE swing when shot (damped: v -= (angle + v) / 24).
// ---------------------------------------------------------------------------------------------------------------------
struct Komono66Task : ActorTask {
    int16_t life = 0;         // +0x11C areas it survives
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};        // +0x19C
    float scale[3] = {};      // +0x1A8
    float zOff = 0;           // +0x1C8
    int rot[3] = {};          // +0x1CC / +0x1D0 / +0x1D4
    int spin = 0;             // +0x1D8
    int16_t model = 0;        // +0x28C
    float hitY = 0;           // +0x2C0
};

void TaskKomono66(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono66Task& k = t.As<Komono66Task>();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            w.ts.KillCurrent();
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (k.model == 0x10DC || k.model == 0x10DD || k.model == 0x10DE) {
        if (k.flags34 & 8) {
            const uint32_t f = k.flags34 & ~8u;
            k.flags34 = f;
            // 0x004650F0(flags, 0): the shooter (rand when both), hits + 1 (no points)
            int p;
            if ((f & 2) && (f & 4)) p = w.Rand(0x00465106) & 1;
            else p = (f & 2) ? 0 : 1;
            if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
            w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.0f, k.pos[2]);  // 0x004666B0
            w.Sound(0x1116A9);
            k.spin = w.Rand(0) % 0x201 + 0x600;
        }
        if (w.cfg.eventMode == 0 && (w.shakeTimer == 0x17 || w.shakeTimer == 0x2F)) {
            const int s = w.Rand(0) & 1;
            k.spin = (s * -2 + 1) * (w.Rand(0) % 0x201 + w.shakeTimer * 0x20);
        }
        const float p[3] = { k.pos[0], k.hitY + k.pos[1], k.pos[2] };
        k.spin = k.spin - (k.rot[0] + k.spin) / 0x18;
        k.rot[0] = k.spin + k.rot[0];
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);
    }
    k.flags34 &= ~6u;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.zOff + k.pos[2]);
    if (k.model == 0x10DE) mx::Translate(m, 0.0f, 1.5f, 0.0f);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    if (k.model == 0x10DE) mx::Translate(m, 0.0f, -1.5f, 0.0f);
    mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
    w.Draw(k.model, m);
}

void SpawnKomono66(World::Impl& w, const SpawnRecord& r) {
    const bool big = r.ParamS8(0) > 0;  // +0x1F4
    const int n = big ? 0x1D : 0x14;
    const uint32_t tab = big ? 0x005953D8 : 0x00595158;
    for (int i = 0; i < n; ++i) {
        const uint32_t e = tab + static_cast<uint32_t>(i) * 32;
        Komono66Task* k = w.NewTask<Komono66Task>(TaskKomono66, 0x0046FE00);
        for (int c = 0; c < 3; ++c) k->pos[c] = F32(w.exe, e + 4 + 4 * c);
        k->rot[1] = static_cast<int>(w.exe.U32(e + 0x10));
        for (int c = 0; c < 3; ++c) k->scale[c] = F32(w.exe, e + 0x14 + 4 * c);
        k->model = w.exe.S16(e);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->radius = 4.0f;
        k->life = static_cast<int16_t>(r.state);
        k->hitY = -3.5f;
        if (k->model == 0x10DC) {
            k->radius = 6.5f;
            k->hitY = -5.0f;
        }
        if (k->model == 0x10B1) k->rot[0] = static_cast<int>(0xFFFFC000u);
        k->flags34 = 0x80000001u;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x53 (0x00431250 -> 0x00431340): a background figure (skeleton 0x1A) looping the motions of row P s16 0 of
// 0x00589A64 (5 per row, loop counts 0x00589AA0, -1 ends the row, -2 loops forever); removed after 1000 frames.
// P s16 1 == 1: alternative models (+0x38 |= 8); >= 2: game type 1 only (destroyed).
// ---------------------------------------------------------------------------------------------------------------------
struct Actor53Task : EnemyTask {
    int16_t row = 0, age = 0, idx = 0, loops = 0;  // control block (0x24 bytes at +0x1310): +0x10 / +0x12 / +0x1C / +0x1E
};

void TaskActor53(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor53Task& a = t.As<Actor53Task>();
    w.AnimDraw(a);  // 0x00411090 (+ shadow)
    w.EnemyShadow(a);
    int& c = a.anim.counter[0];
    ++c;
    ++a.age;
    const uint32_t row = static_cast<uint32_t>(a.row) * 5;
    if (c == w.MotionTicks(w.exe.S16(0x00589A64 + (row + static_cast<uint32_t>(a.idx)) * 2)) - 1) {
        c = 0;
        ++a.loops;
        const int n = w.exe.S16(0x00589AA0 + (static_cast<uint32_t>(a.idx) + row) * 2);
        if (n != -2 && n == a.loops) {
            ++a.idx;
            if (w.exe.S16(0x00589A64 + (static_cast<uint32_t>(a.idx) + row) * 2) == -1) a.idx = 0;
            a.anim.motion[0] = w.exe.S16(0x00589A64 + (static_cast<uint32_t>(a.idx) + row) * 2);
            c = 0;
            a.loops = 0;
        }
    }
    if (1000 < a.age) w.EnemyDestroy(a);
}

// 0x00431430 (actor 0x53 with P s16 1 >= 2, game type 1): removed by event flag 0x83; motion 0x305, after 200 frames
// 0x2FA; shot in scene 8 while the route branch is 0: route branch 2, motion 0x2FD (+0x10 = 1); once it is left of
// x = float 0x0055D7DC it walks on (motion 0x305, +0x10 = 2); a sphere target at its position (0x0043F950)
void TaskActor53Type1(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor53Task& a = t.As<Actor53Task>();
    if (w.runner->flags[0x83] == 1) {
        w.EnemyDestroy(a);
        return;
    }
    const float lim = F32(w.exe, 0x0055D7DC);
    bool walk = false;
    if (a.row == 0) {
        ++a.age;
        if (a.anim.motion[0] == 0x305 && 199 < a.age) w.AnimBlendTo(a, 0x2FA, 0, 10);
        if ((a.flags34 & 8) && w.runner->camera.routeBranch == 0 && w.runner->scene == 8) {
            w.runner->camera.routeBranch = 2;  // 0x009C88A4
            w.AnimBlendTo(a, 0x2FD, 0, 10);
            a.row = 1;
        }
        walk = !(lim <= a.pos[0]);
    } else if (a.row == 1) {
        walk = !(lim <= a.pos[0]);
    }
    if (walk) {
        a.row = 2;
        w.AnimBlendTo(a, 0x305, 0, 10);
    }
    w.AnimDraw(a);  // 0x00411090 (lights 0x0041DC70 / 0x0041DCC0: renderer)
    w.EnemyShadow(a);
    ++a.anim.counter[0];
    mx::Transform(w.cam.view, a.pos, a.viewPos);  // 0x0043F950
    w.RegisterTarget(&a);
}
// A task whose first run is Actor_Destroy 0x00409CC0 (komono kinds 70 / 72 outside game type 1: 0x004675A0)
struct SelfDestroyTask : ActorTask {};
void TaskSelfDestroy(Task& t) {
    World::Impl& w = *CurrentWorld();
    SelfDestroyTask& a = t.As<SelfDestroyTask>();
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;
    w.ts.KillCurrent();
}

// Type 0x44 state 2 (0x00472C90): two hanging props (task 0x00473CF0, models 0x1794 / 0x1795) either side of the record
struct Hanging2Task : ActorTask {
    HangingProp p;
};
void TaskHanging2(Task& t) {  // as World::Impl::TaskHanging (0x00473CF0)
    World::Impl& w = *CurrentWorld();
    Hanging2Task& h = t.As<Hanging2Task>();
    PropWorld pw = w.PropState();
    h.p.flags |= h.flags34 & 0xE;
    const PropResult r = h.p.Update(pw, w.hanging);
    w.AfterPropUpdate(pw);
    if (r != PropResult::Alive) w.ts.KillCurrent();
    const PropDraw d = h.p.Draw(pw);
    w.Draw(d.model, d.world, 8, -1.0f, d.alt);
    h.flags34 = h.p.flags;
    if (h.p.HitTarget()) w.RegisterTarget(&h);
}

void SpawnHanging2(World::Impl& w, const SpawnRecord& r) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, r.pos[0], r.pos[1], r.pos[2]);
    mx::RotY(m, r.rot[1]);
    int side = -1, yaw = 0;
    for (int n = 0; n < 2; ++n, side += 2, yaw += 0x8000) {
        Hanging2Task* h = w.NewTask<Hanging2Task>(TaskHanging2, 0x00473CF0);
        HangingProp& p = h->p;
        const float local[3] = { static_cast<float>(side) * F32(w.exe, 0x00569018), 11.5f, 22.68f };
        mx::Transform(m, local, p.pos);
        p.rot[1] = 0;
        p.baseRot[1] = yaw + r.rot[1];
        p.flags |= 0x51;
        p.model = static_cast<int16_t>(0x1794 + n);
        p.hitId = r.ParamS32(8);
        p.amp = side;
        p.kind = 2;
        p.phase = r.ParamS32(0x14);
        p.triggerFlag = r.ParamS8(0x20);
        p.killFlag = r.ParamS8(0x21);
        p.frame = 0;
        p.ryScale = 1.0f;
        p.scale[0] = p.scale[1] = p.scale[2] = 1.0f;
        h->flags34 = p.flags;
    }
}

// Komono lifetime 0x00466640 (life +0x11C, area +0x196 / +0x197): false = Actor_Destroy'd
template <class T> bool KomonoAlive(World::Impl& w, T& k) {
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        for (World::Impl::Target& tg : w.targetCopy)
            if (tg.obj == &k) tg.obj = nullptr;
        w.ts.KillCurrent();
        return false;
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) return destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) return destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    return true;
}

// Komono kind 77 (generic ctor, task 0x004717A0): game type 0 -> sound 0x800A9 and Actor_Destroy on the first run
struct Komono77Task : ActorTask {};
void TaskKomono77(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono77Task& a = t.As<Komono77Task>();
    w.Sound(0x800A9);
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;
    w.ts.KillCurrent();
}

// Komono kind 35 (generic ctor, task 0x0046B320): two shutters 0x1812 / 0x1813 that flap while event flag 0x68 is set
// and 0x69 clear (sound 0x1C16A9 at frames 20 / 50)
struct Komono35Task : ActorTask {
    int16_t life = 0;
    int8_t area = 0, areaCount = 0;
    int8_t state = 0;     // +0x192
    int counter = 0;      // +0x2A0
    int phase = 0;        // +0x1E8
    int yaw = 0;          // +0x1D0
};
void TaskKomono35(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono35Task& k = t.As<Komono35Task>();
    if (!KomonoAlive(w, k)) return;
    if (w.runner->flags[0x68] == 1 && w.runner->flags[0x69] == 0) {
        if (k.state == 0) {
            const int c = ++k.counter;
            if (c == 0x14 || c == 0x32) {
                k.state = 1;
                if (c == 0x32) k.counter = 0;
                w.Sound(0x1C16A9);
            }
        } else if (k.state == 1) {
            int p = k.phase;
            p += p < 0x4000 ? 0x2000 : 0x1000;
            k.phase = p;
            double k2pi = 0;
            w.exe.Read(0x004C4370, &k2pi, 8);
            // fild; fmul qword 2pi/65536; fsin; fmul qword 1536.0 (0x005690B0); __ftol (24-bit: products rounded to float)
            const float a = static_cast<float>(static_cast<double>(p) * k2pi);
            k.yaw = static_cast<int>(static_cast<float>(std::sin(static_cast<double>(a)) * 1536.0));
            if (p > 0x8000) {
                k.phase = 0;
                k.yaw = 0;
                k.state = 0;
            }
        }
    } else {
        k.yaw = 0;
    }
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    Mat4 m = Mat4::Identity();
    mx::Translate(m, bits(0xC41B2354), bits(0x4284966D), bits(0xC4797ED9));
    mx::RotY(m, k.yaw);
    w.Draw(0x1812, m);
    m = Mat4::Identity();
    mx::Translate(m, bits(0xC41B2354), bits(0x4284966D), bits(0xC475345A));
    mx::RotY(m, -k.yaw);
    w.Draw(0x1813, m);
}

// Komono kind 37 (0x004632F0): two stacked boxes (task 0x0046B5F0, model 0x17A9, prop skeleton 7 / break motion
// 0x1D5), an item in the last one broken (P byte 0 slot). 0x007DCDC4[2] / 0x007DCD05 (boxes left) are shared.
struct Komono37Task : ActorTask {
    int16_t life = 0;
    int8_t area = 0, areaCount = 0;
    int8_t slot = 0;          // +0x194 item slot
    int8_t state = 0;         // +0x192 (0 standing, 1 falling, 2 tumbling, 3 broken)
    int16_t idx = 0;          // +0x290
    float pos[3] = {};        // +0x19C
    float ground = 0;         // +0x1B8
    int rot[3] = {};          // +0x1CC / +0x1D0 / +0x1D4
    int spin[2] = {};         // +0x1D8 / +0x1E0
    int dust = 0;             // +0x2A4
    float vy = 0;             // +0x1C4 fall velocity
    float pivot[3] = {};      // +0x1A8 tumbling pivot (corner on the ground)
    int8_t corner = 0;        // +0x198 corner index (0x00593E40 rows)
    int settled = 0;          // +0x2A0 axes come to rest
    PropAnim anim;            // +0x324 skeleton, +0x328 motion, +0x32C frame
    int16_t model = 0x17A9;   // +0x28C
};
int g_boxesLeft = 0;          // 0x007DCD05 (per world; set by the ctor)
Komono37Task* g_boxes[2] = {};  // 0x007DCDC4[idx] (refreshed by each box every frame)

// 0x00593E40: the box's 8 corners (s16 x, y, z) * 0x0055D2B0
void BoxCorner(const World::Impl& w, int i, float out[3]) {
    for (int c = 0; c < 3; ++c)
        out[c] = static_cast<float>(w.exe.S16(0x00593E40 + static_cast<uint32_t>(i * 6 + c * 2))) * F32(w.exe, 0x0055D2B0);
}

void TaskKomono37(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono37Task& k = t.As<Komono37Task>();
    g_boxes[k.idx & 1] = &k;
    if (!KomonoAlive(w, k)) return;
    if ((k.flags34 & 8) && k.state != 3) {
        // 0x004650F0(flags, 1): the shooter, +10, a hit
        const uint32_t f = k.flags34;
        int p;
        if ((f & 2) && (f & 4)) p = w.Rand(0x00465106) & 1;
        else p = (f & 2) ? 0 : 1;
        w.AddScore(p, 10);
        if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
        k.state = 3;
        w.Sound(0x1D16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0
        if (k.idx == 0 && g_boxesLeft == 2 && g_boxes[1]) {  // the upper box falls off, spinning +-(0x80..0x100)
            Komono37Task& u = *g_boxes[1];
            u.state = 1;
            const int s0 = w.Rand(0x0046B68D) & 1;
            u.spin[0] = (s0 * -2 + 1) * (w.Rand(0x0046B6A7) % 0x81 + 0x80);
            const int s1 = w.Rand(0x0046B6C9) & 1;
            u.spin[1] = (s1 * -2 + 1) * (w.Rand(0x0046B6E3) % 0x81 + 0x80);
        }
        if (--g_boxesLeft == 0 && k.slot > 0) {
            if (--w.itemCounter[k.slot & 15] == 0) {
                BreakableProp b;
                b.pos[0] = k.pos[0];
                b.pos[1] = k.ground;  // +0x1A0 = +0x1B8
                b.pos[2] = k.pos[2];
                k.pos[1] = k.ground;
                b.subKind = k.idx;
                b.model = k.model;
                b.areaChanges = k.areaCount;
                b.areaSeen = k.area;
                b.lifetime = k.life;
                w.DropItem(b, k.slot);  // 0x00471BD0 / 0x004723F0 / 0x004722A0
            }
        }
    }
    if (k.state == 1) {  // falling and spinning until a corner reaches the ground (+0x1B8)
        const float vy = k.vy - F32(w.exe, 0x0055CB10);
        k.rot[0] += k.spin[0];
        k.vy = vy;
        k.rot[2] += k.spin[1];
        k.pos[1] = vy + k.pos[1];
        Mat4 m = Mat4::Identity();
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        for (int i = 0; i < 8; ++i) {
            float c[3], o[3];
            BoxCorner(w, i, c);
            mx::TransformX87(m, c, o);  // 0x004A8A80
            if (o[1] + k.pos[1] < k.ground) {
                k.state = 2;
                k.pivot[1] = k.ground;
                k.pivot[0] = o[0] + k.pos[0];
                k.pivot[2] = o[2] + k.pos[2];
                const float s = F32(w.exe, 0x005690EC);
                k.spin[0] = static_cast<int>(static_cast<float>(k.spin[0]) * s);  // __ftol
                k.spin[1] = static_cast<int>(static_cast<float>(k.spin[1]) * s);
                k.settled = 0;
                k.corner = static_cast<int8_t>(i);
                break;
            }
        }
    } else if (k.state == 2) {  // tumbling about the corner back to rest (x / z angles to 0), then dust
        const int s0 = k.spin[0], s1 = k.spin[1];
        const int r0 = k.rot[0] + s0, r2 = k.rot[2] + s1;
        k.rot[0] = r0;
        k.rot[2] = r2;
        if ((0 < s0 && 0 < r0) || (s0 < 0 && r0 < 0)) {
            k.spin[0] = 0;
            k.rot[0] = 0;
            ++k.settled;
        }
        if ((0 < s1 && 0 < r2) || (s1 < 0 && r2 < 0)) {
            k.spin[1] = 0;
            k.rot[2] = 0;
            ++k.settled;
        }
        if (k.settled == 2) {
            k.state = 0;
            k.dust = 1;
            k.pos[1] = k.ground;
        }
    } else if (k.state == 3) {
        if (0x45 < k.anim.frame) {
            k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
            for (World::Impl::Target& tg : w.targetCopy)
                if (tg.obj == &k) tg.obj = nullptr;
            w.ts.KillCurrent();
            return;
        }
        k.anim.frame += 2;
    }
    if (k.dust > 0 && k.dust < 0x10) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        w.Draw(k.dust + 0x93, m);
        ++k.dust;
    }
    if (k.state == 0 || k.state == 1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        w.Draw(k.model, m);
    } else if (k.state == 2) {  // about the pivot; the position = the matrix translation (+0x2E4)
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pivot[0], k.pivot[1], k.pivot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        float c[3];
        BoxCorner(w, k.corner, c);
        mx::Translate(m, -c[0], -c[1], -c[2]);
        mx::GetTranslation(m, k.pos);  // 0x004A8CC0
        w.Draw(k.model, m);
    } else if (k.state == 3 && w.MotionResident(k.anim.motion)) {  // 0x009A37E4[motion] == 2
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.rot[1]);
        for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *w.PropMotion(k.anim.motion), k.anim, m)) {  // 0x0040DD90
            if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
            if (d.model != 0) w.Draw(d.model, d.world, 8, -1.0f, d.alt);
        }
    }
    const float p[3] = { k.pos[0], k.pos[1] + F32(w.exe, 0x0055E1C8), k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

void SpawnKomono37(World::Impl& w, const SpawnRecord& r) {
    for (int n = 0; n < 2; ++n) {
        Komono37Task* k = w.NewTask<Komono37Task>(TaskKomono37, 0x0046B5F0);
        k->life = static_cast<int16_t>(r.state);
        const float f = static_cast<float>(n) * F32(w.exe, 0x004C4C88);
        k->slot = r.ParamS8(0);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->idx = static_cast<int16_t>(n);
        k->state = 0;
        k->pos[0] = f + r.pos[0];
        k->pos[2] = f + r.pos[2];
        k->pos[1] = static_cast<float>(n) * F32(w.exe, 0x0056900C) + r.pos[1];
        k->ground = r.pos[1];
        k->rot[1] = r.rot[1];
        k->radius = 6.0f;
        k->model = 0x17A9;
        k->flags34 = 0x80000001u;
        k->anim.skeleton = 7;
        k->anim.motion = 0x1D5;
        if (k->slot > 0) {
            if (r.rot[0] < 2) w.itemCounter[k->slot & 15] = 1;
            else w.itemCounter[k->slot & 15] = static_cast<uint8_t>(w.Rand(0) % r.rot[0] + 1);
        }
    }
    g_boxesLeft = 2;
}

}  // namespace

void SpawnActor31(World::Impl& w, Task& t, const SpawnRecord& r);  // world_st2_actor31.cpp
void SpawnBat(World::Impl& w, const SpawnRecord& r);                // world_st2_bats.cpp
bool SpawnKomonoSt2(World::Impl& w, const SpawnRecord& r);          // world_st2_komono.cpp

// Stage-2 spawn dispatch (first run of a spawned actor, before the stage-1 handlers)
bool World::Impl::SpawnStage2(Task& t, SpawnTask& s) {
    if (SpawnStage2c(t, s)) return true;  // world_st2c_boss.cpp: actor types 0x14 / 0x16, komono 18
    const SpawnRecord r = s.rec;
    switch (r.type) {
    case 0x21:
        SpawnActor21(*this, t, r);
        return true;
    case 0x31:
        SpawnActor31(*this, t, r);
        return true;
    case 0x2A:  // 0x00432D40: jmp Task_Kill (the type does nothing)
        ts.KillCurrent();
        return true;
    case 0x43:  // 0x00445DB0
        SpawnBat(*this, r);
        ts.KillCurrent();
        return true;
    case 0x53: {  // 0x00431250
        auto d = FromRecord<Actor53Task>(r, 0x00431250);
        d->anim.kind = 0x1A;
        d->anim.rotOrder = 1;
        d->row = r.ParamS16(0);
        d->anim.motion[0] = exe.S16(0x00589A64 + static_cast<uint32_t>(d->row) * 10);
        const int v = r.ParamS16(2);
        if (v >= 2) {  // game type 1 only
            if (runner->cfg.gameType != 1) {
                d->flags34 = (d->flags34 & ~1u) | 0x80018000u;
                ts.KillCurrent();
                return true;
            }
            d->anim.motion[0] = 0x305;  // 0x00431430: a target in stage-2 scene 8
            d->radius = 4.0f;
            d->row = 0;
            d->age = 0;
            EnemyInitPose(*d);
            d->flags34 &= ~0x80u;
            d->nodeCb = 0x00411050;
            d->fn = 0x00431430;
            t.data = std::move(d);
            t.fn = TaskActor53Type1;
            return true;
        }
        if (v == 1) d->flags38 |= 8;
        EnemyInitPose(*d);  // Anim_InitEnemy 0x00410440 (+ actor slot)
        d->anim.rotOrder = 1;
        d->flags34 &= ~0x80u;
        d->nodeCb = 0x00411050;
        d->fn = 0x00431340;
        t.data = std::move(d);
        t.fn = TaskActor53;  // first run next frame
        return true;
    }
    case 0x27: {
        auto d = FromRecord<Actor27Task>(r, 0x004329D0);
        t.data = std::move(d);
        t.fn = TaskActor27;
        TaskActor27(t);
        return true;
    }
    case 0x33:
        if (r.state == 2) {
            auto d = FromRecord<Static33Task>(r, 0x00432FF0);
            RegisterSlot(*this, *d);
            t.data = std::move(d);
            t.fn = TaskStatic33;
            TaskStatic33(t);  // only sets the function
            return true;
        }
        if (r.state == 1) {
            auto d = FromRecord<Actor33PathTask>(r, 0x00432FF0);
            RegisterSlot(*this, *d);
            t.data = std::move(d);
            t.fn = TaskActor33Path;
            TaskActor33Path(t);  // only sets the function
            return true;
        }
        if (r.state == 5) {
            auto d = FromRecord<EnemyTask>(r, 0x00432FF0);
            RegisterSlot(*this, *d);
            t.data = std::move(d);
            t.fn = TaskActor33Burst;
            TaskActor33Burst(t);  // only sets the function
            return true;
        }
        return false;
    case 0x41:
        if (SpawnKomonoSt2(*this, r)) {  // kinds 14 / 17
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 66) {
            SpawnKomono66(*this, r);
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 77) {  // generic ctor 0x00461CF0 (radius 6) -> 0x004717A0
            Komono77Task* k = NewTask<Komono77Task>(TaskKomono77, 0x004717A0);
            k->flags34 = 0x80000001u;
            k->radius = 6.0f;
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 35) {  // generic ctor 0x00461CF0 -> 0x0046B320
            Komono35Task* k = NewTask<Komono35Task>(TaskKomono35, 0x0046B320);
            k->flags34 = 0x80000001u;
            k->life = static_cast<int16_t>(r.state);
            k->area = static_cast<int8_t>(runner->variant);
            k->yaw = r.rot[1];
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 37) {  // 0x004632F0
            SpawnKomono37(*this, r);
            ts.KillCurrent();
            return true;
        }
        if (runner->cfg.gameType == 1 && modes && r.ParamS8(1) >= 70 && r.ParamS8(1) <= 72) {  // game type 1: the item
            SpawnStageItem(*this, r);  // world_modes_items.cpp
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 70 || r.ParamS8(1) == 72) {  // generic ctor 0x00461CF0 + item pick 0x004629C0 (one rand)
            Rand(0x004629DE);
            NewTask<SelfDestroyTask>(TaskSelfDestroy, 0x004675A0);  // game type 0: destroyed on its first run
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 71) {  // case 0x47: the item pick and the sway's four rands, then as 70 / 72
            Rand(0x004629DE);
            Rand(0x004627AD);
            Rand(0x004627CB);
            Rand(0x004627EA);
            Rand(0x00462808);
            NewTask<SelfDestroyTask>(TaskSelfDestroy, 0x004675A0);
            ts.KillCurrent();
            return true;
        }
        return false;
    case 0x44:
        if (r.state == 2) {  // 0x00472C90
            SpawnHanging2(*this, r);
            ts.KillCurrent();
            return true;
        }
        if (r.state == 17) {  // 0x00473A70
            Prop17Task* p = NewTask<Prop17Task>(TaskProp17, 0x00474F30);
            memcpy(p->pos, r.pos, 12);
            p->yaw = r.rot[1];
            p->rot[1] = 0;
            p->flags34 = 1;
            if (r.ParamS32(8) == -1) {
                p->radius = 8.0f;
                p->flags34 = 0x80000001u;
            } else {
                p->flags34 = 0x51;
            }
            p->model = r.ParamS16(4);
            p->objMesh = r.ParamS32(8);
            p->t1.c1DC = r.ParamS32(12);
            p->t1.kind = r.ParamS8(0);
            p->t1.flag2A0 = r.ParamS8(0x10);
            for (int i = 0; i < 4; ++i) p->t1.items[i] = r.ParamS8(0x20 + i);
            Prop17Created(*this);
            p->flagKill = r.ParamS8(0x11);
            for (int c = 0; c < 3; ++c) memcpy(&p->scale[c], &r.params[0x14 + 4 * c], 4);
            ts.KillCurrent();
            return true;
        }
        return false;
    default:
        return false;
    }
}

}  // namespace game
