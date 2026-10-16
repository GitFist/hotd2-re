// game::World - the objects stage 1 scene 1 spawns (game mode 6): komono kinds 7, 9, 38, 39, 44 (kind 40 variant 0
// is in world_actors.cpp), actor types 0x24 (0x00482CE0) and 0x33 state 4 (0x00433B70).
// CONFIRMED = code read + frame-exact against build\scene_capture_play2scene1.txt; CODE = read, not exercised.

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

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// Komono kind 38 (ctor 0x00463420: 9 tasks 0x0046BCC0 from table 0x00593E70): bottles on a shelf that fall and
// tumble when shot, then land on a corner and settle
struct Komono38Task : ActorTask {
    int16_t life = 0;        // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int16_t index = 0;       // +0x290
    float pos[3] = {};       // +0x19C
    int rot[3] = {};         // +0x1CC / +0x1D0 / +0x1D4
    int16_t model = 0x1237;  // +0x28C
    float baseY = 0;         // +0x1B8
    int8_t state = 0;        // +0x192 (0 standing, 1 falling, 2 landed on a corner)
    float vy = 0;            // +0x1C4
    int spin[2] = {};        // +0x1DC (y) / +0x1E0 (z)
    float land[3] = {};      // +0x1A8..+0x1B0 landing corner position
    int8_t corner = 0;       // +0x198
    int stops = 0;           // +0x2A0
    Mat4 stored;             // +0x2E4
};
// Komono kind 39 (ctor 0x00463510: 8 tasks 0x0046C240 from table 0x00593F48): stacks of 8 plates that slide off
// one by one when shot, then blink out
struct Komono39Task : ActorTask {
    int16_t life = 0;
    int8_t area = 0, areaCount = 0;
    int16_t index = 0;        // +0x290
    float pos[3] = {};        // +0x19C
    int rot[3] = {};          // +0x1CC..
    int16_t model = 0x1237;   // +0x28C
    float piece[8][3] = {};   // +0x22C
    int16_t ang[8][3] = {};   // +0x1FC (x from rand at creation, y animated)
    int8_t state = 0;         // +0x192
    int counter = 0;          // +0x2A0 frames since shot
    int blink = 0;            // +0x2A4
};
// Komono kind 44 (ctor 0x004639F0: 7 tasks 0x0046D850 from table 0x005946F8): pots (prop skeleton 0x13, motion
// 0x1D4) that break when shot; index >= 2 draw the static model 0x1064
struct Komono44Task : ActorTask {
    int16_t life = 0;
    int8_t area = 0, areaCount = 0;
    int16_t index = 0;   // +0x290
    float pos[3] = {};   // +0x19C
    int rot[3] = {};     // +0x1CC..
    PropAnim anim;       // +0x324 (skeleton 0x13, motion 0x1D4, frame +0x32C)
};
// Komono kind 7 (generic ctor 0x00461CF0 -> task 0x00466930): a hanging lamp (model 0x1736) that swings when shot
struct Komono7Task : ActorTask {
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};   // +0x19C
    int rot[3] = {};     // +0x1CC / +0x1D0 / +0x1D4
    int vel[2] = {};     // +0x1D8 (x) / +0x1E0 (z)
    int item = 0;        // +0x2A0
    int16_t life = 0;    // +0x11C
};
// Komono kind 9 (generic ctor 0x00461CF0 -> task 0x00472830): the door frame (0x123B / 0x123C) and the two door
// leaves (prop skeletons 8 / 9, motions 0x1D0 / 0x1D1) opened by event flag 0x1F
struct Komono9Task : WTask {
    int8_t area = 0, areaCount = 0;
    PropAnim anim;  // +0x324 (+0x32C frame)
};
// Actor type 0x33 state 4 (0x00433B70): a static model target (params: model, mesh or -1, radius, flag bytes)
struct Type33Task : EnemyTask {
    int model = 0;       // +0x13F0
    bool init = false;   // +0x1312
};
// Actor type 0x24 (0x00482CE0 -> 0x00482EE0): scripted background character (enemy skeleton P byte 4, motion P s16
// 10), removed at camera P s16 6 / tick P s16 8 (or by event flag P s16 6 when +0x34 & 0x2000000)
struct Actor24Task : EnemyTask {
    int c1324 = 0;  // +0x1324 (animation frozen)
    int c1320 = 0;  // +0x1320 frames waited (0x00482F60)
    int c1330 = 0;  // +0x1330 (variant 4: frames to walk / wait)
};

// Actor type 0x52 (op 0x0C, 0x0043F4C0 -> 0x0043F5C0): rats (models 0x1385..0x138E) running about for 600 frames;
// block of 0x28 bytes at +0x1310 (vx, -, vz, speed, -, -, s16 state, age, pause, frame, first, last, variant)
struct RatTask : ActorTask {
    float pos[3] = {};      // +0x40
    int rot[3] = {};        // +0x64..
    float vx = 0, vz = 0;   // block +0x00 / +0x08
    float speed = 0.4f;     // block +0x0C
    int16_t state = 0;      // block +0x18 (0 running, 1 pausing)
    int16_t age = 0;        // block +0x1C
    int16_t pause = 0;      // block +0x1E
    int16_t frame = 0;      // block +0x20 model
    int16_t variant = 0;    // block +0x26 (P s16 0)
    float y10 = 0;          // block +0x10 (variants 2..4: the height when shot)
    int actorSlot = -1;
    LockOnTarget* lock = nullptr;
};

// Type 0x44 state 0 (ctor 0x00472B30 -> task 0x00473B90): a door (prop skeleton 2 for model P s16 4 == 0x13F5, else
// 3; motion 0x1D7) opened by event flag 0x12, removed by flag 0x13; a collision-mesh target (mesh P s32 8, matrix of
// node +0x2A0)
struct DoorTask : EnemyTask {
    PropAnim panim;     // +0x324 (+0x32C frame)
    int store = 1;      // +0x2A0
    int snd[2] = {};    // +0x2A4 / +0x2A8 sound table positions
};

// Item 0x004724A0 (created by 0x004723F0 when a breakable prop's item counter reaches 0): rows 0x00595058 + slot * 12
// {s16 model, s16 points, float scale, float height}; shot: points, then the score digits for 49 frames
struct ItemTask : ActorTask {
    int8_t slot = 0;           // +0x194
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int16_t life = 0;          // +0x11C
    float pos[3] = {};         // +0x19C
    int rotY = 0;              // +0x1D0
    int16_t model = 0;         // +0x28C
    int counter = 0;           // +0x2A0 frames since shot
    int digits = 0;            // +0x2A4 score digit model base
};

}  // namespace

