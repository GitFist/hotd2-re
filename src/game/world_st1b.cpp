// game::World - stage 1 after scene 1 (auto-play capture build\scene_capture_auto_st1.txt): objects first reached in
// scenes 2..9 and the boss. See notes/agents/stage1b.md.
// CONFIRMED = code read + frame-exact against the auto-play capture; CODE = read and ported, not exercised.

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

// fpatan(x - cam x, z - cam z) * 65536 / 2pi (fmul qword at 24-bit precision: rounded to float), __ftol, movsx
int YawToward(const World::Impl& w, float x, float z) {
    const float dx = x - w.cam.pos[0], dz = z - w.cam.pos[2];
    const float a = static_cast<float>(std::atan2(static_cast<double>(dx), static_cast<double>(dz)) * mx::kToAngle);
    return static_cast<int16_t>(static_cast<int>(a));
}

// Item slot 1 (0x00471BD0 -> task 0x00471CC0): life-up (0x00415630: a life, or 300 points at the maximum 0x009A2440 = 5)
struct LifeItemTask : ActorTask {
    float pos[3] = {};          // +0x19C
    int rotY = 0;               // +0x1D0
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int16_t life = 0;           // +0x11C
    int16_t digits = 0;         // +0x28C label model base (counts up while shown)
    int counter = 0;            // +0x2A0 frames since shot
    int label = 0;              // +0x2A4 player label model (0x1256 P1 / 0x1257 P2)
    float rise = 0;             // +0x2C0
};

// Item slot 3 (0x004722A0 -> task 0x00471FA0): a creature (skeleton 0x1C, motion 0x140) worth 1000 points
struct CreatureItemTask : EnemyTask {
    int area = 0, areaCount = 0;  // +0x1330 / +0x1334
    int c1338 = 0, c133C = 0;     // +0x1338 / +0x133C light angles (0x0040E0B0, renderer)
    int fade = 0;                 // +0x1350 frames of the score label
    int digits = 0;               // +0x13F0
};

// Board 0x0043DD00 (actor type 0x40 variant 2, 0x0043DC30): a sign (model 0x17CC, two-sided) hanging until 0x007DCC48
// is set (by the scene's creature group 0x0043A990), then it swings down; shot: flies off, lands, lies flat
struct BoardTask : ActorTask {
    float pos[3] = {};        // +0x194
    float rest[3] = {};       // +0x1A0 (landing pivot)
    int ang[3] = {};          // +0x1AC / +0x1B0 / +0x1B4
    int vel[2] = {};          // +0x1B8 / +0x1C0
    int acc[2] = {};          // +0x1C4 / +0x1CC
    float vy = 0;             // +0x1D4
    float fall = 0.8f;        // +0x1D8
    int state = 0;            // +0x1DC
    int area = 0;             // +0x1E0
    int areaCount = 0;        // +0x1E4
    int corner = -1;          // +0x1E8
    int c1EC = -1;            // +0x1EC
    int settle = 0;           // +0x1F0
    int life = 1;             // +0x1F4
};

// Hit spark 0x0043E360 (0x0043E3D0): like 0x004667A0, scale 1.5, no roll
struct Spark2Task : WTask {
    float pos[3] = {};  // +0x34
    int16_t model = 0xE25;  // +0x44
    float scale = 1.5f;     // +0x48
};

// Komono kind 50 (ctor 0x00463BA0): static models 0x00594F08[sub] rows {s16 model, x, y, z, rx, ry, rz}, task 0x00467E50
struct StaticModelTask : ActorTask {
    float pos[3] = {};          // +0x19C
    float scale[3] = { 1, 1, 1 };  // +0x1A8
    float dz = 0;               // +0x1C8
    int rot[3] = {};            // +0x1CC / +0x1D0 / +0x1D4
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int16_t life = 0;           // +0x11C
    int16_t model = 0;          // +0x28C
    int16_t sub = 0;            // +0x290
    int index = 0;              // +0x2A0
};

// Komono kind 57 (generic ctor 0x00461CF0 -> task 0x0046F350): a shootable sign that shakes when shot
struct SignTask : ActorTask {
    float pos[3] = {};      // +0x19C
    int16_t model = 0;      // +0x28C
    float shake = 0;        // +0x2C0
    int8_t area = 0, areaCount = 0;
    int16_t life = 0;
};

// Spark 0x00465950 (0x00465860): models 0x904..0x919 at the shot point, roll +0x44, scale +0x4C
struct Spark3Task : WTask {
    float pos[3] = {};  // +0x34
    int roll = 0;       // +0x44
    int model = 0x904;  // +0x48
    float scale = 1.0f; // +0x4C
};

// Type 0x44 state 17 (ctor 0x00473A70 -> task 0x00474F30): a fixed model, optionally a collision-mesh target
struct MeshPropTask : ActorTask {
    float pos[3] = {};          // +0x19C
    float scale[3] = { 1, 1, 1 };  // +0x1A8
    int baseYaw = 0;            // +0x1D0
    int rot[3] = {};            // +0x64 / +0x68 / +0x6C
    const int* Rot64() const override { return rot; }
    int16_t model = 0;          // +0x28C
    int killFlag = -1;          // +0x2A4
    Prop17Type1 t1;             // +0x192 / +0x194 / +0x11C / +0x1DC / +0x2A0 / +0x2A8..+0x2B0 / items (game type 1)
};

// Komono kind 34 (generic ctor 0x00461CF0 case 0x22 -> task 0x0046A580): a barrel (model 0xA50); the first shot
// cracks it (0xA51) and throws it up, the second breaks it into two pieces (0x0046AD20) and may drop an item
struct BarrelTask : ActorTask {
    int8_t slot = 0;           // +0x194 item slot
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int8_t corner = 0;         // +0x198
    int8_t life = 0;           // +0x199
    int8_t state = 0;          // +0x192 (1 flying, 2 rocking on a corner)
    int16_t hp = 2;            // +0x11C
    float pos[3] = {};         // +0x19C
    float pivot[3] = {};       // +0x1A8
    float vel[3] = {};         // +0x1C0 / +0x1C4 / +0x1C8
    int rot[3] = {};           // +0x1CC / +0x1D0 / +0x1D4
    int spinX = 0, spinZ = 0;  // +0x1D8 / +0x1E0
    int16_t model = 0xA50;     // +0x28C
    int f2A0 = -1;             // +0x2A0 (pieces: frame counter)
    float floorY = 0;          // +0x2E0
};

// Komono kind 66 (ctor 0x00464500 -> task 0x0046FE00): fixed props from 0x00595158 (20) / 0x005953D8 (29 when
// params byte 0 > 0); the lamps 0x10DC..0x10DE swing when shot or while the hit shake timer 0x009C8E8C is 0x17 / 0x2F
struct LampTask : ActorTask {
    float pos[3] = {};             // +0x19C
    float scale[3] = { 1, 1, 1 };  // +0x1A8
    float dz = 0;                  // +0x1C8
    int rot[3] = {};               // +0x1CC / +0x1D0 / +0x1D4
    int swing = 0;                 // +0x1D8
    int16_t model = 0;             // +0x28C
    int16_t life = 0;              // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float hitDy = -3.5f;           // +0x2C0
};

// Actor type 0x29 (task 0x00432C80): fixed model lists 0x00589AF0 / 0x00589BE0 / 0x00589C88 by state, removed at
// camera P s16 0 tick P s16 2
struct ModelListTask : WTask {
    SpawnRecord rec;
};

// Actor type 0x12 (0x0043F9D0 -> task 0x0043FA60): a model (P s16 0, kept as a float) with an optional collision mesh
// (P word 1 -> +0x14C); after a countdown (P s16 1, gated by event flag P s16 9) the model steps from P s16 7 by the
// float P +0x14 up to P s16 8; removed at camera P s16 5 tick P s16 6
struct Type12Task : ActorTask {
    float pos[3] = {};    // +0x40
    int rot[3] = {};      // +0x64
    const int* Rot64() const override { return rot; }
    uint32_t cb = 0;      // c+0x00 per-frame function 0x005926A8[P s16 4]
    int16_t countdown = 0, camId = 0, camTick = 0, mStart = 0, mEnd = 0, flag = -1;  // c+4 .. c+0xE
    float scale = 1.0f;   // c+0x10
    float model = 0;      // c+0x14
    float step = 0;       // c+0x18
    int16_t f1F4 = 0;     // +0x1F4
};

// Komono kind 31 (generic ctor 0x00461CF0 case 0x1F -> task 0x0046A1C0): an animated model sequence: model (state) +
// frame (+0x2A0, 0 .. record angle z, also used as the z rotation)
struct Komono31Task : ActorTask {
    float pos[3] = {};             // +0x19C
    float scale[3] = { 1, 1, 1 };  // +0x1A8
    int rot[3] = {};               // +0x1CC / +0x1D0 / +0x1D4
    int16_t model = 0;             // +0x28C
    int16_t life = 0;              // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int frame = 0;                 // +0x2A0
    int last = 0;                  // +0x2A4
};

// Actor type 0x28 (task 0x00432610): a model (0x33) on camera path 0x00589AE0[i] at its tick; it follows the path
// from that tick on camera 0x2F and ends at tick 0x00576D38[path]; burning (flame models) until it moves
struct Type28Task : ActorTask {
    float pos[3] = {};  // +0x40
    int rot[3] = {};    // +0x64
    int16_t index = 0;  // +0x11C (record state)
    int16_t sub = 0;    // +0x1312
    int follow = 0;     // +0x1320
    int model = 0x33;   // +0x13F0
};

}  // namespace

