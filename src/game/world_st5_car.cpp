// game::World - stage 5: the players' car (actor 0x26 state 4) and its wreck, komono kind 51. Status and verification:
// notes/agents/stage5.md.

#include <cmath>
#include <cstring>

#include "game/world_st5.h"

namespace game {

using namespace st5;

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x26 state 4 (0x0048E290 -> 0x00475A50 + task 0x0048F190): the players' car of the stage-5 opening. On
// cameras 0xCD / 0xCE / 0xCF / 0xD1 it follows camera paths 0x17A..0x17D (at the camera tick, clamped to the path
// end); camera 0xCC places it; ends when event flag 0xE0 is 1. Draw only.
struct Car5Task : EnemyTask {
    int c1324 = 0;   // +0x1324 (the right front wheel mirrors +0x1334)
    int onGround = 0;  // +0x1328 (1: the shadow at 0x009C8E58)
    int wheel = 0;   // +0x1330 wheel roll
    int steer = 0;   // +0x1334 steering
    int faceCam = 0;  // +0x1350 (1: yaw = camera yaw + 0x8000)
};

// 0x00475B50 (block 0xD14, created by 0x00475A50): the car's body / wreck in the car's frame; after event flag 0x0D the
// wreck model 0x8CA and 45 pieces (models 0x89A + i) fly for 60 frames
struct Wreck5Task : WTask {
    Car5Task* car = nullptr;  // +0xD10
    uint8_t broken = 0;       // +0x1B9
    int counter = 0;          // +0x1C0
    float pos[45][3] = {};    // +0x238
    float vel[45][2] = {};    // +0x5A4 (x, y) per piece (stride 12)
    int16_t ang[45][3] = {};  // +0x910
    int16_t spin[45][3] = {}; // +0xAC6
};

Mat4 CarFrame(const EnemyTask& c) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, c.pos[0], c.pos[1], c.pos[2]);
    mx::RotZ(m, c.rot[2]);
    mx::RotY(m, c.rot[1]);
    mx::RotX(m, c.rot[0]);
    return m;
}

void TaskWreck5(Task& t) {
    World::Impl& w = *CurrentWorld();
    Wreck5Task& k = t.As<Wreck5Task>();
    if (w.runner->flags[0xE0] == 1) {
        w.ts.KillCurrent();
        return;
    }
    if (w.runner->flags[0x0D] == 1 && k.broken == 0) k.broken = 1;
    // 0x004A79F0(0xC) ... (8): render state
    const Mat4 m = CarFrame(*k.car);
    if (k.broken == 0) {
        w.Draw(0x8C9, m);
        return;
    }
    w.Draw(0x8CA, m);
    if (k.counter++ < 0x3C) {
        const float g = F32(w.exe, 0x0055CB10), dz = F32(w.exe, 0x004C43AC);
        for (int i = 0; i < 0x2D; ++i) {
            k.vel[i][1] = k.vel[i][1] - g;
            k.pos[i][0] = k.vel[i][0] + k.pos[i][0];
            k.pos[i][1] = k.pos[i][1] + k.vel[i][1];
            k.pos[i][2] = k.pos[i][2] - dz;
            for (int a = 0; a < 3; ++a) k.ang[i][a] = static_cast<int16_t>(k.ang[i][a] + k.spin[i][a]);
            Mat4 q = m;
            mx::Translate(q, k.pos[i][0], k.pos[i][1], k.pos[i][2]);
            mx::RotZ(q, k.ang[i][2]);
            mx::RotY(q, k.ang[i][1]);
            mx::RotX(q, k.ang[i][0]);
            w.Draw(i + 0x89A, q);
        }
    }
}