static int PropShooter(World::Impl& w, uint32_t flags, bool score) {  // Prop_ScoreShot 0x004650F0
    int p;
    if ((flags & 2) && (flags & 4)) p = w.Rand(0x00465106) & 1;
    else p = (flags & 2) ? 0 : 1;
    if (score) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

// Area lifetime check shared by the komono tasks: true when the task was removed (Actor_Destroy / Task_Kill)
template <class T> static void AreaLife(World::Impl& w, T& k, int life) {
    if (w.runner->variant != k.area) {
        if (life < ++k.areaCount) w.ts.KillCurrent();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
}

namespace {

void TaskKomono38(Task& t) {
    World::Impl& w = *CurrentWorld();
    const ExeImage& exe = w.exe;
    Komono38Task& k = t.As<Komono38Task>();
    AreaLife(w, k, k.life);  // 0x00409CC0
    if ((k.flags34 & 8) && k.state == 0) {
        PropShooter(w, k.flags34, false);
        w.Sound(0x1D16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, Bits(0x3F333333), k.pos[2]);  // 0x004666B0(obj, p, 0.7)
        k.model = 0x1236;
        k.state = 1;
        k.vy = 0.75f;
        for (int i = 0; i < 2; ++i) {
            const int s = w.Rand(i == 0 ? 0x0046BD78 : 0x0046BDAE) & 1;
            const int r = w.Rand(i == 0 ? 0x0046BD92 : 0x0046BDC8);
            k.spin[i] = (1 - 2 * s) * (r % 0x81 + 0x80);
        }
    }
    k.flags34 &= ~0xEu;
    if (k.state == 1) {
        k.vy = k.vy - F32(exe, 0x0055D2CC);
        k.rot[1] += k.spin[0];
        k.rot[2] += k.spin[1];
        k.pos[1] = k.vy + k.pos[1];
        if (k.vy < 0.0f) {
            Mat4 m = Mat4::Identity();
            mx::RotZ(m, k.rot[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotX(m, k.rot[0]);
            const float sc = F32(exe, 0x0055D2B0);
            for (int c = 0; c < 8; ++c) {
                const uint32_t a = 0x00594008 + static_cast<uint32_t>(c) * 6;
                const float in[3] = { static_cast<float>(exe.S16(a)) * sc, static_cast<float>(exe.S16(a + 2)) * sc,
                                      static_cast<float>(exe.S16(a + 4)) * sc };
                float o[3];
                mx::Transform(m, in, o);
                if (o[1] + k.pos[1] < k.baseY) {
                    k.state = 2;
                    k.land[1] = k.baseY;
                    k.land[0] = o[0] + k.pos[0];
                    k.land[2] = o[2] + k.pos[2];
                    k.spin[0] = static_cast<int>(static_cast<float>(k.spin[0]) * -2.0f);  // 0x004C4C90
                    k.spin[1] = static_cast<int>(static_cast<float>(k.spin[1]) * -2.0f);
                    k.stops = 0;
                    k.corner = static_cast<int8_t>(c);
                }
            }
        }
    } else if (k.state == 2) {
        const float damp = F32(exe, 0x0055D2C0);
        const int sy = static_cast<int>(static_cast<float>(k.spin[0]) * damp);
        k.spin[0] = sy;
        const int sz = static_cast<int>(static_cast<float>(k.spin[1]) * damp);
        const int ry = k.rot[1] + sy, rz = k.rot[2] + sz;
        k.spin[1] = sz;
        k.rot[1] = ry;
        k.rot[2] = rz;
        if ((sy > 0 && ry > 0) || (sy < 0 && ry < 0)) {
            k.spin[0] = 0;
            k.rot[1] = 0;
            ++k.stops;
        }
        if ((sz > 0 && rz > 0x4000) || (sz < 0 && rz < 0x4000)) {
            k.spin[1] = 0;
            k.rot[2] = 0x4000;
            ++k.stops;
        }
        if (k.stops == 2) {
            k.state = 0;
            k.pos[1] = k.baseY;
        }
    }
    if (k.state == 0 || k.state == 1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        w.Draw(k.model, m);
    } else {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.land[0], k.land[1] - 0.5f, k.land[2]);  // 0x004C43AC
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        const uint32_t a = 0x00594008 + static_cast<uint32_t>(k.corner) * 6;
        const float sc = F32(exe, 0x0055D2B0);
        mx::Translate(m, static_cast<float>(-exe.S16(a)) * sc, static_cast<float>(-exe.S16(a + 2)) * sc,
                      static_cast<float>(-exe.S16(a + 4)) * sc);
        k.stored = m;  // 0x004A8CA0 -> +0x2E4
        mx::GetTranslation(m, k.pos);
        w.Draw(k.model, k.stored);  // 0x004A92A0(+0x2E4): stored * view
    }
    const float p[3] = { k.pos[0], k.pos[1] - 1.0f, k.pos[2] };  // 0x004C4380
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

void TaskKomono39(Task& t) {
    World::Impl& w = *CurrentWorld();
    const ExeImage& exe = w.exe;
    Komono39Task& k = t.As<Komono39Task>();
    const float count = 8.0f - static_cast<float>(k.index) * F32(exe, 0x0055D1A0);
    AreaLife(w, k, k.life);  // 0x00409CC0
    if (k.blink != 0) {
        const int b = k.blink++;
        if (b > 0x3C) w.ts.KillCurrent();  // Task_Kill 0x004A7040
    }
    if ((k.flags34 & 8) && k.state == 0) {
        PropShooter(w, k.flags34, true);
        w.Sound(0x1D16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.0f, k.pos[2]);  // 0x004666B0(obj, p, 1.0)
        k.state = 1;
        k.counter = 0;
    }
    k.flags34 &= ~8u;
    if (k.state == 1) {
        ++k.counter;
        const int n = static_cast<int>(count);
        const float g = w.ground;
        for (int i = 0; i < n; ++i) {
            bool slide = true;
            if (i == 0) {
                const float lim = g + F32(exe, 0x005690F8);
                if (!(lim < k.piece[0][1])) {  // fcom: piece y <= ground + 0.463
                    k.piece[0][1] = lim;
                    k.ang[0][1] = 0;
                    slide = false;
                }
            } else if (!(g + F32(exe, 0x005690F4) < k.piece[i][1])) {
                k.piece[i][2] = k.piece[0][2] - static_cast<float>(i) * F32(exe, 0x00564708);
                k.piece[i][1] = g + F32(exe, 0x005690F4);
                k.ang[i][1] = static_cast<int16_t>(0x17C0 - i);
                slide = false;
            }
            if (slide) {
                k.piece[i][2] = k.piece[i][2] - F32(exe, 0x004C4D10);
                if (i * 2 < k.counter) {
                    k.ang[i][1] = static_cast<int16_t>(k.ang[i][1] + 0x300);
                    k.piece[i][1] = k.piece[i][1] - F32(exe, 0x005690F0);
                }
            } else if (i == n - 1 && k.blink == 0) {
                k.blink = 1;
            }
        }
    }
    if ((k.blink % 2) != 1) {
        const int n = static_cast<int>(count);
        for (int i = 0; i < n; ++i) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, k.piece[i][0], k.piece[i][1], k.piece[i][2]);
            mx::RotZ(m, k.rot[2]);
            mx::RotY(m, k.ang[i][1] + k.rot[1]);
            mx::RotX(m, k.ang[i][0] + k.rot[0]);
            w.Draw(k.model, m);
        }
    }
    if (k.state == 0) {
        const float p[3] = { k.pos[0], k.pos[1] + 3.0f, k.pos[2] };  // 0x004C49C0
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);
    }
}

void TaskKomono44(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono44Task& k = t.As<Komono44Task>();
    AreaLife(w, k, k.life);  // 0x00409CC0
    if (k.anim.frame == 0 && (k.flags34 & 8)) {
        PropShooter(w, k.flags34, true);
        w.Sound(0x1A16A9);
        const uint32_t f = k.flags34;
        k.anim.frame = 1;
        k.flags34 = f & ~8u;
        w.SpawnSpark(k.viewPos[2], (f & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
    }
    if (k.anim.frame > 0) {
        const int lim = w.MotionTicks(k.anim.motion) - 2;
        if (lim < ++k.anim.frame) k.anim.frame = lim;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    if (k.index < 2) {
        if (w.MotionResident(k.anim.motion)) {  // 0x009A4684 == 2
            const Motion* mo = w.PropMotion(k.anim.motion);
            if (mo)
                for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m)) {
                    if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                    w.Draw(d.model, d.world);
                }
        }
    } else {
        w.Draw(0x1064, m);
    }
    Mat4 s = Mat4::Identity();
    mx::Translate(s, k.pos[0], w.ground + 0.2f, k.pos[2]);  // 0x004D1D24
    mx::Scale(s, 8.0f, 1.0f, 8.0f);
    w.Draw(0x10D1, s);
    if (k.anim.frame == 0) {
        const float p[3] = { k.pos[0], k.pos[1] + 5.0f, k.pos[2] };  // 0x0055D2B4
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);
    }
}

void TaskKomono7(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono7Task& k = t.As<Komono7Task>();
    AreaLife(w, k, 4);  // 0x00409CC0
    if (k.flags34 & 8) {
        PropShooter(w, k.flags34, false);
        k.flags34 &= ~8u;
        w.Sound(0xE16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
        k.vel[0] = w.Rand(0x004669C3) % 0x201;
        k.vel[1] = w.Rand(0x004669D6) % 0x401 - 0x200;
        if (k.item == 0 && w.runner->cfg.gameType == 1 && w.modes) {  // game type 1: an item falls (0x00466B40)
            k.item = 1;
            SpawnDropItem(w, 38.0f, 0, k.pos, k.area, k.areaCount, k.life);
        }
    }
    k.flags34 &= ~6u;
    const int vx = k.vel[0] - (k.rot[0] + k.vel[0]) / 0x30;
    k.vel[0] = vx;
    const int vz = k.vel[1] - (k.vel[1] + k.rot[2]) / 0x30;
    k.vel[1] = vz;
    k.rot[0] += vx / 4;
    k.rot[2] += vz / 4;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotX(m, k.rot[0]);
    w.Draw(0x1736, m);
    const float p[3] = { k.pos[0], k.pos[1] - 57.0f, k.pos[2] };  // (float) double 0x00569078
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

void TaskKomono9(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono9Task& k = t.As<Komono9Task>();
    if (w.runner->variant != k.area) {
        if (5 < ++k.areaCount) w.ts.KillCurrent();  // Task_Kill 0x004A7040
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.runner->flags[0x1F] == 1 && k.anim.frame < 0x4E) ++k.anim.frame;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, 0.0f, 0.0f, -5.0f);
    if (k.anim.frame == 0) {
        w.Draw(0x123B, m);
        return;
    }
    if (!w.MotionResident(0x1D0) || !w.MotionResident(0x1D1)) return;  // 0x009A4664 / 0x009A466C == 2
    w.Draw(0x123C, m);
    mx::Translate(m, Bits(0xBF462B6B), Bits(0xBD7212D7), Bits(0xC32B147B));
    for (int i = 0; i < 2; ++i) {
        k.anim.skeleton = 8 + i;
        k.anim.motion = 0x1D0 + i;
        const Motion* mo = w.PropMotion(k.anim.motion);
        if (!mo) continue;
        for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m)) {
            if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
            w.Draw(d.model, d.world);
        }
    }
}

// 0x00433B70
void TaskType33State4(Task& t) {
    World::Impl& w = *CurrentWorld();
    Type33Task& a = t.As<Type33Task>();
    const SpawnRecord& r = a.record;
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;
        w.ts.KillCurrent();
    };
    if (w.runner->flags[r.params[0xD]] == 1) destroy();
    if (!a.init) {
        a.flags34 |= 1;
        a.model = r.ParamS32(0);
        if (r.ParamS32(4) == -1) {
            const int32_t rad = r.ParamS32(8);
            memcpy(&a.radius, &rad, 4);
            memcpy(&a.radius128, &rad, 4);
        } else {
            a.flags34 |= 0x50;  // collision-mesh object (+0x14C mesh)
            w.Note("actor 0x33 state 4 collision-mesh object (0x00405550 / 0x00404A00) not ported");
        }
        a.init = true;
    }
    if (w.runner->flags[r.params[0xC]] == 1) a.flags34 &= ~0x8000u;
    if (!(a.flags34 & 0x8000)) {  // 0x00433CE0: pushed like the enemies (0x00405B10 / 0x004057F0)
        auto centre = [&] {  // 0x00433E00
            a.sphere[0] = a.pos[0];
            a.sphere[2] = a.pos[2];
            a.sphere[1] = a.radius128 + a.pos[1];
        };
        centre();
        if (a.pushed) {
            float f = a.pushAmount * 0.1f;  // 0x004C4CC8
            if (a.pusherFlags & 0x18000000) f = f * 1.8f;  // 0x0055DD48
            for (int c = 0; c < 3; ++c) a.pos[c] = f * a.pushDir[c] + a.pos[c];
            centre();
            a.pushed = false;
            if (w.SpherePush(a)) {
                const float g = w.cac4c * 0.1f;
                a.pos[0] = w.cac64[0] * g + a.pos[0];
                a.pos[2] = w.cac64[2] * g + a.pos[2];
                centre();
            }
        }
        if (w.MeshPush(a.sphere, a.radius128)) {
            const float g = w.cac4c;
            a.pos[0] = w.cac64[0] * g + a.pos[0];
            a.pos[1] = w.cac64[1] * g + a.pos[1];
            a.pos[2] = w.cac64[2] * g + a.pos[2];
            centre();
        }
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotZ(m, a.rot[2]);
    mx::RotY(m, a.rot[1]);
    mx::RotX(m, a.rot[0]);
    w.Draw(a.model, m);
    mx::Transform(w.cam.view, a.pos, a.viewPos);  // context 0x009C6F00's view
    w.RegisterTarget(&a);
}

// The kind-0x55 extras of 0x004834F0 (story sequences): 0x00483930 -> task 0x00483A40 (debris model 0xC53.. animated
// to 0xC68 at a random node), 0x00483AE0 -> task 0x00483B40 (model 0x7F4 shrinking at the body point, then the
// character hidden and models 0x7EF..0x815)
struct Debris24Task : EnemyTask {
    int model = 0;     // +0x13F0
    float scale = 1;   // +0x118
};
struct Vanish24Task : EnemyTask {
    Task* owner = nullptr;  // +0x1390
    uint32_t ownerSerial = 0;
    int model = 0;     // +0x13F0
    float scale = 1;   // +0x118
    int counter = 0;   // +0x1330
};

void TaskDebris24(Task& t) {  // 0x00483A40
    World::Impl& w = *CurrentWorld();
    Debris24Task& k = t.As<Debris24Task>();
    if (0xC68 < ++k.model) {
        w.ts.KillCurrent();  // Task_KillSelf 0x004A7040: nothing drawn this frame
        return;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::Scale(m, k.scale, k.scale, k.scale);
    w.LightChar();  // 0x0041DC70
    w.Draw(k.model, m);
    w.LightScene();  // 0x0041DCC0
}

void TaskVanish24(Task& t) {  // 0x00483B40
    World::Impl& w = *CurrentWorld();
    Vanish24Task& k = t.As<Vanish24Task>();
    auto draw = [&] {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::Scale(m, k.scale, k.scale, k.scale);
        w.LightChar();  // 0x0041DC70
        w.Draw(k.model, m);
        w.LightScene();  // 0x0041DCC0
    };
    if (k.sub == 0) {
        k.scale = k.scale - F32(w.exe, 0x005644F8);
        if (k.scale > 0.0f) return draw();
        ++k.sub;
        k.counter = 0xF;
        k.scale = 0;
    }
    if (k.sub == 1) {
        if (0 < --k.counter) return draw();
        k.model = 0x7EF;
        k.scale = 1.0f;
        if (k.owner && k.owner->serial == k.ownerSerial && k.owner->data) {
            EnemyTask& o = k.owner->As<EnemyTask>();
            o.anim.drawFlags &= ~1u;  // the character is no longer drawn
            o.anim.attachHidden = true;  // byte 1 of the first +0x40 attachment entry (moot: drawFlags 1 is off)
        }
        ++k.sub;
        k.counter = 0;
    }
    if (k.sub == 2 && 2 < ++k.counter) {
        k.counter = 0;
        if (0x815 < ++k.model) w.ts.KillCurrent();
    }
    draw();
}

// 0x00483DD0 (kind 0x53 with motion 0xD9): 30 birds circling (0x00569300 table: centre x, y, radius-ish z), task
// 0x00483F00 -> 0x00483F90; models 0x21F..0x232 cycled (wing beats), removed at camera 0x1A
struct Bird24Task : EnemyTask {
    int model = 0;             // +0x13F0
    float centre[3] = {};      // +0x13C0
    float prev[3] = {};        // +0x13CC
    int angle = 0;             // +0x94
    int step = 0;              // +0x133C
    float rx = 7.0f, rz = 6.0f;  // +0x1340 / +0x1344
    float vy = 0;              // +0x5C
    int c1350 = 0;             // +0x1350 index
};

void TaskBird24(Task& t);

// 0x00483DD0: the 30 birds (table 0x00569300, 3 floats each: x, y, z), 3 rand() each
void SpawnBirds24(World::Impl& w) {
    int i = 0;
    for (uint32_t e = 0x00569304; e < 0x0056946C; e += 12, ++i) {
        Bird24Task* k = w.NewTask<Bird24Task>(TaskBird24, 0x00483F00);
        k->c1350 = i;  // +0x1350
        k->flags34 = 1;
        k->actorSlot = 0;  // +0x3C as Task_Create / 0x004A73D0 leave it (0)
        const float bx = F32(w.exe, e - 4), by = F32(w.exe, e), bz = F32(w.exe, e + 4);
        double d78 = 0;
        w.exe.Read(0x00569478, &d78, 8);
        const float y = static_cast<float>(static_cast<double>(by) + d78);  // +0x1388
        const float z = -bz;                                               // +0x138C
        k->rot[0] = 0;
        k->rot[1] = 0x4000;
        k->rot[2] = 0;
        k->pos[0] = bx + F32(w.exe, 0x00569470);
        k->pos[1] = y + F32(w.exe, 0x0056946C);
        k->pos[2] = z + F32(w.exe, 0x004C43AC);
        k->centre[0] = k->pos[0];
        k->centre[1] = k->pos[1];
        k->centre[2] = k->pos[2];
        k->model = (w.Rand(0x00483E8A) & 7) * 2 + 0x21F;
        k->rx = 7.0f;
        k->angle = (w.Rand(0x00483E9F) & 7) << 12;
        k->rz = 6.0f;
        k->sub = 0;
        k->step = ((w.Rand(0x00483EC4) & 3) + 0x1C) * 0x20;
    }
}
void TaskBird24(Task& t) {  // 0x00483F00
    World::Impl& w = *CurrentWorld();
    Bird24Task& k = t.As<Bird24Task>();
    if (w.runner->camera.camId == 0x1A && -1 < w.runner->camera.tick) {
        // 0x004092D0 with +0x3C as created (0: Task_Create zeroes the block): actor slot 0 is freed
        if (k.actorSlot != -1) {
            if (k.actorSlot >= 0 && k.actorSlot < 14) w.actorSlots[k.actorSlot] = nullptr;
            k.actorSlot = -1;
        }
        w.ts.KillCurrent();
    }
    // 0x00483F90
    const int old = k.model;
    k.model = old + 1;
    if (0x231 < old) k.model = 0x21F;
    if (k.sub == 0) {
        k.rot[1] = 0;
        memcpy(k.prev, k.pos, 12);
        k.vy = Bits(0x3E99999A);
        k.sub = 1;
    }
    if (k.sub == 1) {
        k.angle += k.step;
        // x87 at 24-bit precision: the angle product and every mul / add round to float, fsin / fcos do not
        const double a = static_cast<float>(static_cast<double>(k.angle) * 9.587379924285257e-05);
        const float x = static_cast<float>(std::sin(a) * k.rx) + k.centre[0];
        k.pos[0] = x;
        k.pos[2] = static_cast<float>(std::cos(a) * k.rz) + k.centre[2];
        int pitch = 0;
        mx::DirToAngles(k.prev[0] - x, k.prev[1] - k.pos[1], k.prev[2] - k.pos[2], &pitch, &k.rot[1]);
        memcpy(k.prev, k.pos, 12);
    }
    if (k.model == 0x21F || k.model == 0x229) k.vy = k.vy * -1.0f;  // 0x004C4C64
    k.pos[1] = k.vy + k.pos[1];
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::Scale(m, Bits(0x3F333333), Bits(0x3F333333), Bits(0x3F333333));
    w.Draw(k.model, m);
}

// 0x00483C90 (game mode 11, kind 0x41 with counter 0 and motion 0xEA): task 0x00483CE0, three billboards of model 0xB33
// (scale 5) closing in on one point over 45 frames
struct Flash24Task : EnemyTask {
    int counter = 0;  // +0x1330
};

void TaskFlash24(Task& t) {  // 0x00483CE0
    World::Impl& w = *CurrentWorld();
    Flash24Task& k = t.As<Flash24Task>();
    if (k.sub == 0) {
        k.counter = 0;
        k.sub = 1;
    }
    if (k.sub == 1 && 0x2D < ++k.counter) {
        w.ts.KillCurrent();  // Task_KillSelf 0x004A7040: nothing drawn this frame
        return;
    }
    w.LightChar();  // 0x0041DC70 .. 0x0041DCC0 around the three billboards
    struct Restore {
        World::Impl& w;
        ~Restore() { w.LightScene(); }
    } restore{ w };
    for (int i = 0xF; i < 0x3C; i += 0xF) {
        int d = i - k.counter;
        if (d < 0) d = 0;
        Mat4 m = w.cam.view;  // the view on the stack: 0x004A9F70 makes it a billboard
        mx::Translate(m, static_cast<float>(d) + static_cast<float>(d) + k.pos[0], k.pos[1], k.pos[2]);
        mx::ClearRotation(m);
        mx::Scale(m, 5.0f, 5.0f, 5.0f);
        w.Draw(0xB33, m, 8, -1.0f, false, -1, true);
    }
}

// 0x004834F0: the skeleton, the extras by kind, the animation counter unless frozen
void Actor24Draw(World::Impl& w, Actor24Task& a) {
    if (!(w.flags5900 & 0x20)) {
        w.AnimDraw(a);  // 0x00411090 (light 0x0041DC70 / 0x0041DCC0: renderer)
        w.EnemyShadow(a);
    }
    if (a.anim.kind == 0x55) {
        if (a.anim.motion[0] == 0x93 && (w.front.modeFrames & 7) == 0 && w.runner->camera.tick < 0x13B) {  // 0x00483930
            Debris24Task* k = w.NewTask<Debris24Task>(TaskDebris24, 0x00483A40);
            k->model = 0xC53;
            k->scale = 1.0f;
            const int n = w.Rand(0x00483965) % 0xF + 1;
            mx::Transform(w.cam.inverse, a.anim.nodes[static_cast<size_t>(n)].hit, k->pos);
            k->rot[0] = w.Rand(0x004839F8) % 0xFFFF;
            k->rot[1] = w.Rand(0x00483A08) % 0xFFFF;
            k->rot[2] = w.Rand(0x00483A18) % 0xFFFF;
            k->scale = static_cast<float>((w.Rand(0x00483A28) & 3) + 1);
        }
        if (a.anim.motion[0] == 0x91 && w.runner->camera.tick == 0x15D) {  // 0x00483AE0
            Vanish24Task* k = w.NewTask<Vanish24Task>(TaskVanish24, 0x00483B40);
            k->owner = w.ts.Current();
            k->ownerSerial = k->owner ? k->owner->serial : 0;
            memcpy(k->pos, a.body, 12);
            k->model = 0x7F4;
            k->scale = 1.0f;
        }
    }
    if (a.anim.kind == 0x53) {  // a fixed model 0x1382 beside the character
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0x44C7747B), Bits(0x44160000), Bits(0xC3482E14));
        mx::RotY(m, 0xDC51);
        mx::RotX(m, 0x4000);
        w.Draw(0x1382, m);
    }
    if (a.c1324 == 0) ++a.anim.counter[0];
}