// 0x00415630: life-up for player p (game type 0): +1 life below the maximum 0x009A2440 (5, 0x0040AB50), else 300 points
static void LifeUp(World::Impl& w, int p) {
    if (p < 0 || p > 1) return;
    if (w.player[p].lives >= 5) {
        w.AddScore(p, 300);
        return;
    }
    ++w.player[p].lives;
}

static int PropShooterB(World::Impl& w, uint32_t flags, bool score = false) {  // Prop_ScoreShot 0x004650F0(flags, score)
    int p;
    if ((flags & 2) && (flags & 4)) p = w.Rand(0x00465106) & 1;
    else p = (flags & 2) ? 0 : 1;
    if (score) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

// 0x00466640: item lifetime in camera variants (+0x196 / +0x197 / +0x11C); true = destroyed
template <class T> static void ItemAreaLife(World::Impl& w, T& k) {
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
}

// 0x00467E50
static void TaskStaticModel(Task& t) {
    World::Impl& w = *CurrentWorld();
    StaticModelTask& k = t.As<StaticModelTask>();
    ItemAreaLife(w, k);
    if (w.runner->camera.camId == 0x2F && w.runner->camera.tick == 0x96) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.dz + k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
    w.Draw(k.model, m, 8, -1.0f, w.runner->cfg.gameType != 2 && w.altModels != 0);
}

// 0x00465950
static void TaskSpark3(Task& t) {
    World::Impl& w = *CurrentWorld();
    Spark3Task& s = t.As<Spark3Task>();
    if (0x919 < ++s.model) {
        w.ts.KillCurrent();
        return;  // the original returns here (no draw of model 0x91A)
    }
    Mat4 m = w.cam.view;
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::ClearRotation(m);  // 0x004A9F70
    mx::RotZ(m, s.roll);
    mx::Scale(m, s.scale, s.scale, s.scale);
    w.Draw(s.model, m, 8, -1.0f, false, -1, true);
}

// 0x00465860(obj, player): spark at the player's aim on the object's view depth (+0x78); z = the object's +0x1A4
static void SpawnSpark3(World::Impl& w, float viewZ, int p, float z) {
    Spark3Task* s = w.NewTask<Spark3Task>(TaskSpark3, 0x00465950);
    s->model = 0x904;
    s->roll = 0;
    s->scale = 1.0f;
    const float fx = (w.player[p].aim[0] * viewZ) / w.focal, fy = (w.player[p].aim[1] * viewZ) / w.focal;
    const float local[3] = { -fx, -fy, viewZ };
    mx::Transform(w.cam.inverse, local, s->pos);
    s->pos[2] = z;
}

// The same spawn as an Impl member (breakable props, stage-2 komono 58: world.cpp / world_st2_komono.cpp)
void World::Impl::SpawnSmoke(float viewZ, int p, float z) { SpawnSpark3(*this, viewZ, p, z); }

// 0x0046F350
static void TaskSign(Task& t) {
    World::Impl& w = *CurrentWorld();
    SignTask& k = t.As<SignTask>();
    const ExeImage& exe = w.exe;
    float a = 0, b = 0, c = 0, d = 0;
    if (w.runner->camera.camId == 0x2F && w.runner->camera.tick == 0x96) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    }
    if (k.flags34 & 8) {
        w.Sound(0x1516A9);
        const uint32_t f = k.flags34;
        k.flags34 = f & ~8u;
        SpawnSpark3(w, k.viewPos[2], (f & 2) ? 0 : 1, k.pos[2]);
        const uint32_t z = 0xC4044F9Eu;
        memcpy(&k.pos[2], &z, 4);
        k.shake = 1.0f;
    }
    k.flags34 &= ~6u;
    if (F32(exe, 0x004C4CC0) < k.shake) {
        const float c2ac = F32(exe, 0x0055D2AC), c464 = F32(exe, 0x004D5464);
        a = ((static_cast<float>(w.Rand(0x0046F408) % 0x65) - c2ac) * k.shake) * c464;
        b = ((static_cast<float>(w.Rand(0x0046F433) % 0x65) - c2ac) * k.shake) * c464;
        c = ((static_cast<float>(w.Rand(0x0046F45E) % 0x65) - c2ac) * k.shake) * c464;
        d = ((static_cast<float>(w.Rand(0x0046F489) % 0x65) - c2ac) * k.shake) * c464;
        k.shake = k.shake * F32(exe, 0x00564534);
    }
    double k120 = 0, k128 = 0;
    exe.Read(0x00569120, &k120, 8);
    exe.Read(0x00569128, &k128, 8);
    Mat4 m = Mat4::Identity();
    uint32_t yb = 0xC14B374Cu, y2b = 0x40B5C28Fu;
    float y, y2;
    memcpy(&y, &yb, 4);
    memcpy(&y2, &y2b, 4);
    mx::Translate(m, static_cast<float>(a - k120), y, static_cast<float>(b - k128));
    mx::RotY(m, static_cast<int>(0xFFFFF500u));
    w.Draw(0xD43, m);
    mx::Translate(m, c - a, y2, d - b);
    w.Draw(0xD44, m);
    uint32_t pb[3] = { 0xC42E42B0u, 0xC11DC6A8u, 0xC4044F9Eu };
    float p[3];
    memcpy(p, pb, 12);
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

// 0x00474F30 (game type 0 part)
static void TaskMeshProp(Task& t) {
    World::Impl& w = *CurrentWorld();
    MeshPropTask& k = t.As<MeshPropTask>();
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    };
    if (k.killFlag >= 0 && w.runner->flags[k.killFlag & 0xFF] == 1) destroy();
    if (w.cfg.eventMode == 1) {
        if (w.runner->flags[0x77] != 0) destroy();
    } else if (w.cfg.eventMode == 2 && w.runner->scene == 2 && !(k.flags34 & 0)) {
        w.Note("type 0x44 state 17 stage-2 scene-2 flag 0x15 (0x00474F30) not ported");
    }
    if (w.runner->cfg.gameType == 1 && !Prop17GameType1(w, k, k.t1, k.pos, k.rot)) return destroy();
    if (k.t1.t2A8 <= 0x3B || (k.t1.t2AC & 1) == 0) {
        auto build = [&](Mat4 m) {
            mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
            mx::RotY(m, k.baseYaw);
            mx::RotZ(m, k.rot[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotX(m, k.rot[0]);
            mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
            return m;
        };
        const Mat4 wm = build(Mat4::Identity());
        w.Draw(k.model, wm, 8, -1.0f, w.altModels != 0 && w.runner->camera.camId != 0x46);
        k.m150 = build(w.cam.view);  // 0x004A8CA0 -> +0x150 (model-view)
    }
    w.RegisterTarget(&k);
}

// 0x0046B040(obj, corners, n): a corner below the floor reference +0x2E0 lands the object; state 2 rocks on the
// lowest other corner (pivot +0x1A8) and places the object from the pivot
static bool BarrelCorners(World::Impl& w, BarrelTask& k, uint32_t corners, int n) {
    const ExeImage& exe = w.exe;
    bool landed = false;
    float best = 10000.0f;
    float wallMin = 0.0f;
    Mat4 m = Mat4::Identity();
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    const float ks = F32(exe, 0x0055D2B0), lim = k.floorY - F32(exe, 0x004C4CC8);
    for (int i = 0; i < n; ++i) {
        const uint32_t e = corners + static_cast<uint32_t>(i) * 6;
        const float c[3] = { static_cast<float>(exe.S16(e)) * ks, static_cast<float>(exe.S16(e + 2)) * ks,
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
                k.pivot[1] = k.floorY;
                best = t[1];
                k.corner = static_cast<int8_t>(i);
                k.pivot[0] = t[0] + k.pos[0];
                k.pivot[2] = t[2] + k.pos[2];
            }
        }
        if (w.cfg.eventMode == 1 && w.runner->scene == 0x12) {  // stage-2 scene 0x12: a wall at x 0x005690E4
            const float x = t[0] + k.pos[0];
            if (x < F32(exe, 0x005690E4) && t[0] < wallMin) {
                wallMin = t[0];
                k.pos[0] = k.pos[0] - (x + F32(exe, 0x005690E0));
                k.vel[0] = k.vel[0] * F32(exe, 0x004C4C64);
            }
        }
    }
    if (k.state == 2) {
        Mat4 q = Mat4::Identity();
        mx::Translate(q, k.pivot[0], k.pivot[1], k.pivot[2]);
        mx::RotZ(q, k.rot[2]);
        mx::RotY(q, k.rot[1]);
        mx::RotX(q, k.rot[0]);
        const uint32_t e = corners + static_cast<uint32_t>(k.corner) * 6;
        const float kc = F32(exe, 0x0056903C);
        mx::Translate(q, static_cast<float>(exe.S16(e)) * kc, static_cast<float>(exe.S16(e + 2)) * kc,
                      static_cast<float>(exe.S16(e + 4)) * kc);
        mx::GetTranslation(q, k.pos);
    }
    return landed;
}

static void BarrelDraw(World::Impl& w, BarrelTask& k, uint32_t corners) {
    Mat4 m = Mat4::Identity();
    if (k.state == 2) {
        mx::Translate(m, k.pivot[0], k.pivot[1], k.pivot[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        const uint32_t e = corners + static_cast<uint32_t>(k.corner) * 6;
        const float kc = F32(w.exe, 0x0056903C);
        mx::Translate(m, static_cast<float>(w.exe.S16(e)) * kc, static_cast<float>(w.exe.S16(e + 2)) * kc,
                      static_cast<float>(w.exe.S16(e + 4)) * kc);
    } else {
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
    }
    w.Draw(k.model, m, 8, -1.0f, w.altModels != 0);
}

// 0x0046AD20: a barrel piece (180 frames, blinking from 150 while lying)
static void TaskBarrelPiece(Task& t) {
    World::Impl& w = *CurrentWorld();
    BarrelTask& k = t.As<BarrelTask>();
    const ExeImage& exe = w.exe;
    if (0xB4 < k.f2A0++) w.ts.KillCurrent();
    if (k.state == 1) {
        const float vy = k.vel[1] - F32(exe, 0x00569098);
        const int e = k.rot[0] + k.spinX;
        k.vel[1] = vy;
        k.pos[0] = k.vel[0] + k.pos[0];
        k.spinX = k.spinX - e / 32;
        k.pos[1] = vy + k.pos[1];
        k.rot[0] = k.spinX + k.rot[0];
        k.rot[2] += k.spinZ;
        k.pos[2] = k.vel[2] + k.pos[2];
        if (BarrelCorners(w, k, 0x005948A8, 0x37) && k.vel[1] < 0.0f) {
            k.state = 2;
            k.spinX = -k.spinX;
            w.Sound((w.Rand(0x0046AE7E) & 1) ? 0x1816A9u : 0x1916A9u);
        }
    } else if (k.state == 2) {
        int e = k.rot[0] + k.spinX;
        k.spinX = k.spinX - e / 32;
        k.rot[0] = k.spinX + k.rot[0];
        e = k.rot[2] + k.spinZ;
        k.spinZ = k.spinZ - e / 32;
        k.rot[2] = k.spinZ + k.rot[2];
        BarrelCorners(w, k, 0x005948A8, 0x37);
    }
    if (k.state == 2 && 0x96 < k.f2A0 && (k.f2A0 & 1)) return;
    BarrelDraw(w, k, 0x005948A8);
}

// 0x0046A580
static void TaskBarrel(Task& t) {
    World::Impl& w = *CurrentWorld();
    BarrelTask& k = t.As<BarrelTask>();
    const ExeImage& exe = w.exe;
    auto destroy = [&] {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    };
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->camera.camId == 0x2F && w.runner->camera.tick == 0x96) destroy();
    const float k5464 = F32(exe, 0x004D5464);
    if ((k.flags34 & 8) && 0 < k.hp) {
        if (k.hp == 1) {
            PropShooterB(w, k.flags34, true);  // 0x004650F0(flags, 1): + 10 points
            {
                const uint32_t f = k.flags34;
                const int p = ((f & 2) && (f & 4)) ? -1 : ((f & 2) ? 0 : 1);
                (void)p;
            }
            w.Sound((w.Rand(0x0046A7B3) & 1) ? 0x1616A9u : 0x1716A9u);
            int i = 0, yaw = 0x4000, sign = 1;
            do {
                BarrelTask* p = w.NewTask<BarrelTask>(TaskBarrelPiece, 0x0046AD20);
                const float fi = static_cast<float>(i);
                p->model = 0xA55;
                p->floorY = k.floorY;
                p->pos[0] = k.pos[0];
                p->pos[1] = fi + fi + k.floorY;
                p->pos[2] = k.pos[2];
                p->vel[0] = (static_cast<float>(w.Rand(0x0046A850) % 0xB) * k5464 + F32(exe, 0x004C4CC8)) * static_cast<float>(sign) + p->vel[0];
                p->vel[1] = fi * F32(exe, 0x004C43AC) + static_cast<float>(w.Rand(0x0046A881) % 0x15) * k5464 + F32(exe, 0x004C4CB8);
                const int r3 = w.Rand(0x0046A8B4);
                p->rot[0] = yaw;
                p->vel[2] = (static_cast<float>(r3 % 0xB) * k5464 + F32(exe, 0x004C4CC8)) * static_cast<float>(sign) + p->vel[2];
                p->rot[1] = k.rot[1];
                p->spinX = w.Rand(0x0046A8F7) % 0x101 - 0x80;
                const int r5 = w.Rand(0x0046A910);
                p->state = 1;
                p->spinZ = -0x40 - r5 % 0x41;
                p->life = static_cast<int8_t>(k.hp);
                p->area = static_cast<int8_t>(w.runner->variant);
                p->f2A0 = 0;
                ++i;
                sign -= 2;
                yaw -= 0x8000;
            } while (-3 < sign);
            if (w.runner->cfg.gameType == 1 && ItemFlag(w, 2)) {  // game type 1 with item 0x1E (0x009C88AA): always a life item
                BreakableProp b;
                b.pos[0] = k.pos[0];
                b.pos[1] = k.floorY;
                b.pos[2] = k.pos[2];
                b.areaSeen = static_cast<uint8_t>(k.area);
                b.areaChanges = static_cast<uint8_t>(k.areaCount);
                b.lifetime = k.life;
                b.model = k.model;
                w.DropItem(b, 1);  // 0x00471BD0
                destroy();
                return;  // (0x0046A9B0: the task returns)
            }
            if (0 < k.slot) {
                uint8_t& c = w.itemCounter[k.slot & 15];
                --c;
                if (c == 0 && w.runner->cfg.gameType == 1 && k.f2A0 != -1) {
                    // 0x0046A9EB: game type 1 and +0x2A0 set (the op-0x0B barrel 0x00473940): an original-mode item
                    // (0x00467B90, slot +0x2A0) at the floor + float 0x004C43AC, life +0x199; 0x007DCD14 = 0
                    const float at[3] = { k.pos[0], k.floorY + F32(exe, 0x004C43AC), k.pos[2] };
                    SpawnItemAt(w, at, k.area, k.areaCount, k.life, k.f2A0);
                    SetItemTaken(w, 0);
                } else if (c == 0) {
                    BreakableProp b;  // the drop reads the barrel's +0x19C / +0x196 / +0x197 / +0x11C
                    b.pos[0] = k.pos[0];
                    b.pos[1] = k.floorY;
                    b.pos[2] = k.pos[2];
                    b.areaSeen = static_cast<uint8_t>(k.area);
                    b.areaChanges = static_cast<uint8_t>(k.areaCount);
                    b.lifetime = k.life;
                    b.model = k.model;
                    w.DropItem(b, k.slot);  // 0x00471BD0 / 0x004723F0 / 0x004722A0
                }
            }
            destroy();
        }
        if (k.hp == 2) {
            const uint32_t f = k.flags34;
            PropShooterB(w, f);  // 0x004650F0(flags, 0)
            w.Sound(0xE16A9);
            k.model = 0xA51;
            k.rot[1] = w.cam.yaw;
            w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) ? 0 : 1, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
            k.flags34 &= ~8u;
            k.vel[1] = static_cast<float>(w.Rand(0x0046A6B5) % 0x29) * k5464 + F32(exe, 0x004C43A8);
            k.spinX = -0x180 - w.Rand(0x0046A6DC) % 0x101;
            const int r = w.Rand(0x0046A6F6);
            k.state = 1;
            k.spinZ = -0x80 - r % 0x81;
        }
        --k.hp;
    }
    k.flags34 &= ~6u;
    if (k.state == 1) {
        const float vy = k.vel[1] - F32(exe, 0x00569098);
        k.vel[1] = vy;
        k.rot[0] += k.spinX;
        k.rot[2] += k.spinZ;
        k.pos[1] = vy + k.pos[1];
        if (BarrelCorners(w, k, 0x00594788, 0x30) && k.vel[1] < 0.0f) {
            k.state = 2;
            w.Sound((w.Rand(0x0046AAFF) & 1) ? 0x1816A9u : 0x1916A9u);
        }
    } else if (k.state == 2) {
        int e = k.rot[2] + k.spinZ;
        k.spinZ = k.spinZ - e / 32;
        k.rot[2] = k.spinZ + k.rot[2];
        e = k.rot[0] + 0x4000 + k.spinX;
        k.spinX = k.spinX - e / 32;
        k.rot[0] = k.spinX + k.rot[0];
        BarrelCorners(w, k, 0x00594788, 0x30);
    }
    BarrelDraw(w, k, 0x00594788);
    if (0 < k.hp) {
        mx::Transform(w.cam.view, k.pos, k.viewPos);
        w.RegisterTarget(&k);
    }
}

// 0x0046FE00
static void TaskLamp(Task& t) {
    World::Impl& w = *CurrentWorld();
    LampTask& k = t.As<LampTask>();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) w.ts.KillCurrent();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    const int m = k.model;
    if (m == 0x10DC || m == 0x10DD || m == 0x10DE) {
        if (k.flags34 & 8) {
            const uint32_t f = k.flags34 & ~8u;
            k.flags34 = f;
            PropShooterB(w, f);  // 0x004650F0(flags, 0)
            w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) ? 0 : 1, 1.0f, k.pos[2]);  // 0x004666B0(obj, p, 1.0)
            w.Sound(0x1116A9);
            k.swing = w.Rand(0x0046FEAC) % 0x201 + 0x600;
        }
        if (w.cfg.eventMode == 0 && (w.shakeTimer == 0x17 || w.shakeTimer == 0x2F)) {
            const int s = w.Rand(0x0046FEDE) & 1;
            const int r = w.Rand(0x0046FEF8);
            k.swing = (s * -2 + 1) * (r % 0x201 + w.shakeTimer * 0x20);
        }
        const float p[3] = { k.pos[0], k.hitDy + k.pos[1], k.pos[2] };
        const int sw = k.swing - (k.rot[0] + k.swing) / 0x18;
        k.swing = sw;
        k.rot[0] = sw + k.rot[0];
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);
    }
    k.flags34 &= ~6u;
    Mat4 mm = Mat4::Identity();
    mx::Translate(mm, k.pos[0], k.pos[1], k.dz + k.pos[2]);
    if (m == 0x10DE) mx::Translate(mm, 0.0f, 1.5f, 0.0f);
    mx::RotZ(mm, k.rot[2]);
    mx::RotY(mm, k.rot[1]);
    mx::RotX(mm, k.rot[0]);
    if (m == 0x10DE) mx::Translate(mm, 0.0f, -1.5f, 0.0f);
    mx::Scale(mm, k.scale[0], k.scale[1], k.scale[2]);
    w.Draw(m, mm);
}