void SpawnWreck5(World::Impl& w, Car5Task& car) {  // 0x00475A50
    Wreck5Task* k = w.NewTask<Wreck5Task>(TaskWreck5, 0x00475B50);
    k->car = &car;
    k->broken = 0;
    const float s = F32(w.exe, 0x0055D2B0), f = F32(w.exe, 0x004C4CC8);
    for (int i = 0; i < 0x2D; ++i) {
        const uint32_t row = 0x005960E0 + static_cast<uint32_t>(i) * 6;
        for (int a = 0; a < 3; ++a) k->pos[i][a] = static_cast<float>(w.exe.S16(row + 2 * a)) * s;
        k->vel[i][0] = k->pos[i][0] * f;
        k->vel[i][1] = 0.5f;
        k->spin[i][0] = static_cast<int16_t>(w.Rand(0x00475AE9) % 0x401 - 0x200);
        k->spin[i][1] = static_cast<int16_t>(w.Rand(0x00475B00) % 0x401 - 0x200);
        k->spin[i][2] = static_cast<int16_t>(w.Rand(0x00475B16) % 0x401 - 0x200);
    }
}

void Car5Draw(World::Impl& w, Car5Task& c) {
    const Mat4 m = CarFrame(c);
    w.Draw(0x8C7, m);
    w.Draw(0x145C, m);
    {
        Mat4 q = m;
        mx::Translate(q, Bits(0xC0959DB2), 0.0f, Bits(0x3E74BC6A));
        mx::Translate(q, 0.0f, 9.0f, 6.0f);
        mx::RotX(q, 0xD000);
        w.Draw(0x899, q);
    }
    {
        Mat4 q = m;
        mx::Translate(q, 0.0f, Bits(0x412BD2F2), Bits(0x41366F00));
        mx::RotX(q, -0x1C00);
        mx::RotZ(q, 0x4000);
        mx::RotX(q, 0x1C00);
        w.Draw(0x8CD, q);
    }
    {
        Mat4 q = m;
        mx::Translate(q, Bits(0xC1211F8A), Bits(0x40C5AC71), Bits(0x40DB35A8));
        mx::RotY(q, c.steer);
        w.Draw(0x896, q);
    }
    {
        Mat4 q = m;
        mx::Translate(q, Bits(0x41213B64), Bits(0x40C6C083), Bits(0x40DA9100));
        if (c.c1324 != 0) mx::RotY(q, -c.steer);
        w.Draw(0x893, q);
    }
    {
        Mat4 q = m;
        mx::Translate(q, 0.0f, Bits(0x4076617C), Bits(0x4181CA8C));
        mx::RotX(q, c.wheel);
        w.Draw(0x157F, q);
    }
    {
        Mat4 q = m;
        mx::Translate(q, 0.0f, Bits(0x4076617C), Bits(0xC14AC56D));
        mx::RotX(q, c.wheel);
        w.Draw(0x1580, q);
    }
    Mat4 s = Mat4::Identity();
    mx::Translate(s, c.pos[0], c.onGround ? w.ground : c.pos[1], c.pos[2]);
    mx::RotZ(s, c.rot[2]);
    mx::RotY(s, c.rot[1]);
    mx::RotX(s, c.rot[0]);
    w.Draw(0x145D, s);
}

