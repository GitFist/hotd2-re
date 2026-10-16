// game::World - stage-3 actors (st3evtbl.bin). Each function names the original it ports; CONFIRMED / CODE status and
// the verification are in notes/agents/stage3.md (world_test --stage-start 29736 on build\scene_capture_auto_st3.txt).

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

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
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

// Actor_Destroy 0x00409CC0 for a non-enemy actor object
void DestroyActor(World::Impl& w, ActorTask& a) {
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;  // 0x00405220
    w.ts.KillCurrent();
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x26 state 2 (0x0048E290 -> 0x0048EAD0): the players' boat, a collision-mesh target (P word 0 = coli mesh,
// flags | 0x51) on camera paths 0x156..0x15D / 0x199 chosen by the camera id 0x009A2D78, at the camera tick (clamped to
// the path length 0x00576D38[path]); +0x1350 = 1: yaw = the active camera's yaw + 0x8000 instead of the path's (switched
// at fixed ticks). 0x009A5C34 = the boat. Removed by event flag 0xE0.
// ---------------------------------------------------------------------------------------------------------------------
struct BoatTask : EnemyTask {
    int faceCamera = 0;  // +0x1350
};

void TaskBoat(Task& t) {
    World::Impl& w = *CurrentWorld();
    BoatTask& a = t.As<BoatTask>();
    if (w.runner->flags[0xE0] == 1) {
        w.EnemyDestroy(a);
        return;
    }
    if (a.sub == 0) {
        a.flags34 |= 0x51;
        a.objMesh = a.record.ParamS32(0);
        w.obj5C34 = &a;
        RegisterSlot(w, a);
        ++a.sub;
    }
    const int camId = w.runner->camera.camId, tick = w.runner->camera.tick;
    int path = -1;
    auto at = [&](std::initializer_list<int> on, std::initializer_list<int> off) {
        for (int v : on)
            if (tick == v) a.faceCamera = 1;
        for (int v : off)
            if (tick == v) a.faceCamera = 0;
    };
    switch (camId) {
    case 0x7C: path = 0x156; at({ 0x3A2, 0x140 }, { 0x29E, 0x401 }); break;
    case 0x7D: path = 0x157; at({ 0x3F7, 0x2EE }, { 0x33E, 0x45B }); break;
    case 0x7E: path = 0x158; at({ 0x24E, 0 }, { 0x145, 0x339 }); break;
    case 0x7F: path = 0x159; at({ 0x537 }, { 0x794 }); break;
    case 0x82: path = 0x15A; at({ 0xE6, 0x3C5, 0x5C8 }, { 0x483, 0x15E, 0x6AE }); break;
    case 0x85: path = 0x15B; at({ 0x1DB, 200, 0x33E, 0x3D9, 0x46F }, { 0x154, 0x24E, 0x375, 0x410, 0x4BA }); break;
    case 0x86: path = 0x15C; at({ 0 }, { 0xBE }); break;
    case 0x87: path = 0x15D; at({ 0x1D1, 0 }, { 0x149, 0x334 }); break;
    case 0xF6: case 0xF7: case 0xF8: path = 0x199; break;
    default: break;
    }
    if (path >= 0) {
        int lim = static_cast<int>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4));
        if (tick <= lim) lim = tick;
        float p[3];
        int ang[3];
        if (EvalPath(w, path, static_cast<float>(lim), p, ang)) {
            a.pos[0] = p[0];
            a.pos[1] = p[1] + F32(w.exe, 0x004E30F0);
            a.pos[2] = p[2];
            a.rot[0] = ang[0];
            a.rot[1] = a.faceCamera == 0 ? ang[1] : w.cam.yaw + 0x8000;  // 0x009A60D0[ctx]
            a.rot[2] = ang[2];
        }
    }
    auto ops = [&](Mat4& m) {
        mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotZ(m, a.rot[2]);
        mx::RotY(m, a.rot[1]);
        mx::RotX(m, a.rot[0]);
    };
    Mat4 m = Mat4::Identity();
    ops(m);
    w.Draw(0x1A37, m);
    Mat4 mv = w.cam.view;  // +0x150 = model-view (0x004A8CA0)
    ops(mv);
    a.m150 = mv;
    mx::Transform(w.cam.view, a.pos, a.viewPos);  // +0x70
    w.RegisterTarget(&a);  // 0x00405160
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 1 (0x00462F70 -> task 0x0046E3A0, block 0x44): a water surface model 0x00593DA4[P byte 0] drawn as placed
// (identity), living +0x11C area changes (0x009A2BB0); stage 3: removed by event flag P byte 0 + 11 or flag 4.
// The per-frame wave of the model's vertex data (strips with flag 1, sin / cos of 0x009A2BAC * 0x180 + ..., scale
// 0x00569108) changes the model itself and is left to the renderer.
// ---------------------------------------------------------------------------------------------------------------------
struct Komono1Task : ActorTask {
    int8_t kind = 0;      // +0x34 (P byte 0)
    int8_t life = 0;      // +0x35
    int8_t area = 0;      // +0x36
    int8_t areaCount = 0; // +0x37
    uint8_t flag = 0;     // +0x38
    int model = 0;        // +0x40
};