// 0x0043FA60
static void TaskType12(Task& t) {
    World::Impl& w = *CurrentWorld();
    Type12Task& k = t.As<Type12Task>();
    if (w.runner->camera.camId == k.camId && w.runner->camera.tick == k.camTick) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    }
    const int16_t c = k.countdown;
    if (-1 < c && (k.flag == -1 || w.runner->flags[k.flag & 0xFF] != 0)) {
        if (c == 0) {
            const float m0 = k.model;
            k.model = k.step + m0;
            if (static_cast<float>(k.mEnd) < k.step + m0) {
                k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
                w.ts.KillCurrent();
            }
        } else {
            k.countdown = static_cast<int16_t>(c - 1);
            if (k.countdown == 0) {
                k.model = static_cast<float>(k.mStart);
                k.f1F4 = static_cast<int16_t>(static_cast<int>(k.model));
                k.cb = 0x0041EBB0;
                k.flags34 |= 0x8000;
            }
        }
    }
    if (k.cb != 0x0041EBB0) w.Note("actor type 0x12 frame function 0x005926A8 entry (0x0043FA60) not ported");
    auto build = [&](Mat4 m) {
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotX(m, k.rot[0]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        if (k.scale != 1.0f) mx::Scale(m, k.scale, k.scale, k.scale);
        return m;
    };
    const int model = static_cast<int>(k.model);
    if (model != 0) w.Draw(model, build(Mat4::Identity()));  // Mdl_Draw 0x00418560(0) draws nothing
    k.m150 = build(w.cam.view);  // 0x004A8CA0 -> +0x150 (model-view)
    w.RegisterTarget(&k);
}