// 0x0048F190
void TaskCar5(Task& t) {
    World::Impl& w = *CurrentWorld();
    Car5Task& c = t.As<Car5Task>();
    if (w.runner->flags[0xE0] == 1) {
        w.ts.KillCurrent();
        return;
    }
    const int cam = w.runner->camera.camId, tick = w.runner->camera.tick;
    int path = -1;
    switch (cam) {
    case 0xCC:
        c.pos[0] = Bits(0x4404199A);
        c.pos[1] = Bits(0xBDCCCCCD);
        c.pos[2] = Bits(0xC39A199A);
        c.rot[0] = 0;
        c.rot[1] = 0xA16D;
        c.rot[2] = 0;
        c.onGround = 1;
        break;
    case 0xD2:
        c.onGround = 1;
        break;
    case 0xCD:
        path = 0x17A;
        c.onGround = 1;
        break;
    case 0xCE:
        c.onGround = 0;
        path = 0x17B;
        if (tick == 100) c.faceCam = 1;
        else if (tick == 200) c.faceCam = 0;
        break;
    case 0xCF:
        c.onGround = 0;
        path = 0x17C;
        if (tick > 0x276) {
            if (tick == 0x3CA) c.faceCam = 0;
        } else if (tick == 0x276 || tick == 0x46) {
            c.faceCam = 1;
        } else if (tick == 0xE6) {
            c.faceCam = 0;
        }
        break;
    case 0xD1:
        c.onGround = 0;
        path = 0x17D;
        if (tick < 0x1D7) {
            if (tick == 0x1D6 || tick == 0x50) c.faceCam = 1;
            else if (tick == 0x140) c.faceCam = 0;
        } else if (tick == 0x4BF) {
            c.faceCam = 0;
        }
        break;
    default:
        break;
    }
    if (path >= 0) {
        int end = static_cast<int>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4));
        if (tick <= end) end = tick;
        float p[3];
        int ang[3];
        if (EvalPath(w, path, static_cast<float>(end), p, ang)) {
            memcpy(c.pos, p, 12);
            c.rot[0] = ang[0];
            if (c.faceCam == 0) c.rot[1] = ang[1];
            else c.rot[1] = w.runner->camera.ctx[w.runner->camera.activeCtx & 3].yaw + 0x8000;
            c.rot[2] = ang[2];
        }
    }
    Car5Draw(w, c);
}

// ---------------------------------------------------------------------------------------------------------------------
// Komono kind 51 (generic ctor 0x00461CF0 case 0x33 -> task 0x0046EB20, block 0x378): a model (record state) at the
// record, lifetime P byte 0 area changes (0x00466640); Mdl_SelectAlt when 0x009A2BB4 != 0 outside game type 2
struct Komono51Task : ActorTask {
    int16_t life = 0;      // +0x11C
    int8_t area = 0;       // +0x196
    int8_t areaCount = 0;  // +0x197
    float pos[3] = {};     // +0x19C
    float dz = 0;          // +0x1C8
    int rot[3] = {};       // +0x1CC
    int16_t model = 0;     // +0x28C
};

