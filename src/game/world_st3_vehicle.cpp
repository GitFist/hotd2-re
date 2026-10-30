// game::World - actor type 0x13 vehicles (stage 3: the enemy boat with a civilian and enemies on board) and the actors
// riding them (actor 0x18 enemies 0x0045CD60 -> 0x0045CD90, civilians 0x0048B140). Verification: notes/agents/stage3.md.
//
// A rider runs inside the vehicle's matrix (translate, RotX, RotZ, RotY of the vehicle): its position and angles are
// local to the vehicle. EnemyTask::framed / frame make AnimDraw and the floor shadow start from that matrix; when the
// vehicle sinks (+0x34 & 0x4000000) or the rider dies, 0x0045D920 turns the local position / angles into world ones.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

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

double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

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

// translate, RotX(+0x64), RotZ(+0x6C), RotY(+0x68): the vehicle's frame (0x0043FE90 draw, 0x0048B140, 0x0045CD90)
void VehicleOps(Mat4& m, const float pos[3], const int rot[3]) {
    mx::Translate(m, pos[0], pos[1], pos[2]);
    mx::RotX(m, rot[0]);
    mx::RotZ(m, rot[2]);
    mx::RotY(m, rot[1]);
}

// 0x0043FBC0 (created by 0x0043FCA0): an explosion / splash model sequence (types 0..3)
struct ExplosionTask : EnemyTask {
    int16_t delay = 1;       // ctl +4
    int16_t first = 0;       // ctl +0xA
    int16_t last = 0;        // ctl +0xC
    float scale = 1.0f;      // ctl +0x10
    int16_t model = 0;       // +0x1F4
};

void TaskExplosion(Task& t) {
    World::Impl& w = *CurrentWorld();
    ExplosionTask& x = t.As<ExplosionTask>();
    if (x.delay == 0) {
        if (x.last < ++x.model) {
            w.EnemyDestroy(x);
            return;
        }
    } else if (--x.delay == 0) {
        x.model = x.first;
    }
    Mat4 m = Mat4::Identity();
    VehicleOps(m, x.pos, x.rot);
    if (x.scale != 1.0f) mx::Scale(m, x.scale, x.scale, x.scale);
    w.Draw(x.model, m);
}

void SpawnExplosion(World::Impl& w, const float pos[3], const int rot[3], int type, float scale) {  // 0x0043FCA0
    static const int16_t kModels[4][2] = { { 0x1339, 0x1356 }, { 0x16E1, 0x172F }, { 0xDD7, 0xE22 }, { 0x174A, 0x1785 } };
    ExplosionTask* x = w.NewTask<ExplosionTask>(TaskExplosion, 0x0043FBC0);
    x->flags34 = 0x8000;
    memcpy(x->pos, pos, 12);
    memcpy(x->rot, rot, 12);
    x->scale = scale;
    x->objMesh = -1;
    x->delay = 1;
    x->model = x->first = kModels[type & 3][0];
    x->last = kModels[type & 3][1];
    if (type < 2) w.Sound(0x4116A9);
}

struct VehicleTask : EnemyTask {
    uint32_t motion = 0;     // ctl +0 (motion function)
    int mode = 0;            // *(ctl +8) = P word 5
    int16_t state = 0;       // ctl +0xC
    int16_t endCam = 0, endTick = 0;  // ctl +0xE / +0x10
    float scale = 1.0f;      // ctl +0x14
    float alpha = 1.0f;      // ctl +0x18
    int16_t model = 0;       // +0x1F4
    // motion 6 data block (ctl +4, 0x18 bytes)
    int tick = 0;            // [0]
    int wave = 0x24A;        // [1]
    int waveOn = 1;          // [2]
    float waveScale = 1.0f;  // [3]
    float waveStep = 0;      // [4]
    int fire = 0x1AAB;       // [5]
    int st4[24] = {};        // motions 2 / 3 / 4 data block (world_st4_vehicle.cpp)
    uint8_t drawMode = 8;    // 0x004A79F0 mode set by the motion for the vehicle's draw
    int drillHits = 0; // PC coin globals 0x5F548C / 0x5F5488 / 0x5F5490.
    float drillStep = .015f,drillSpeed = .3f;
};

// 0x00440770(pos block, model, scale, z): the two bow-wave models on the floor below the vehicle, along its heading
void BowWaves(World::Impl& w, VehicleTask& v, int model, float scale, float z) {
    Mat4 r = Mat4::Identity();
    mx::RotX(r, v.rot[0]);
    mx::RotZ(r, v.rot[2]);
    mx::RotY(r, v.rot[1]);
    const float fwd[3] = { 0.0f, 0.0f, 1.0f };
    float dir[3];
    mx::Transform(r, fwd, dir);
    int pitch = 0, yaw = 0;
    mx::DirToAngles(dir[0], dir[1], dir[2], &pitch, &yaw);  // Cam_DirToAngles 0x004016B0
    int mat = 0;
    const float fl = w.FloorHeight(v.pos[0], v.pos[1] + F32(w.exe, 0x004C43B0), v.pos[2], &mat);  // 0x00409D40
    Mat4 m = Mat4::Identity();
    mx::Translate(m, v.pos[0], fl, v.pos[2]);
    mx::RotY(m, yaw);
    mx::Translate(m, 0.0f, 0.0f, z);
    mx::Scale(m, 1.0f, scale, scale);
    w.Draw(model, m);
    w.Draw(model + 0x16, m);
}