// 0x0046A1C0
static void TaskKomono31(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono31Task& k = t.As<Komono31Task>();
    ItemAreaLife(w, k);  // 0x00466640
    if (w.runner->camera.camId == 0x84 && w.runner->camera.tick == 0x1D6) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
    w.Draw(k.model + k.frame, m);
    if (w.cfg.eventMode == 2 && w.runner->scene == 0xB) {  // stage 3, scene 0xB: two more models (0x009A2BAC % 7)
        const int extra = w.frameCounter2BAC % 7 + 0x1797;
        mx::Translate(m, 0.0f, -55.125f, 0.0f);  // 0xC25C8000
        w.Draw(extra, m);
        mx::RotY(m, 0x8000);
        mx::Translate(m, 0.0f, 0.0f, -3.0f);
        w.Draw(extra, m);
    }
    if (k.last < ++k.frame) k.frame = 0;
}

// 0x00432610 (+ draw 0x00432840)
static void TaskType28(Task& t) {
    World::Impl& w = *CurrentWorld();
    Type28Task& k = t.As<Type28Task>();
    const ExeImage& exe = w.exe;
    const uint32_t e = 0x00589AE0 + static_cast<uint32_t>(k.index) * 4;
    const int path = exe.S16(e);
    const CameraDirector& c = w.runner->camera;
    auto place = [&](float tick) {  // 0x004042D0
        float p[3] = {}, tg[3] = {};
        int roll = 0;
        if (!w.cameraBank.Evaluate(path, tick, p, tg, &roll, false)) return;
        memcpy(k.pos, p, 12);
        for (int i = 0; i < 3; ++i) k.rot[i] = static_cast<int>(tg[i]);
    };
    bool check = false;
    bool story = k.fn == 0x00432810;
    if (k.sub == 0 && !story) {
        k.model = 0x33;
        k.follow = 0;
        if (w.cfg.gameMode == 10) {  // story mode: fixed placement 0x0055DD18[index] {pos, rot}, then 0x00432810 at once
            const uint32_t s = 0x0055DD18 + static_cast<uint32_t>(k.index) * 0x18;
            for (int i = 0; i < 3; ++i) {
                k.pos[i] = F32(exe, s + 4 * static_cast<uint32_t>(i));
                k.rot[i] = static_cast<int32_t>(exe.U32(s + 12 + 4 * static_cast<uint32_t>(i)));
            }
            k.fn = 0x00432810;
            story = true;
        }
    }
    if (story) {  // 0x00432810: removed once camera 8 runs (tick >= 0), else drawn (0x00432840)
        if (c.camId == 8 && c.tick >= 0) {
            w.ts.KillCurrent();
            return;
        }
    } else if (k.sub == 0) {
        place(static_cast<float>(exe.S16(e + 2)));
        ++k.sub;
        check = true;
    } else if (k.sub == 1) {
        check = true;
    } else if (k.sub == 2) {
        if (static_cast<int32_t>(exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4)) <= c.tick) {
            w.ts.KillCurrent();
            return;
        }
    }
    if (check && c.camId == 0x2F && exe.S16(e + 2) <= c.tick) {
        ++k.sub;
        k.follow = 1;
    }
    if (k.follow != 0) place(static_cast<float>(c.tick));
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.model, m);
    if (k.follow == 0) {
        int pitch = 0, yaw = 0;
        mx::DirToAngles(w.cam.pos[0] - k.pos[0], 0.0f, w.cam.pos[2] - k.pos[2], &pitch, &yaw);  // 0x004016B0
        Mat4 f = Mat4::Identity();
        mx::Translate(f, k.pos[0], k.pos[1] + F32(exe, 0x0055D2B4), k.pos[2]);
        mx::RotY(f, yaw);
        mx::Scale(f, 1.5f, 2.0f, 1.0f);
        // These separate tall smoke sheets overlap. The native near-first model
        // order lets one sheet's depth writes cut a straight edge through the other.
        if (auto* smoke = w.Draw(static_cast<int>(w.seedCounter % 0xF) + 0x135F, f))
            smoke->sortBackToFront = true;
        Mat4 g = Mat4::Identity();
        mx::Translate(g, k.pos[0], k.pos[1] + F32(exe, 0x004C43A0), k.pos[2]);
        mx::RotY(g, yaw);
        mx::Translate(g, 0.0f, 0.0f, 12.0f);
        mx::Scale(g, 7.0f, 7.0f, 7.0f);
        w.Draw(static_cast<int>(w.seedCounter & 7) + 0xB67, g);
    }
}

// 0x00432C80
static void TaskModelList(Task& t) {
    World::Impl& w = *CurrentWorld();
    ModelListTask& k = t.As<ModelListTask>();
    if (w.runner->camera.camId == k.rec.ParamS16(0) && k.rec.ParamS16(2) <= w.runner->camera.tick) {
        w.ts.KillCurrent();
        return;  // Task_Kill, then return: no draws on that frame
    }
    uint32_t a = 0;
    switch (static_cast<int16_t>(k.rec.state)) {
    case 0: a = 0x00589AF0; break;
    case 1: a = 0x00589BE0; break;
    case 2: a = 0x00589C88; break;
    default: w.Note("actor 0x29 state " + std::to_string(k.rec.state) + " (0x00432C80) reads garbage"); return;
    }
    for (; static_cast<int32_t>(w.exe.U32(a)) != -1; a += 24) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, F32(w.exe, a + 4), F32(w.exe, a + 8), F32(w.exe, a + 12));
        mx::RotY(m, static_cast<int32_t>(w.exe.U32(a + 16)));
        const float s = F32(w.exe, a + 20);
        mx::Scale(m, s, s, s);
        w.Draw(static_cast<int>(w.exe.U32(a)), m);
    }
}