// The removal of 0x00483030 / 0x004830F0 / 0x004831E0 / 0x004832E0 (variant 5 0x00483410: the camera test only)
bool Actor24Removed(World::Impl& w, Actor24Task& a, bool cameraOnly) {
    const SpawnRecord& r = a.record;
    bool remove;
    if (cameraOnly || !(a.flags34 & 0x2000000))
        remove = w.runner->camera.camId == r.ParamS16(6) && w.runner->camera.tick >= r.ParamS16(8);
    else
        remove = w.runner->flags[r.ParamS16(6) & 0xFF] == 1;
    if (!remove) return false;
    if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;  // 0x004092D0
    a.actorSlot = -1;
    // 0x004124B0: parts list released
    w.ts.KillCurrent();  // (does not return)
    return true;
}

// Variants 1, 2, 4, 5 (0x00483030 / 0x004830F0 / 0x004832E0 / 0x00483410)
void TaskActor24Variant(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor24Task& a = t.As<Actor24Task>();
    const SpawnRecord& r = a.record;
    const int camId = w.runner->camera.camId, tick = w.runner->camera.tick;
    const int lastTick = w.MotionTicks(a.anim.motion[0]) - 1;
    switch (a.variant) {
    case 1:  // frozen from camera P s16 14 / tick P s16 16
        Actor24Removed(w, a, false);
        if (camId == r.ParamS16(14) && r.ParamS16(16) <= tick) a.c1324 = 1;
        Actor24Draw(w, a);
        break;
    case 2:  // starts frozen; runs from camera P s16 18 / tick P s16 20, frozen again from P s16 14 / P s16 16
        Actor24Removed(w, a, false);
        if (camId == r.ParamS16(18) && r.ParamS16(20) <= tick) a.c1324 = 0;
        if (camId == r.ParamS16(14) && r.ParamS16(16) <= tick) a.c1324 = 1;
        Actor24Draw(w, a);
        break;
    case 4: {  // walks (0.646, 0, 0.614) per frame for 40 frames, waits 26, held at the motion's last tick
        Actor24Removed(w, a, false);
        if (a.sub == 0) {
            a.c1330 = 0x27;
            a.vel[0] = Bits(0x3F2570B8);
            a.vel[2] = Bits(0x3F1D0E68);
            ++a.sub;
        }
        if (a.sub == 1) {
            a.pos[0] = a.vel[0] + a.pos[0];
            a.pos[2] = a.vel[2] + a.pos[2];
            if (a.c1330-- < 1) {
                ++a.sub;
                a.c1330 = 0x19;
            }
        } else if (a.sub == 2) {
            if (a.c1330-- < 1) a.sub = 3;
        }
        Actor24Draw(w, a);
        if (a.anim.tick[0] == lastTick) a.c1324 = 1;
        break;
    }
    case 5: {  // from tick 0x32 of its motion falls (gravity 0x00569468) to the ground 0x009C8E58
        Actor24Removed(w, a, true);
        bool fall = false;
        if (a.sub == 0) {
            if (a.anim.tick[0] >= 0x32) {
                a.acc[1] = 0;
                a.vel[1] = 0;
                ++a.sub;
                fall = true;
            }
        } else if (a.sub == 1) {
            fall = true;
        }
        if (fall) {
            a.acc[1] = a.acc[1] - F32(w.exe, 0x00569468);
            a.vel[1] = a.acc[1] + a.vel[1];
            a.pos[1] = a.vel[1] + a.pos[1];
            if (a.pos[1] <= w.ground) {
                a.pos[1] = w.ground;
                ++a.sub;
            }
        }
        if (a.anim.tick[0] == lastTick) a.c1324 = 1;
        Actor24Draw(w, a);
        break;
    }
    default: break;
    }
}