void TaskKomono51(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono51Task& k = t.As<Komono51Task>();
    if (static_cast<int8_t>(w.runner->variant) != k.area) {  // 0x00466640
        if (k.life < ++k.areaCount) {
            DestroyActor(w, k);
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.dz + k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotZ(m, k.rot[2]);
    mx::RotX(m, k.rot[0]);
    const bool alt = w.runner->cfg.gameType != 2 && w.altModels != 0;  // 0x004185E0
    w.Draw(k.model, m, 8, -1.0f, alt);
}

// Komono kind 53 (generic ctor 0x00461CF0 case 0x35 -> task 0x0046EBD0): like kind 51 (rotation order Z, Y, X); in
// scenes 4 / 5 also a fire (models 0x135F + n % 15, 0xB67 + n & 7, n = frame counter 0x009A2BAC) facing the camera
void TaskKomono53(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono51Task& k = t.As<Komono51Task>();
    if (static_cast<int8_t>(w.runner->variant) != k.area) {
        if (k.life < ++k.areaCount) {
            w.ts.KillCurrent();  // Task_Kill 0x004A7040
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m);
    const int scene = w.runner->scene;  // 0x009A2BC0
    if (scene == 4 || scene == 5) {
        const CameraContext& c = w.runner->camera.ctx[w.runner->camera.activeCtx & 3];
        const double a = std::atan2(static_cast<double>(c.pos[0] - k.pos[0]), static_cast<double>(c.pos[2] - k.pos[2]));
        const int yaw = static_cast<int16_t>(static_cast<int>(a * mx::kToAngle));
        const int n = w.frameCounter2BAC;  // 0x009A2BAC
        Mat4 q = Mat4::Identity();
        mx::Translate(q, k.pos[0], k.pos[1] + F32(w.exe, 0x0055D2B4), k.pos[2]);
        mx::RotY(q, yaw);
        mx::Scale(q, 1.5f, 2.0f, 1.0f);
        w.Draw(n % 0xF + 0x135F, q);
        Mat4 r = Mat4::Identity();
        mx::Translate(r, k.pos[0], k.pos[1] + F32(w.exe, 0x004C43A0), k.pos[2]);
        mx::RotY(r, yaw);
        mx::Translate(r, 0.0f, 0.0f, 12.0f);
        mx::Scale(r, 7.0f, 7.0f, 7.0f);
        w.Draw((n & 7) + 0xB67, r);
    }
}

// Komono kind 54 (generic ctor 0x00461CF0 case 0x36 -> task 0x0046EDC0): a model (record state) that, once event flag
// 0x0C is 1, flies off (velocity (5, 1.5, -4) per frame, x / y rotation +0x300 / -0x400) and ends after 301 frames
struct Komono54Task : ActorTask {
    float pos[3] = {};                      // +0x19C
    float vel[3] = { 5.0f, 1.5f, -4.0f };   // +0x1C0
    int rot[3] = {};                        // +0x1CC
    int spin[2] = { 0x300, -0x400 };        // +0x1D8 / +0x1DC
    int16_t model = 0;                      // +0x28C
    int count = 0;                          // +0x2A0
};

void TaskKomono54(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono54Task& k = t.As<Komono54Task>();
    if (w.runner->flags[0x0C] == 1) {
        const int c = k.count;
        k.rot[0] += k.spin[0];
        k.pos[0] = k.vel[0] + k.pos[0];
        k.rot[1] += k.spin[1];
        k.pos[1] = k.vel[1] + k.pos[1];
        k.count = c + 1;
        k.pos[2] = k.vel[2] + k.pos[2];
        if (300 < c) {
            w.ts.KillCurrent();
            return;
        }
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m);
}

}  // namespace

namespace st5 {

bool EvalPath(World::Impl& w, int path, float t, float pos[3], int ang[3]) {  // 0x004042D0
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

bool SpawnCar(World::Impl& w, Task& t, SpawnTask& s) {
    const SpawnRecord r = s.rec;
    if (r.type == 0x26 && r.state == 4) {  // 0x0048E290 case 4
        auto d = FromRecord<Car5Task>(r, 0x0048F190);
        Car5Task* c = d.get();
        t.data = std::move(d);
        t.fn = TaskCar5;
        SpawnWreck5(w, *c);
        TaskCar5(t);
        return true;
    }
    if (r.type == 0x41 && (r.ParamS8(1) == 51 || r.ParamS8(1) == 53)) {  // 0x00461CF0 cases 0x33 / 0x35
        const bool k53 = r.ParamS8(1) == 53;
        Komono51Task* k = w.NewTask<Komono51Task>(k53 ? TaskKomono53 : TaskKomono51, k53 ? 0x0046EBD0 : 0x0046EB20);
        memcpy(k->pos, r.pos, 12);
        for (int i = 0; i < 3; ++i) k->rot[i] = r.rot[i];
        k->model = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->life = r.ParamS8(0);  // spawn object +0x1F4 (op 0x09: P byte 0)
        w.ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 54) {  // 0x00461CF0 case 0x36
        Komono54Task* k = w.NewTask<Komono54Task>(TaskKomono54, 0x0046EDC0);
        memcpy(k->pos, r.pos, 12);
        for (int i = 0; i < 3; ++i) k->rot[i] = r.rot[i];
        k->model = static_cast<int16_t>(r.state);
        k->flags34 = 0x80000001u;
        w.ts.KillCurrent();
        return true;
    }
    return false;
}

}  // namespace st5
}  // namespace game