// 0x00471CC0
static void TaskLifeItem(Task& t) {
    World::Impl& w = *CurrentWorld();
    LifeItemTask& k = t.As<LifeItemTask>();
    const ExeImage& exe = w.exe;
    ItemAreaLife(w, k);
    if (!(k.flags34 & 0x40000000) && (k.flags34 & 8)) {
        PropShooterB(w, k.flags34);
        k.flags34 |= 0x40000000;
        w.Sound(0x3616A9);
        k.counter = 1;
        const uint32_t g = k.flags34 & 2;
        if (g == 0 || !(k.flags34 & 4)) {
            if (g == 0) {
                LifeUp(w, 1);
                k.digits = 0x119C;
                k.label = 0x1257;
            } else {
                LifeUp(w, 0);
                k.digits = 0x116A;
                k.label = 0x1256;
            }
        } else {
            const int p = w.Rand(0x00471D27) & 1;
            LifeUp(w, p);
            k.label = p + 0x1256;
            k.digits = static_cast<int16_t>(p * 0x32 + 0x116A);
        }
    }
    if (k.counter > 0 && 0x31 < ++k.counter) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    }
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1] + F32(exe, 0x004C4CB8), k.pos[2]);
        mx::RotY(m, k.rotY);
        mx::Scale(m, 3.0f, 3.0f, 3.0f);
        if (k.counter < 0x19) w.Draw(0x10C3, m);
        else w.Draw(0x10C3, m, 8, F32(exe, 0x004C4380) - static_cast<float>(k.counter) * F32(exe, 0x004E3100));  // 0x004185A0
    }
    if (k.counter > 0) {
        k.rise = k.rise + F32(exe, 0x004C4C88);
        Mat4 m = w.cam.view;
        mx::Translate(m, k.pos[0], static_cast<float>((k.rise + k.pos[1]) + F64(exe, 0x004C4CA8)), k.pos[2]);
        mx::ClearRotation(m);  // 0x004A9F70: faces the screen
        mx::Scale(m, 1.5f, 1.5f, 1.5f);
        w.Draw(k.label, m, 8, -1.0f, false, -1, true);
        Mat4 d = Mat4::Identity();
        mx::Translate(d, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(d, k.rotY);
        w.Draw(k.digits - 1 + k.counter, d);
    }
    const float p[3] = { k.pos[0], k.pos[1] + F32(exe, 0x004C49C0), k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

// 0x00471FA0
static void TaskCreatureItem(Task& t) {
    World::Impl& w = *CurrentWorld();
    CreatureItemTask& k = t.As<CreatureItemTask>();
    const ExeImage& exe = w.exe;
    auto destroy = [&] {
        if (k.actorSlot >= 0 && k.actorSlot < 14) w.actorSlots[k.actorSlot] = nullptr;
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    };
    if (w.runner->variant != k.area) {
        if (k.hp < ++k.areaCount) destroy();  // +0x11C lifetime
        k.area = w.runner->variant;
    }
    if (w.cfg.eventMode == 0 && w.runner->scene == 0xE) {
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
    k.c1338 += 0x300;
    k.c133C -= 0x180;
    // 0x0040E0B0 / 0x004AA0E0: light direction from +0x1338 / +0x133C (renderer state), restored after the draw
    w.AnimDraw(k);  // 0x00411090
    w.EnemyShadow(k);
    if ((k.flags34 & 8) && k.behaviour == 0) {
        PropShooterB(w, k.flags34);
        w.AnimBlendTo(k, 0x13F, 0, 4);  // 0x004119A0
        k.behaviour = 1;
        w.Sound(0x3B17A9);
        const uint32_t g = k.flags34 & 2;
        if (g == 0 || !(k.flags34 & 4)) {
            if (g == 0) {
                w.AddScore(1, 1000);
                k.digits = 0x119C;
            } else {
                w.AddScore(0, 1000);
                k.digits = 0x116A;
            }
        } else {
            const int p = w.Rand(0x00472114) & 1;
            w.AddScore(p, 1000);
            k.digits = p * 0x32 + 0x116A;
        }
    }
    if (k.behaviour == 1 && k.anim.ended) k.behaviour = 2;  // +0x1F1 = anim +0x5D
    // game type 2 (training): the motion only runs while the lesson does (event flag 0xF1 set, 0xF2 not)
    const bool runs = w.runner->cfg.gameType != 2 || (w.runner->flags[0xF1] != 0 && w.runner->flags[0xF2] == 0);
    if (k.behaviour < 2 && runs) ++k.anim.counter[0];
    if (k.behaviour > 0 && k.fade < 0x32) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.rot[1]);
        w.Draw(k.digits + k.fade, m);
        if (0x31 < ++k.fade) destroy();
    }
    const float b[3] = { k.body[0], k.body[1] - F32(exe, 0x004C4C58), k.body[2] };
    mx::Transform(w.cam.view, b, k.viewPos);
    w.RegisterTarget(&k);
}

// Node callback 0x00463F90 of the slot-3 creature: opaque, fading with the score label from frame 0x19
void World::Impl::CreatureItemNode(EnemyTask& e, int idx, const Mat4& m) {
    const CreatureItemTask& k = static_cast<const CreatureItemTask&>(e);
    if (k.fade < 0x19) Draw(e.anim.nodes[idx].model, m, 8, -1.0f, false, idx);
    else Draw(e.anim.nodes[idx].model, m, 8, F32(exe, 0x004C4380) - static_cast<float>(k.fade) * F32(exe, 0x004E3100), false, idx);
}

// Behaviour 39 (0x0045BAD0): a civilian's child enemy kept hidden and not shootable (flags 0x18000, not drawn) until
// the civilian's script op 0x1A sets its counter +0x2E: then behaviour = the civilian's +0x2C (0x31: dies at once,
// killer = the civilian's rescuer +0x6C), flags restored, drawn again, and the new behaviour runs in the same frame
void World::Impl::EnemyBeh39(EnemyTask& e) {
    CivCtl* k = (e.parent && e.parent->civilian) ? &static_cast<CivTask*>(e.parent)->c : nullptr;
    if (e.sub == 0) {
        e.facing1350 = static_cast<int>(e.flags34);  // +0x1350 saved flags
        e.flags34 |= 0x18000;
        e.anim.drawFlags &= ~1u;  // + the first attachment's enable byte (anim +0x40)[1] = 0
        ++e.sub;
    } else if (e.sub != 1) {
        if (e.sub != 2) return;
        EnemyDestroy(e);  // 0x00409CC0 (+ actor slot)
        return;
    }
    if (!k) {
        Note("behaviour 39 without a civilian parent (0x0045BAD0)");
        return;
    }
    if (k->c2E == 0) return;
    --k->c2E;
    if (k->c2C == 0x31) {
        e.flags34 |= 0x4000000;
        if (k->scorePlayer == -1) {
            if (players == 1) e.killer = static_cast<int8_t>(playersInGame);
            else e.killer = static_cast<int8_t>(Rand(0x0045BB98) & 1);
        } else {
            e.killer = static_cast<int8_t>(k->scorePlayer);
        }
        if (!(e.flags38 & 2)) {  // 0x00456560
            e.flags38 |= 2;
            --liveEnemies;
        }
        if (!(e.flags38 & 4)) {  // 0x00456580
            e.flags38 |= 4;
            --attackers;
        }
        ++e.sub;
        return;
    }
    e.behaviour = k->c2C;
    e.sub = 0;
    e.flags34 = static_cast<uint32_t>(e.facing1350);
    e.anim.drawFlags |= 1;
    EnemyBehaviour(e);  // 0x00592AE8[behaviour]
}

// 0x0043E360
static void TaskSpark2(Task& t) {
    World::Impl& w = *CurrentWorld();
    Spark2Task& s = t.As<Spark2Task>();
    if (0xE33 < ++s.model) w.ts.KillCurrent();
    Mat4 m = w.cam.view;
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::ClearRotation(m);  // 0x004A9F70
    mx::Scale(m, s.scale, s.scale, s.scale);
    w.Draw(s.model, m, 8, -1.0f, false, -1, true);
}

// 0x0043E3D0(obj, player): spark at the player's aim on the object's view depth (+0x78); z = the object's +0x19C
static void SpawnSpark2(World::Impl& w, float viewZ, int p, float z) {
    Spark2Task* s = w.NewTask<Spark2Task>(TaskSpark2, 0x0043E360);
    const float fx = (w.player[p].aim[0] * viewZ) / w.focal, fy = (w.player[p].aim[1] * viewZ) / w.focal;
    const float local[3] = { -fx, -fy, viewZ };
    mx::Transform(w.cam.inverse, local, s->pos);
    s->pos[2] = z;
}

// 0x0043DD00
static void TaskBoard(Task& t) {
    World::Impl& w = *CurrentWorld();
    BoardTask& b = t.As<BoardTask>();
    const ExeImage& exe = w.exe;
    if (w.runner->variant != b.area) {
        if (b.life * 2 + 1 < ++b.areaCount) {
            b.flags34 = (b.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
            w.ts.KillCurrent();
        }
        b.area = w.runner->variant;
    }
    switch (b.state) {
    case 0:
        if (w.dcc48 != 0) b.state = 1;
        break;
    case 1: {
        if (b.ang[0] < 0x4000) {
            b.ang[0] += 0x600;
            if (0x1800 < b.ang[0]) b.ang[1] += 0x40;
        }
        const float f = b.fall - F32(exe, 0x004C4CB0);
        b.fall = f;
        if (f <= F32(exe, 0x004C436C)) {
            b.pos[2] = b.pos[2] + F32(exe, 0x004C49C0);
            b.state = 4;
        } else {
            b.pos[2] = f + b.pos[2];
        }
        break;
    }
    case 2: {
        const float vy = b.vy - F32(exe, 0x0055CB10);
        b.vy = vy;
        b.vel[0] += b.acc[0];
        b.vel[1] += b.acc[1];
        b.pos[1] = vy + b.pos[1];
        b.ang[2] += b.vel[1];
        b.ang[0] += b.vel[0];
        if (vy < F32(exe, 0x004C436C)) {
            Mat4 m = Mat4::Identity();
            mx::RotY(m, b.ang[1]);
            mx::RotZ(m, b.ang[2]);
            mx::RotX(m, b.ang[0]);
            for (int i = 0; i < 4; ++i) {
                const float c[3] = { F32(exe, 0x00592688 + 8 * i), F32(exe, 0x0059268C + 8 * i), 0.0f };
                float d[3];
                mx::Transform(m, c, d);
                if (d[1] + b.pos[1] < w.ground + F32(exe, 0x004D1D24)) {
                    b.vel[0] = b.ang[0] < 0x4001 ? 0x200 : -0x200;
                    b.rest[0] = d[0] + b.pos[0];
                    b.vel[1] = b.ang[2] < 1 ? 0x200 : -0x200;
                    b.rest[2] = d[2] + b.pos[2];
                    b.rest[1] = w.ground;
                    b.state = 3;
                    ++b.settle;
                    b.corner = i;
                }
            }
        }
        break;
    }
    case 3: {
        const int v0 = b.vel[0], v1 = b.vel[1];
        b.ang[0] += v0;
        b.ang[2] += v1;
        if ((v0 > 0 && b.ang[0] > 0x4000) || (v0 < 0 && b.ang[0] < 0x4000)) {
            b.vel[0] = 0;
            b.ang[0] = 0x4000;
            ++b.settle;
        }
        if ((v1 > 0 && b.ang[2] > 0) || (v1 < 0 && b.ang[2] < 0)) {
            b.vel[1] = 0;
            b.ang[2] = 0;
            ++b.settle;
        }
        if (2 < b.settle) {
            b.ang[0] = 0x4000;
            b.ang[2] = 0;
            b.vel[0] = b.vel[1] = 0;
            b.settle = 0;
            b.state = 4;
        }
        break;
    }
    case 4:
        if (b.flags34 & 8) {
            w.Sound(0x1116A9);
            b.state = 2;
            b.vy = 0.6f;  // 0x3F19999A
            b.vel[0] = w.Rand(0x0043DE2E) % 0x401 - 0x200;
            b.vel[1] = w.Rand(0x0043DE47) % 0x401 - 0x200;
            b.acc[0] = -(b.vel[0] / 32);
            b.acc[1] = -(b.vel[1] / 32);
            SpawnSpark2(w, b.viewPos[2], (b.flags34 & 2) ? 0 : 1, b.pos[2]);
        }
        break;
    default:
        break;
    }
    if (b.state == 3) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, b.rest[0], b.rest[1], b.rest[2]);
        mx::RotY(m, b.ang[1]);
        mx::RotZ(m, b.ang[2]);
        mx::RotX(m, b.ang[0]);
        mx::Translate(m, -F32(exe, 0x00592688 + 8 * b.corner), -F32(exe, 0x0059268C + 8 * b.corner), 0.0f);
        mx::GetTranslation(m, b.pos);
        mx::Scale(m, 1.0f, 1.25f, 1.0f);
        w.Draw(0x17CC, m);
        mx::RotY(m, 0x8000);
        w.Draw(0x17CC, m);
    } else {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, b.pos[0], b.pos[1], b.pos[2]);
        mx::RotY(m, b.ang[1]);
        mx::RotZ(m, b.ang[2]);
        mx::RotX(m, b.ang[0]);
        if (b.state == 0 || b.state == 1) mx::Translate(m, 0.0f, 3.0f, 0.0f);
        w.Draw(0x17CC, m);
        mx::RotY(m, 0x8000);
        mx::Scale(m, 1.0f, 1.25f, 1.0f);
        w.Draw(0x17CC, m);
    }
    b.flags34 &= ~8u;
    mx::Transform(w.cam.view, b.pos, b.viewPos);
    w.RegisterTarget(&b);
}