void TaskKomono1(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono1Task& k = t.As<Komono1Task>();
    const int mode = w.cfg.eventMode;
    if (mode == 1 && w.runner->flags[0x77] != 0) return DestroyActor(w, k);
    if (static_cast<int8_t>(w.runner->variant) != k.area) {
        if (k.life < ++k.areaCount) return DestroyActor(w, k);
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.GlobalInt(0x009CA08C, 0) != 3) {
        if (mode == 2) {
            if (w.runner->flags[k.flag] == 1) {
                w.ts.KillCurrent();
                return;
            }
        } else if (mode == 1 && w.runner->scene == 0x23 && w.runner->variant == 2) {
            w.ts.KillCurrent();
            return;
        }
        if (k.model == 0x13B5 && w.runner->variant == 0xF) {
            w.ts.KillCurrent();
            return;
        }
        if (mode == 2 && w.runner->flags[4] != 0) {
            w.ts.KillCurrent();
            return;
        }
    }
    // the wave: every material TSP | 0x2000 (bilinear), the uv of the inline vertices with z <= 0x00569110 (all when
    // P byte 0 != 0) += A * (sin, cos) of 0x009A2BAC * 0x180 + ftol(x or z) * 600 (A = double 0x00569108; 24-bit x87,
    // fsin / fcos unrounded; v stored | 1). The loaded model keeps the sums (several tasks of one model all add).
    const int id0 = k.model;
    const bool loaded = !w.residency || (id0 > 0 && id0 < static_cast<int>(w.modelLoaded.size()) && w.modelLoaded[static_cast<size_t>(id0)] != 0);
    const auto& fl = w.runner->flags;
    const int camId = w.runner->camera.camId, tick = w.runner->camera.tick;
    if (loaded && (id0 != 0x13A7 || fl[8] != 0) && (id0 != 0x13A0 || camId == 0x6E) && fl[0x6A] == 0 &&
        (camId != 0x7E || tick != 0x163) && (w.GlobalInt(0x009CA08C, 0) != 2 || (fl[0xF1] != 0 && fl[0xF2] == 0))) {
        const auto& rec = w.InlineRecords(id0);
        std::vector<std::array<float, 2>>& uv = w.waterUv[id0];
        if (uv.size() != rec.size()) {
            uv.resize(rec.size());
            for (size_t i = 0; i < rec.size(); ++i) memcpy(uv[i].data(), &rec[i][6], 8);
        }
        const int e = w.frameCounter2BAC * 0x180;
        double amp = 0;
        w.exe.Read(0x00569108, &amp, 8);
        const float zmax = F32(w.exe, 0x00569110);
        for (size_t i = 0; i < rec.size(); ++i) {
            float x, z;
            memcpy(&x, &rec[i][0], 4);
            memcpy(&z, &rec[i][2], 4);
            if (k.kind == 0 && !(z <= zmax)) continue;
            const int a = (e + static_cast<int>(x) * 600) & 0xFFFF;  // _ftol
            const float pa = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(a * mx::kToRad))) * amp);
            uv[i][0] = pa + uv[i][0];
            const int b = (e + static_cast<int>(z) * 600) & 0xFFFF;
            const float pb = static_cast<float>(std::cos(static_cast<double>(static_cast<float>(b * mx::kToRad))) * amp);
            float v = pb + uv[i][1];
            uint32_t vb;
            memcpy(&vb, &v, 4);
            vb |= 1;
            memcpy(&uv[i][1], &vb, 4);
        }
        auto edit = std::make_shared<ModelEdit>();
        edit->tspOr = 0x2000;
        for (size_t i = 0; i < uv.size(); ++i) {
            ModelEdit::Uv u;
            u.index = static_cast<int>(i);
            u.uv[0] = uv[i][0];
            u.uv[1] = uv[i][1];
            edit->uvs.push_back(u);
        }
        w.modelEdits[id0] = std::move(edit);
    }
    const Mat4 id = Mat4::Identity();
    w.Draw(k.model, id);
    if (k.model == 0x13A7) w.Draw(0x13A5, id);
    if (k.model == 0x13A9) w.Draw(0x13AC, id);
    if (w.runner->flags[9] != 0) {
        if (k.model == 0x13A7) k.model = 0x13A9;
        else if (k.model == 0x13A0) k.model = 0x13A2;
    }
    if (k.model == 0x13A2 && w.runner->camera.camId == 0x6E) k.model = 0x13A0;
}

