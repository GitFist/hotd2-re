// game::World - stage-6 objects (st6evtbl.bin). Each function names the original it ports; status and the verification
// (world_test --stage-start 70806 on build\scene_capture_auto_st6.txt) are in notes/agents/stage6.md.

#include <cmath>
#include <cstring>

#include "game/world_st6.h"

namespace game {

namespace st6 {

St6State& State(World::Impl& w) {
    if (!w.st6) w.st6 = std::make_shared<St6State>();
    return *w.st6;
}

}  // namespace st6

using namespace st6;

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x26 state 5 (0x0048E290 -> task 0x0048F560): the lift of the stage-6 opening (model 0x1913) with its two
// floor displays (models 0xAD4.. / 0xADE.. / 0xAE8.. / 0xAF2.. = tens / units). Camera 0xDA: path 0x182 at 0, display
// "01", doors 0xAFE / 0xAFF; 0xDB: path 0x182 at the camera tick (clamped to the path end), doors 0xAFC / 0xAFD, the
// floor counts (tick - 50) / 6 + 5; 0xDC / 0xEC: fixed place, floor tick / 6 + 36 up to 50. Ends on event flag 0xE0.
struct Lift6Task : EnemyTask {};

void TaskLift6(Task& t) {
    World::Impl& w = *CurrentWorld();
    Lift6Task& a = t.As<Lift6Task>();
    if (w.runner->flags[0xE0] == 1) {
        w.ts.KillCurrent();  // Task_Kill 0x004A7040
        return;
    }
    const int cam = w.runner->camera.camId;
    int tickArg = 0;  // the task pointer's stack slot, reused for the clamped tick (camera 0xDB)
    int doors = 0, mode = 0;
    auto place = [&](float tm) {
        float p[3];
        int ang[3];
        if (st5::EvalPath(w, 0x182, tm, p, ang)) {  // 0x004042D0
            memcpy(a.pos, p, 12);
            for (int k = 0; k < 3; ++k) a.rot[k] = ang[k];
        }
    };
    auto fixed = [&] {
        a.pos[0] = Bits(0x440B6000);
        a.pos[1] = Bits(0x451BC333);
        a.pos[2] = Bits(0xC61A60CD);
        a.rot[0] = 0;
        a.rot[1] = 0x7555;
        a.rot[2] = 0;
    };
    switch (cam) {
    case 0xDA:
        place(0.0f);
        mode = 1;
        break;
    case 0xDB: {
        doors = 0x182;
        mode = 2;
        tickArg = static_cast<int>(w.exe.U32(0x00577340));  // path 0x182's end (0x00576D38 + 0x182 * 4)
        if (w.runner->camera.tick <= tickArg) tickArg = w.runner->camera.tick;
        place(static_cast<float>(tickArg));
        break;
    }
    case 0xDC:
        fixed();
        mode = 3;
        break;
    case 0xEC:
        fixed();
        mode = 4;
        break;
    default:
        break;
    }
    World::Impl::DrawMvScope mvScope(w);  // drawn from the model-view chain
    Mat4 m = w.cam.view;
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotZ(m, a.rot[2]);
    mx::RotY(m, a.rot[1]);
    mx::RotX(m, a.rot[0]);
    w.Draw(0x1913, m);
    if (cam == 0xDA) {
        Mat4 q = m;
        mx::Translate(q, 0.0f, Bits(0x40D00000), Bits(0x41780000));
        w.Draw(0xAFE, q);
        w.Draw(0xAFF, q);
    }
    if (doors != 0 || cam == 0xEC) {
        Mat4 q = m;
        mx::Translate(q, 0.0f, Bits(0x40D00000), Bits(0xC134CCCD));
        w.Draw(0xAFC, q);
        w.Draw(0xAFD, q);
    }
    int tens = 0, units = 0;
    switch (mode) {
    case 1: tens = 0, units = 1; break;
    case 2: {
        const int16_t v = static_cast<int16_t>((tickArg - 0x32) / 6 + 5);
        tens = v / 10, units = v % 10;
        break;
    }
    case 3: {
        const int tick = w.runner->camera.tick;
        if (tick < 0x54) {
            const int16_t v = static_cast<int16_t>(tick / 6 + 0x24);
            tens = v / 10, units = v % 10;
        } else {
            tens = 5, units = 0;
        }
        break;
    }
    case 4: tens = 5, units = 0; break;
    default:
        // the original draws 0xAD4 + (short)(task pointer) etc. here; no stage-6 camera reaches it
        w.Note("actor 0x26 state 5: display on camera " + std::to_string(cam) + " (0x0048F819)");
        return;
    }
    static const float kDigit[4][3] = {
        { Bits(0xC0CF1412), Bits(0x41D36E2F), Bits(0x416DDBF5) },
        { Bits(0xC0F106F7), Bits(0x41D35254), Bits(0x416DDD2F) },
        { Bits(0x40C61062), Bits(0x41D36E2F), Bits(0xC12B67A1) },
        { Bits(0x40E80347), Bits(0x41D35254), Bits(0xC12B62B7) },
    };
    const int models[4] = { tens + 0xAD4, units + 0xADE, tens + 0xAE8, units + 0xAF2 };
    for (int i = 0; i < 4; ++i) {
        Mat4 q = m;
        mx::Translate(q, kDigit[i][0], kDigit[i][1], kDigit[i][2]);
        w.Draw(models[i], q);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x26 states 6 / 7 (0x0048E290 -> task 0x0048F930): the two halves of the final room's floor bridge (6:
// models 0x1914 / 0x1915 + 0xD36 + 0x9A8, 7: 0x190C + 0x7E9 / 0x7EB) placed on camera paths 0x183 / 0x184 (0x005703B4
// [state - 6]) at 1311 on cameras 0xDD / 0xDF (0xDF from tick 0x51E: at the camera tick), then 0x0048FB40 (camera 0xDF
// from tick 0x629) or 0x0048FD00 (cameras 0xE9..0xEB, ends on camera 0xEC). Draw only.
struct Bridge6Task : EnemyTask {
    int f1320 = 0;  // +0x1320 (state 6: the whole floor 0x1914 from camera 0xDF tick 0x578)
};

void Bridge6Draw(World::Impl& w, Bridge6Task& a) {  // 0x0048FB40
    const int st = static_cast<int16_t>(a.hp);
    World::Impl::DrawMvScope mvScope(w);  // drawn from the model-view chain
    Mat4 m = w.cam.view;
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    if (st == 6) {
        if (a.f1320 == 1) {
            w.Draw(0x1914, m);
        } else {
            w.Draw(0x1915, m);
            mx::Translate(m, Bits(0x43190000), 0.0f, 0.0f);
            w.Draw(0xD36, m);
            mx::Translate(m, Bits(0xC10B70A4), Bits(0x4105460B), Bits(0xBF4EBEE0));
            mx::Scale(m, Bits(0x3DCCCCCD), Bits(0x3EDA9FBE), Bits(0x422DA8F6));
            w.LightColor(Bits(0x3D4CCCCD), Bits(0x3C23D70A), 0.0f);  // 0x004AA0A0(0.05, 0.01, 0)
            w.Draw(0x9A8, m);
            w.LightScene();  // 0x0041DCC0
        }
        const int tick = w.runner->camera.tick;
        if (w.runner->camera.camId == 0xDF && 0x56D < tick) {
            Mat4 q = w.cam.view;
            mx::Translate(q, a.pos[0], Bits(0x452DD19A),
                          static_cast<float>(tick - 0x56E) * 0.5f + a.pos[2] + F32(w.exe, 0x0055D2AC));
            w.Draw(0x1914, q);
        }
        return;
    }
    if (st != 7) return;
    w.Draw(0x190C, m);
    Mat4 q = w.cam.view;
    mx::Translate(q, Bits(0x443C3805), a.pos[1], Bits(0xC61A405C));
    if (w.runner->flags[0x31] == 0) w.Draw(0x7E9, q);
    else if (w.GlobalInt(0x009A5C48, 0) == 0) w.Draw(0x7EB, q);
}

void TaskBridge6End(Task& t) {  // 0x0048FD00
    World::Impl& w = *CurrentWorld();
    Bridge6Task& a = t.As<Bridge6Task>();
    if (w.runner->camera.camId == 0xEC) {
        w.ts.KillCurrent();
        return;
    }
    World::Impl::DrawMvScope mvScope(w);
    Mat4 m = w.cam.view;
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    const int st = static_cast<int16_t>(a.hp);
    if (st == 6) w.Draw(0x1914, m);
    else if (st == 7) w.Draw(0x190C, m);
}

void TaskBridge6Hold(Task& t) {  // 0x0048FB40 as the task function
    World::Impl& w = *CurrentWorld();
    Bridge6Draw(w, t.As<Bridge6Task>());
}

void TaskBridge6(Task& t) {  // 0x0048F930
    World::Impl& w = *CurrentWorld();
    Bridge6Task& a = t.As<Bridge6Task>();
    const int path = w.exe.S16(0x005703B4 + static_cast<uint32_t>(static_cast<int16_t>(a.hp) - 6) * 2);
    float p[3];
    int ang[3];
    auto place = [&](float tm) {
        if (!st5::EvalPath(w, path, tm, p, ang)) return false;
        a.pos[0] = p[0];
        a.pos[2] = p[2];
        for (int k = 0; k < 3; ++k) a.rot[k] = ang[k];
        return true;
    };
    if (w.runner->cfg.gameType == 3) {  // boss mode: placed at 1311 (y 0x452DD19A), then 0x0048FD00 at once
        if (place(Bits(0x44A3C000))) a.pos[1] = Bits(0x452DD19A);
        a.fn = 0x0048FD00;
        t.fn = TaskBridge6End;
        TaskBridge6End(t);
        return;
    }
    const int cam = w.runner->camera.camId, tick = w.runner->camera.tick;
    auto at1311 = [&] {  // camera 0xDD (and 0xDF before tick 0x51E)
        if (place(Bits(0x44A3C000))) a.pos[1] = static_cast<float>(static_cast<double>(p[1]) + F64(w.exe, 0x004C4C98));
        Bridge6Draw(w, a);
    };
    switch (cam) {
    case 0xDD:
        at1311();
        return;
    case 0xDF:
        if (tick < 0x51E) {
            at1311();
            return;
        }
        if (0x628 < tick) {
            a.pos[1] = Bits(0x452DD19A);
            a.fn = 0x0048FB40;
            t.fn = TaskBridge6Hold;
            Bridge6Draw(w, a);
            return;
        }
        if (0x577 < tick) a.f1320 = 1;
        if (path != 0) {
            int end = static_cast<int>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4));
            if (tick <= end) end = tick;
            if (st5::EvalPath(w, path, static_cast<float>(end), p, ang)) {
                memcpy(a.pos, p, 12);
                for (int k = 0; k < 3; ++k) a.rot[k] = ang[k];
            }
        }
        Bridge6Draw(w, a);
        return;
    case 0xE9: case 0xEA: case 0xEB:
        if (place(Bits(0x44A3C000))) a.pos[1] = Bits(0x452E619A);
        a.fn = 0x0048FD00;
        t.fn = TaskBridge6End;
        TaskBridge6End(t);
        return;
    default:
        Bridge6Draw(w, a);
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x44 state 12 (0x004734A0 -> task 0x004755B0, block 0x378): a collision-mesh model (P s32 4 model, P s32 8
// mesh) that slides along a fixed direction (speed P s32 0x10, up to P s32 0x14 units) while event flag P byte 0x20 is
// set; event flag P byte 0x21 removes it, camera 0xDD tick 0x35C ends it. The lift doors 0xAFC..0xAFF are placed in the
// lift's frame (stage scene 0: lower floor).
struct Slide44Task : ActorTask {
    float pos[3] = {};    // +0x19C
    float limit = 0;      // +0x1A8
    float dir[2] = {};    // +0x1C0 / +0x1C8 (x, z step)
    int yaw = 0;          // +0x1D0
    int16_t model = 0;    // +0x28C
    int moveFlag = 0;     // +0x2A0
    int killFlag = 0;     // +0x2A4
    int prepared = 0;     // +0x2A8 (renderer: the model's polygons flagged once)
    float moved = 0;      // +0x2C0
};

void TaskSlide44(Task& t) {
    World::Impl& w = *CurrentWorld();
    Slide44Task& k = t.As<Slide44Task>();
    if (w.runner->flags[k.killFlag & 0xFF] == 1) {
        if (k.objMesh != -1) {
            st5::DestroyActor(w, k);  // 0x00409CC0
            return;
        }
        w.ts.KillCurrent();
        return;
    }
    if (w.runner->camera.camId == 0xDD && w.runner->camera.tick == 0x35C) {
        w.ts.KillCurrent();
        return;
    }
    if (w.runner->flags[k.moveFlag & 0xFF] == 1) {
        float d = std::sqrt(k.dir[1] * k.dir[1] + k.dir[0] * k.dir[0]) + k.moved;
        k.moved = d;
        if (d < 0.0f) d = d * -1.0f;
        if (d < k.limit) {
            k.pos[0] = k.dir[0] + k.pos[0];
            k.pos[2] = k.dir[1] + k.pos[2];
        }
    }
    // pass-1 mode 0x004A79F0(9) around the draws
    auto build = [&](Mat4 m) {
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.yaw);
        return m;
    };
    const Mat4 m = build(Mat4::Identity());
    w.Draw(k.model, m, 9);
    k.m150 = build(w.cam.view);  // 0x004A8CA0 -> +0x150 (model-view)
    if (k.model == 0x189C) {
        Mat4 q = m;
        mx::Translate(q, Bits(0x4196F3EB), Bits(0x41DBFE5D), Bits(0xBED2CA58));
        mx::Scale(q, Bits(0x3C11D14E), 1.0f, Bits(0x3F28F5C3));
        w.LightColor(0.0f, Bits(0x3C23D70A), Bits(0x3C23D70A));  // 0x004AA0A0(0, 0.01, 0.01)
        w.Draw(0x1730, q, 9);
        w.LightScene();  // 0x0041DCC0
    }
    if (k.objMesh != -1) w.RegisterTarget(&k);  // 0x00405160
}

void SpawnSlide44(World::Impl& w, const SpawnRecord& r) {  // 0x004734A0
    Slide44Task* k = w.NewTask<Slide44Task>(TaskSlide44, 0x004755B0);
    memcpy(k->pos, r.pos, 12);
    k->yaw = 0;
    k->flags34 |= 0x51;
    k->model = static_cast<int16_t>(r.ParamS32(4));
    k->objMesh = r.ParamS32(8);
    const double speed = static_cast<double>(r.ParamS32(0x10));
    const double a0 = F64(w.exe, 0x00564410);
    k->dir[0] = static_cast<float>(std::sin(a0) * speed);
    k->dir[1] = static_cast<float>(std::cos(a0) * speed);
    k->limit = static_cast<float>(r.ParamS32(0x14));
    k->moveFlag = r.ParamS8(0x20);
    k->killFlag = r.ParamS8(0x21);
    k->prepared = 0;
    const int model = r.ParamS32(4);
    if (model == 0xAFC || model == 0xAFD || model == 0xAFE || model == 0xAFF) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0x440B6000), w.runner->scene == 0 ? Bits(0xC2673333) : Bits(0x451BC333), Bits(0xC61A60CD));
        mx::RotY(m, 0x7555);
        mx::Translate(m, k->pos[0], k->pos[1], k->pos[2]);
        mx::GetTranslation(m, k->pos);  // 0x004A8CC0
        k->yaw = 0x7555;
        const double a1 = F64(w.exe, 0x00569168);
        k->dir[0] = static_cast<float>(std::sin(a1) * speed);
        k->dir[1] = static_cast<float>(std::cos(a1) * speed);
    }
}


// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 61 (0x00593580[61] = 0x004641F0): nine posed skeletons in two rows (kinds 0x0059504C[i], motion 0x2F6 /
// 0x3EA for kind 0x13 / 0x3DA for kind 0x16, frame 0), task 0x004729E0 each: drawn until event flag 0 is 1
void TaskFigure61(Task& t) {
    World::Impl& w = *CurrentWorld();
    EnemyTask& e = t.As<EnemyTask>();
    if (w.runner->flags[0] == 1) {
        w.EnemyDestroy(e);  // Actor_Destroy 0x00409CC0
        return;
    }
    // 0x0041DC70 / 0x0040E0B0 / 0x004AA0E0 / 0x0041DCC0: light (renderer)
    w.AnimDraw(e);  // 0x00411090 (+ the floor shadow 0x0040A590)
    w.EnemyShadow(e);
}

void SpawnFigures61(World::Impl& w, const SpawnRecord& r) {  // 0x004641F0
    for (int i = 0; i < 9; ++i) {
        EnemyTask* e = w.NewTask<EnemyTask>(TaskFigure61, 0x004729E0);  // block 0x13F4, Task_Clear
        e->flags34 = 1;
        const float k = F32(w.exe, 0x004ECB84);
        if (i < 4) {
            e->pos[2] = Bits(0xC6161400);
            e->rot[1] = 0x8000;
            e->pos[0] = static_cast<float>(i) * k + F32(w.exe, 0x00569024);
        } else {
            e->pos[2] = Bits(0xC6158000);
            e->rot[1] = 0;
            e->pos[0] = static_cast<float>(i - 4) * k + F32(w.exe, 0x00569020);
        }
        const int kind = static_cast<int8_t>(w.exe.U32(0x0059504C + static_cast<uint32_t>(i)) & 0xFF);
        e->pos[1] = (kind == 0x19 || kind == 0x16) ? Bits(0x451CB000) : Bits(0x451CE000);
        e->actorSlot = -1;
        e->slot120 = -1;
        e->anim.kind = static_cast<int16_t>(kind);
        e->anim.motion[0] = 0x2F6;
        if (kind == 0x13) {
            e->anim.motion[0] = 0x3EA;
            e->pos[2] = Bits(0xC6162000);
        }
        if (kind == 0x16) {
            e->anim.motion[0] = 0x3DA;
            e->pos[2] = Bits(0xC6157866);
        }
        w.EnemyInitPose(*e);  // 0x00410440 (+ actor slot)
        e->nodeCb = 0x00411050;
        e->anim.rotOrder = 5;
        e->anim.counter[0] = 0;
        e->behaviour = 0;
        e->hp = static_cast<int16_t>(r.state);
        e->counter1330 = static_cast<int8_t>(w.runner->variant);  // +0x1330 = 0x009A2BB0
        e->counter1334 = 0;
        e->facing1350 = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 62 (generic ctor 0x00461CF0 case 0x3E -> task 0x0046FA10, block 0x378): eight prop-skeleton 0x1C flames
// (motion 0x1C6, scale 0.3) facing the camera at fixed points; each has its frame counter (0..100) at +0x2A0 + 4i (the
// ctor's eight rands all land in +0x2A0); removed on event flag 0
struct Komono62Task : ActorTask {
    int counter[8] = {};  // +0x2A0
    PropAnim anim;        // +0x324 {0x1C, 0x1C6, frame +0x32C, prev +0x330}
};

void TaskKomono62(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono62Task& k = t.As<Komono62Task>();
    if (w.runner->flags[0] == 1) {
        w.ts.KillCurrent();
        return;
    }
    for (int i = 0; i < 8; ++i) {
        float x, z;
        const float step = F32(w.exe, 0x004ECB84);
        if (i < 4) {
            z = -9605.0f;
            x = static_cast<float>(i) * step + F32(w.exe, 0x00569024);
        } else {
            z = -9568.0f;
            x = static_cast<float>(i - 4) * step + F32(w.exe, 0x00569130);
        }
        if (100 < ++k.counter[i]) k.counter[i] = 0;
        k.anim.frame = k.counter[i];
        const float dx = x - w.camState.pos[0], dz = z - w.camState.pos[2];
        const int16_t yaw = static_cast<int16_t>(static_cast<int>(
            static_cast<float>(std::atan2(static_cast<double>(dx), static_cast<double>(dz)) * mx::kToAngle)));  // fpatan, fmul, __ftol
        if (w.MotionResident(k.anim.motion)) {  // 0x009A4614 == 2
            World::Impl::DrawMvScope mvScope(w);  // drawn from the model-view chain
            Mat4 m = w.cam.view;
            mx::Translate(m, x, Bits(0x451CE000), z);
            mx::RotY(m, yaw + 0x8000);
            mx::Scale(m, Bits(0x3E99999A), Bits(0x3E99999A), Bits(0x3E99999A));
            const Motion* mo = w.PropMotion(k.anim.motion);
            if (mo)
                for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m))  // 0x0040DF70(+0x324, 0.3)
                    w.Draw(d.model, d.world, 8, Bits(0x3E99999A));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 63 (generic ctor 0x00461CF0, no case -> task 0x0046FB50; stage-6 scene 5, reached when every civilian
// met was rescued): its first run drops the twelve items of the table 0x005950C8 {s8 slot, x, z + 3} at y 2498.7
// (0x451C2B33) with the item helpers 0x00471BD0 (1) / 0x004723F0 (2, 5..8) / 0x004722A0 (3), then Task_Kill 0x004A7040.
// The items get the komono's +0x196 (0x009A2BB0 at its creation), +0x197 = 0 and +0x11C (the record's state).
struct Komono63Task : WTask {
    uint8_t area = 0;      // +0x196
    int16_t life = 0;      // +0x11C
};

void TaskKomono63(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono63Task& k = t.As<Komono63Task>();
    BreakableProp b;
    b.pos[1] = Bits(0x451C2B33);
    b.areaSeen = k.area;
    b.areaChanges = 0;
    b.lifetime = k.life;
    for (uint32_t e = 0x005950C8; e < 0x00595158; e += 0xC) {
        uint8_t s = 0;
        w.exe.Read(e, &s, 1);
        b.pos[0] = F32(w.exe, e + 4);
        b.pos[2] = F32(w.exe, e + 8) - F32(w.exe, 0x004C49C0);
        const int slot = static_cast<int8_t>(s);
        if (slot == 1 || slot == 2 || slot == 3 || (slot >= 5 && slot <= 8)) w.DropItem(b, slot);
    }
    w.ts.KillCurrent();  // 0x004A7040
}

void SpawnKomono63(World::Impl& w, const SpawnRecord& r) {  // 0x00461CF0 (kind 63: the common header only)
    Komono63Task* k = w.NewTask<Komono63Task>(TaskKomono63, 0x0046FB50);
    k->area = static_cast<uint8_t>(w.runner->variant);
    k->life = static_cast<int16_t>(r.state);
}

void SpawnKomono62(World::Impl& w, const SpawnRecord& r) {  // 0x00461CF0 case 0x3E
    Komono62Task* k = w.NewTask<Komono62Task>(TaskKomono62, 0x0046FA10);
    k->flags34 = 0x80000001u;
    (void)r;
    k->anim.skeleton = 0x1C;
    k->anim.motion = 0x1C6;
    for (int i = 0; i < 8; ++i) k->counter[0] = w.Rand(0x004624F1) % 100;
}
// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 55 (0x00593580[55] = 0x00463FE0 -> task 0x0046EEB0, block 0x8500): 800 debris pieces (models 0x19D + i % 48,
// alpha 0.5) at the record; from event flag 0x31 on they fly (gravity, bounce on y 2782.09 with new spins) for 141
// frames, drawn for the first 111. Pieces 0..47 keep scale 0 (only pieces 48.. get a random row and scale).
struct Debris55Task : WTask {
    float base[3] = {};          // +0x194
    int counter = 0;             // +0x1A0
    float pos[800][3] = {};      // +0x1C0
    float vel[800][3] = {};      // +0x2740
    int16_t ang[800][3] = {};    // +0x4CC0
    int16_t spin[800][3] = {};   // +0x5F80
    float scale[800] = {};       // +0x7240
};

void TaskDebris55(Task& t) {
    World::Impl& w = *CurrentWorld();
    Debris55Task& k = t.As<Debris55Task>();
    if (w.runner->flags[0x31] == 0) return;
    if (0x8C < k.counter++) {
        w.ts.KillCurrent();
        return;
    }
    const float g = F32(w.exe, 0x00569098), floorY = F32(w.exe, 0x00569114);
    for (int i = 0; i < 800; ++i) {
        float* p = k.pos[i];
        float* v = k.vel[i];
        v[1] = v[1] - g;
        p[0] = v[0] + p[0];
        p[1] = v[1] + p[1];
        p[2] = p[2] + v[2];
        for (int a = 0; a < 3; ++a) k.ang[i][a] = static_cast<int16_t>(k.ang[i][a] + k.spin[i][a]);
        if (p[1] < floorY) {
            const int r = w.Rand(0x0046EF69);
            v[1] = -((static_cast<float>(r % 0x15) * F32(w.exe, 0x004D5464) + F32(w.exe, 0x004C4D10)) * v[1]);
            k.spin[i][0] = static_cast<int16_t>(w.Rand(0x0046EF90) % 0x601 - 0x300);
            k.spin[i][1] = static_cast<int16_t>(w.Rand(0x0046EFA7) % 0x601 - 0x300);
            k.spin[i][2] = static_cast<int16_t>(w.Rand(0x0046EFBD) % 0x601 - 0x300);
        }
        if (k.counter < 0x6F) {  // || 0x009A2BAC == 0 (frames since Stage_ResetState: not 0 here)
            Mat4 m = Mat4::Identity();
            mx::Translate(m, p[0], p[1], p[2]);
            mx::RotZ(m, k.ang[i][2]);
            mx::RotY(m, k.ang[i][1]);
            mx::RotX(m, k.ang[i][0]);
            const float s = k.scale[i];
            mx::Scale(m, s, s, s);
            w.Draw(i % 0x30 + 0x19D, m, 8, 0.5f);  // 0x004185A0
        }
    }
}

void SpawnDebris55(World::Impl& w, const SpawnRecord& r) {  // 0x00463FE0
    Debris55Task* k = w.NewTask<Debris55Task>(TaskDebris55, 0x0046EEB0);
    memcpy(k->base, r.pos, 12);
    const float m = F32(w.exe, 0x0055D2B0), c = F32(w.exe, 0x004D5464);
    for (int i = 0; i < 800; ++i) {
        int row = i;
        if (0x2F < i) {
            row = w.Rand(0x0046404C) % 0x30;
            k->scale[i] = static_cast<float>(w.Rand(0x0046405B) % 0x29) * c + F32(w.exe, 0x004C4CC8);
        }
        const uint32_t e = 0x00594F2A + static_cast<uint32_t>(row) * 6;
        const float tx = static_cast<float>(w.exe.S16(e)), ty = static_cast<float>(w.exe.S16(e + 2)),
                    tz = static_cast<float>(w.exe.S16(e + 4));
        k->pos[i][0] = tx * m + k->base[0];
        k->pos[i][1] = ty * m + k->base[1] + F32(w.exe, 0x0056901C);
        k->pos[i][2] = tz * m + k->base[2];
        k->vel[i][0] = static_cast<float>(w.Rand(0x004640E6) % 0x65) * m * tx * m;
        k->vel[i][2] = static_cast<float>(w.Rand(0x0046411C) % 0x65) * m * tz * m;
        k->vel[i][1] = static_cast<float>(w.Rand(0x00464152) % 0x12D) * c + 1.0f;
        k->spin[i][0] = static_cast<int16_t>(w.Rand(0x00464179) % 0x601 - 0x300);
        k->spin[i][1] = static_cast<int16_t>(w.Rand(0x00464190) % 0x601 - 0x300);
        k->spin[i][2] = static_cast<int16_t>(w.Rand(0x004641A7) % 0x601 - 0x300);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x64 (0x00435FB0): sets the route branch 0x009C88A4 by record state (0: all civilians met were rescued
// (0x009CA0EC == 0x009A21BA) -> 1; 1 / 2: score thresholds of game type 1), then ends
// (spawned by op 0x0A = 0x00408990: a short record {type, s16 state}, +0x11C = the low half of word 1; stage-6 scene 5's
// record 0x0097A0E8 has state 1, scene 3's 0x00979B48 state 0)
void SpawnRouteCheck(World::Impl& w, const SpawnRecord& r, uint32_t op) {
    const int16_t state = op == 0x0A ? static_cast<int16_t>(r.flags & 0xFFFF) : static_cast<int16_t>(r.state);
    int& br = w.runner->camera.routeBranch;
    const int pig = w.playersInGame;  // 0x009C7000
    const int sum = w.player[0].score + w.player[1].score;
    const int one = (pig >= 0 && pig < 2) ? w.player[pig].score : 0;
    const int gt = w.runner->cfg.gameType;
    switch (state) {
    case 0:
        br = 0;
        if (static_cast<int16_t>(w.rescueTotal) == static_cast<int16_t>(w.civTotal)) {
            br = 1;
            return;
        }
        if (gt == 1) {
            if (pig == 2) {
                if (69999 < sum) br = 2;
            } else if (69999 < one) {
                br = 2;
            }
        }
        return;
    case 1:
        br = 0;
        if (gt == 1) {
            const int v = pig == 2 ? sum : one;
            if (99999 < v) br = 1;
            else if (69999 < v) br = 2;
        }
        return;
    case 2:
        br = 0;
        if ((pig == 2 ? sum : one) < 100000) br = 1;
        return;
    default:
        return;
    }
}

}  // namespace

bool World::Impl::SpawnStage6(Task& t, SpawnTask& s) {
    const SpawnRecord r = s.rec;
    if (r.type == 0x26 && r.state == 5) {  // 0x0048E290 case 5
        auto d = st5::FromRecord<Lift6Task>(r, 0x0048F560);
        t.data = std::move(d);
        t.fn = TaskLift6;
        TaskLift6(t);
        return true;
    }
    if (SpawnEnding(t, s)) return true;  // world_ending.cpp
    if (r.type == 0x2D && r.ParamS8(1) == 1) return SpawnFinalBoss(t, r);  // 0x00426A70 variant 1 (world_st6_boss.cpp)
    if (r.type == 0x64) {  // 0x00435FB0 (ends in its first run)
        SpawnRouteCheck(*this, r, s.op);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 55) {  // 0x00461CD0 -> 0x00463FE0
        SpawnDebris55(*this, r);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 61) {  // 0x00461CD0 -> 0x004641F0
        SpawnFigures61(*this, r);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 62) {  // 0x00461CD0 -> 0x00461CF0 case 0x3E
        SpawnKomono62(*this, r);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 63) {  // 0x00461CD0 -> 0x00461CF0 (no case) -> task 0x0046FB50
        SpawnKomono63(*this, r);
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x26 && (r.state == 6 || r.state == 7)) {  // 0x0048E290 cases 6 / 7
        auto d = st5::FromRecord<Bridge6Task>(r, 0x0048F930);
        t.data = std::move(d);
        t.fn = TaskBridge6;
        TaskBridge6(t);
        t.fn = TaskBridge6;  // the dispatcher sets 0x0048F930 again after the call (a switch shows next frame)
        t.As<Bridge6Task>().fn = 0x0048F930;
        return true;
    }
    if (r.type == 0x44 && r.state == 12) {  // 0x00472B10 -> 0x004734A0
        SpawnSlide44(*this, r);
        ts.KillCurrent();
        return true;
    }
    return false;
}

}  // namespace game