// Spawned actors first reached after scene 1 (chained from SpawnScene1). Returns true when handled.
bool World::Impl::SpawnSt1b(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    if (SpawnStageClear(t, s)) return true;  // world_st1b_boss.cpp
    if (r.type == 0x41 && r.ParamS8(1) == 66) {  // 0x00464500
        const bool big = r.ParamS8(0) > 0;
        const int n = big ? 0x1D : 0x14;
        const uint32_t tab = big ? 0x005953D8 : 0x00595158;
        for (int i = 0; i < n; ++i) {
            const uint32_t e = tab + static_cast<uint32_t>(i) * 0x20;
            LampTask* k = NewTask<LampTask>(TaskLamp, 0x0046FE00);
            for (int c = 0; c < 3; ++c) k->pos[c] = F32(exe, e + 4 + 4 * c);
            k->rot[1] = static_cast<int32_t>(exe.U32(e + 0x10));
            for (int c = 0; c < 3; ++c) k->scale[c] = F32(exe, e + 0x14 + 4 * c);
            k->model = exe.S16(e);
            k->area = static_cast<int8_t>(runner->variant);
            k->areaCount = 0;
            k->radius = 4.0f;
            k->life = static_cast<int16_t>(r.state);
            k->hitDy = -3.5f;  // 0xC0600000
            if (k->model == 0x10DC) {
                k->radius = 6.5f;
                k->hitDy = -5.0f;
            }
            if (k->model == 0x10B1) k->rot[0] = -0x4000;
            k->flags34 = 0x80000001u;
        }
        ts.KillCurrent();
    }
    if (r.type == 0x41 && r.ParamS8(1) == 34) {  // generic ctor 0x00461CF0 case 0x22 -> 0x0046A580
        BarrelTask* k = NewTask<BarrelTask>(TaskBarrel, 0x0046A580);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->slot = r.ParamS8(0);
        k->life = static_cast<int8_t>(r.state & 0xFF);
        k->hp = 2;
        k->model = 0xA50;
        k->radius = 8.0f;
        k->floorY = k->pos[1] - F32(exe, 0x00568FF8);
        k->state = 0;
        k->f2A0 = -1;
        if (0 < k->slot) {
            if (1 < r.rot[0]) itemCounter[k->slot & 15] = static_cast<uint8_t>(Rand(0x00462145) % r.rot[0] + 1);
            else itemCounter[k->slot & 15] = 1;
        }
        ts.KillCurrent();
    }
    if (r.type == 0x44 && r.state == 16) {  // 0x00473940: the kind-34 barrel from an op-0x0B record (routes2a)
        BarrelTask* k = NewTask<BarrelTask>(TaskBarrel, 0x0046A580);
        memcpy(k->pos, r.pos, 12);
        k->rot[1] = r.rot[1];  // (+0x1D0 only; +0x290 = rot z and +0x324 / +0x328 from 0x00593DB8: unused by the task)
        k->hp = 2;
        k->model = 0xA50;
        k->radius = 8.0f;
        k->floorY = k->pos[1] - F32(exe, 0x00568FF8);
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->state = 0;
        k->life = r.ParamS8(0);
        k->slot = r.ParamS8(4);
        k->f2A0 = r.ParamS32(8);
        if (0 < k->slot) {
            if (1 < r.rot[0]) itemCounter[k->slot & 15] = static_cast<uint8_t>(Rand(0x00473A4C) % r.rot[0] + 1);
            else itemCounter[k->slot & 15] = 1;
        }
        ts.KillCurrent();
    }
    if (r.type == 0x41 && r.ParamS8(1) == 57) {  // generic ctor 0x00461CF0 -> 0x0046F350
        SignTask* k = NewTask<SignTask>(TaskSign, 0x0046F350);
        memcpy(k->pos, r.pos, 12);
        k->model = static_cast<int16_t>(r.state);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(runner->variant);
        k->flags34 = 0x80000001u;
        k->radius = 5.0f;  // case 0x39
        ts.KillCurrent();
    }
    if (r.type == 0x44 && r.state == 17) {  // 0x00473A70
        MeshPropTask* k = NewTask<MeshPropTask>(TaskMeshProp, 0x00474F30);
        memcpy(k->pos, r.pos, 12);
        k->baseYaw = r.rot[1];
        k->rot[1] = 0;
        k->t1.life = 1;
        const int32_t mesh = r.ParamS32(8);
        k->flags34 |= 1;
        if (mesh == -1) {
            k->radius = 8.0f;
            k->flags34 |= 0x80000001u;
        } else {
            k->flags34 |= 0x51;
        }
        k->model = r.ParamS16(4);
        k->objMesh = mesh;
        k->t1.c1DC = r.ParamS32(0xC);
        k->t1.kind = r.ParamS8(0);
        k->t1.flag2A0 = r.ParamS8(0x10);
        for (int i = 0; i < 4; ++i) k->t1.items[i] = r.ParamS8(0x20 + i);
        Prop17Created(*this);
        k->killFlag = r.ParamS8(0x11);
        float sc[3];
        memcpy(sc, &r.params[0x14], 12);
        for (int c = 0; c < 3; ++c) k->scale[c] = sc[c];
        // +0x1FC.. (s8 0x20..0x23): game type 1 shot conditions; 0x009A26EC = 0
        ts.KillCurrent();
    }
    if (r.type == 0x41 && r.ParamS8(1) == 77) {  // generic ctor 0x00461CF0 -> 0x005936BC[77] = 0x004717A0
        ActorTask* k = NewTask<ActorTask>(
            [](Task& t) {  // 0x004717A0, game type 0x009CA08C != 1: sound 0x800A9, Actor_Destroy on its first run
                World::Impl& w = *CurrentWorld();
                ActorTask& a = t.As<ActorTask>();
                w.Sound(0x800A9);
                a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
                w.ts.KillCurrent();
            },
            0x004717A0);
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
    }
    if (r.type == 0x41 && r.ParamS8(1) == 50) {  // 0x00463BA0: 0x00594F20[sub] static models (sub = params byte 0)
        const int sub = r.ParamS8(0);
        const int n = static_cast<int8_t>(exe.S16(0x00594F20 + static_cast<uint32_t>(sub)) & 0xFF);
        const uint32_t tab = exe.U32(0x00594F08 + 4 * static_cast<uint32_t>(sub));
        for (int i = 0; i < n; ++i) {
            const uint32_t e = tab + static_cast<uint32_t>(i) * 0x1C;
            StaticModelTask* k = NewTask<StaticModelTask>(TaskStaticModel, 0x00467E50);
            k->life = static_cast<int16_t>(r.state);
            k->area = static_cast<int8_t>(runner->variant);
            k->areaCount = 0;
            k->sub = static_cast<int16_t>(sub);
            for (int c = 0; c < 3; ++c) k->pos[c] = F32(exe, e + 4 + 4 * c);
            for (int c = 0; c < 3; ++c) k->rot[c] = static_cast<int32_t>(exe.U32(e + 0x10 + 4 * c));
            if (sub == 5) k->scale[2] = F32(exe, 0x00594EF0 + 4 * static_cast<uint32_t>(i));
            k->index = i;
            k->model = exe.S16(e);
        }
        ts.KillCurrent();
    }
    if (r.type == 0x41 && r.ParamS8(1) == 31) {  // generic ctor 0x00461CF0 case 0x1F -> 0x0046A1C0
        Komono31Task* k = NewTask<Komono31Task>(TaskKomono31, 0x0046A1C0);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->model = static_cast<int16_t>(r.state);
        k->life = r.ParamS8(0);  // +0x1F4 of the spawner (op 0x09: params byte 0)
        k->last = r.rot[2];
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        ts.KillCurrent();
    }
    if (r.type == 0x44 && r.state == 10) {  // 0x00473370: the same task 0x0046A1C0 from the record
        Komono31Task* k = NewTask<Komono31Task>(TaskKomono31, 0x0046A1C0);
        memcpy(k->pos, r.pos, 12);
        k->rot[1] = r.rot[1];  // +0x1D0 (+0x1CC / +0x1D4 stay 0)
        k->area = static_cast<int8_t>(runner->variant);
        k->areaCount = 0;
        k->last = r.rot[2];    // +0x2A4
        k->life = r.ParamS16(0);
        k->model = r.ParamS16(4);
        for (int i = 0; i < 3; ++i) {
            const int32_t v = r.ParamS32(0x14 + 4 * i);
            memcpy(&k->scale[i], &v, 4);
        }
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x28) {  // task 0x00432610 (the spawn task becomes the actor and runs now)
        auto d = std::make_unique<Type28Task>();
        d->fn = 0x00432610;
        d->flags34 = r.flags | 1;
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->index = static_cast<int16_t>(r.state);
        t.data = std::move(d);
        t.fn = TaskType28;
        TaskType28(t);
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 52) {  // 0x00463D20: two static models 0x1794 / 0x1795 (task 0x00467E50)
        Mat4 m = Mat4::Identity();
        mx::Translate(m, r.pos[0], r.pos[1], r.pos[2]);
        mx::RotY(m, r.rot[1]);
        int add = 0;
        for (int i = -1, n = 0; i < 3; i += 2, ++n) {
            StaticModelTask* k = NewTask<StaticModelTask>(TaskStaticModel, 0x00467E50);
            const float p[3] = { static_cast<float>(i) * F32(exe, 0x00569018), 11.5f, 22.68f };
            mx::Transform(m, p, k->pos);
            k->rot[1] = r.rot[1] + add;
            k->model = static_cast<int16_t>(0x1794 + n);
            k->life = static_cast<int16_t>(r.state);
            k->area = static_cast<int8_t>(runner->variant);
            k->areaCount = 0;
            add += 0x8000;
        }
        ts.KillCurrent();
    }
    if (r.type == 0x12) {  // 0x0043F9D0 (the spawn task becomes the actor; 0x0043FA60 from the next frame)
        auto d = std::make_unique<Type12Task>();
        d->fn = 0x0043FA60;
        d->flags34 = r.flags | 1;
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->model = static_cast<float>(r.ParamS16(0));
        d->objMesh = r.ParamS32(4);
        d->cb = exe.U32(0x005926A8 + 4 * static_cast<uint32_t>(r.ParamS16(8)));
        d->countdown = r.ParamS16(2);
        d->camId = r.ParamS16(10);
        d->camTick = r.ParamS16(12);
        d->mStart = r.ParamS16(14);
        d->mEnd = r.ParamS16(16);
        d->flag = r.ParamS16(18);
        memcpy(&d->step, &r.params[20], 4);
        memcpy(&d->scale, &r.params[24], 4);
        t.data = std::move(d);
        t.fn = TaskType12;
        return true;
    }    if (r.type == 0x29) {  // task 0x00432C80 (the type function itself)
        // the spawn task is that task in the original (it runs from the spawn frame on)
        const SpawnRecord rec = r;
        t.data = std::make_unique<ModelListTask>();
        t.As<ModelListTask>().fn = 0x00432C80;
        t.As<ModelListTask>().rec = rec;
        t.fn = TaskModelList;
        TaskModelList(t);
        return true;
    }
    if (r.type == 0x40) {  // 0x0043BD30: variant +0x130C (op 0x09 params byte 1)
        const int variant = s.op == 0x09 ? r.ParamS8(1) : 0;
        if (variant == 2) {  // 0x0043DC30: the board
            BoardTask* b = NewTask<BoardTask>(TaskBoard, 0x0043DD00);
            b->pos[0] = r.pos[0];
            uint32_t y = 0xC1146E2Fu, z = 0xC406B333u;
            memcpy(&b->pos[1], &y, 4);
            memcpy(&b->pos[2], &z, 4);
            b->area = runner->variant;
            b->fall = 0.8f;  // 0x3F4CCCCD
            b->state = 0;
            b->radius = 3.0f;
            b->flags34 = 1;
            b->life = runner->scene == 3 ? 0 : 1;
            dcc48 = 0;
            ts.KillCurrent();
        }
        SpawnBatGroup(r, variant);  // world_st1b_bats.cpp
        ts.KillCurrent();
    }
    return false;
}