// 0x00482EE0 (variant 0, P s16 12 == 0), 0x00482F60 (variant 0, P s16 12 != 0), 0x004831E0 (variant 3)
void TaskActor24(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor24Task& a = t.As<Actor24Task>();
    const SpawnRecord& r = a.record;
    bool remove;
    if (!(a.flags34 & 0x2000000))
        remove = w.runner->camera.camId == r.ParamS16(6) && w.runner->camera.tick >= r.ParamS16(8);
    else
        remove = w.runner->flags[r.ParamS16(6) & 0xFF] == 1;
    if (remove) {
        if (a.actorSlot >= 0 && a.actorSlot < 14) w.actorSlots[a.actorSlot] = nullptr;  // 0x004092D0
        w.ReleasePartModels(a.parts);  // 0x004124B0
        w.ts.KillCurrent();
        if (a.fn == 0x004831E0 || a.fn == 0x00482F60) return;  // Task_Kill does not return
    }
    if (a.fn == 0x00482F60) {  // 0x00482F60: after P s16 12 frames Anim_BlendTo(P s16 14, tick 0, P s16 16 frames)
        if (r.ParamS16(12) <= a.c1320++) {
            w.AnimBlendTo(a, r.ParamS16(14), 0, r.ParamS16(16));  // 0x004119A0
            a.fn = 0x00482EE0;
        }
    }
    if (a.fn == 0x004831E0) {  // variant 3: fall (+0x5C acceleration, +0x50 velocity) to the ground 0x009C8E58
        bool fall = true;
        if (a.sub == 0) {
            a.acc[1] = 0;
            a.vel[1] = 0;
            ++a.sub;
        } else if (a.sub != 1) {
            fall = false;
        }
        if (fall) {
            float g;
            w.exe.Read(0x00565E1C, &g, 4);
            a.acc[1] = a.acc[1] - g;
            a.vel[1] = a.acc[1] + a.vel[1];
            a.pos[1] = a.vel[1] + a.pos[1];
            if (a.pos[1] <= w.ground) {
                a.pos[1] = w.ground;
                ++a.sub;
                a.c1324 = 0;
            }
        }
    }
    Actor24Draw(w, a);  // 0x004834F0
    if (a.fn == 0x004831E0 && a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) - 1) a.c1324 = 1;
}