// 0x004413C0 (motion 6): along camera path 0x161 (then 0x160 when 0x009CA0E8 != 0 at tick 0x635), bow waves fading out
// at tick 0x668 / 0x672; path 0x161 to 0x6AE: explosion at 0x6A4, burning (0x1AAB..0x1AD2) until off screen, then sunk
// (+0x34 | 0x4000000) and removed
void Motion6(World::Impl& w, VehicleTask& v) {
    const ExeImage& exe = w.exe;
    bool count = false;
    switch (v.state) {
    case 0:
        v.wave = 0x24A;
        v.waveOn = 1;
        v.waveScale = 1.0f;
        v.waveStep = 0;
        v.tick = w.runner->camera.tick;
        v.radius = 40.0f;
        ++v.state;
        [[fallthrough]];
    case 1: {
        float p[3];
        if (EvalPath(w, 0x161, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);  // 0x00440130
        if (v.tick == 0x635) v.state = static_cast<int16_t>(w.countA0E8 != 0 ? 2 : 4);
        count = true;
        break;
    }
    case 2: {
        float p[3];
        if (EvalPath(w, 0x160, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);
        if (v.tick == 0x672) v.waveStep = Bits(0xBC75C28F);
        else if (v.tick == 0x6AE) v.state = 3;
        count = true;
        break;
    }
    case 4: {
        float p[3];
        if (EvalPath(w, 0x161, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);
        if (v.tick >= static_cast<int>(exe.U32(0x005772BC))) {
            v.fire = 0x1AAB;
            v.state = 5;
            v.flags34 |= 0x400000;
        } else if (v.tick == 0x668) {
            v.waveStep = Bits(0xBC75C28F);
        } else if (v.tick == 0x6A4) {
            Mat4 m = Mat4::Identity();
            VehicleOps(m, v.pos, v.rot);
            mx::Translate(m, 0.0f, 0.0f, 8.0f);
            float at[3];
            mx::GetTranslation(m, at);
            const int rot[3] = { 0, w.cam.yaw, 0 };  // 0x009A60D0[ctx]
            SpawnExplosion(w, at, rot, 3, 1.0f);
            w.Sound(0xB16A9);
        }
        count = true;
        break;
    }
    case 5:
    case 6: {
        Mat4 mv = w.cam.view;
        VehicleOps(mv, v.pos, v.rot);
        mx::GetTranslation(mv, v.viewPos);
        Mat4 m = Mat4::Identity();
        VehicleOps(m, v.pos, v.rot);
        mx::Translate(m, 0.0f, 0.0f, -2.0f);
        mx::RotY(m, -v.rot[1]);
        mx::RotZ(m, v.rot[2]);
        mx::RotX(m, v.rot[0]);
        mx::RotY(m, w.cam.yaw);
        w.Draw(v.fire, m);
        if (0x1AD2 < ++v.fire) v.fire = 0x1AAB;
        if (v.state == 5 && w.runner->camera.tick >= static_cast<int>(exe.U32(0x005772BC))) v.state = 6;
        if (v.state == 6) {
            // 0x004459C0: on screen (view point +0x70, radius +0x124)
            const float* p = v.viewPos;
            bool on = false;
            if (p[2] < 0.0f) {
                const float r = v.radius;
                const float ex = p[0] <= 0.0f ? -p[0] - r : r - p[0];
                const float ey = p[1] <= 0.0f ? -p[1] - r : r - p[1];
                const float s = w.focal / p[2];
                const float x1 = s * ex, y1 = s * ey;
                const float x2 = -((w.focal * p[0]) / p[2]), y2 = -((w.focal * p[1]) / p[2]);
                const float kx = F32(exe, 0x004C49CC), kx2 = F32(exe, 0x004D1D10), ky = F32(exe, 0x004C49C8), ky2 = F32(exe, 0x004C4D00);
                if ((x1 < kx || x2 < kx) && (kx2 < x1 || kx2 < x2) && (y1 < ky || y2 < ky)) on = !(y1 <= ky2 && y2 <= ky2);
            }
            if (!on) {
                v.flags34 |= 0x4000000;
                v.state = 7;
            }
        }
        break;
    }
    case 7:
        w.EnemyDestroy(v);
        return;
    default:
        break;
    }
    if (count) ++v.tick;
    if (v.waveStep != 0.0f) {
        v.waveScale = v.waveStep + v.waveScale;
        if (v.waveScale <= 0.0f) v.waveOn = 0;
    }
    if (v.waveOn != 0) {
        BowWaves(w, v, v.wave, v.waveScale, Bits(0x42020000));
        if (0x25F < ++v.wave) v.wave = 0x24A;
    }
}

// 0x004459C0: the vehicle's view point (+0x70, radius +0x124) still on screen
bool VehicleOnScreen(World::Impl& w, const VehicleTask& v) {
    const ExeImage& exe = w.exe;
    const float* p = v.viewPos;
    if (!(p[2] < 0.0f)) return false;
    const float r = v.radius;
    const float ex = p[0] <= 0.0f ? -p[0] - r : r - p[0];
    const float ey = p[1] <= 0.0f ? -p[1] - r : r - p[1];
    const float s = w.focal / p[2];
    const float x1 = s * ex, y1 = s * ey;
    const float x2 = -((w.focal * p[0]) / p[2]), y2 = -((w.focal * p[1]) / p[2]);
    const float kx = F32(exe, 0x004C49CC), kx2 = F32(exe, 0x004D1D10), ky = F32(exe, 0x004C49C8), ky2 = F32(exe, 0x004C4D00);
    if ((x1 < kx || x2 < kx) && (kx2 < x1 || kx2 < x2) && (y1 < ky || y2 < ky)) return !(y1 <= ky2 && y2 <= ky2);
    return false;
}

// 0x004403D0 (motion 1, stage 3 scene 0): as motion 6 on camera paths 0x15F / 0x15E (switch at tick 0x500, by
// 0x009CA0E8), bow waves at 27 fading from 0x501 / 0x550; explosion 5 behind at 0x550, sinking flag 0x400000 at 0x55A,
// burning from tick 0x005772B4 until off screen
void Motion1(World::Impl& w, VehicleTask& v) {
    const ExeImage& exe = w.exe;
    bool count = false;
    switch (v.state) {
    case 0:
        v.wave = 0x24A;
        v.waveOn = 1;
        v.waveScale = 1.0f;
        v.waveStep = 0;
        v.tick = w.runner->camera.tick;
        v.radius = 40.0f;
        ++v.state;
        [[fallthrough]];
    case 1: {
        float p[3];
        if (EvalPath(w, 0x15F, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);  // 0x00440130
        if (v.tick == 0x500) v.state = static_cast<int16_t>(w.countA0E8 != 0 ? 2 : 4);
        count = true;
        break;
    }
    case 2: {
        float p[3];
        if (EvalPath(w, 0x15E, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);
        if (v.tick == 0x501) v.waveStep = Bits(0xBC75C28F);
        else if (v.tick == 0x528) v.state = 3;
        count = true;
        break;
    }
    case 4: {
        float p[3];
        if (EvalPath(w, 0x15F, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);
        if (v.tick >= static_cast<int>(exe.U32(0x005772B4))) {
            v.fire = 0x1AAB;
            v.state = 5;
        } else if (v.tick == 0x550) {
            v.waveStep = Bits(0xBC75C28F);
            Mat4 m = Mat4::Identity();  // (0x004A9E10: identity)
            VehicleOps(m, v.pos, v.rot);
            mx::Translate(m, 0.0f, 0.0f, -5.0f);
            float at[3];
            mx::GetTranslation(m, at);
            const int rot[3] = { 0, w.cam.yaw, 0 };  // 0x009A60D0[ctx]
            SpawnExplosion(w, at, rot, 3, 1.0f);
            w.Sound(0xB16A9);
        } else if (v.tick == 0x55A) {
            v.flags34 |= 0x400000;
        }
        count = true;
        break;
    }
    case 5:
    case 6: {
        Mat4 mv = w.cam.view;
        VehicleOps(mv, v.pos, v.rot);
        mx::GetTranslation(mv, v.viewPos);
        Mat4 m = Mat4::Identity();
        VehicleOps(m, v.pos, v.rot);
        mx::Translate(m, 0.0f, 0.0f, -5.0f);
        mx::RotY(m, -v.rot[1]);
        mx::RotZ(m, v.rot[2]);
        mx::RotX(m, v.rot[0]);
        mx::RotY(m, w.cam.yaw);
        w.Draw(v.fire, m);
        if (0x1AD2 < ++v.fire) v.fire = 0x1AAB;
        if (v.state == 5 && w.runner->camera.tick >= static_cast<int>(exe.U32(0x005772B4))) v.state = 6;
        if (v.state == 6 && !VehicleOnScreen(w, v)) {
            v.flags34 |= 0x4000000;
            v.state = 7;
        }
        break;
    }
    case 7:
        w.EnemyDestroy(v);
        return;
    default:
        break;
    }
    if (count) ++v.tick;
    if (v.waveStep != 0.0f) {
        v.waveScale = v.waveStep + v.waveScale;
        if (v.waveScale <= 0.0f) v.waveOn = 0;
    }
    if (v.waveOn != 0) {
        BowWaves(w, v, v.wave, v.waveScale, 27.0f);  // 0x41D80000
        if (0x25F < ++v.wave) v.wave = 0x24A;
    }
}

// 0x00440210 (motion 0, stage 2 scene 16): camera path 0x151 to tick 0x275 with a wake model (0x24A..0x25F at z 27.5,
// scale y 0.15), then to tick 0x0057727C; from tick 0x276 a splash sequence 0xFD4..0x1031 at a fixed point
void Motion0(World::Impl& w, VehicleTask& v) {
    const ExeImage& exe = w.exe;
    bool count = false;
    if (v.state == 0) {
        v.wave = 0x24A;
        v.fire = 0;  // ([2]: the splash model, 0 = off)
        v.tick = w.runner->camera.tick;
        ++v.state;
    }
    if (v.state == 1) {
        float p[3];
        if (EvalPath(w, 0x151, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);  // 0x00440130
        if (0x275 < v.tick) ++v.state;
        Mat4 m = Mat4::Identity();
        VehicleOps(m, v.pos, v.rot);
        mx::Translate(m, 0.0f, 0.0f, 27.5f);  // 0x41DC0000
        mx::Scale(m, 1.0f, Bits(0x3E19999A), 1.0f);
        w.Draw(v.wave, m);
        if (0x25F < ++v.wave) v.wave = 0x24A;
        count = true;
    } else if (v.state == 2) {
        float p[3];
        if (EvalPath(w, 0x151, static_cast<float>(v.tick), p, v.rot)) memcpy(v.pos, p, 12);
        if (static_cast<int>(exe.U32(0x0057727C)) <= v.tick) ++v.state;
        else count = true;
    }
    if (count) ++v.tick;
    if (v.fire == 0) {
        if (v.tick == 0x276) {
            v.fire = 0xFD4;
            w.Sound(0x4116A9);
        }
    } else {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0xC493B6B8), Bits(0xC1974396), Bits(0xC4BC8D1F));
        mx::RotY(m, 0x18E3);
        const float s = Bits(0x3F19999A);
        mx::Scale(m, s, s, s);
        w.Draw(v.fire++, m);
        if (0x1031 < v.fire) v.fire = 0;
    }
}

// ---- vehicle motion function 9 (0x00445050, training lesson 9: the target thrown up, kept in the air by shots) ----
// data (ctl +4, 0x38 bytes) in VehicleTask::st4: [0] spin angle, [1..3] spin axis, [4] start yaw to the camera,
// [5..7] / [8..10] the points left / right of the start (view x -+ tan(0x00564548) z), [0xB] count, [0xC] limit by
// difficulty {1200, 1500, 1800, 2400, 3000}, [0xD] end timer
float DF(const int* d, int i) {
    float f;
    memcpy(&f, &d[i], 4);
    return f;
}
void DS(int* d, int i, float f) { memcpy(&d[i], &f, 4); }

int YawTo(World::Impl& w, float x, float z) {  // fpatan(x - cam x, z - cam z) * 0x004C4378, __ftol, movsx
    return TruncToAngle(std::atan2(static_cast<double>(x - w.cam.pos[0]), static_cast<double>(z - w.cam.pos[2])));
}

Mat4 CamRotation(World::Impl& w) {  // 0x004A9230(camera 0x009A6040) + 0x004A9FD0(0, 0, 0)
    Mat4 r = w.cam.inverse;
    r.m[12] = r.m[13] = r.m[14] = 0.0f;
    return r;
}

void Normalize3(float v[3]) {  // 0x004AA9C0
    const float len = std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
    v[0] = v[0] / len;
    v[1] = v[1] / len;
    v[2] = v[2] / len;
}

bool Motion9(World::Impl& w, VehicleTask& v) {  // false: removed
    const bool pcDrill = w.cfg.portTyping && w.cfg.eventMode==7 && w.cfg.gameType==2;
    const ExeImage& exe = w.exe;
    int* d = v.st4;
    switch (v.state) {
    case 0: {
        v.radius = 3.0f;  // +0x124
        d[4] = YawTo(w, v.pos[0], v.pos[2]);
        Mat4 mv = w.cam.view;
        mx::Translate(mv, v.pos[0], v.pos[1], v.pos[2]);
        mx::GetTranslation(mv, v.viewPos);  // 0x004A8CC0 -> +0x70
        const float t = static_cast<float>(std::tan(F64(exe, 0x00564548)));  // fptan (24-bit)
        const float k = t * v.viewPos[2];
        const float l[3] = { v.viewPos[0] - k, v.viewPos[1], v.viewPos[2] };
        float p[3];
        mx::Transform(w.cam.inverse, l, p);
        DS(d, 5, p[0]), DS(d, 6, p[1]), DS(d, 7, p[2]);
        const float r[3] = { k + v.viewPos[0], v.viewPos[1], v.viewPos[2] };
        mx::Transform(w.cam.inverse, r, p);
        DS(d, 8, p[0]), DS(d, 9, p[1]), DS(d, 10, p[2]);
        const Mat4 rot = CamRotation(w);
        float a[3];
        a[0] = static_cast<float>(w.Rand(0x00445229) % 0x65 - 0x32);
        a[1] = static_cast<float>(w.Rand(0x0044524B) % 0x65 - 0x32);
        a[2] = 0.0f;
        float ax[3];
        mx::Transform(rot, a, ax);
        Normalize3(ax);
        DS(d, 1, ax[0]), DS(d, 2, ax[1]), DS(d, 3, ax[2]);
        d[0] = 0xC00;
        const float sx[3] = { static_cast<float>(w.Rand(0x004452AC) % 0xC9 - 100) * F32(exe, 0x0055CB98), 0.0f, 0.0f };
        float vv[3];
        mx::Transform(rot, sx, vv);
        v.vel[0] = vv[0];
        v.vel[2] = vv[2];
        v.vel[1] = Bits(0x3F99999A);
        d[0xB] = 0;
        static const int kLimit[5] = { 0x4B0, 0x5DC, 0x708, 0x960, 0xBB8 };
        const int dv = w.runner->cfg.playerVariant;
        d[0xC] = kLimit[dv < 0 || dv > 4 ? 0 : dv];
        v.state = 1;
    }
        [[fallthrough]];
    case 1:
        if (w.runner->flags[0xF1] != 0) {
            v.flags34 &= ~0xEu;
            v.acc[1] = Bits(0xBCDF0123);  // +0x5C
            v.state = 2;
            if(pcDrill) {
                v.typingOriginType=0x13;v.typingFlags=0x20000A04u;
                v.drillHits=0;v.drillStep=.015f;v.drillSpeed=.3f;
            }
        }
        break;
    case 2: {
        if (v.flags34 & 8) {  // shot: the push away from the aim point, the spin about the shot's axis
            if(pcDrill)w.TypingWords(-1,v.drillHits<10?100:v.drillHits<40?200:v.drillHits<80?300:400);
            float f = w.focal / v.viewPos[2];
            if (f < 0.0f) f = f * F32(exe, 0x004C4C64);
            const int p = static_cast<int>((~v.flags34 >> 1) & 1);
            const float ax = w.player[p].aim[0] / f, ay = w.player[p].aim[1] / f;
            const float dx = v.viewPos[0] - ax, dy = v.viewPos[1] - ay;
            const Mat4 rot = CamRotation(w);
            float n[3];
            const float l[3] = { dy, -dx, 0.0f };
            mx::Transform(rot, l, n);
            Normalize3(n);
            const int spin = static_cast<int>(static_cast<float>(std::sqrt(dy * dy + dx * dx)) * F32(exe, 0x0055D22C) / v.radius);  // __ftol
            if (d[0] == 0) {
                DS(d, 1, n[0]), DS(d, 2, n[1]), DS(d, 3, n[2]);
                d[0] = spin;
            } else {
                const float axis[3] = { DF(d, 1), DF(d, 2), DF(d, 3) };
                float c[3];
                if (AngleBetween(axis, n) == 0) {  // 0x00401D70
                    c[0] = n[2] - n[1];
                    c[1] = n[0] - n[2];
                    c[2] = n[1] - n[0];
                } else {
                    c[0] = n[2] * axis[1] - n[1] * axis[2];
                    c[1] = n[0] * axis[2] - n[2] * axis[0];
                    c[2] = n[1] * axis[0] - n[0] * axis[1];
                }
                Normalize3(c);
                Mat4 m = Mat4::Identity();
                mx::RotAxis(m, axis, d[0]);
                float A[3], B[3];
                mx::Transform(m, c, A);
                m = Mat4::Identity();
                mx::RotAxis(m, n, spin);
                mx::Transform(m, c, B);
                const float s[3] = { (B[0] - c[0]) + A[0], (B[1] - c[1]) + A[1], (B[2] - c[2]) + A[2] };
                d[0] = AngleBetween(s, c);
                if (d[0] != 0) {
                    DS(d, 1, s[2] * c[1] - s[1] * c[2]);
                    DS(d, 2, s[0] * c[2] - s[2] * c[0]);
                    DS(d, 3, s[1] * c[0] - s[0] * c[1]);
                }
            }
            const int dyaw = YawTo(w, v.pos[0], v.pos[2]) - d[4];
            float side;
            if (dyaw < -0x1000 && !(dx <= 0.0f)) side = Bits(0xBDCCCCCD);
            else if (0x1000 < dyaw && dx < 0.0f) side = Bits(0x3DCCCCCD);
            else side = static_cast<float>(w.Rand(0x0044571B) % 3 - 1) * F32(exe, 0x004C4CC0) + dx * F32(exe, 0x00564544);
            const float sl[3] = { side, 0.0f, 0.0f };
            float sd[3];
            mx::Transform(CamRotation(w), sl, sd);
            v.vel[0] = sd[0];
            v.vel[1] = sd[1];
            v.vel[2] = sd[2];
            // PC 0x43290E..432925 fixes every word rebound at1.2; gun
            // hits vary their vertical impulse with the aim's local offset.
            const float vy = pcDrill ? 1.2f : dy * F32(exe, 0x004D1D24) / v.radius + F32(exe, 0x004C4380);
            v.vel[1] = vy;
            const float n2 = (vy / v.acc[1]) * F32(exe, 0x004C4C90);
            const int dyaw2 = YawTo(w, n2 * sd[0] + v.pos[0], n2 * sd[2] + v.pos[2]) - d[4];
            if (side <= 0.0f) {
                if (0x1000 < dyaw2) {
                    v.vel[0] = (DF(d, 8) - v.pos[0]) / n2;
                    v.vel[2] = (DF(d, 10) - v.pos[2]) / n2;
                }
            } else if (dyaw2 < -0x1000) {
                v.vel[0] = (DF(d, 5) - v.pos[0]) / n2;
                v.vel[2] = (DF(d, 7) - v.pos[2]) / n2;
            }
            w.Sound(0x1116A9);
            v.flags34 &= ~0xEu;
            if(pcDrill) { // PC 0x4329FF: gradually speed the coin after successful words.
                ++v.drillHits;
                if(v.drillHits==10)v.drillStep=.01f;
                else if(v.drillHits==15)v.drillStep=.008f;
                else if(v.drillHits==20)v.drillStep=.004f;
                else if(v.drillHits==25)v.drillStep=.002f;
                v.drillSpeed+=v.drillStep;
            }
        }
        if (d[0] != 0) {
            if (0x1000 < d[0]) d[0] = 0x1000;
            const float axis[3] = { DF(d, 1), DF(d, 2), DF(d, 3) };
            Mat4 m = Mat4::Identity();
            mx::RotAxis(m, axis, d[0]);
            mx::RotX(m, v.rot[0]);
            mx::RotZ(m, v.rot[2]);
            mx::RotY(m, v.rot[1]);
            MatrixToAnglesYAxis(m, &v.rot[0], &v.rot[1], &v.rot[2]);  // 0x00401AE0
        }
        const float speed=pcDrill ? v.drillStep+v.drillSpeed : 1.0f;
        v.pos[0] = speed*v.vel[0] + v.pos[0];
        v.pos[1] = speed*v.vel[1] + v.pos[1];
        v.pos[2] = speed*v.vel[2] + v.pos[2];
        v.vel[1] = speed*v.acc[1] + v.vel[1];
        if (v.vel[1] < F32(exe, 0x00564540)) v.vel[1] = Bits(0xBFE66666);
        if (v.pos[1] <= w.ground) {  // dropped: the lesson is failed
            w.runner->globals[0x009A2234] = 1;
            d[0xD] = 0x14;
            v.state = 4;
            if(pcDrill)v.typingFlags=(v.typingFlags&~0x200u)|0x400u;
        }
        if (++d[0xB] == d[0xC] && !pcDrill) {  // the time is up: passed
            w.runner->flags[3] = 1;  // 0x009C7203
            d[0xD] = 0x14;
            v.state = 3;
        }
        break;
    }
    case 3:
    case 4:
        if (--d[0xD] == 0) {
            w.ts.KillCurrent();  // Task_Kill 0x004A7040
            return false;
        }
        break;
    default:
        break;
    }
    memcpy(v.body, v.pos, 12);  // +0x100
    if (!v.lock) v.lock = &w.lockTargets[w.nextLockTarget++];
    w.SyncLock(v);
    w.lockOn.Register(v.lock, w.playerPos);  // 0x00408EC0
    if(!pcDrill)TrainTimeLeft(w, d[0xC], d[0xB]);        // 0x00498C10
    return true;
}

void TaskVehicle(Task& t) {  // 0x0043FE90
    World::Impl& w = *CurrentWorld();
    VehicleTask& v = t.As<VehicleTask>();
    if (w.runner->camera.camId == v.endCam && w.runner->camera.tick == v.endTick) {
        w.EnemyDestroy(v);
        return;
    }
    v.drawMode = 8;
    if (v.motion == 0x0041EBB0) {  // motion function 0: the no-op (a parked vehicle, stage 2 scenes 16 / 17 / 20)
    } else if (v.motion == 0x00445050) {  // motion function 9 (training lesson 9)
        if (!Motion9(w, v)) return;
    } else if (v.mode == 6) {
        Motion6(w, v);
    } else if (v.mode == 1) {
        Motion1(w, v);
    } else if (v.mode == 0) {
        Motion0(w, v);
    } else if (v.mode < 0 || 9 < v.mode) {
        // 0x00440190 has no case: the dispatcher stays installed and the vehicle does not move
    } else {
        const int r = w.VehicleMotionSt4(v, v.mode, v.state, v.alpha, v.model, v.drawMode, v.st4);
        if (r == 2) return;  // Task_Kill
        if (r == 0) w.Note("vehicle motion " + std::to_string(v.mode) + " (0x00440190 table) not ported");
    }
    auto ops = [&](Mat4& m) {
        VehicleOps(m, v.pos, v.rot);
        if (v.scale != 1.0f) mx::Scale(m, v.scale, v.scale, v.scale);
    };
    Mat4 m = Mat4::Identity();
    ops(m);
    if (v.alpha == 1.0f) w.Draw(v.model, m, v.drawMode);
    else w.Draw(v.model, m, v.drawMode, v.alpha);
    Mat4 mv = w.cam.view;
    ops(mv);
    v.m150 = mv;  // +0x150 (0x004A8CA0)
    mx::GetTranslation(mv, v.viewPos);
    w.RegisterTarget(&v);  // 0x00405160
}

// 0x0045D920(&rider pos block, &vehicle pos block): local position / angles -> world
void ToWorld(EnemyTask& r, const EnemyTask& v) {
    Mat4 m = Mat4::Identity();
    VehicleOps(m, v.pos, v.rot);
    VehicleOps(m, r.pos, r.rot);
    mx::GetTranslation(m, r.pos);
    MatrixToAnglesYAxis(m, &r.rot[0], &r.rot[1], &r.rot[2]);  // 0x00401AE0
}

bool VehicleOf(World::Impl& w, EnemyTask& r, EnemyTask*& v) {
    v = r.vehicle;
    bool live = false;
    if (v)
        for (Task* t : w.ts.Live()) live = live || t->data.get() == v;
    if (v && !live) {
        w.Note("rider of a removed vehicle");
        r.vehicle = v = nullptr;
    }
    return v != nullptr;
}

// 0x0045CFC0 (behaviour 46): on the vehicle, idle motion; turn to the players' point taken into the vehicle's frame;
// at camera P s16 0xC / tick P s16 0xE: behaviour P byte 3
void Beh46(World::Impl& w, EnemyTask& e) {
    if (e.sub == 0) {
        const int idle = w.EnemyMotionSet(e, 0);
        if (e.anim.motion[0] != idle) w.AnimBlendTo(e, idle, 0, 0x14);
        ++e.sub;
        e.headPitch = e.anim.motion[0];  // +0x1320
    } else if (e.sub != 1) {
        goto tail;
    }
    if (e.vehicle) {
        Mat4 m = Mat4::Identity();
        VehicleOps(m, e.vehicle->pos, e.vehicle->rot);
        const Mat4 inv = mx::Inverse(m);  // 0x004A8D20
        float p[3];
        mx::Transform(inv, w.playerPos, p);
        w.EnemyTurnTo(e, p[0], p[2], 0x68);  // 0x00409F90
    }
    if (w.runner->camera.camId == e.record.ParamS16(0xC) && w.runner->camera.tick == e.record.ParamS16(0xE)) {
        e.behaviour = e.record.ParamS8(3);
        e.sub = 0;
    }
tail:
    if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.headPitch) w.AnimBlendTo(e, e.headPitch, 0, 10);
}

void TaskVehicleEnemy(Task& t) {  // 0x0045CD60 / 0x0045CD90
    World::Impl& w = *CurrentWorld();
    EnemyTask& e = t.As<EnemyTask>();
    if (e.fn == 0x0045CD60) {
        w.EnemyCreate(e);  // 0x00452DA0
        e.vehicle = static_cast<EnemyTask*>(w.obj9A2C88);
        e.fn = 0x0045CD90;
        return;
    }
    if (e.fn == fn::kEnemy) {  // left the vehicle (0x004533F0 alone: no vehicle matrix pushed)
        e.framed = false;
        w.EnemyUpdate(e);
        return;
    }
    EnemyTask* v = nullptr;
    VehicleOf(w, e, v);
    e.framed = v != nullptr;
    if (v) {
        e.frame = Mat4::Identity();
        VehicleOps(e.frame, v->pos, v->rot);
        e.frameMv = w.cam.view;  // 0x0048B140-style: push (the view), the vehicle ops on the top
        VehicleOps(e.frameMv, v->pos, v->rot);
    }
    w.EnemyUpdate(e);  // 0x004533F0
    if (e.behaviour == e.record.ParamS8(3) && e.sub == 0 && e.record.ParamS32(0xC) != -1 &&
        w.runner->camera.tick < e.record.ParamS16(0xE) && w.runner->camera.camId == e.record.ParamS16(0xC)) {
        e.behaviour = 0x2E;
        e.sub = 0;
    }
    if (e.behaviour == 7 && e.sub == 1 && e.counter1330 == 2) {
        if (v) ToWorld(e, *v);  // 0x0045D920
        e.framed = false;
        e.vehicle = nullptr;
        e.fn = fn::kEnemy;
    }
}

}  // namespace

// PC motion9's coin keeps its assigned word while it bounces, and receives
// the completed strike through the same motion handler as an accepted shot.
bool TypingDrillCoin(World::Impl& w,ActorTask& actor,TypingTargetSpec& out,bool complete) {
    auto* coin=dynamic_cast<VehicleTask*>(&actor);
    if(!coin || coin->motion!=0x00445050 ||
       !(w.cfg.portTyping&&w.cfg.eventMode==7&&w.cfg.gameType==2))return false;
    if(coin->state!=2)return true;
    out.node=1;out.danger=true;out.retain=true;out.token=0x130009u;
    memcpy(out.view,coin->viewPos,12);
    if(complete){TypingDeliver(*coin,1);coin->typingStrike=false;}
    return true;
}

// 0x0043FCA0 for other files (the stage-2 boss, world_st2c_boss.cpp)
void World::Impl::SpawnExplosionAt(const float pos[3], const int rot[3], int type, float scale) {
    SpawnExplosion(*this, pos, rot, type, scale);
}

// 0x0043FE10 (actor type 0x13)
void World::Impl::SpawnVehicle(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    auto d = std::make_unique<VehicleTask>();
    VehicleTask& v = *d;
    v.fn = 0x0043FE90;
    SetTypingSpawnOrigin(v,r);
    v.record = r;
    v.flags34 = r.flags | 1;
    memcpy(v.pos, r.pos, 12);
    memcpy(v.rot, r.rot, 12);
    v.hp = v.hpMax = static_cast<int16_t>(r.state);
    v.lo = static_cast<int16_t>(r.lo);
    v.model = r.ParamS16(0);
    v.objMesh = r.ParamS32(4);
    v.motion = exe.U32(0x005926A8 + 4 * static_cast<uint32_t>(r.ParamS32(0x10)));
    v.mode = r.ParamS32(0x14);
    v.state = 0;
    v.endCam = r.ParamS16(8);
    v.endTick = r.ParamS16(0xA);
    const int32_t sc = r.ParamS32(0xC);
    memcpy(&v.scale, &sc, 4);
    v.alpha = 1.0f;
    v.actorSlot = -1;
    if (v.motion == 0x00440190) {  // the ctor's first motion call (0x00440190): selects the motion function
        obj9A2C88 = &v;
        v.motion = 0;
    } else if (v.motion != 0x00445050 && v.motion != 0x0041EBB0) {
        Note("vehicle motion function " + std::to_string(r.ParamS32(0x10)) + " (0x005926A8) not ported");
    }
    t.data = std::move(d);
    t.fn = TaskVehicle;
    if (static_cast<VehicleTask&>(*t.data).motion == 0x00445050) Motion9(*this, static_cast<VehicleTask&>(*t.data));  // the ctor's first call
}

// 0x00408A20 for an actor 0x18 record (a civilian's child riding the vehicle): first run 0x0045CD60
EnemyTask* World::Impl::SpawnVehicleEnemy(const SpawnRecord& r) {
    EnemyTask* e = NewTask<EnemyTask>(TaskVehicleEnemy, 0x0045CD60);
    e->lock = &lockTargets[nextLockTarget++];
    e->flags34 = r.flags | 1;  // 0x00408970
    memcpy(e->pos, r.pos, 12);
    memcpy(e->rot, r.rot, 12);
    e->hp = e->hpMax = static_cast<int16_t>(r.state);
    e->lo = static_cast<int16_t>(r.lo);
    e->record = r;
    return e;
}

namespace {

int16_t EvS16(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a & ~3u, &w);
    return static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
}

float EvF32(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a, &w);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

// 0x0045D500 (behaviour 48): jump off the vehicle. List 0x0045CA10 (P word 2 while behaviour P byte 3, else P word 1):
// {float x, z (vehicle frame), vy, gravity; s16 motion, jump tick, tick of flag 0x4000}. At the jump tick: to world
// (0x0045D920), a plain enemy (0x004533F0), velocities to reach (x, z) at the apex time x 2; falls (through water
// surfaces 0x37 / 5 to the floor below, splash 0x61 once), levels its pitch / roll by half; lands: corpse (hp < 1:
// behaviour 6) or the landing motion, then behaviour 1.
void Beh48(World::Impl& w, EnemyTask& e) {
    const ExeImage& exe = w.exe;
    const EventMemory& mem = w.mem;
    const uint32_t list = static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1));
    switch (e.sub) {
    case 0:
        w.AnimBlendTo(e, EvS16(mem, list + 0x10), 0, 10);
        ++e.sub;
        e.flags34 |= 0x2000;
        return;
    case 1: {
        EnemyTask* v = nullptr;
        VehicleOf(w, e, v);
        if (e.anim.tick[0] == EvS16(mem, list + 0x12)) {
            if (v) ToWorld(e, *v);  // 0x0045D920
            e.fn = fn::kEnemy;
            e.framed = false;
            e.flags136C = (e.flags136C & 0xDFFEFFFFu) | 0x4000;
            const float vy = EvF32(mem, list + 8), g = EvF32(mem, list + 12);
            float t = vy / g;
            if (t < F32(exe, 0x004C436C)) t = t * F32(exe, 0x004C4C64);
            e.vel[0] = (EvF32(mem, list) - e.pos[0]) / (t + t);
            e.vel[2] = (EvF32(mem, list + 4) - e.pos[2]) / (t + t);
            e.vel[1] = vy;
            e.acc[1] = g;
            ++e.sub;
        }
        if (v) {  // turn to the list point taken into the vehicle's frame
            Mat4 m = Mat4::Identity();
            VehicleOps(m, v->pos, v->rot);
            const Mat4 inv = mx::Inverse(m);  // 0x004A8D20
            const float q[3] = { EvF32(mem, list), w.playerPos[1], EvF32(mem, list + 4) };
            float o[3];
            mx::Transform(inv, q, o);
            w.EnemyTurnTo(e, o[0], o[2], 0x68);  // 0x00409F90
        }
        return;
    }
    case 2: {
        e.vel[1] = e.acc[1] + e.vel[1];
        if (e.anim.tick[0] == EvS16(mem, list + 0x14)) e.flags34 |= 0x4000;
        int mat = 0;
        float f = w.FloorHeight(e.pos[0], e.pos[1] + F32(exe, 0x004C43B0), e.pos[2], &mat);  // 0x00409D40
        if (e.pos[1] < f && (w.cac40 == 0x37 || w.cac40 == 5)) {
            if (!(e.flags136C & 0x10000)) {
                const float p[3] = { e.pos[0], f, e.pos[2] };
                w.SpawnEffectAt(p, 0, 0, 0x61, 1, -1);  // 0x00407340
                e.flags136C |= 0x10000;
            }
            float y = f;
            do {
                do {
                    y = y - F32(exe, 0x004C4380);
                    f = w.FloorHeight(e.pos[0], y, e.pos[2], &mat);
                } while (w.cac40 == 0x37);
            } while (w.cac40 == 5);
        }
        if (((e.rot[0] | e.rot[2]) & 0xFFFF) != 0) {
            MtxStack s;
            s.LoadIdentity();
            s.RotateX(e.rot[0]);
            s.RotateZ(e.rot[2]);
            s.RotateY(e.rot[1]);
            int a = 0, b = 0, c = 0;
            NormalizeAngles(s, &a, &b, &c);  // 0x004018E0
            Mat4 m = Mat4::Identity();
            mx::RotY(m, b);
            mx::RotX(m, a / 2);
            mx::RotZ(m, c / 2);
            MatrixToAnglesYAxis(m, &e.rot[0], &e.rot[1], &e.rot[2]);  // 0x00401AE0
        }
        if (e.pos[1] <= f) {
            if (w.cac40 == 0x35) w.Sound(0x1C16A9);
            e.vel[0] = e.vel[1] = e.vel[2] = 0.0f;
            e.acc[1] = 0.0f;
            e.pos[1] = f;
            const uint32_t f6c = e.flags136C & 0xFFFEBFFFu;
            e.flags136C = f6c;
            if (e.hp < 1) {
                e.behaviour = 6;
                e.flags34 &= ~0x20000u;
                e.flags136C = f6c | 0xA0000002u;
                e.sub = 0;
            } else {
                e.flags34 &= w.cac40 == 0 ? 0xFFFD9FFFu : 0xFFFF9FFFu;
                ++e.sub;
                e.flags136C = f6c | 0x20000000u;
            }
        }
        w.EnemyTurnTo(e, w.playerPos[0], w.playerPos[2], 0x1A0);
        return;
    }
    case 3:
        if (e.anim.tick[0] == w.MotionTicks(e.anim.motion[0]) - 1) {
            e.behaviour = 1;
            e.sub = 0;
        }
        w.EnemyTurnTo(e, w.playerPos[0], w.playerPos[2], 0x1A0);
        return;
    default:
        return;
    }
}

}  // namespace

bool World::Impl::EnemyBehaviourVehicle(EnemyTask& e) {
    if (e.behaviour == 0x30) {
        Beh48(*this, e);
        return true;
    }
    if (e.behaviour != 0x2E) return false;
    Beh46(*this, e);
    return true;
}

// 0x0048B140: civilian update inside the vehicle's frame; the vehicle sunk: to world, then 0x0048A920
void World::Impl::CivOnVehicle(CivTask& c) {
    EnemyTask* v = nullptr;
    VehicleOf(*this, c, v);
    if (!v || (v->flags34 & 0x4000000)) {
        if (v) ToWorld(c, *v);  // 0x0045D920
        c.framed = false;
        c.vehicle = nullptr;
        c.fn = 0x0048A920;
        CivUpdate(c);
        return;
    }
    c.framed = true;
    c.frame = Mat4::Identity();
    VehicleOps(c.frame, v->pos, v->rot);
    c.frameMv = cam.view;  // 0x0048B140: push (the view), the vehicle ops on the top
    VehicleOps(c.frameMv, v->pos, v->rot);
    CivUpdate(c);
}

}  // namespace game
