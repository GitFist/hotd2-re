// game::World - komono kinds (actor type 0x41, ctor table 0x00593580, task table 0x005936BC) first met in stage 2
// beyond those in world_st2_actors.cpp: kind 14 (generic ctor 0x00461CF0 -> task 0x00468180, the route sign: shooting
// it flips the camera route 0x009C88A4) and kind 17 (0x004630B0 -> three tasks 0x00468D10, falling bits).
// Verification: notes/agents/stage2b.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

constexpr double kToRad = 9.587379924285257e-05;  // double 0x004C4370

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

int Ftol(float f) { return static_cast<int>(f); }  // __ftol 0x004ACF50

// 0x004650F0(flags, score): the shooter (rand when both gun bits), +10 when score, a hit
int ShotBy(World::Impl& w, uint32_t f, bool score) {
    int p;
    if ((f & 2) && (f & 4)) p = w.Rand(0x00465106) & 1;
    else p = (f & 2) ? 0 : 1;
    if (score) w.AddScore(p, 10);
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

void Destroy(World::Impl& w, ActorTask& a) {  // Actor_Destroy 0x00409CC0 (no actor slot)
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;
    w.ts.KillCurrent();
}

// ---------------------------------------------------------------------------------------------------------------------
// Kind 14: route sign (model 0x10D2, scale 0.4). Shot: route 0x009C88A4 = 1 - state, flies up spinning and falls onto
// one of its two ends (state 2), then lies flat (3). Gone at scene 6 or after 450 frames.
// ---------------------------------------------------------------------------------------------------------------------
struct Komono14Task : ActorTask {
    int16_t state = 0;        // +0x11C (route of the sign)
    int8_t phase = 0;         // +0x192 (0 standing, 1 flying, 2 falling over, 3 lying)
    float pos[3] = {};        // +0x19C
    float rest[3] = {};       // +0x1A8 the end it rests on
    float vy = 0;             // +0x1C4
    int rot[3] = {};          // +0x1CC / +0x1D0 / +0x1D4
    int spin = 0;             // +0x1D8
    int light = 0;            // +0x1F4 (light direction: renderer)
    int16_t end = 0;          // +0x290 (-1 / +1)
    int slot[2] = {};         // +0x2A0 frame counter / +0x2A8 (the loop of 0x00468180 reads both as "end taken")
    float gravity = 0;        // +0x2C0
};

void TaskKomono14(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono14Task& k = t.As<Komono14Task>();
    const int c = ++k.slot[0];
    if (c == 0xD1 && k.phase == 0) {
        --w.liveEnemies;
        k.flags34 |= 0x40000000;
    }
    if (w.runner->scene == 6 || 0x1C1 < k.slot[0]) {
        w.ts.KillCurrent();  // Task_Kill (not Actor_Destroy)
        return;
    }
    if ((k.flags34 & 8) && !(k.flags34 & 0x40000000)) {
        ShotBy(w, k.flags34, false);
        w.runner->camera.routeBranch = 1 - k.state;  // 0x009C88A4
        w.Sound(0x1116A9);
        k.vy = Bits(0x3EE66666);
        k.flags34 |= 0x40000000;
        const int r = w.Rand(0x00468228);
        k.gravity = Bits(0xBCDEFC7A);
        k.phase = 1;
        k.spin = r % 0x81 + 0xC0;
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, Bits(0x3F333333), k.pos[2]);  // 0x004666B0
    }
    const int8_t ph = k.phase;
    if (0 < ph) {
        if (ph == 1) {
            k.vy = k.gravity + k.vy;
            k.pos[1] = k.vy + k.pos[1];
        }
        k.rot[0] += k.spin;
    }
    if (ph == 1) {
        if (k.vy <= 0.0f) {
            Mat4 m = Mat4::Identity();
            mx::RotY(m, k.rot[1]);
            mx::RotZ(m, k.rot[2]);
            mx::RotX(m, k.rot[0]);
            for (int e = -1, s = 0; e < 3; e += 2, ++s) {
                const float v[3] = { 0.0f, static_cast<float>(e) * F32(w.exe, 0x00569094), 0.0f };
                float o[3];
                mx::Transform(m, v, o);
                if (o[1] + k.pos[1] < F32(w.exe, 0x0055CC58) && k.slot[s] == 0) {
                    k.phase = 2;
                    k.spin = k.rot[0] < 0x4001 ? 0x600 : -0x600;
                    k.rest[1] = 36.0f;
                    k.end = static_cast<int16_t>(e);
                    k.rest[0] = o[0] + k.pos[0];
                    k.rest[2] = o[2] + k.pos[2];
                    k.slot[s] = 1;
                    k.gravity = 0;
                    k.vy = 0;
                }
            }
        }
    } else if (ph == 2 && ((0 < k.spin && 0x4000 < k.rot[0]) || (k.spin < 0 && k.rot[0] < 0x4000))) {
        k.rot[0] = 0x4000;
        k.spin = 0;
        k.rest[1] = 36.0f;
        k.phase = 3;
        --w.liveEnemies;
    }
    if (k.phase == 0) k.light += 0x400;  // 0x0040E0B0 / 0x004AA0E0: light (renderer)
    const float s = Bits(0x3ECCCCCD);
    if (k.phase < 2) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        mx::Scale(m, s, s, s);
        w.Draw(0x10D2, m);
        mx::Transform(w.cam.view, k.pos, k.viewPos);
        w.RegisterTarget(&k);  // 0x00405160
    } else {
        auto ops = [&](Mat4& m) {
            mx::Translate(m, k.rest[0], k.rest[1], k.rest[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotZ(m, k.rot[2]);
            mx::RotX(m, k.rot[0]);
            mx::Translate(m, 0.0f, static_cast<float>(k.end) * F32(w.exe, 0x00569090), 0.0f);
        };
        Mat4 mv = w.cam.view;  // 0x004A8CC0 on the stack top (model-view): +0x19C = a view-space point
        ops(mv);
        mx::GetTranslation(mv, k.pos);
        Mat4 m = Mat4::Identity();
        ops(m);
        mx::Scale(m, s, s, s);
        w.Draw(0x10D2, m);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Kind 17 (0x004630B0): three bits (models 0x842..0x844, scale 0.05) falling from around the record until below the
// ground 0x009C8E58; shot: +10, they hop (0.3)
// ---------------------------------------------------------------------------------------------------------------------
struct Komono17Task : ActorTask {
    float pos[3] = {};   // +0x19C
    float vy = 0;        // +0x1C4
    int16_t idx = 0;     // +0x290
};

void TaskKomono17(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono17Task& k = t.As<Komono17Task>();
    if (k.flags34 & 8) {
        ShotBy(w, k.flags34, true);
        const uint32_t f = k.flags34;
        k.flags34 = f & ~8u;
        w.SpawnSpark(k.viewPos[2], (f & 2) == 0 ? 1 : 0, Bits(0x3F99999A), k.pos[2]);  // 0x004666B0
        w.Sound(0xE16A9);
        k.vy = Bits(0x3E99999A);
    }
    k.vy = k.vy - F32(w.exe, 0x00569098);
    k.pos[1] = k.vy + k.pos[1];
    if (k.pos[1] < w.ground) return Destroy(w, k);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    const float s = Bits(0x3D4CCCCD);
    mx::Scale(m, s, s, s);
    w.Draw(k.idx + 0x842, m);
    mx::Transform(w.cam.view, k.pos, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

// Komono lifetime 0x00466640 (life +0x11C, area +0x196 / +0x197): false = Actor_Destroy'd
template <class T> bool Alive(World::Impl& w, T& k) {
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) {
        Destroy(w, k);
        return false;
    }
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            Destroy(w, k);
            return false;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Kind 12 (generic ctor case 0xC -> task 0x00467E50): a static model (record state) at the record, removed at camera
// 0x2F tick 0x96; alternative model with 0x009A2BB4
// ---------------------------------------------------------------------------------------------------------------------
struct Komono12Task : ActorTask {
    int16_t life = 0;                // +0x11C (record P byte 0)
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    float scale[3] = { 1, 1, 1 };    // +0x1A8
    float zOff = 0;                  // +0x1C8
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int16_t model = 0;               // +0x28C
};

void TaskKomono12(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono12Task& k = t.As<Komono12Task>();
    if (!Alive(w, k)) return;
    if (w.runner->camera.camId == 0x2F && w.runner->camera.tick == 0x96) return Destroy(w, k);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.zOff + k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
    w.Draw(k.model, m, 8, -1.0f, w.altModels != 0);  // 0x004185E0 when 0x009A2BB4
}

// Generic komono object of the ctor 0x00461CF0 (block 0x378)
struct GenericKomono : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t phase = 0;                // +0x192
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    float v[3] = {};                 // +0x1C0 / +0x1C4 / +0x1C8
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int spin = 0;                    // +0x1DC
    int yaw68 = 0;                   // +0x68
    int counter = 0;                 // +0x2A0
};

void Register(World::Impl& w, GenericKomono& k, float dy = 0.0f) {
    const float p[3] = { k.pos[0], k.pos[1] - dy, k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

// Kind 20 (0x00469380): a sign (model 0x1E2) spinning faster when shot
void TaskKomono20(Task& t) {
    World::Impl& w = *CurrentWorld();
    GenericKomono& k = t.As<GenericKomono>();
    if (!Alive(w, k)) return;
    if (k.flags34 & 8) {
        ShotBy(w, k.flags34, false);
        k.flags34 &= ~8u;
        w.Sound(0xE16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.0f, k.pos[2]);  // 0x004666B0
        k.spin += 0x400;
    }
    k.flags34 &= ~6u;
    if (0x180 < k.spin) k.spin -= 0x20;
    k.yaw68 += k.spin;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.yaw68);
    w.Draw(0x1E2, m);
    Register(w, k, 1.0f);  // y - qword 1.0
}

// Kind 21 (0x004694A0): model 0x132F + seed counter % 10 (an animated sign), draw mode 9
void TaskKomono21(Task& t) {
    World::Impl& w = *CurrentWorld();
    GenericKomono& k = t.As<GenericKomono>();
    if (!Alive(w, k)) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    w.Draw(static_cast<int>(w.seedCounter % 10u) + 0x132F, m, 9);  // 0x004A79F0(9), back to 8
}

// Kind 30 (0x0046A0F0): model 0x1DF, falling and tumbling once event flag 0x67 is set
void TaskKomono30(Task& t) {
    World::Impl& w = *CurrentWorld();
    GenericKomono& k = t.As<GenericKomono>();
    if (!Alive(w, k)) return;
    if (w.runner->flags[0x67] == 1) {  // 0x009C7267
        const float vy = k.v[1] - F32(w.exe, 0x0055CB10);
        k.rot[0] -= 0x100;
        k.rot[2] += 0x100;
        k.v[1] = vy;
        k.pos[1] = vy + k.pos[1];
        k.pos[2] = k.pos[2] + F32(w.exe, 0x004C43A8);
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(0x1DF, m);
}

// Kind 58 (0x0046F580): a can (model 0x1D1) knocked away when shot (+10, smoke 0x00465860)
void TaskKomono58(Task& t) {
    World::Impl& w = *CurrentWorld();
    GenericKomono& k = t.As<GenericKomono>();
    if (!Alive(w, k)) return;
    if ((k.flags34 & 8) && k.phase == 0) {
        ShotBy(w, k.flags34, true);
        w.Sound(0x1516A9);
        const uint32_t f = k.flags34;
        k.flags34 = f & ~8u;
        w.SpawnSmoke(k.viewPos[2], (f & 2) == 0 ? 1 : 0, k.pos[2]);  // 0x00465860
        if (w.runner->cfg.gameType == 1) {  // an item (slot +0x2A0 = 9) at y 44.2 (0x00467B90); the fall starts at once
            const float y = k.pos[1];
            k.counter = 9;
            k.pos[1] = Bits(0x4230CCCD);
            SpawnItemAt(w, k.pos, k.area, k.areaCount, k.life, k.counter);
            SetItemTaken(w, 0);  // 0x007DCD14
            k.pos[1] = y;
        }
        k.phase = 1;
    }
    if (k.phase == 1) {
        const int c = k.counter;
        k.pos[0] = k.pos[0] + 0.5f;
        const float z = k.pos[2] + 0.5f;
        k.counter = c + 1;
        k.pos[2] = z;
        if (8 < c) {
            k.v[1] = k.v[1] - F32(w.exe, 0x0055CB10);
            k.pos[1] = k.v[1] + k.pos[1];
        }
        k.rot[0] += 0x200;
        k.rot[2] += 0x200;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(0x1D1, m);
    Register(w, k);
}

// Kind 60 (0x0046F840): a bottle (model 0x1D8, scale 0.25 / 0.5) flying off when shot
void TaskKomono60(Task& t) {
    World::Impl& w = *CurrentWorld();
    GenericKomono& k = t.As<GenericKomono>();
    if (!Alive(w, k)) return;
    if (k.phase == 0) {
        if (k.flags34 & 8) {
            w.Sound(0x1116A9);
            w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.0f, k.pos[2]);  // 0x004666B0
            const int r = w.Rand(0x0046F902);
            k.v[1] = Bits(0x3E99999A);
            k.v[2] = 0.5f;
            k.phase = 1;
            k.v[0] = static_cast<float>(r % 0xB) * F32(w.exe, 0x0055D230) - 0.5f;
        }
    } else if (k.phase == 1) {
        k.v[1] = k.v[1] - F32(w.exe, 0x0055CB10);
        k.rot[0] += 0x400;
        k.rot[2] += 0x400;
        k.pos[0] = k.v[0] + k.pos[0];
        k.pos[1] = k.v[1] + k.pos[1];
        k.pos[2] = k.v[2] + k.pos[2];
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::Scale(m, 0.25f, 0.5f, 0.25f);
    w.Draw(0x1D8, m);
    Register(w, k);
}

// Kind 19 (0x00468F00): a window shutter (model 0x1CE) swinging open three times with a sign (+0x28C = 0x10D3) on
// it; shooting the sign flips the route 0x009C88A4 (1 - record state) and drops it; event flag 0x21 slams it
// (angles table 0x00595B00); it goes after the third area change
struct Komono19Task : GenericKomono {
    float base[3] = {};     // +0x40
    float signY = 0;        // +0x1AC
    int swing = 0;          // +0x1E4
    int phase1E8 = 0;       // +0x1E8
    int rot64[3] = {};      // +0x64 / +0x68 / +0x6C
    int16_t model = 0;      // +0x28C
    int swings = 0;         // +0x2A4
    int slam = 0;           // +0x2A8
    int timer = 0;          // +0x2AC
    int slammed = 0;        // +0x2B0
};

void TaskKomono19(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono19Task& k = t.As<Komono19Task>();
    if (w.runner->variant != k.area) {
        if (2 < ++k.areaCount) return Destroy(w, k);
        k.phase = 2;
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (k.timer++ == 0x14) w.Sound(0x2000001F);
    const uint32_t f = k.flags34;
    if ((f & 8) && !(f & 0x40000000) && k.phase < 2) {
        ShotBy(w, f, false);
        w.runner->camera.routeBranch = 1 - k.life;  // 0x009C88A4
        w.Sound(0xF16A9);
        const uint32_t g = k.flags34;
        k.flags34 = g | 0x40000000;
        w.SpawnSpark(k.viewPos[2], (g & 2) == 0 ? 1 : 0, 0.75f, k.pos[2]);  // 0x004666B0
        --w.attackers;
    }
    if (k.flags34 & 0x40000000) {
        k.v[1] = k.v[1] - F32(w.exe, 0x0055CB10);
        k.signY = k.v[1] + k.signY;
    }
    if (w.runner->flags[0x21] == 1 && k.slammed == 0) {
        k.phase = 3;
        k.slammed = 1;
        w.Sound(0x2116A9);
    }
    if (k.phase == 0) {
        const int c = ++k.counter;
        if (c == 0x1E || c == 0x46) {
            k.phase = 1;
            if (c == 0x46) k.counter = 0;
            w.Sound(0x1C16A9);
        }
    } else if (k.phase == 1) {
        int a = k.phase1E8;
        a += a < 0x4000 ? 0x1000 : 0x800;
        k.phase1E8 = a;
        k.rot64[1] = Ftol(static_cast<float>(std::sin(static_cast<double>(static_cast<float>(static_cast<double>(a) * kToRad))) * 1536.0));
        k.swing = Ftol(static_cast<float>(std::sin(static_cast<double>(static_cast<float>(static_cast<double>(a - 0x2000) * kToRad))) * -4096.0));
        if (0x8000 < a) {
            k.phase1E8 = 0;
            k.rot64[1] = 0;
            k.phase = 0;
            if (3 < ++k.swings) {
                if (0 < w.attackers) --w.attackers;
                k.phase = 2;
                w.Sound(0x20000015);
                ++w.civTotal;  // 0x009A21BA (read by actor 0x64, world_st6.cpp)
            }
        }
    } else if (k.phase == 3 && k.slam < 0x3C) {
        const uint32_t e = 0x00595B00 + static_cast<uint32_t>(k.slam) * 6;
        k.rot64[2] = w.exe.S16(e + 4) + k.rot[2];
        k.rot64[0] = w.exe.S16(e) + k.rot[0];
        k.rot64[1] = w.exe.S16(e + 2);
        ++k.slam;
    }
    auto frame = [&](Mat4& m) {
        mx::RotY(m, 0xC000);
        mx::RotZ(m, k.rot64[2]);
        mx::RotY(m, k.rot64[1]);
        mx::RotX(m, k.rot64[0]);
    };
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.base[0], k.base[1], k.base[2]);
    frame(m);
    w.Draw(0x1CE, m);
    Mat4 q = Mat4::Identity();
    frame(q);
    mx::RotX(q, k.swing);
    mx::Translate(q, 0.0f, -2.0f, 0.0f);
    const float local[3] = { -9.0f, 11.0f, 0.5f };
    float o[3];
    mx::Transform(q, local, o);
    Mat4 s = Mat4::Identity();
    mx::Translate(s, k.base[0], k.base[1], k.base[2]);
    frame(s);
    mx::Translate(s, -9.0f, static_cast<float>(static_cast<double>(k.signY) + 11.0), 0.5f);  // qword 0x005690A0
    mx::RotX(s, k.swing);
    mx::Translate(s, 0.0f, -2.0f, 0.0f);
    w.Draw(k.model, s);
    k.pos[0] = o[0] + k.base[0];
    k.pos[1] = o[1] + k.base[1];
    k.pos[2] = o[2] + k.base[2];
    Register(w, k);
}

// ---------------------------------------------------------------------------------------------------------------------
// Kind 8 (generic ctor case 8 -> task 0x00467080): a swaying lamp (model 0x1A36, scale 2.5) with three hanging pieces
// (tasks 0x00467290, model 0x1AAA) that fall when shot and splash (0x00472A50) below y 0x0055D2C8
// ---------------------------------------------------------------------------------------------------------------------
struct Komono8Task : ActorTask {
    int16_t life = 0;                // +0x11C
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};               // +0x19C
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int phase[3] = {};               // +0x298 sway, +0x1D8 / +0x1E0 tilt phases
    int16_t sub = 0;                 // +0x290
    Mat4 world = Mat4::Identity();   // +0x2E4 (the original keeps model-view; world here)
};

struct LampPieceTask : ActorTask {
    float local[3] = {};             // +0x194 offset in the lamp's frame
    float vy = 0;                    // +0x1A4
    int rot[3] = {};                 // +0x1AC / +0x1B0 / +0x1B4
    int8_t idx = 0;                  // +0x1B8
    int8_t falling = 0;              // +0x1B9
    int8_t area = 0, areaCount = 0;  // +0x1BA / +0x1BB
    Komono8Task* lamp = nullptr;     // +0x1BC
    float pos[3] = {};               // +0x40
};

struct SplashTask : WTask {  // 0x00472AA0: models 0x8F8..0x903 at the point, facing the camera yaw, scale 8
    float pos[3] = {};       // +0x34
    int yaw = 0;             // +0x40
    int model = 0x8F8;       // +0x48
};

void TaskSplash(Task& t) {
    World::Impl& w = *CurrentWorld();
    SplashTask& s = t.As<SplashTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::RotY(m, s.yaw);
    mx::Scale(m, 8.0f, 8.0f, 8.0f);
    w.Draw(s.model, m);  // light scale 8 (0x0041EBB0): renderer
    if (0x903 < ++s.model) w.ts.KillCurrent();
}

float SinA(int a) { return static_cast<float>(std::sin(static_cast<double>(static_cast<float>(a * kToRad)))); }
float CosA(int a) { return static_cast<float>(std::cos(static_cast<double>(static_cast<float>(a * kToRad)))); }

// 0x00467080
void TaskKomono8(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono8Task& k = t.As<Komono8Task>();
    if (w.runner->variant != k.area) {
        if (0xB < ++k.areaCount) {
            w.ts.KillCurrent();
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    k.phase[0] += 0x180;
    k.phase[1] += 0x100;
    k.phase[2] += 0x100;
    const float k4d08 = F32(w.exe, 0x004C4D08);
    const float ox = SinA(k.phase[0]) * k4d08, oz = CosA(k.phase[0]) * k4d08;
    double d;
    w.exe.Read(0x00569080, &d, 8);
    k.rot[0] = Ftol(static_cast<float>(static_cast<double>(SinA(k.phase[1])) * d));
    k.rot[2] = Ftol(static_cast<float>(static_cast<double>(CosA(k.phase[2])) * d));
    Mat4 r = Mat4::Identity();
    mx::RotY(r, k.rot[1]);
    mx::RotZ(r, k.rot[2]);
    mx::RotX(r, k.rot[0]);
    const float off[3] = { ox, 0.0f, oz };
    float v[3];
    mx::TransformX87(r, off, v);  // 0x004A8A80
    Mat4 m = Mat4::Identity();
    mx::Translate(m, ox + k.pos[0], k.pos[1], oz + k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotZ(m, k.rot[2]);
    mx::RotX(m, k.rot[0]);
    mx::Translate(m, -v[0], -v[1], -v[2]);
    mx::Scale(m, 2.5f, 2.5f, 2.5f);
    w.Draw(0x1A36 - k.sub, m);  // light scale 2.5 (0x0041EBB0): renderer
    k.world = m;                // 0x004A8CA0 -> +0x2E4
}

// 0x00467290
void TaskLampPiece(Task& t) {
    World::Impl& w = *CurrentWorld();
    LampPieceTask& k = t.As<LampPieceTask>();
    if (w.runner->variant != k.area) {
        if (0xB < ++k.areaCount) return Destroy(w, k);
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (k.flags34 & 8) {
        ShotBy(w, k.flags34, true);
        k.flags34 &= ~8u;
        w.Sound(0x1D16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) ? 0 : 1, 1.0f, k.pos[2]);  // 0x004674C0 (= 0x004666B0)
        k.falling = 1;
    }
    if (k.falling == 1) {
        const float vy = k.vy - F32(w.exe, 0x00569088);
        k.rot[2] -= 0x200;
        k.vy = vy;
        k.local[1] = vy + k.local[1];
    }
    // the lamp's +0x2E4 (model-view) brought back to world space: 0x004A9230(camera matrix +0x40), 0x004A92A0(+0x2E4).
    // The camera matrix is not the exact inverse of the view (one angle unit off in the pieces' rotation otherwise).
    Mat4 mv = k.lamp->world;
    mx::MulRight(mv, w.cam.view);
    Mat4 m = w.cam.inverse;
    mx::MulLeft(m, mv);
    mx::Translate(m, k.local[0], k.local[1], k.local[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::GetTranslation(m, k.pos);
    w.Draw(0x1AAA, m);
    if (k.pos[1] < F32(w.exe, 0x0055D2C8)) {
        SplashTask* s = w.NewTask<SplashTask>(TaskSplash, 0x00472AA0);  // 0x00472A50
        memcpy(s->pos, k.pos, 12);
        s->yaw = w.cam.yaw;
        s->model = 0x8F8;
        w.Sound(0x4316A9);
        w.ts.KillCurrent();
        return;
    }
    mx::Transform(w.cam.view, k.pos, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

// ---------------------------------------------------------------------------------------------------------------------
// Kind 1 (0x00462F70 -> task 0x0046E3A0): a scene model (0x00593DA4[P byte 0]) drawn as it is (world identity), its
// UV-scrolled vertices (water) animated by the renderer; removed by event flags / scenes
// ---------------------------------------------------------------------------------------------------------------------
struct Komono1Task : ActorTask {
    int8_t sub = 0;                  // +0x34 (block +0x00)
    int8_t life = 0;                 // +0x35
    int8_t area = 0, areaCount = 0;  // +0x36 / +0x37
    uint8_t flag = 0;                // +0x38 (event mode 2: event flag sub + 11)
    int model = 0;                   // +0x40
};

void TaskKomono1(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono1Task& k = t.As<Komono1Task>();
    const int mode = w.cfg.eventMode;
    const uint8_t* f = w.runner->flags;
    if (mode == 1 && f[0x77] != 0) return Destroy(w, k);
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) return Destroy(w, k);
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.runner->cfg.gameType != 3) {
        if (mode == 2) {
            if (f[k.flag] == 1) return w.ts.KillCurrent();
        } else if (mode == 1 && w.runner->scene == 0x23 && w.runner->variant == 2) {
            return w.ts.KillCurrent();
        }
        if (k.model == 0x13B5 && w.runner->variant == 0xF) return w.ts.KillCurrent();
        if (mode == 2 && f[4] != 0) return w.ts.KillCurrent();
    }
    w.Note("komono 1 vertex UV animation (0x0046E3A0) left to the renderer");
    const Mat4 id = Mat4::Identity();
    w.Draw(k.model, id);
    if (k.model == 0x13A7) w.Draw(0x13A5, id);
    if (k.model == 0x13A9) w.Draw(0x13AC, id);
    if (f[9] != 0) {
        if (k.model == 0x13A7) k.model = 0x13A9;
        else if (k.model == 0x13A0) k.model = 0x13A2;
    }
    if (k.model == 0x13A2 && w.runner->camera.camId == 0x6E) k.model = 0x13A0;
}

}  // namespace

// Komono kinds 12 / 14 / 17 / 19 / 20 / 58 / 60 (first run of the spawned actor; false: another kind)
bool SpawnKomonoSt2(World::Impl& w, const SpawnRecord& r) {
    const int kind = r.ParamS8(1);  // +0x130C
    if (kind == 20 || kind == 58 || kind == 60 || kind == 21 || kind == 30) {  // generic ctor 0x00461CF0 (cases 0x14 /
        // 0x3A / 0x3C; 21 / 30: no case of their own)
        static const uint32_t kFn[5] = { 0x00469380, 0x0046F580, 0x0046F840, 0x004694A0, 0x0046A0F0 };
        static void (*const kTask[5])(Task&) = { TaskKomono20, TaskKomono58, TaskKomono60, TaskKomono21, TaskKomono30 };
        const int i = kind == 20 ? 0 : kind == 58 ? 1 : kind == 60 ? 2 : kind == 21 ? 3 : 4;
        GenericKomono* k = w.NewTask<GenericKomono>(kTask[i], kFn[i]);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        if (kind == 20) {
            k->radius = 7.0f;
            k->spin = 0x100;
        } else if (kind == 58) {
            k->radius = 3.0f;  // case 0x3A
        } else if (kind == 60) {
            k->radius = 2.0f;  // case 0x3C
        }
        return true;
    }
    if (kind == 8) {  // generic ctor 0x00461CF0 case 8: lamp + three pieces (0x005946B8 offsets, 0x005946E0 angles)
        Komono8Task* k = w.NewTask<Komono8Task>(TaskKomono8, 0x00467080);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->phase[0] = w.Rand(0x00461DDC) & 0xFFFF;
        k->phase[1] = w.Rand(0x00461DF5) & 0xFFFF;
        k->phase[2] = w.Rand(0x00461E0E) & 0xFFFF;
        for (int i = 0; i < 3; ++i) {
            LampPieceTask* p = w.NewTask<LampPieceTask>(TaskLampPiece, 0x00467290);
            p->area = static_cast<int8_t>(w.runner->variant);
            p->areaCount = 0;
            p->flags34 = 0x80000001u;
            p->idx = static_cast<int8_t>(i);
            p->lamp = k;
            const uint32_t o = 0x005946B8 + static_cast<uint32_t>(i) * 12, a = 0x005946E0 + static_cast<uint32_t>(i) * 6;
            for (int c = 0; c < 3; ++c) p->local[c] = F32(w.exe, o + 4 * static_cast<uint32_t>(c));
            p->rot[2] = w.exe.S16(a + 4);  // +0x1B4
            p->rot[1] = w.exe.S16(a + 2);  // +0x1B0
            p->rot[0] = w.exe.S16(a);      // +0x1AC
            p->radius = 2.5f;
        }
        return true;
    }
    if (kind == 1) {  // 0x00462F70
        Komono1Task* k = w.NewTask<Komono1Task>(TaskKomono1, 0x0046E3A0);
        k->sub = r.ParamS8(0);
        k->life = static_cast<int8_t>(r.state);
        k->model = w.exe.S16(0x00593DA4 + static_cast<uint32_t>(r.ParamS16(0)) * 2);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        if (w.cfg.eventMode == 2) k->flag = static_cast<uint8_t>(k->sub + 11);
        return true;
    }
    if (kind == 19) {  // generic ctor 0x00461CF0 case 0x13
        Komono19Task* k = w.NewTask<Komono19Task>(TaskKomono19, 0x00468F00);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->base, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->life = static_cast<int16_t>(r.state);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        w.runner->camera.routeBranch = k->life;  // 0x009C88A4
        k->radius = 1.5f;
        k->model = 0x10D3;
        ++w.attackers;
        return true;
    }
    if (kind == 12) {  // 0x00461CF0 case 0xC
        Komono12Task* k = w.NewTask<Komono12Task>(TaskKomono12, 0x00467E50);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->area = static_cast<int8_t>(w.runner->variant);
        k->areaCount = 0;
        k->flags34 = 0x80000001u;
        k->life = r.ParamS8(0);                     // +0x11C = +0x1F4
        k->model = static_cast<int16_t>(r.state);   // +0x28C = +0x11C
        return true;
    }
    if (kind == 14) {  // 0x00461CF0 case 0xE
        Komono14Task* k = w.NewTask<Komono14Task>(TaskKomono14, 0x00468180);
        memcpy(k->pos, r.pos, 12);
        memcpy(k->rot, r.rot, 12);
        k->state = static_cast<int16_t>(r.state);  // +0x11C (+0x28C = the same)
        k->flags34 = 0x80000001u;
        k->pos[1] = 36.0f;  // 0x42100000
        k->radius = 2.0f;
        // +0x199 = P byte 0 (not read by 0x00468180)
        ++w.liveEnemies;
        w.runner->camera.routeBranch = k->state;  // 0x009C88A4
        return true;
    }
    if (kind == 17) {  // 0x004630B0
        for (int i = 0; i < 3; ++i) {
            Komono17Task* k = w.NewTask<Komono17Task>(TaskKomono17, 0x00468D10);
            k->idx = static_cast<int16_t>(i);
            const double s = std::sin(static_cast<double>(static_cast<float>(static_cast<double>(i * 0x4000) * kToRad)));
            const float s5 = static_cast<float>(s * 5.0);  // qword 0x00565E10
            k->pos[0] = r.pos[0] + s5;
            k->pos[1] = static_cast<float>(s * 4.0) + r.pos[1];  // qword 0x004C4CA8
            k->pos[2] = r.pos[2] + s5;
            k->vy = Bits(0xBDCCCCCD);
            k->radius = 2.0f;
            k->flags34 = 0x80000001u;
        }
        return true;
    }
    return false;
}

}  // namespace game