// Komono lifetime (life +0x11C, area +0x196 / +0x197): false = Actor_Destroy'd
template <class T> bool KomonoAlive(World::Impl& w, T& k) {
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        DestroyActor(w, k);
        return false;
    }
    if (static_cast<int8_t>(w.runner->variant) != k.area) {
        if (k.life < ++k.areaCount) {
            DestroyActor(w, k);
            return false;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    return true;
}

// 0x004650F0(flags, score): the shooter (rand when both gun bits), +10 points when score, hits + 1
int PropShooter(World::Impl& w, uint32_t flags, bool score) {
    int p;
    if ((flags & 2) && (flags & 4)) p = w.Rand(0x00465106) & 1;
    else p = (flags & 2) ? 0 : 1;
    if (score && w.GlobalInt(0x009CA08C, 0) != 2) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

// Aim effect 0x00465950 (created by 0x00465860(obj, player)): models 0x905..0x919 facing the screen at the player's aim
// on the object's view depth, z replaced by the object's +0x1A4
struct AimFxTask : WTask {
    float pos[3] = {};  // +0x34
    int roll = 0;       // +0x44
    int model = 0x904;  // +0x48
    float scale = 1.0f; // +0x4C
};

void TaskAimFx(Task& t) {
    World::Impl& w = *CurrentWorld();
    AimFxTask& s = t.As<AimFxTask>();
    if (++s.model > 0x919) {
        w.ts.KillCurrent();
        return;
    }
    Mat4 m = w.cam.view;
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::ClearRotation(m);  // 0x004A9F70
    mx::RotZ(m, s.roll);
    mx::Scale(m, s.scale, s.scale, s.scale);
    w.Draw(s.model, m, 8, -1.0f, false, -1, true);
}

}  // namespace

// 0x00465860(obj, player): aim effect at the player's aim on the object's view depth (+0x78), z = the object's +0x1A4
void World::Impl::SpawnAimFx(float viewZ, int p, float z) {
    World::Impl& w = *this;
    AimFxTask* s = w.NewTask<AimFxTask>(TaskAimFx, 0x00465950);
    const float fx = (w.player[p].aim[0] * viewZ) / w.focal, fy = (w.player[p].aim[1] * viewZ) / w.focal;
    const float local[3] = { -fx, -fy, viewZ };
    mx::Transform(w.cam.inverse, local, s->pos);
    s->pos[2] = z;
}

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 43 (generic ctor 0x00461CF0 case 0x2B -> task 0x0046CEA0, block 0x378): a crate / barrel floating in the
// water (bobbing on a sine, random tumbling), table 0x00593DB8[record rot z] = {prop skeleton, break motion, sound,
// radius, ...}; shot: sound, spark, +10 / hit, the break motion (prop skeleton), then the item P byte 0 (1: a life
// 0x00415630, torch 0x10C3 + the life models 0x1256 / 0x1257). Entry 3 (skeleton 0, model 0x19E8) first only drops.
// Game type 1 (0x009CA08C == 1): item 2 is an item by the stage table (0x004629C0), taken into the inventory.
// ---------------------------------------------------------------------------------------------------------------------
struct Komono43Task : ActorTask {
    int16_t life = 0;          // +0x11C
    int8_t area = 0;           // +0x196
    int8_t areaCount = 0;      // +0x197
    int8_t item = 0;           // +0x194
    int16_t idx = 0;           // +0x290
    int16_t model = 0;         // +0x28C
    int16_t model2 = -1;       // +0x28E (game type 1 item 2: the item's second model)
    float scale = 1.0f;        // +0x2C4 (game type 1 item 2)
    int yaw1F4 = 0;            // +0x1F4 (game type 1 item 2: spin)
    float pos[3] = {};         // +0x19C
    float baseY = 0;           // +0x1AC
    float viewOff = 0;         // +0x1B8
    int rot[3] = {};           // +0x1CC / +0x1D0 / +0x1D4
    int spinX = 0, spinZ = 0;  // +0x1D8 / +0x1E0
    int phaseStep = 0;         // +0x1DC
    int phase = 0;             // +0x1E8
    float amp = 0;             // +0x2C0
    int itemTimer = 0;         // +0x2A0
    int itemModel = 0;         // +0x2A4
    PropAnim anim;             // +0x324 skeleton, +0x328 motion, +0x32C frame
};

void TaskKomono43(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono43Task& k = t.As<Komono43Task>();
    const ExeImage& exe = w.exe;
    if (!KomonoAlive(w, k)) return;
    const uint32_t f = k.flags34;
    if ((f & 8) && !(f & 0x40000000) && k.idx != -1) {
        k.flags34 = f & ~8u;
        if (k.anim.frame == 0) {
            w.Sound(exe.U32(0x00593DBC + static_cast<uint32_t>(k.idx) * 12));
            w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0
            if (k.anim.skeleton == 0 && k.model == 0x19E8) {
                PropShooter(w, k.flags34, false);
                k.model = 0x19E6;
                k.rot[1] = w.cam.yaw;  // 0x009A60D0[ctx]
                w.SpawnAimFx(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, k.pos[2]);  // 0x00465860
            } else {
                PropShooter(w, k.flags34, true);
                k.anim.frame = 1;
                if (k.model == 0x19E6) {
                    k.radius = 5.0f;
                    k.viewOff = 1.5f;
                    double d = 0;
                    exe.Read(0x00569100, &d, 8);
                    k.pos[1] = k.pos[1] - static_cast<float>(d);
                }
                if (k.item == 2 && w.GlobalInt(0x009CA08C, 0) == 1) {  // an item by 0x004629C0(obj, 0) (into +0x290 / +0x28C..)
                    ItemPick(w, 0, &k.idx, &k.model, &k.model2, &k.scale);
                    double d = 0;
                    exe.Read(0x004C4CA8, &d, 8);
                    k.pos[1] = k.pos[1] + static_cast<float>(d);
                }
            }
        } else if (k.item > 0) {
            k.flags34 = (f & ~8u) | 0x40000000u;
            if (k.item == 1) {
                w.Sound(0x3616A9);
                k.itemTimer = 1;
                const uint32_t g = k.flags34 & 2;
                int p;
                if (g == 0 || !(k.flags34 & 4)) p = g == 0 ? 1 : 0;
                else p = w.Rand(0x0046D0AA) & 1;
                w.AddLife(p);  // 0x00415630
                k.model = static_cast<int16_t>(p * 0x32 + 0x116A);
                k.itemModel = p + 0x1256;
            } else if (w.GlobalInt(0x009CA08C, 0) == 1) {  // the picked item: inventory, caption, the shooter's digits
                w.Sound(0x3616A9);
                k.itemTimer = 1;
                ItemInventoryCaption(w, k.idx);  // 0x009C90C0[+0x290] + 1, 0x00475E40
                const uint32_t g = k.flags34 & 2;
                if (g == 0 || !(k.flags34 & 4)) k.model = static_cast<int16_t>(g == 0 ? 0x119C : 0x116A);
                else k.model = static_cast<int16_t>((w.Rand(0x0046D19B) & 1) * 0x32 + 0x116A);
            }
        }
    }
    k.flags34 &= ~6u;
    if (k.anim.frame > 0 && ++k.anim.frame > w.MotionTicks(k.anim.motion) - 2 && k.item == 0) {
        DestroyActor(w, k);
        return;
    }
    {  // bobbing
        const int old = k.phase;
        k.phase = old + k.phaseStep;
        double k2pi = 0;
        exe.Read(0x004C4370, &k2pi, 8);
        const float a = static_cast<float>(static_cast<double>(old) * k2pi);
        const float prod = static_cast<float>(std::sin(static_cast<double>(a)) * static_cast<double>(k.amp));
        k.pos[1] = prod + k.baseY;
        if ((k.phase & 0xFFFF) == 0) {
            const int s1 = w.Rand(0x0046D26D) & 1;
            const int r1 = w.Rand(0x0046D288);
            k.spinX = (s1 * -0x100 + 0x90) - r1 % 0x21;
            const int s2 = w.Rand(0x0046D29D) & 1;
            const int r2 = w.Rand(0x0046D2B8);
            k.spinZ = (s2 * -0x100 + 0x90) - r2 % 0x21;
            const int r3 = w.Rand(0x0046D2CD);
            k.amp = static_cast<float>(r3 % 0x33) * F32(exe, 0x004D5464) + F32(exe, 0x004C4C58);
        }
    }
    k.spinX = k.spinX - (k.rot[0] + k.spinX) / 0x30;
    k.spinZ = k.spinZ - (k.rot[2] + k.spinZ) / 0x30;
    k.rot[2] = k.spinZ + k.rot[2];
    k.rot[0] = k.spinX + k.rot[0];
    auto place = [&](Mat4& m, float y) {
        mx::Translate(m, k.pos[0], y, k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
    };
    if (w.MotionResident(k.anim.motion) && k.anim.frame < w.MotionTicks(k.anim.motion) - 2) {
        Mat4 m = Mat4::Identity();
        place(m, k.pos[1]);
        if (k.anim.skeleton == 0 && k.model == 0x19E8) {
            w.Draw(k.model, m);
        } else if (k.anim.frame == 0 && k.anim.skeleton == 7) {
            mx::Translate(m, 0.0f, Bits(0x3F4CCCCD), 0.0f);
            w.Draw(0x17A9, m);
        } else {
            for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *w.PropMotion(k.anim.motion), k.anim, m)) {  // 0x0040DD90
                if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                if (d.model != 0) w.Draw(d.model, d.world, 8, -1.0f, d.alt);
            }
        }
    }
    if (k.idx == -1) return;
    if (k.item >= 1 && k.anim.frame >= 1) {
        if (k.item == 1) {
            Mat4 m = Mat4::Identity();
            place(m, k.pos[1] + F32(exe, 0x004C49C0));
            mx::Scale(m, 3.0f, 3.0f, 3.0f);
            if (k.itemTimer < 0x19) w.Draw(0x10C3, m);
            else w.Draw(0x10C3, m, 8, F32(exe, 0x004C4380) - static_cast<float>(k.itemTimer) * F32(exe, 0x004E3100));
        } else if (w.GlobalInt(0x009CA08C, 0) == 1) {  // the item model (+0x28C) spinning (+0x1F4 += 0x400), the second one
            k.yaw1F4 += 0x400;                         // facing the view at z + 1.5; fading after 0x18 frames taken
            const float alpha = k.itemTimer < 0x19 ? -1.0f : F32(exe, 0x004C4380) - static_cast<float>(k.itemTimer) * F32(exe, 0x004E3100);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
            mx::RotZ(m, k.rot[2]);
            mx::RotY(m, k.yaw1F4);
            mx::RotX(m, k.rot[0]);
            mx::Scale(m, k.scale, k.scale, k.scale);
            w.Draw(k.model, m, 8, alpha);
            if (k.model2 != -1) {
                Mat4 v = w.cam.view;
                mx::Translate(v, k.pos[0], k.pos[1], k.pos[2]);
                mx::ClearRotation(v);  // 0x004A9F70
                mx::Translate(v, 0.0f, 0.0f, 1.5f);
                mx::Scale(v, k.scale, k.scale, k.scale);
                w.Draw(k.model2, v, 8, 0x18 < k.itemTimer ? alpha : -1.0f, false, -1, true);
            }
        }
        if (k.itemTimer > 0) {
            if (k.item == 1) {
                double d4 = 0;
                exe.Read(0x004C4CA8, &d4, 8);
                Mat4 m = w.cam.view;
                mx::Translate(m, k.pos[0], static_cast<float>(k.pos[1] + d4), k.pos[2]);
                mx::ClearRotation(m);  // 0x004A9F70
                mx::Scale(m, 1.5f, 1.5f, 1.5f);
                w.Draw(k.itemModel, m, 8, -1.0f, false, -1, true);
            }
            Mat4 m = Mat4::Identity();
            mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
            mx::RotY(m, k.rot[1]);
            w.Draw(k.model - 1 + k.itemTimer, m);
        }
        if (k.itemTimer > 0 && ++k.itemTimer > 0x31) {
            DestroyActor(w, k);
            return;
        }
    }
    const float p[3] = { k.pos[0], k.viewOff + k.pos[1], k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

void SpawnKomono43(World::Impl& w, const SpawnRecord& r) {
    Komono43Task* k = w.NewTask<Komono43Task>(TaskKomono43, 0x0046CEA0);
    const ExeImage& exe = w.exe;
    memcpy(k->pos, r.pos, 12);
    k->rot[0] = r.rot[0];
    k->rot[1] = r.rot[1];
    k->rot[2] = r.rot[2];
    k->life = static_cast<int16_t>(r.state);
    k->area = static_cast<int8_t>(w.runner->variant);
    k->areaCount = 0;
    k->flags34 = 0x80000001u;
    k->idx = static_cast<int16_t>(r.rot[2]);
    k->item = r.ParamS8(0);
    const uint32_t e = 0x00593DB8 + static_cast<uint32_t>(k->idx) * 12;
    k->radius = static_cast<float>(exe.S16(e + 8));
    k->anim.skeleton = exe.S16(e);
    k->anim.motion = exe.S16(e + 2);
    k->baseY = r.pos[1];
    const int s1 = w.Rand(0x004622AD) & 1;
    const int r1 = w.Rand(0x004622CB);
    k->phaseStep = 0x200;
    k->spinX = (s1 * -0xA0 + 0x60) - r1 % 0x21;
    const int s2 = w.Rand(0x004622EA) & 1;
    const int r2 = w.Rand(0x00462308);
    k->amp = 1.5f;
    k->spinZ = (s2 * -0xA0 + 0x60) - r2 % 0x21;
    k->model = static_cast<int16_t>(k->idx != 3 ? -1 : 0x19E8);
    k->rot[0] = 0;
    k->rot[2] = 0;
    k->viewOff = static_cast<float>(exe.S16(0x00593DC2));  // indexed by the spawn object's +0x131B (0)
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x44 state 11 (0x00473410 -> task 0x004753F0, block 0x378): a model (P s16 2) placed at the record that
// shakes (rand jitter scaled by +0x2C0, *0.95 per frame) while event flag P s8 0x20 is clear (model 0xA58 only) and
// rises once it is set (model 0xA58 to y 20 accelerating by 0.1, others to y 35 by 0.01); removed by flag P s8 0x21.
// ---------------------------------------------------------------------------------------------------------------------
struct Rising44Task : ActorTask {
    float pos[3] = {};    // +0x19C
    float rise = 0;       // +0x1C4
    int yaw = 0;          // +0x1D0
    int16_t model = 0;    // +0x28C
    int flagUp = 0;       // +0x2A0
    int flagKill = 0;     // +0x2A4
    int rising = 0;       // +0x2A8
    float shake = 0;      // +0x2C0
};

void TaskRising44(Task& t) {
    World::Impl& w = *CurrentWorld();
    Rising44Task& a = t.As<Rising44Task>();
    const ExeImage& exe = w.exe;
    if (w.runner->flags[a.flagKill & 0xFF] == 1) {
        w.ts.KillCurrent();
        return;
    }
    float dx = 0, dz = 0;
    if (a.model == 0xA58 && w.runner->flags[a.flagUp & 0xFF] == 0) {
        if (a.shake < F32(exe, 0x0055CB98)) a.shake = 1.0f;  // (0x00420810(2, 0x14): no-op)
        const int r1 = w.Rand(0x00475472) % 0x191;
        dx = (static_cast<float>(r1) - F32(exe, 0x0055E1D0)) * a.shake * F32(exe, 0x004D5464);
        const int r2 = w.Rand(0x0047549D) % 0x65;
        dz = (static_cast<float>(r2) - F32(exe, 0x0055D2AC)) * a.shake * F32(exe, 0x004D5464);
        a.shake = a.shake * F32(exe, 0x0055CB40);
    }
    if (w.runner->flags[a.flagUp & 0xFF] == 1) {
        if (a.rising == 0) {
            a.rising = 1;
            a.rise = 0.5f;
        }
        if (a.model == 0xA58) {
            if (a.pos[1] < F32(exe, 0x004C4C8C)) {
                a.rise = a.rise + F32(exe, 0x004C4CC8);
                a.pos[1] = a.rise + a.pos[1];
            }
        } else if (a.pos[1] < F32(exe, 0x004E30E4)) {
            a.rise = a.rise + F32(exe, 0x004C4CC0);
            a.pos[1] = a.rise + a.pos[1];
        }
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, dx + a.pos[0], a.pos[1], dz + a.pos[2]);
    mx::RotY(m, a.yaw);
    w.Draw(a.model, m);
}

}  // namespace

// Player_AddLife 0x00415630: lives + 1 up to 0x009A2440 (5), else +300 points
void World::Impl::AddLife(int p) {
    if (player[p].lives >= (runner->cfg.gameType == 1 ? ItemLivesMax(*this, p) : 5)) {  // game type 1: items +5
        AddScore(p, 300);
        return;
    }
    ++player[p].lives;
}

// Stage-3 spawn dispatch (first run of a spawned actor, before the stage-2 / stage-1 handlers)
bool World::Impl::SpawnStage3(Task& t, SpawnTask& s) {
    const SpawnRecord r = s.rec;
    switch (r.type) {
    case 0x44:
        if (r.state == 11) {  // 0x00473410
            Rising44Task* a = NewTask<Rising44Task>(TaskRising44, 0x004753F0);
            memcpy(a->pos, r.pos, 12);
            a->yaw = r.rot[1];
            a->flags34 = 0x51;
            a->model = r.ParamS16(4);
            a->objMesh = r.ParamS32(8);
            a->flagUp = r.ParamS8(0x20);
            a->flagKill = r.ParamS8(0x21);
            ts.KillCurrent();
            return true;
        }
        if (r.state == 14) {  // 0x004736D0 (world_st3_boss.cpp)
            SpawnDecor44(r);
            ts.KillCurrent();
            return true;
        }
        return false;
    case 0x46:  // 0x0042D9C0
        SpawnBirds(t, s);
        return true;
    case 0x45:  // 0x0041FC00 (world_st3_boss.cpp)
        SpawnHydra(t, s);
        return true;
    case 0x13:  // 0x0043FE10
        SpawnVehicle(t, s);
        return true;
    case 0x51:  // 0x00438540
        SpawnFish(t, s);
        return true;
    case 0x26:
        if (r.state == 2) {
            auto d = std::make_unique<BoatTask>();
            d->fn = 0x0048EAD0;
            d->record = r;
            d->flags34 = r.flags | 1;
            memcpy(d->pos, r.pos, 12);
            memcpy(d->rot, r.rot, 12);
            d->hp = d->hpMax = static_cast<int16_t>(r.state);
            d->lo = static_cast<int16_t>(r.lo);
            t.data = std::move(d);
            t.fn = TaskBoat;
            TaskBoat(t);
            return true;
        }
        return false;
    case 0x41:
        if (r.ParamS8(1) == 43) {  // generic ctor 0x00461CF0
            SpawnKomono43(*this, r);
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 1) {  // 0x00462F70
            Komono1Task* k = NewTask<Komono1Task>(TaskKomono1, 0x0046E3A0);
            k->kind = r.ParamS8(0);
            k->life = static_cast<int8_t>(r.state);
            k->model = exe.S16(0x00593DA4 + static_cast<uint32_t>(k->kind) * 2);
            k->area = static_cast<int8_t>(runner->variant);
            k->areaCount = 0;
            if (cfg.eventMode == 2) k->flag = static_cast<uint8_t>(k->kind + 11);
            ts.KillCurrent();
            return true;
        }
        return false;
    default:
        return false;
    }
}

}  // namespace game