// Behaviour 33 (0x00459080): world_st2_enemy.cpp

// 0x00474120 (type 0x44 state 3): the collision-mesh target part: mesh +0x14C (params +8) and +0x150 = the
// model-view matrix of node 1 stored by 0x0040DFD0 (+0x338); +0x64.. (motion rotations at slot +0x2A0, 0x0040E070)
// are not needed by the mesh tests
void World::Impl::AnimatedPropMesh(ActorTask& a, const AnimatedProp& p, const PropAnim& before) {
    a.objMesh = p.hitId;
    PropAnim mv = before;
    Mat4 parent = cam.view;
    mx::Translate(parent, p.pos[0], p.pos[1], p.pos[2]);
    Mat4 stored = Mat4::Identity();
    PropPose(PropSkel(p.anim.skeleton), *PropMotion(p.anim.motion), mv, parent, p.model, false, 2, &stored);
    a.m150 = stored;
}

// 0x0040A090(&pos, from, to, g, &frames): ballistic jump: frames until the fall passes to.y, velocity +0x4C / acc +0x58
static int JumpSetup(EnemyTask& e, const float from[3], const float to[3], float g) {
    int n = 0;
    float v = 0, y = from[1];
    while (to[1] < y) {
        v = v - g;
        ++n;
        y = v + y;
    }
    e.acc[1] = -g;
    e.acc[0] = e.acc[2] = 0;
    const float fn = static_cast<float>(n);
    e.vel[0] = (to[0] - from[0]) / fn;
    const float dy = to[1] - from[1];
    e.vel[1] = ((dy + dy) + (fn * fn) * g) / (fn + fn);
    e.vel[2] = (to[2] - from[2]) / fn;
    return n;
}

// Hook 0x00592BC8 = 0x00456B70: landing dust / water effect under the enemy (once: +0x136C bit 0x10000)
void World::Impl::EnemyLandingDust(EnemyTask& e) {
    if (e.flags136C & 0x10000) return;
    int mat = 0;
    float p[3] = { e.pos[0], 0.0f, e.pos[2] };
    p[1] = FloorHeight(e.pos[0], e.pos[1] + F32(exe, 0x004C4C8C), e.pos[2], &mat);
    if (cac40 == 5 || cac40 == 0x37) {
        SpawnSplashRing(p, 1.0f);  // 0x004567C0 (world_enemy_beh.cpp)
        SpawnSplashRing(p, 0.5f);
        SpawnEffectAt(p, 0, 0, 0x61, 1, -1);
    } else if (rainFlag == 1) {
        SpawnEffectAt(p, 0, 0, 0x61, 1, -1);
    } else {
        SpawnEffectAt(p, 0, 0, 0x46, 2, -1);
    }
    e.flags136C |= 0x10000;
}

