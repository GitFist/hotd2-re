// game::World - stage-5 actors (st5evtbl.bin). Each function names the original it ports; status and the verification
// (world_test --stage-start 56007 on build\scene_capture_auto_st5.txt) are in notes/agents/stage5.md.

#include <cmath>
#include <cstring>

#include "game/world_st5.h"

namespace game {

namespace st5 {

St5State& State(World::Impl& w) {
    if (!w.st5) w.st5 = std::make_shared<St5State>();
    return *w.st5;
}

void RegisterSlot(World::Impl& w, EnemyTask& e) {  // 0x00409270
    e.actorSlot = -1;
    for (int i = 0; i < 14; ++i)
        if (!w.actorSlots[i]) {
            e.flags38 |= 0x40;
            w.actorSlots[i] = &e;
            e.actorSlot = i;
            return;
        }
}

void ReleaseSlot(World::Impl& w, EnemyTask& e) {  // 0x004092D0
    if (e.actorSlot >= 0 && e.actorSlot < 14) w.actorSlots[e.actorSlot] = nullptr;
}

void DestroyActor(World::Impl& w, ActorTask& a) {  // 0x00409CC0
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;  // 0x00405220
    w.ts.KillCurrent();
}

}  // namespace st5

using namespace st5;

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x2B (0x00438060): a light source by record state. The light slot (0x00480A60: slots 3..15 of the table
// 0x009A1A20, stride 0x74) and the light parameters are renderer state; what the logic sees is the task, its removal
// and the rand calls of state 0.
struct LightActorTask : EnemyTask {
    int8_t slot = 0;    // +0x131B
    int c1320 = 0;      // +0x1320 flicker level (0..7)
    int c1324 = 0;      // +0x1324 frames to the next flicker roll
};

int8_t LightAlloc(World::Impl& w) {  // 0x00480A60
    St5State& s = State(w);
    for (int i = 3; i < 16; ++i)
        if (!s.lightUsed[i]) {
            s.lightUsed[i] = true;
            return static_cast<int8_t>(i);
        }
    return 0;  // (uVar1 & 0xFFFF0000)
}

void LightFree(World::Impl& w, int slot) {  // 0x00480AA0
    if (slot >= 0 && slot < 16) State(w).lightUsed[slot] = false;
}

// The light slot's D3DLIGHT7 (0x009A1A20 + slot * 0x74; renderer state, read by the light table task 0x00480970).
// spot: direction = (0, 0, 1) * Rx(pitch) Ry(yaw) Rz(0) (0x004A8A80 after identity, RotZ(0), RotY(yaw), RotX(pitch)).
void LightSet(World::Impl& w, int slot, int type, const float pos[3], const float diffuse[3], float att0, float att2, float range,
              float theta, int pitch = 0, int yaw = 0) {
    if (slot < 0 || slot >= 16) return;
    World::Impl::LightSlot& s = w.lightSlots[slot];
    s.enabled = true;  // +0x68
    s.l.type = type;
    for (int k = 0; k < 3; ++k) {
        s.l.pos[k] = pos[k];
        s.l.diffuse[k] = diffuse[k];
    }
    if (type == 2) {
        Mat4 r = Mat4::Identity();
        mx::RotZ(r, 0);
        mx::RotY(r, yaw);
        mx::RotX(r, pitch);
        for (int k = 0; k < 3; ++k) s.l.dir[k] = r.m[8 + k];
        s.l.theta = theta;  // +0x60; phi +0x64 is never set (0)
    }
    s.l.att[0] = att0;
    s.l.att[1] = 0;
    s.l.att[2] = att2;
    s.l.range = range;
    s.l.falloff = 0;
}

void LightOff(World::Impl& w, int slot) {
    if (slot >= 0 && slot < 16) w.lightSlots[slot].enabled = false;
}

void LightEnd(World::Impl& w, LightActorTask& l) {
    if (l.slot > 2) LightOff(w, l.slot);   // light +0x68 = 0
    if (l.slot > 2) LightFree(w, l.slot);
    w.ts.KillCurrent();                    // Task_Kill 0x004A7040
}

// 0x004380B0 (state 0): flickering point light; ends when event flag 0xE1 is 1
void TaskLight0(Task& t) {
    World::Impl& w = *CurrentWorld();
    LightActorTask& l = t.As<LightActorTask>();
    if (w.runner->flags[0xE1] == 1) {
        LightEnd(w, l);
        return;
    }
    if (l.sub == 0) {
        l.slot = LightAlloc(w);
        l.c1320 = w.Rand(0x0043811E) % 8;
        l.c1324 = w.Rand(0x00438135) % 16;
        ++l.sub;
        l.c1324 += 3;
    } else if (l.sub != 1) {
        return;
    }
    if (w.altModels == 1) {
        // point light at (-897.8, 4.1, -1071.4), colour (0.2, 0.6, 8), attenuation 0.1 + level * 0x0055E1C4 d^2,
        // range 100 (renderer state)
        const float pos[3] = { Bits(0xC4603333), Bits(0x40833333), Bits(0xC485ECCD) };
        const float col[3] = { Bits(0x3E4CCCCD), Bits(0x3F19999A), 8.0f };
        LightSet(w, l.slot, 1, pos, col, Bits(0x3DCCCCCD), static_cast<float>(l.c1320) * F32(w.exe, 0x0055E1C4), 100.0f, 0.0f);
        if (--l.c1324 < 1) {
            l.c1320 = w.Rand(0x004381ED) % 8;
            l.c1324 = w.Rand(0x00438204) % 16 + 3;
        }
    } else {
        LightOff(w, l.slot);
    }
}

// 0x00438230 (state 1): spot light; ends on camera 0xAF after tick 0x6D or camera 0xB2 after tick 0x81
void TaskLight1(Task& t) {
    World::Impl& w = *CurrentWorld();
    LightActorTask& l = t.As<LightActorTask>();
    const int cam = w.runner->camera.camId, tick = w.runner->camera.tick;
    if ((cam == 0xAF && tick > 0x6D) || (cam == 0xB2 && tick > 0x81)) {
        LightEnd(w, l);
        return;
    }
    if (l.sub == 0) {
        ++l.sub;
        l.slot = LightAlloc(w);
    } else if (l.sub != 1) {
        return;
    }
    if (w.altModels == 1) {  // spot at (-125, -25, -670) pointing down (RotX 0x4000), colour 10, att 0.1, range 65536
        const float pos[3] = { -125.0f, -25.0f, -670.0f }, col[3] = { 10.0f, 10.0f, 10.0f };
        LightSet(w, l.slot, 2, pos, col, Bits(0x3DCCCCCD), 0.0f, 65536.0f, Bits(0x3EC90FDB), 0x4000, 0);
    } else {
        LightOff(w, l.slot);
    }
}

// 0x004383C0 (state 2): spot light; ends on camera 0xCC
void TaskLight2(Task& t) {
    World::Impl& w = *CurrentWorld();
    LightActorTask& l = t.As<LightActorTask>();
    if (w.runner->camera.camId == 0xCC && w.runner->camera.tick > -1) {
        LightEnd(w, l);
        return;
    }
    if (l.sub == 0) {
        ++l.sub;
        l.slot = LightAlloc(w);
    } else if (l.sub != 1) {
        return;
    }
    if (w.altModels == 1) {  // spot at (762.2, 2616, -9778.5), RotY 0x400 / RotX 0x6F00, colour 4, att 0.5, range 65536
        const float pos[3] = { Bits(0x443E8CCD), 2616.0f, -9778.5f }, col[3] = { 4.0f, 4.0f, 4.0f };
        LightSet(w, l.slot, 2, pos, col, 0.5f, 0.0f, 65536.0f, Bits(0x3E490FDB), 0x6F00, 0x400);
    } else {
        LightOff(w, l.slot);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x2D (0x00426A70) variant 0: a posed skeleton 0x4C (the stage-5 opening: the final boss watching), task
// 0x00428CD0, node callback 0x00429040. Variant 1 is the final boss (0x00426C30, stage 6): not ported here.
// (struct Actor2DTask: world_st5.h, shared with the final boss of world_st6_boss.cpp)

// 0x00429040: node models through the translucent path with +0x1370, plus the extra models of some nodes
void Actor2DNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    Actor2DTask& a = static_cast<Actor2DTask&>(e);
    const int model = e.anim.nodes[idx & 31].model;
    auto draw = [&](int id) {  // 0x00418660 (env-map uv) + 0x004185A0
        w.envMapDraw = true;
        w.Draw(id, m, 8, a.alpha1370, false, idx);
        w.envMapDraw = false;
    };
    if (model != 0x7C8 || a.mode1360 == 0) draw(model);
    const int spin = static_cast<int>(((w.seedCounter << 16) / 0x3C) & 0xFFFF);  // 0x009A32A0 (unsigned mul / shr)
    switch (model) {
    case 0x7B2: draw(0x7CD); break;
    case 0x7B3: draw(0x7CE); break;
    case 0x7B9: draw(0x7D4); break;
    case 0x7BA: draw(0x7D5); break;
    case 0x7BB: draw(0x7D6); break;
    case 0x7BC: draw(0x7D7); break;
    case 0x7BD: draw(0x7D8); break;
    case 0x7C0: draw(0x7DB); break;
    case 0x7C1: draw(0x7DC); break;
    case 0x7C2: draw(0x7DD); break;
    case 0x7C3: {
        draw(0x7DE);
        draw(0x7E6);
        Mat4 q = m;  // 0x004A9880(0)
        mx::Translate(q, Bits(0x4013F972), Bits(0x3DE0AA65), 0.0f);
        mx::RotX(q, 0x4000);
        mx::RotZ(q, spin);
        w.Draw(0x791, q, 8, -1.0f, false, idx);  // 0x00418560
        if (a.c1364 != 0) {
            Mat4 r = q;
            mx::RotX(r, spin);
            mx::RotY(r, spin);
            mx::RotZ(r, spin);
            w.Draw(static_cast<int>(w.seedCounter % 0x1E) + 0x792, r, 8, -1.0f, false, idx);
        }
        break;
    }
    case 0x7C7: draw(0x7E2); break;
    case 0x7C8:
        if (a.mode1360 == 0) {
            draw(0x7E3);
        } else if (a.mode1360 == 1) {
            if (a.c1338 < 0x3B) ++a.c1338;
            draw(a.c1338 + 0x755);
            Mat4 q = m;
            const float s = static_cast<float>(a.c1338) * F32(w.exe, 0x0055D198);
            mx::Scale(q, 0.5f, s, 0.5f);
            w.Draw(0x19B3 - w.frameCounter5C50 % 0x18, q, 8, -1.0f, false, idx);
        } else if (a.mode1360 == 2) {
            if (a.c1338 < 1) {
                a.mode1360 = 0;
                a.c1338 = 0;
            } else {
                --a.c1338;
            }
            draw(a.c1338 + 0x755);
        }
        break;
    case 0x7C9: draw(0x7E4); break;
    case 0x7CA: draw(0x7E5); break;
    default: break;
    }
}

// 0x00428F70: light colour ramp (renderer), skeleton (0x00411090) between the light calls 0x0041DC10 / 0x0041DCC0
void Actor2DDraw(World::Impl& w, Actor2DTask& a) {
    const float one = F32(w.exe, 0x004C4380);
    const float step = static_cast<float>(F64(w.exe, 0x0055D190));
    for (float& c : a.light)
        if (c < one) c = c + step;
    // 0x0041DC10(0.7, 0x009A3558, 0x009A355C, +0x1344, +0x1348, +0x1348): the original passes +0x1348 twice
    const float col[3] = { a.light[0], a.light[1], a.light[1] };
    w.LightCustom(0.7f, w.env.lightPitch, w.env.lightYaw, col);
    w.animLight = 0;
    w.AnimDraw(a);
    w.animLight = 1;
    w.LightScene();  // 0x0041DCC0
    w.EnemyShadow(a);
}

// 0x00428CD0
void TaskActor2D(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor2DTask& a = t.As<Actor2DTask>();
    const int cam = w.runner->camera.camId, tick = w.runner->camera.tick;
    if (cam == a.record.ParamS16(6) && tick >= a.record.ParamS16(8)) {
        if (a.actorSlot != -1) ReleaseSlot(w, a);  // 0x004092D0
        w.ts.KillCurrent();                        // (longjmp: Task_Kill)
        return;
    }
    if (a.sub == 0) {
        a.pos[0] = Bits(0x443C3805);
        a.pos[1] = Bits(0x45223000);
        a.pos[2] = Bits(0xC61A405C);
        a.rot[0] = 0x8000;
        a.rot[1] = 0xFAA8;
        a.rot[2] = 0;
        ++a.sub;
    }
    if (!(w.flags5900 & 0x20)) Actor2DDraw(w, a);
}

void SpawnActor2D(World::Impl& w, Task& t, const SpawnRecord& r) {  // 0x00426A70
    auto d = FromRecord<Actor2DTask>(r, 0x00428CD0);
    Actor2DTask& a = *d;
    a.behaviour = 0;
    a.sub = 0;
    a.actorSlot = -1;
    a.slot120 = -1;
    a.variant = r.ParamS8(1);
    a.anim.kind = 0x4C;
    a.anim.motion[0] = r.ParamS16(2);
    w.EnemyInitPose(a);  // 0x00410440 (-> 0x00409270(0x009A26A0 = this object): one actor slot)
    a.anim.rotOrder = 5;
    a.nodeFn = Actor2DNode;  // +0x12EC = 0x00429040
    a.postRootFn = [](World::Impl&, EnemyTask&) {};  // +0x12F0 = 0x00420810 (no-op)
    a.anim.drawFlags |= 0xC;
    a.anim.counter[0] = r.ParamS16(4);
    a.radius = F32(w.exe, 0x004C4D28 + 0x4C * 4);
    t.data = std::move(d);
    t.fn = TaskActor2D;
    TaskActor2D(t);
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x44 state 13 (0x00473640 -> task 0x004757F0, block 0x378): a collision-mesh model (P s16 4, mesh P word
// 8, flags | 0x51) that rises by 1 per frame while event flag P byte 0x20 is 1, up to P word 0x14 above the record;
// removed by event flag P byte 0x21 (Actor_Destroy) or on camera 0xDD at tick 0x35C
struct Gate44Task : ActorTask {
    float pos[3] = {};      // +0x19C
    int yaw = 0;            // +0x1D0
    int16_t model = 0;      // +0x28C
    int riseFlag = 0;       // +0x2A0
    int endFlag = 0;        // +0x2A4
    float top = 0;          // +0x2C0
};

void TaskGate44(Task& t) {
    World::Impl& w = *CurrentWorld();
    Gate44Task& k = t.As<Gate44Task>();
    if (w.runner->flags[k.endFlag & 0xFF] == 1) {
        if (k.objMesh != -1) {
            DestroyActor(w, k);  // Actor_Destroy 0x00409CC0
            return;
        }
    } else if (w.runner->camera.camId != 0xDD || w.runner->camera.tick != 0x35C) {
        if (w.runner->flags[k.riseFlag & 0xFF] == 1 && k.pos[1] < k.top) k.pos[1] = k.pos[1] + F32(w.exe, 0x004C4380);
        auto build = [&](const Mat4& base) {
            Mat4 m = base;
            mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
            mx::RotY(m, k.yaw);
            return m;
        };
        w.Draw(k.model, build(Mat4::Identity()));
        k.m150 = build(w.cam.view);  // 0x004A8CA0 -> +0x150 (model-view)
        if (k.objMesh != -1) w.RegisterTarget(&k);  // Hit_RegisterTarget 0x00405160
        return;
    }
    w.ts.KillCurrent();
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x33 state 7 (0x00432FF0 -> task 0x00433E90 from the next frame, actor slot 0x00409270): a sound list at
// the record params, entries {s16 mode, s16 time, u32 sound}: mode 0 = after `time` frames (one counter for the whole
// list), 1 = at camera tick `time`; a mode -1 entry ends the list (the task then idles; its end test reads the entry
// just played, so it never destroys itself)
struct SoundListTask : EnemyTask {
    uint32_t entry = 0;  // +0x1390 (event memory address)
    int counter = 0;     // +0x1330
    int c1350 = 0;       // +0x1350
    uint32_t c1354 = 0;  // +0x1354
};

bool MemWord(World::Impl& w, uint32_t addr, uint32_t* out) {
    uint32_t v = 0;
    if (!w.mem.Read(addr & ~3u, &v)) return false;
    *out = (addr & 2) ? (v >> 16) : v;
    return true;
}

void TaskSoundList(Task& t) {
    World::Impl& w = *CurrentWorld();
    SoundListTask& k = t.As<SoundListTask>();
    uint32_t w0 = 0, snd = 0;
    if (!MemWord(w, k.entry, &w0) || !MemWord(w, k.entry + 4, &snd)) {
        w.Note("actor 0x33 state 7: sound list unreadable");
        return;
    }
    const int16_t mode = static_cast<int16_t>(w0 & 0xFFFF), time = static_cast<int16_t>(w0 >> 16);
    if (k.sub == 0) {
        k.counter = 0;
        k.c1350 = time;
        k.c1354 = snd;
        k.sub = 1;
    } else if (k.sub != 1) {
        return;
    }
    bool fire = false;
    if (mode == 0) fire = ++k.counter == time;
    else if (mode == 1) fire = w.runner->camera.tick == time;
    if (!fire) return;
    w.Sound(snd);  // 0x0041CFD0
    k.entry += 8;
    k.sub = 1;  // (*old entry != -1 always)
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x33 states 8 / 9 (0x00432FF0 -> 0x00433FE0 / 0x00434100, from the next frame, actor slot): animated
// explosion models at the record (scale +0x118)
struct Anim33Task : EnemyTask {
    float scale = 0;     // +0x118
    int count = 0;       // +0x1330
    int model = 0;       // +0x13F0
};

void Draw33(World::Impl& w, Anim33Task& k, float dy) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1] - dy, k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::RotZ(m, k.rot[2]);
    mx::Scale(m, k.scale, k.scale, k.scale);
    w.Draw(k.model, m);
}

// 0x00433FE0 (state 8): models 0x174A..0x1785 once (scale 10), sounds 0x1523A9 / 0x2F16A9 (20 frames later)
void TaskAnim33a(Task& t) {
    World::Impl& w = *CurrentWorld();
    Anim33Task& k = t.As<Anim33Task>();
    if (k.sub == 0) {
        w.Sound(0x1523A9);
        ++k.sub;
        k.count = 0x14;
        k.scale = 10.0f;
        k.model = 0x1749;
        if (--k.count < 1) {
            w.Sound(0x2F16A9);
            ++k.sub;
        }
    } else if (k.sub == 1) {
        if (--k.count < 1) {
            w.Sound(0x2F16A9);
            ++k.sub;
        }
    }
    if (++k.model < 0x1786) {
        Draw33(w, k, F32(w.exe, 0x0055DD4C));
        return;
    }
    k.model = 0x174A;
    w.EnemyDestroy(k);  // 0x00409CC0
}

// 0x00434100 (state 9): looping models 0x1AAB..0x1AD2, pulsing scale; ends (sound 0x823A9) after P s16 2 frames (P s16
// 0 = 0) or at camera tick P s16 2 (P s16 0 = 1)
void TaskAnim33b(Task& t) {
    World::Impl& w = *CurrentWorld();
    Anim33Task& k = t.As<Anim33Task>();
    bool run = true;
    if (k.sub == 0) {
        w.Sound(0x723A9);
        ++k.sub;
        k.count = 0;
        k.scale = 10.0f;
        k.model = 0x1AAA;
    } else if (k.sub != 1) {
        run = false;
    }
    if (run) {
        const float s = k.scale + F32(w.exe, 0x004E3100);
        k.scale = s;
        if (F32(w.exe, 0x004C43A4) < s) k.scale = 10.0f;
        const int mode = k.record.ParamS16(0), time = k.record.ParamS16(2);
        if ((mode == 0 && ++k.count == time) || (mode == 1 && w.runner->camera.tick == time)) {
            w.Sound(0x823A9);
            w.EnemyDestroy(k);
            return;
        }
    }
    if (0x1AD2 < ++k.model) k.model = 0x1AAB;
    Draw33(w, k, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 65 (ctor 0x00464360 -> task 0x0046FCC0, block 0x8500): 300 debris pieces (models 0xCA5 + i % 72) with
// random offsets / velocities / spins, falling for 301 frames
struct Debris65Task : WTask {
    int count = 0;           // +0x1A0
    float pos[300][3] = {};  // +0x1C0
    float vel[300][3] = {};  // +0x2740
    int16_t ang[300][3] = {};   // +0x4CC0
    int16_t spin[300][3] = {};  // +0x5F80
    float scale[300] = {};   // +0x7240
};

void TaskDebris65(Task& t) {
    World::Impl& w = *CurrentWorld();
    Debris65Task& k = t.As<Debris65Task>();
    if (300 < k.count++) {
        w.ts.KillCurrent();
        return;
    }
    const float g = F32(w.exe, 0x00569134), zs = F32(w.exe, 0x004C43A8);
    for (int i = 0; i < 300; ++i) {
        float* p = k.pos[i];
        float* v = k.vel[i];
        v[1] = v[1] - g;
        p[0] = v[0] + p[0];
        p[1] = v[1] + p[1];
        p[2] = v[2] + p[2];
        for (int a = 0; a < 3; ++a) k.ang[i][a] = static_cast<int16_t>(k.ang[i][a] + k.spin[i][a]);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, p[0], p[1], p[2]);
        mx::RotZ(m, k.ang[i][2]);
        mx::RotY(m, k.ang[i][1]);
        mx::RotX(m, k.ang[i][0]);
        const float s = k.scale[i];
        mx::Scale(m, s, s, s * zs);
        w.Draw(i % 0x48 + 0xCA5, m);
    }
}

void SpawnDebris65(World::Impl& w) {  // 0x00464360
    Debris65Task* k = w.NewTask<Debris65Task>(TaskDebris65, 0x0046FCC0);
    const ExeImage& x = w.exe;
    const float c0 = F32(x, 0x00569030), c1 = F32(x, 0x0056902C), c2 = F32(x, 0x00569028), off = F32(x, 0x0055D2B4);
    const float vs = F32(x, 0x004D5464), v0 = F32(x, 0x0055D1A0), v2 = F32(x, 0x004C43AC);
    const float v1 = static_cast<float>(F64(x, 0x0055D7D0));
    for (int i = 0; i < 300; ++i) {
        k->pos[i][0] = (static_cast<float>(w.Rand(0x004643B9) % 0xB) + c0) - off;
        k->pos[i][1] = (static_cast<float>(w.Rand(0x004643DD) % 0xB) + c1) - off;
        k->pos[i][2] = (static_cast<float>(w.Rand(0x00464400) % 0xB) - c2) - off;
        k->vel[i][0] = static_cast<float>(w.Rand(0x00464424) % 0x51) * vs - v0;
        k->vel[i][1] = static_cast<float>(w.Rand(0x0046444B) % 0x65) * vs - v1;
        k->vel[i][2] = static_cast<float>(w.Rand(0x00464472) % 0x65) * vs + v2;
        k->spin[i][0] = static_cast<int16_t>(w.Rand(0x00464499) % 0x601 - 0x300);
        k->spin[i][1] = static_cast<int16_t>(w.Rand(0x004644B0) % 0x601 - 0x300);
        k->spin[i][2] = static_cast<int16_t>(w.Rand(0x004644C6) % 0x601 - 0x300);
        k->scale[i] = 1.5f;
    }
}

}  // namespace

// Sky_Task 0x004132D0, types 8 / 11 (0x00413620; type 8 with its light 0x0041DC10 when 0x009A5FE0 == 1) and 10
// (0x00413500): the sky model on the identity (view space) at (0, -500, -50) turned x 0xDE00 and y by -(camera yaw), or
// at (0, 3000, -50) turned x 0xDB00; the uv scroll 0x00413560 is renderer state; then the second model as usual
void World::Impl::SkyPanels() {
    if (skyType < 0 || skyType >= static_cast<int>(skyTable.size())) return;
    const SkyEntry& e = skyTable[static_cast<size_t>(skyType)];
    Mat4 m = Mat4::Identity();  // 0x004A9E10
    if (skyType == 10) {
        mx::Translate(m, 0.0f, Bits(0x453B8000), Bits(0xC2480000));
        mx::RotX(m, 0xDB00);
    } else {
        mx::Translate(m, 0.0f, Bits(0xC3FA0000), Bits(0xC2480000));
        mx::RotX(m, 0xDE00);
        mx::RotY(m, -runner->camera.ctx[runner->camera.activeCtx & 3].yaw);
    }
    int& steps = skyScroll[e.model];  // 0x00413560 before the draw unless frozen (0x009CA090 == 2)
    if (skyMode != 2) ++steps;
    auto g = [&](uint32_t a) {
        auto it = runner->globals.find(a);
        return it == runner->globals.end() ? 0u : it->second;
    };
    const bool lit = skyType == 8 && g(0x009A5FE0) == 1;  // event op 0x13: the panel's own light
    if (lit) {
        auto f = [&](uint32_t a) {
            const uint32_t u = g(a);
            float v;
            memcpy(&v, &u, 4);
            return v;
        };
        const float col[3] = { f(0x009A1A0C), f(0x009A21BC), f(0x009A21B4) };
        LightCustom(f(0x009A2444), env.lightPitch, env.lightYaw, col);  // 0x0041DC10(.., 0x009A3558 / 5C, ..)
    }
    if (WorldDraw* d = Draw(e.model, m, 8, -1.0f, false, -1, true)) d->uvScroll = steps;
    if (lit) LightScene();  // 0x0041DCC0
    if (e.model2 != 0) {
        Mat4 t = Mat4::Identity();
        mx::Translate(t, camState.pos[0], e.yOffset + camState.pos[1], camState.pos[2]);
        Draw(e.model2, t);
    }
}

// 0x004198B0 (skin deformation 0x00419980: renderer) / 0x00419B40 case 0x4C for attachment i of a skeleton-0x4C
// object: entries 0..5 draw the two models of 0x004EDA50[i] instead of their own, entries 6.. their model, all through
// the translucent path with +0x1370 (0x00418620 with the alternative models)
bool World::Impl::AttachDraw4C(EnemyTask& e, int i, uint32_t def) {
    const Kind4CTask* k = dynamic_cast<const Kind4CTask*>(&e);
    const float alpha = k ? k->alpha1370 : 1.0f;
    const uint32_t tab = 0x004ED618 + static_cast<uint32_t>(i) * 0x14;
    const int baseNode = static_cast<int>(exe.U32(tab + 0x10)) & 15;
    const bool alt = (e.flags38 & 8) && altModels != 0;
    auto draw = [&](int model) {
        // 0x00419B40 case 0x4C: 0x00418660 (env-map uv from the deformed normals: 0x004198B0 runs first) before each
        // draw; the twins 0x004EDA52[i] get the deformation with negated normals (scene.cpp)
        envMapDraw = true;
        WorldDraw* d = Draw(model, e.anim.nodes[baseNode].mv, 8, alpha, alt, baseNode, true);
        envMapDraw = false;
        if (!d) return;
        d->skinCount = 4;
        for (int s = 0; s < 4; ++s) d->skin[s] = e.anim.nodes[static_cast<int>(exe.U32(tab + 4 * s)) & 15].mv;
    };
    if (i > 5) {
        draw(static_cast<int>(exe.U32(def)));
    } else {
        draw(exe.S16(0x004EDA50 + static_cast<uint32_t>(i) * 4));
        draw(exe.S16(0x004EDA52 + static_cast<uint32_t>(i) * 4));
    }
    return true;
}

bool World::Impl::SpawnStage5(Task& t, SpawnTask& s) {
    if (SpawnCar(*this, t, s)) return true;  // world_st5_car.cpp
    const SpawnRecord r = s.rec;
    switch (r.type) {
    case 0x2B: {  // 0x00438060: function by record state
        TaskFn f = nullptr;
        uint32_t orig = 0;
        if (r.state == 0) f = TaskLight0, orig = 0x004380B0;
        else if (r.state == 1) f = TaskLight1, orig = 0x00438230;
        else if (r.state == 2) f = TaskLight2, orig = 0x004383C0;
        else return false;
        auto d = FromRecord<LightActorTask>(r, orig);
        t.data = std::move(d);
        t.fn = f;
        f(t);
        return true;
    }
    case 0x33:  // 0x00432FF0: function by state
        if (r.state == 7) {
            auto d = FromRecord<SoundListTask>(r, 0x00433E90);
            d->entry = r.addr + 0x24;
            SoundListTask* k = d.get();
            t.data = std::move(d);
            t.fn = TaskSoundList;
            RegisterSlot(*this, *k);  // 0x00409270
            return true;
        }
        if (r.state == 8 || r.state == 9) {
            auto d = FromRecord<Anim33Task>(r, r.state == 8 ? 0x00433FE0 : 0x00434100);
            Anim33Task* k = d.get();
            t.data = std::move(d);
            t.fn = r.state == 8 ? TaskAnim33a : TaskAnim33b;
            RegisterSlot(*this, *k);  // 0x00409270
            return true;
        }
        return false;
    case 0x41:  // komono: ctor table 0x00593580[kind]
        if (r.ParamS8(1) == 65) {  // 0x00464360
            SpawnDebris65(*this);
            ts.KillCurrent();
            return true;
        }
        return false;
    case 0x44:  // 0x00472B10: state table 0x00595AB8
        if (r.state == 13) {  // 0x00473640
            Gate44Task* k = NewTask<Gate44Task>(TaskGate44, 0x004757F0);
            memcpy(k->pos, r.pos, 12);
            k->yaw = r.rot[1];
            k->flags34 |= 0x51;
            k->model = r.ParamS16(4);
            k->objMesh = r.ParamS32(8);
            k->riseFlag = r.ParamS8(0x20);
            k->endFlag = r.ParamS8(0x21);
            const float y0 = r.pos[1];
            k->top = static_cast<float>(r.ParamS32(0x14)) + y0;
            ts.KillCurrent();
            return true;
        }
        return false;
    case 0x32:  // 0x0047F5F0: the stage-5 boss (world_st5_boss.cpp)
        SpawnBoss5(t, r);
        return true;
    case 0x2D:  // 0x00426A70
        if (r.ParamS8(1) == 0) {
            SpawnActor2D(*this, t, r);
            return true;
        }
        return false;
    default:
        return false;
    }
}

namespace st5 {

void Node2D(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) { Actor2DNode(w, e, idx, m); }  // 0x00429040
void Draw2D(World::Impl& w, Actor2DTask& a) { Actor2DDraw(w, a); }  // 0x00428F70

}  // namespace st5

}  // namespace game