// 0x0043F5C0
void TaskRat(Task& t) {
    World::Impl& w = *CurrentWorld();
    RatTask& k = t.As<RatTask>();
    if (k.state == 0) {
        k.pos[0] = k.vx + k.pos[0];
        k.pos[2] = k.vz + k.pos[2];
        if (0x138E < ++k.frame) k.frame = 0x1385;
        if (k.age % 100 == 99 && w.Rand(0x0043F67F) % 10 < 4) {
            k.state = 1;
            k.frame = 0x1385;
        }
    } else if (k.state == 1 && 0x3C < ++k.pause) {
        k.state = 0;
        k.pause = 0;
        const int a = w.Rand(0x0043F5FD) & 0xFFF;
        const int b = w.Rand(0x0043F60A) & 0xFFF;
        k.rot[1] += a - b;
        float s, c;
        mx::SinCos(k.rot[1], &s, &c);
        k.vx = s * k.speed;
        k.vz = c * k.speed;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    w.Draw(k.frame, m, 8, -1.0f, k.variant == 1 && w.altModels != 0);  // variant 1: 0x004185E0
    if (600 < ++k.age) {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
}

// 0x0043F720 (actor 0x52 variants 2..4, game type 1): removed once event flag 0x82 / 0x21 / 0x22 (by variant) is 1;
// shot while the variant's word 0x00564444[v] is set: route branch 0x009C88A4 = byte 0x00564442[v], then it runs off
// along its yaw at 0.4 (variant 2 until z < float 0x00564458, 3 until x > float 0x00564454, 4 until x < float
// 0x00564450; then still), animated 0x1385..0x138E; drawn, hit sphere at its position (0x0043F950)
void TaskRatType1(Task& t) {
    World::Impl& w = *CurrentWorld();
    RatTask& k = t.As<RatTask>();
    const ExeImage& exe = w.exe;
    const int v = k.variant;
    const int fl = v == 2 ? 0x82 : v == 3 ? 0x21 : v == 4 ? 0x22 : -1;
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        for (World::Impl::Target& tg : w.targetCopy)
            if (tg.obj == &k) tg.obj = nullptr;
        w.ts.KillCurrent();
    };
    if (fl >= 0 && w.runner->flags[fl] == 1) {
        destroy();
        return;
    }
    auto move = [&] {
        k.pos[0] = k.vx + k.pos[0];
        k.pos[2] = k.vz + k.pos[2];
    };
    auto animate = [&] {
        if (0x138E < ++k.frame) k.frame = 0x1385;
    };
    switch (k.state) {
    case 0:
        if ((k.flags34 & 8) && exe.S16(0x00564444 + static_cast<uint32_t>(v) * 2) != 0) {
            int8_t b = 0;
            exe.Read(0x00564442 + static_cast<uint32_t>(v), &b, 1);
            w.runner->camera.routeBranch = b;  // 0x009C88A4
            k.state = 1;
            k.frame = 0x1385;
            k.y10 = k.pos[1];  // block +0x10
        }
        k.flags34 &= ~8u;
        break;
    case 1: {
        float s, c;
        mx::SinCos(k.rot[1], &s, &c);
        k.vx = s * k.speed;
        k.vz = c * k.speed;
        k.state = static_cast<int16_t>(v == 3 ? 3 : v == 4 ? 4 : v == 2 ? 2 : 1);
        k.age = 0;
        break;
    }
    case 2:
        move();
        if (k.pos[2] < F32(exe, 0x00564458)) k.state = 10;
        animate();
        break;
    case 3:
        move();
        if (F32(exe, 0x00564454) < k.pos[0]) k.state = 10;
        animate();
        break;
    case 4:
        move();
        if (k.pos[0] < F32(exe, 0x00564450)) k.state = 10;
        animate();
        break;
    case 0xB:
        destroy();
        return;
    default:
        break;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    w.Draw(k.frame, m);
    mx::Transform(w.cam.view, k.pos, k.viewPos);  // 0x0043F950
    w.RegisterTarget(&k);
}

// 0x00473B90
void TaskDoor(Task& t) {
    World::Impl& w = *CurrentWorld();
    DoorTask& d = t.As<DoorTask>();
    if (w.runner->flags[0x13] == 1) {  // Actor_Destroy 0x00409CC0
        d.flags34 = (d.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    }
    if (w.runner->flags[0x12] == 1 && d.panim.frame < w.MotionTicks(d.panim.motion) - 2) ++d.panim.frame;
    {
        const int k = d.panim.skeleton == 2 ? 0 : 1;
        const uint32_t tab = k == 0 ? 0x005961F0 : 0x00596204;
        if (w.exe.S16(tab + static_cast<uint32_t>(d.snd[k]) * 2) == d.panim.frame) {
            w.Sound(0x1816A9);
            ++d.snd[k];
            if (w.exe.S16(tab + static_cast<uint32_t>(d.snd[k]) * 2) == -1) d.snd[k] = 0;
        }
    }
    if (!w.MotionResident(d.panim.motion)) return;  // 0x009A469C == 2
    const Motion* mo = w.PropMotion(d.panim.motion);
    if (!mo) return;
    const PropSkeleton& sk = w.PropSkel(d.panim.skeleton);
    // 0x0040DFD0(+0x324, +0x2A0, -1): the skeleton drawn on the view (the node matrices are model-view; node
    // +0x2A0's goes to +0x338 -> +0x150)
    PropAnim mv = d.panim;
    Mat4 stored = Mat4::Identity();
    PropPose(sk, *mo, mv, w.cam.view, -1, false, d.store + 1, &stored);
    for (const PropDraw& x : PropPose(sk, *mo, d.panim, Mat4::Identity(), -1, false)) {
        if (x.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
        if (x.model != 0) w.Draw(x.model, x.world);  // Mdl_Draw 0x00418560(0) draws nothing
    }
    d.m150 = stored;
    // 0x0040E070: rotation of slot +0x2A0 - 1 in frame +0x32C -> +0x64 / +0x68 / +0x6C
    const int count = sk.count;
    const size_t stride = sk.FrameStride();
    const size_t off = 4 + stride * static_cast<size_t>(d.panim.frame) + 12 * static_cast<size_t>(count - 1) +
                       6 * static_cast<size_t>(d.store - 1);
    if (off + 6 <= mo->data.size()) {
        int16_t r[3];
        memcpy(r, &mo->data[off], 6);
        for (int c = 0; c < 3; ++c) d.rot[c] = r[c];
    }
    w.RegisterTarget(&d);
}

// 0x004724A0
void TaskItem(Task& t) {
    World::Impl& w = *CurrentWorld();
    ItemTask& k = t.As<ItemTask>();
    const uint32_t row = 0x00595058 + static_cast<uint32_t>(k.slot) * 12;
    const float scale = F32(w.exe, row + 4);
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        w.ts.KillCurrent();
    };
    // 0x00466640
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) destroy();
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (!(k.flags34 & 0x40000000) && (k.flags34 & 8)) {
        PropShooter(w, k.flags34, false);
        k.flags34 |= 0x40000000;
        w.Sound(k.slot == 7 ? 0x416A9u : 0x3B17A9u);
        const int points = w.exe.S16(row + 2);
        const uint32_t g = k.flags34 & 2;
        if (g == 0 || !(k.flags34 & 4)) {
            if (g == 0) {
                w.AddScore(1, points);
                k.digits = 0x119C;
            } else {
                w.AddScore(0, points);
                k.digits = 0x116A;
            }
        } else {
            const int p = w.Rand(0x0047252E) & 1;
            w.AddScore(p, points);
            k.model = static_cast<int16_t>(p * 0x32 + 0x116A);  // (the original writes the model, not +0x2A4)
        }
        k.counter = 1;
    }
    if (k.counter > 0 && 0x31 < ++k.counter) destroy();
    if (k.slot != 5 && k.slot != 7) k.rotY += 0x200;
    // slot 5: 0x004A9F70 clears the rotation of the model-view (a billboard): drawn in view space
    Mat4 m = k.slot == 5 ? w.cam.view : Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    if (k.slot == 5) mx::ClearRotation(m);
    else mx::RotY(m, k.rotY);
    if (k.slot == 2) mx::RotX(m, 0x4000);
    mx::Scale(m, scale, scale, scale);
    const bool vs = k.slot == 5;
    w.envMapDraw = k.slot == 8 && k.model == 0x10B2;  // slot 8: 0x00418660(0x10B2), its own model's env-map uv (CODE)
    w.LightChar();  // 0x0041DC70 .. 0x0041DCC0 around the item draw
    if (k.counter < 0x19) w.Draw(k.model, m, 8, -1.0f, false, -1, vs);
    else w.Draw(k.model, m, 8, 1.0f - static_cast<float>(k.counter) * F32(w.exe, 0x004E3100), false, -1, vs);  // 0x004185A0
    w.LightScene();
    w.envMapDraw = false;
    if (k.counter == 0) {
        Mat4 s = Mat4::Identity();
        mx::Translate(s, k.pos[0], (k.pos[1] - F32(w.exe, row + 8)) + 0.1f, k.pos[2]);  // 0x004C4CC8
        mx::Scale(s, 3.0f, 1.0f, 3.0f);
        w.Draw(0x10D0, s);
    }
    if (k.slot != 7 && k.counter > 0) {
        Mat4 d = Mat4::Identity();
        mx::Translate(d, k.pos[0], k.pos[1] - 2.5f, k.pos[2]);  // 0x004C4CBC
        mx::RotY(d, k.rotY);
        w.Draw(k.digits - 1 + k.counter, d);
    }
    const float p[3] = { k.pos[0], (k.slot == 8 ? 1.5f : 0.0f) + k.pos[1], k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

}  // namespace

// 0x004723F0(prop, slot) / 0x00471BD0 / 0x004722A0: the item of a breakable prop whose counter reached 0
// (0x00465FB0, item slot cases)
void World::Impl::DropItem(const BreakableProp& b, int slot) {
    float y = b.pos[1];
    switch (slot) {
    case 2: case 5: case 8: break;
    case 6:
        if (b.subKind == 2 || b.subKind == 8 || b.subKind == 9) y = y + 0.5f;  // 0x004C43AC
        break;
    case 7:
        if (b.model == 0x17AB) {
            double d = 0;
            exe.Read(0x00569070, &d, 8);
            y = y + static_cast<float>(d);
        }
        break;
    case 4:
        return;  // (no case in 0x00465FB0's slot switch: slot 4 drops nothing)
    default:
        if (DropItemSt1b(b, slot)) return;  // slots 1 / 3 (world_st1b.cpp)
        Note("item drop slot " + std::to_string(slot) + " (0x00471BD0 / 0x004722A0) not ported");
        return;
    }
    ItemTask* k = NewTask<ItemTask>(TaskItem, 0x004724A0);
    k->slot = static_cast<int8_t>(slot);
    k->flags34 = 0x80000001u;
    k->pos[0] = b.pos[0];
    k->pos[1] = F32(exe, 0x00595060 + static_cast<uint32_t>(slot) * 12) + y;
    k->pos[2] = b.pos[2];
    k->areaCount = static_cast<int8_t>(b.areaChanges);
    k->area = static_cast<int8_t>(b.areaSeen);
    k->life = b.lifetime;
    k->radius = 3.0f;
    k->model = exe.S16(0x00595058 + static_cast<uint32_t>(slot) * 12);
}

namespace {
// Penalty mark 0x0048E190 (created by 0x0048E080 when a civilian is shot): model 0x132D (P1) / 0x132E (P2) above the
// node, in view space, 60 frames (fading below 6)
struct PenaltyTask : WTask {
    float pos[3] = {};  // +0x40
    float scale = 1.0f; // block +0
    int counter = 60;   // block +4
    int16_t model = 0;  // +0x1F4
};

void TaskPenalty(Task& t) {
    World::Impl& w = *CurrentWorld();
    PenaltyTask& k = t.As<PenaltyTask>();
    Mat4 m = w.cam.view;  // 0x009A6000[active] (view of the active context)
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    float v[3];
    mx::GetTranslation(m, v);
    Mat4 d = Mat4::Identity();
    mx::Translate(d, v[0], v[1] + 2.0f, v[2] + 5.0f);  // 0x004E30F0 / 0x0055D2B4
    mx::Scale(d, k.scale, k.scale, k.scale);
    const float c = static_cast<float>(k.counter);
    if (6.0f <= c) w.Draw(k.model, d, 8, -1.0f, false, -1, true);  // 0x0055E1C8
    else w.Draw(k.model, d, 8, c * F32(w.exe, 0x00564544), false, -1, true);
    if (--k.counter == 0) w.ts.KillCurrent();
}
}  // namespace

// 0x0048E080(player, world position)
void World::Impl::SpawnPenaltyMark(int p, const float pos[3]) {
    PenaltyTask* k = NewTask<PenaltyTask>(TaskPenalty, 0x0048E190);
    memcpy(k->pos, pos, 12);
    k->counter = 60;
    Mat4 m = cam.view;
    mx::Translate(m, pos[0], pos[1], pos[2]);
    float v[3];
    mx::GetTranslation(m, v);
    const float z = v[2];
    if (z < F32(exe, 0x0056B188)) k->scale = z * F32(exe, 0x0056B184);
    else if (z <= -20.0f) k->scale = 1.0f;  // 0x004C4C60
    else k->scale = z * F32(exe, 0x0056B180);
    k->model = static_cast<int16_t>(p == 0 ? 0x132D : 0x132E);
}

// 0x00408A20 for an enemy record (the civilian's children, 0x0048A3E0): same as an op 0x0B spawn
EnemyTask* World::Impl::SpawnRecordEnemy(uint32_t record) {
    SpawnRecord r;
    if (!r.Read(mem, record)) return nullptr;
    const uint32_t tfn = r.type < 0x70 ? actorType[r.type] : 0;
    if (tfn == 0x0045CD60) return SpawnVehicleEnemy(r);  // actor 0x18 (world_st3_vehicle.cpp)
    if (tfn != fn::kEnemyCreate) {
        Note("civilian child of actor type " + std::to_string(r.type) + " not ported");
        return nullptr;
    }
    EnemyTask* e = NewTask<EnemyTask>(WorldTramp<&Impl::TaskEnemy>, fn::kEnemyCreate);
    e->lock = &lockTargets[nextLockTarget++];
    e->flags34 = r.flags | 1;  // 0x00408970
    memcpy(e->pos, r.pos, 12);
    memcpy(e->rot, r.rot, 12);
    e->hp = e->hpMax = static_cast<int16_t>(r.state);
    e->lo = static_cast<int16_t>(r.lo);
    e->record = r;
    return e;
}

// The komono / actor constructors (first run of the spawn task). Returns false for other types.
bool World::Impl::SpawnScene1(Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    const float degToAngle = F32(exe, 0x00569010);
    if (r.type == 0x41) {
        const int kind = r.ParamS8(1);
        if (kind == 38 || kind == 39) {  // 0x00463420 / 0x00463510
            const uint32_t tab = kind == 38 ? 0x00593E70 : 0x00593F48;
            const int n = kind == 38 ? 9 : 8;
            for (int i = 0; i < n; ++i) {
                const uint32_t e = tab + static_cast<uint32_t>(i) * 0x18;
                float pos[3];
                int rot[3];
                for (int c = 0; c < 3; ++c) pos[c] = F32(exe, e + 4 * c);
                for (int c = 0; c < 3; ++c) rot[c] = static_cast<int>(F32(exe, e + 12 + 4 * c) * degToAngle);
                if (kind == 38) {
                    Komono38Task* k = NewTask<Komono38Task>(TaskKomono38, 0x0046BCC0);
                    k->life = static_cast<int16_t>(r.state);
                    k->area = static_cast<int8_t>(runner->variant);
                    k->index = static_cast<int16_t>(i);
                    memcpy(k->pos, pos, 12);
                    memcpy(k->rot, rot, 12);
                    k->radius = 3.0f;
                    k->flags34 = 0x80000001u;
                    k->baseY = k->pos[1];
                } else {
                    Komono39Task* k = NewTask<Komono39Task>(TaskKomono39, 0x0046C240);
                    k->life = static_cast<int16_t>(r.state);
                    k->area = static_cast<int8_t>(runner->variant);
                    k->index = static_cast<int16_t>(i);
                    memcpy(k->pos, pos, 12);
                    memcpy(k->rot, rot, 12);
                    for (int j = 0; j < 8; ++j) {
                        k->piece[j][0] = k->pos[0];
                        k->piece[j][1] = static_cast<float>(j) * F32(exe, 0x00569014) + k->pos[1];
                        k->piece[j][2] = k->pos[2];
                        k->ang[j][0] = static_cast<int16_t>(Rand(0x00463605) % 0x1001 - 0x800);
                    }
                    k->radius = 5.0f;
                    k->flags34 = 0x80000001u;
                }
            }
            ts.KillCurrent();
        }
        if (kind == 44) {  // 0x004639F0
            for (int i = 0; i < 7; ++i) {
                const uint32_t e = 0x005946F8 + static_cast<uint32_t>(i) * 20;
                Komono44Task* k = NewTask<Komono44Task>(TaskKomono44, 0x0046D850);
                k->life = static_cast<int16_t>(r.state);
                k->area = static_cast<int8_t>(runner->variant);
                k->index = static_cast<int16_t>(i);
                const float sc = F32(exe, 0x004D5464);
                for (int c = 0; c < 3; ++c) k->pos[c] = static_cast<float>(exe.S16(e + 2 * c)) * sc;
                for (int c = 0; c < 3; ++c) k->rot[c] = static_cast<int>(exe.U32(e + 8 + 4 * c));
                k->radius = 5.0f;
                k->flags34 = 0x80000001u;
                k->anim.skeleton = 0x13;
                k->anim.motion = 0x1D4;
            }
            ts.KillCurrent();
        }
        if (kind == 7) {  // generic ctor 0x00461CF0 -> 0x005936BC[7] = 0x00466930
            Komono7Task* k = NewTask<Komono7Task>(TaskKomono7, 0x00466930);
            memcpy(k->pos, r.pos, 12);
            memcpy(k->rot, r.rot, 12);
            k->life = static_cast<int16_t>(r.state);
            k->area = static_cast<int8_t>(runner->variant);
            k->flags34 = 0x80000001u;
            k->radius = 12.0f;  // 0x41400000
            ts.KillCurrent();
        }
        if (kind == 69 && runner->cfg.gameType == 1 && modes) {  // the game type 1 target (world_modes_items.cpp)
            SpawnKomono69(*this, r);
            ts.KillCurrent();
        }
        if (kind == 69) {  // generic ctor 0x00461CF0 -> 0x005936BC[69] = 0x00470500: only in game type 1
            ActorTask* k = NewTask<ActorTask>(
                [](Task& t) {  // 0x00470500 (game type 0x009CA08C != 1): Actor_Destroy on its first run
                    ActorTask& a = t.As<ActorTask>();
                    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
                    CurrentWorld()->ts.KillCurrent();
                },
                0x00470500);
            k->flags34 = 0x80000001u;
            k->radius = 4.0f;
            ts.KillCurrent();
        }
        if (kind == 9) {  // generic ctor 0x00461CF0 -> 0x005936BC[9] = 0x00472830
            Komono9Task* k = NewTask<Komono9Task>(TaskKomono9, 0x00472830);
            k->area = static_cast<int8_t>(runner->variant);
            k->anim.skeleton = 8;
            k->anim.motion = 0x1D0;
            ts.KillCurrent();
        }
        return SpawnSt1b(t, s);  // world_st1b.cpp
    }
    if (r.type == 0x33 && r.state == 4) {  // 0x00432FF0 -> 0x00433B70, actor slot 0x00409270
        auto d = std::make_unique<Type33Task>();
        d->fn = 0x00433B70;
        d->record = r;
        d->flags34 = r.flags | 1;  // 0x00408A20
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->hp = d->hpMax = static_cast<int16_t>(r.state);
        for (int i = 0; i < 14; ++i)
            if (!actorSlots[i]) {
                actorSlots[i] = d.get();
                d->actorSlot = i;
                d->flags38 |= 0x40;
                break;
            }
        t.data = std::move(d);
        t.fn = TaskType33State4;  // runs from the next frame (0x00432FF0 only sets the function)
        return true;
    }
    if (r.type == 0x25) return SpawnActor25(t, s);  // world_npc.cpp
    if (r.type == 0x44 && r.state == 0) {  // 0x00472B10 -> 0x00472B30: new task 0x00473B90, the spawn task ends
        DoorTask* d = NewTask<DoorTask>(TaskDoor, 0x00473B90);
        d->flags34 |= 0x51;
        const int model = r.ParamS32(4);
        d->panim.skeleton = model == 0x13F5 ? 2 : 3;
        d->store = model == 0x13F5 ? 2 : 1;
        d->panim.motion = 0x1D7;
        d->radius = 40.0f;  // 0x42200000
        d->objMesh = r.ParamS32(8);
        ts.KillCurrent();
    }
    if (r.type == 0x52) {  // 0x0043F4C0 (op 0x0C: params at +0x130C)
        auto d = std::make_unique<RatTask>();
        d->fn = 0x0043F5C0;
        d->flags34 = r.flags | 1;  // 0x00408970
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->radius = 2.0f;  // +0x124
        d->variant = r.ParamS16(0);
        float s, c;
        mx::SinCos(d->rot[1], &s, &c);
        d->vx = s * F32(exe, 0x0055D1A0);
        d->vz = c * F32(exe, 0x0055D1A0);
        d->frame = static_cast<int16_t>(Rand(0x0043F538) % 10 + 0x1385);
        // 0x00408E80: first free lock-on slot 2..13 (cleared again by the next table build); +0x100 (look point)
        // is never written (0) and +0x121 is 0
        d->lock = &lockTargets[nextLockTarget++];
        memcpy(d->lock->pos, d->pos, 12);
        d->lock->targetPlayer = 0;
        lockOn.AddDirect(d->lock);
        if (d->variant >= 2) {
            if (runner->cfg.gameType != 1) {  // Actor_Destroy 0x00409CC0
                d->flags34 = (d->flags34 & ~1u) | 0x80018000u;
                t.data = std::move(d);
                ts.KillCurrent();
                return true;
            }
            d->frame = 0x1385;  // game type 1: waits to be shot (0x0043F720)
            d->vx = d->vz = 0.0f;
            d->fn = 0x0043F720;
            t.data = std::move(d);
            t.fn = TaskRatType1;
            return true;
        }
        d->vx = s * d->speed;
        d->vz = c * d->speed;
        t.data = std::move(d);
        t.fn = TaskRat;  // runs from the next frame
        return true;
    }
    if (r.type == 0x24) {  // 0x00482CE0
        const int variant = r.ParamS8(5);
        auto d = std::make_unique<Actor24Task>();
        d->fn = 0x00482EE0;
        d->record = r;
        d->flags34 = r.flags | 1;  // 0x00408A20
        memcpy(d->pos, r.pos, 12);
        memcpy(d->rot, r.rot, 12);
        d->hp = d->hpMax = static_cast<int16_t>(r.state);
        d->slot120 = -1;
        d->variant = variant;
        d->anim.kind = r.ParamS8(4);
        d->anim.motion[0] = r.ParamS16(10);
        EnemyInitPose(*d);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
        d->anim.drawFlags |= 4;
        const int kind = d->anim.kind, mot = d->anim.motion[0];
        if ((kind == 0x53 && mot == 0xD7) || (kind == 0x54 && mot == 0xDC)) d->anim.drawFlags |= 8;
        else if (kind == 0x53 && mot == 0xD9) SpawnBirds24(*this);  // 0x00483DD0
        d->anim.rotOrder = 1;
        d->anim.counter[0] = d->hp == -1 ? Rand(0x00482D92) : d->hp;
        d->nodeCb = 0x004835D0;
        d->parts = static_cast<uint32_t>(r.ParamS32(0));  // 0x0045EBB0(P[0]) -> +0x1304
        if (getenv("WORLD_TRACE_SPAWN")) fprintf(stderr, "actor 0x24 kind %X motion %X parts %X\n", kind, mot, d->parts);
        for (uint32_t a = d->parts; a;  a += 2) {  // 0x00412440: part ids < 0x24 replace node models
            uint32_t w = 0;
            if (!mem.Read(a & ~3u, &w)) break;
            const int16_t id = static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
            if (id < 0) break;
            const uint32_t def = exe.U32(0x004EC748 + 4 * id);
            if (id < 0x24) d->anim.nodes[static_cast<int>(exe.U32(def)) & 31].model = static_cast<int32_t>(exe.U32(def + 4));
            if (id > 0x11 && residency) resQueue.push_back({ 0x50, exe.U32(def + 4) });  // 0x0041D5D0: model load request
        }
        if (gameMode == 10 && kind == 0x44) {  // story sequence 1: headless (node 2 model and hit radius 0)
            d->anim.nodes[2].model = 0;
            d->anim.nodes[2].radius = 0.0f;
        }
        if (gameMode == 11 && kind == 0x41 && d->anim.counter[0] == 0 && mot == 0xEA) {  // 0x00483C90
            Flash24Task* f = NewTask<Flash24Task>(TaskFlash24, 0x00483CE0);
            f->pos[0] = Bits(0xC48B1333);
            f->pos[1] = Bits(0x41480000);
            f->pos[2] = Bits(0xC4FD7333);
        }
        if (variant == 0 && r.ParamS16(12) != 0) d->fn = 0x00482F60;  // waits P s16 12 frames, then blends (world_st2)
        if (variant == 3) {  // 0x004831E0: falls to the ground, animation held at the motion's last tick
            d->c1324 = 1;
            d->fn = 0x004831E0;
        }
        if (variant == 1 || variant == 2 || variant == 4 || variant == 5) {
            static const uint32_t kFn[6] = { 0, 0x00483030, 0x004830F0, 0, 0x004832E0, 0x00483410 };
            d->fn = kFn[variant];
            if (variant == 2) d->c1324 = 1;
            t.data = std::move(d);
            t.fn = TaskActor24Variant;
            TaskActor24Variant(t);
            return true;
        }
        if (variant != 0 && variant != 3) {  // 0x00482CE0's switch has no case (the ctor would stay the task function)
            Note("actor 0x24 variant " + std::to_string(variant) + " not ported (no case in 0x00482CE0)");
            return false;
        }
        t.data = std::move(d);
        t.fn = TaskActor24;
        TaskActor24(t);
        return true;
    }
    return SpawnSt1b(t, s);  // world_st1b.cpp
}

}  // namespace game