// Behaviour 12 (0x00456DF0): fall (gravity -0.0368) until the floor, bounce, corpse
void World::Impl::EnemyBeh12(EnemyTask& e) {
    if (e.sub == 0) {
        e.flags136C &= ~0x20000000u;
        e.flags34 &= ~0x4000u;
        const uint32_t u = 0xBD16872Bu;
        memcpy(&e.acc[1], &u, 4);
        e.sub = 1;
    } else if (e.sub != 1 && e.sub != 2) {
        return;
    }
    if (e.sub == 1) {
        if (e.anim.tick[0] < 0x3C) return;
        ++e.sub;
        e.flags136C = (e.flags136C & ~0x10000u) | 0x20000000;
    }
    if (MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0] && e.anim.motion[0] == 0x3F9) e.flags34 |= 0x4000;
    e.vel[1] = e.acc[1] + e.vel[1];
    int mat = 0;
    const float f = FloorHeight(e.pos[0], e.pos[1], e.pos[2], &mat);  // 0x00409D40
    if (e.pos[1] + e.vel[1] <= f) {
        e.pos[1] = f;
        e.vel[1] = e.vel[1] * F32(exe, 0x004C4D0C);
        if (!(e.flags136C & 0x10000)) {
            EnemyLandingDust(e);
            const int r = Rand(0x00456EE9);
            AnimBlendTo(e, 0x3F8, r % 10, 10);
            e.flags34 &= ~0x4000u;
        }
        float v = e.vel[1];
        if (v < 0.0f) v = v * F32(exe, 0x004C4C64);
        if (v <= F32(exe, 0x004C4D08)) {
            for (int k = 0; k < 3; ++k) e.acc[k] = e.vel[k] = 0;  // 0x0044E120
            EnemyEnterCorpse(e);  // 0x00456740
        }
    }
}

// Behaviour 26 (0x004581A0): wait P[1] frames, jump to (P[2], P[3], P[4]) with gravity P[5], land, then behaviour 1
void World::Impl::EnemyBeh26(EnemyTask& e) {
    switch (e.sub) {
    case 0: {
        e.flags34 |= 0x2000;
        e.counter1334 = e.P(1);
        ++e.sub;
        [[fallthrough]];
    }
    case 1: {
        if (0 <= --e.counter1334) return;
        float to[3], g;
        const int32_t a = e.P(2), b = e.P(3), c = e.P(4), d = e.P(5);
        memcpy(&to[0], &a, 4);
        memcpy(&to[1], &b, 4);
        memcpy(&to[2], &c, 4);
        memcpy(&g, &d, 4);
        const float from[3] = { e.pos[0], e.pos[1], e.pos[2] };
        e.counter1330 = JumpSetup(e, from, to, g);
        if (e.flags34 & 0x1000000) AnimBlendTo(e, 0x399, 0, 5);
        else AnimBlendTo(e, 0x3BB, 0, 1);
        ++e.sub;
        e.flags136C = (e.flags136C & 0xDFFEFFFFu) | 0x4000;
        e.flags34 &= ~0x1000000u;
        [[fallthrough]];
    }
    case 2:
        if (e.anim.motion[0] == 0x399) {
            if (e.anim.tick[0] == 0x23) {
                AnimBlendTo(e, 0x3BB, 0, 0xC);
                ++e.sub;
                return;
            }
            if (0x1A < e.anim.tick[0]) {
                e.vel[0] = e.acc[0] + e.vel[0];
                e.vel[1] = e.acc[1] + e.vel[1];
                e.vel[2] = e.acc[2] + e.vel[2];
            }
            return;
        }
        ++e.sub;
        [[fallthrough]];
    case 3:
        e.vel[0] = e.acc[0] + e.vel[0];
        e.vel[1] = e.acc[1] + e.vel[1];
        e.vel[2] = e.acc[2] + e.vel[2];
        if (e.anim.tick[0] < 1 || e.counter1330 < 0x15) {
            e.flags34 &= ~0x4000u;
        } else {
            e.flags34 |= 0x4000;
            if (e.hp < 1 && e.anim.motion[0] != 0x3F7) AnimBlendTo(e, 0x3F7, 0, 5);
        }
        if (0 <= --e.counter1330) return;
        e.flags136C |= 0x20000000;
        for (int k = 0; k < 3; ++k) e.acc[k] = e.vel[k] = 0;  // 0x0044E120
        {
            const int r = Rand(0x004583D0);
            e.facing1350 = (MotionTicks(e.anim.motion[0]) - r % 0x1E) - 1;  // +0x1350
        }
        if (!(e.flags136C & 0x10000000)) e.flags34 &= ~0x20000u;
        if (e.variant == 5) {
            Sound(0x1C16A9);
            shakeTimer = 0x20;  // 0x009C8E8C
        } else {
            EnemyLandingDust(e);  // hook 0x00592BC8 = 0x00456B70
            Sound(0x2A16A9);
        }
        EnemyVoice(e, 3);
        ++e.sub;
        [[fallthrough]];
    case 4:
        if (e.hp < 1) {
            e.behaviour = 6;
            e.sub = 0;
            e.flags136C = (e.flags136C & ~0x4000u) | 0x80000000u;
        } else if (e.facing1350 <= e.anim.tick[0]) {
            e.flags136C &= ~0x4000u;
            e.flags34 &= ~0x2000u;
            e.behaviour = 1;
            e.sub = 0;
        }
        return;
    default:
        return;
    }
}

// Breakable prop item slots 1 / 3 (0x00465FB0 -> 0x00471BD0 / 0x004722A0). Returns false for other slots.
bool World::Impl::DropItemSt1b(const BreakableProp& b, int slot) {
    if (slot == 1) {  // 0x00471BD0
        LifeItemTask* k = NewTask<LifeItemTask>(TaskLifeItem, 0x00471CC0);
        k->flags34 = 0x80000001u;
        k->pos[0] = b.pos[0];
        k->pos[1] = b.pos[1] + F32(exe, 0x004C4380);
        k->pos[2] = b.pos[2];
        k->rotY = YawToward(*this, b.pos[0], b.pos[2]) + 0x8000;
        if (cfg.eventMode == 5) k->rotY = 0x8000;
        k->areaCount = static_cast<int8_t>(b.areaChanges);
        k->area = static_cast<int8_t>(b.areaSeen);
        k->life = b.lifetime;
        k->counter = 0;
        k->radius = 4.0f;
        return true;
    }
    if (slot == 3) {  // 0x004722A0
        CreatureItemTask* k = NewTask<CreatureItemTask>(TaskCreatureItem, 0x00471FA0);
        k->flags34 = 1;
        memcpy(k->pos, b.pos, 12);
        k->rot[1] = YawToward(*this, b.pos[0], b.pos[2]);
        if (runner->cfg.gameType != 2 && cfg.eventMode == 5) k->rot[1] = 0;
        k->actorSlot = -1;
        k->target = -1;
        k->anim.kind = 0x1C;
        k->anim.motion[0] = 0x140;
        EnemyInitPose(*k);  // 0x0041DB70 / Anim_InitEnemy 0x00410440 / 0x0041DB90
        k->anim.rotOrder = 5;
        k->anim.counter[0] = 0;
        k->nodeCb = 0x00463F90;
        k->behaviour = 0;
        k->radius = 4.0f;
        k->flags34 = 1;
        k->hp = b.lifetime;
        k->area = runner->variant;
        k->areaCount = 0;
        k->fade = 0;
        return true;
    }
    return false;
}

// Generic komono kind 68 (0x00463E50, training lesson 3): the creature of item slot 3 (task 0x00471FA0) at one of three
// places per difficulty (0x0059579C[(rand % 3 + difficulty 0x009C9118 * 3) * 4] {x, z} * 0x0055D230, the record's y),
// facing 0x9200, hp = the record's state
void SpawnCreatureKomono68(World::Impl& w, const SpawnRecord& r) {
    const int pick = w.Rand(0x00463E5A) % 3 + w.runner->cfg.playerVariant * 3;
    CreatureItemTask* k = w.NewTask<CreatureItemTask>(TaskCreatureItem, 0x00471FA0);
    k->flags34 = 1;
    const float s = F32(w.exe, 0x0055D230);
    k->pos[0] = static_cast<float>(w.exe.S16(0x0059579C + static_cast<uint32_t>(pick) * 4)) * s;
    k->pos[1] = r.pos[1];
    k->pos[2] = static_cast<float>(w.exe.S16(0x0059579E + static_cast<uint32_t>(pick) * 4)) * s;
    k->rot[1] = 0x9200;
    k->actorSlot = -1;
    k->target = -1;
    k->anim.kind = 0x1C;
    k->anim.motion[0] = 0x140;
    w.EnemyInitPose(*k);  // 0x0041DB70 / Anim_InitEnemy 0x00410440 / 0x0041DB90
    k->anim.rotOrder = 5;
    k->anim.counter[0] = 0;
    k->nodeCb = 0x00463F90;
    k->behaviour = 0;
    k->radius = 4.0f;
    k->flags34 = 1;
    k->hp = static_cast<int16_t>(r.state);
    k->area = w.runner->variant;
    k->areaCount = 0;
    k->fade = 0;
}

}  // namespace game
