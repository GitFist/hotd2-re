// game::World - the how-to-play screen (game mode 9, 0x004996C0): camera path 0x1E over a floor model, the HUD
// (label, life torches, bullets, RELOAD prompt), captions, and four scripted zombies that the "player" shoots (shot
// flashes 0x00416E90, a hit flash 0x0049AA90). One script task 0x00499FE0 counts frames and switches the camera hold
// and the zombies' motion hold. No rand calls (the capture's rand state is constant through the mode).
// Spec and verification: notes/agents/frontend.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/world_internal.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

float Bits(uint32_t u) {  // a float immediate of the original code
    float f;
    memcpy(&f, &u, 4);
    return f;
}

SpriteDef Simple(int id, float x, float y, float sx = 1.0f, float sy = 1.0f) {  // Spr_DrawSimple 0x0041C6D0 (depth 1)
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = 1.0f;
    d.sx = sx;
    d.sy = sy;
    return d;
}

struct TutTextTask : WTask {  // 0x0049AD70 / 0x0049ADC0 / 0x0049AC40 / 0x0049AB80 (block 0x54)
    int counter = 0;          // +0x34
    int duration = 0;         // +0x38
    int id = 0;               // +0x3C
    float x = 0, y = 0;       // +0x40 / +0x44
    float sx = 1, sy = 1;     // +0x4C / +0x50
};

struct TutZombieTask : EnemyTask {  // 0x0049A210 / 0x0049A470 / 0x0049A680 / 0x0049A7F0, then 0x0049AFB0 (block 0x13F4)
    int clock = 0;                  // +0x1320
    int sink = 0;                   // +0x1330
    int savedYaw = 0;               // +0x1350
};

struct TutShotTask : WTask {  // 0x00416E90 (block 0x58)
    float pos[3] = {};        // +0x38 view space
    int pitch = 0, yaw = 0;   // +0x44 / +0x48
    int counter = 0;          // +0x50
    int player = 0;           // +0x54
};

struct TutFlashTask : WTask {  // 0x0049AA90 (block 0x54)
    int counter = 0;           // +0x34
};

}  // namespace

// 0x004996C0
void World::Impl::TutorialFrame() {
    if (subState == 0) {
        // 0x0041D350, Mem_Init, model slots, resources (stage 4 request, pol 0x16B, texture 0x80, models 0x11D7 /
        // 0x17FA), event mode 8 (no stage table) + 0x00412FD0, flush
        SoundStopAll();
        ts.Reset();
        resQueue.clear();
        residency = false;  // the mode's init loads its own models (Mdl_Draw residency is tracked in game mode 6)
        front.tutEnd = 0;
        front.tutClock = 0;
        front.tutPause = 0;
        front.tutHold = 0;
        front.tutLives = 3;
        front.tutBullets = 6;
        front.tutReload = 0;
        TutorialInit();  // Task_InitRoot(0x00499F90)
        ++subState;
        front.modeFrames = 0;
    } else if (subState != 1) {
        return;
    }
    // 0x004997A0 (also in the init frame)
    ts.RunFrame();
    ++front.modeFrames;
    if (front.modeFrames == 0x6A3 || front.tutEnd == 1) front.modeRequest = static_cast<int8_t>(exe.S16(0x004E1014 + 9) & 0xFF);
}

// root 0x00499F90
void World::Impl::TutorialInit() {
    NewTask<WTask>(WorldTramp<&Impl::TaskTutCamera>, 0x0049AE90);  // 0x0049AE50 (+ lights 0x004AA070: renderer)
    CameraReset();
    runner->camera.tick = 0;
    // 0x00499FB0
    NewTask<WTask>(WorldTramp<&Impl::TaskTutScript>, 0x00499FE0);
    {
        TutTextTask* t = NewTask<TutTextTask>(WorldTramp<&Impl::TaskTutLabel>, 0x0049AD70);  // 0x0049AD30
        t->id = 0x5B9;
        t->x = 20.0f;
        t->y = 420.0f;
    }
    {
        TutTextTask* t = NewTask<TutTextTask>(WorldTramp<&Impl::TaskTutReload>, 0x0049AC40);  // 0x0049AC10
        t->x = 24.0f;
        t->y = 260.0f;
    }
    {
        TutTextTask* t = NewTask<TutTextTask>(WorldTramp<&Impl::TaskTutTorches>, 0x0049AB80);  // 0x0049AB60
        t->id = 0x6C2;
    }
    NewTask<TutTextTask>(WorldTramp<&Impl::TaskTutBullets>, 0x0049AA00);  // 0x0049A9F0
    auto zombie = [&](uint32_t fn, int kind, int motion, float x, float z, int yaw) {  // 0x0049A190 / 0x0049A3F0 / ..
        TutZombieTask* e = NewTask<TutZombieTask>(WorldTramp<&Impl::TaskTutZombie>, fn);  // block cleared (0x004A73D0)
        e->anim.kind = static_cast<int16_t>(kind);  // +0x1F4
        e->anim.motion[0] = motion;                 // +0x1B4
        EnemyInitPose(*e);                          // Anim_InitEnemy 0x00410440
        e->anim.rotOrder = 1;                       // +0x1FC
        e->nodeCb = 0x00411050;                     // callbacks 0: plain node draw, no post-root step
        e->pos[0] = x;
        e->pos[1] = Bits(0x3FF33333);           // 1.9 (0x3FF33333)
        e->pos[2] = z;
        e->rot[0] = e->rot[2] = 0;
        e->rot[1] = yaw;
        e->clock = 0;
        return e;
    };
    zombie(0x0049A210, 7, 0x3E3, Bits(0xC0B00000), Bits(0xC3922666), 64000);  // 0x0049A190: (-5.5, -292.3)
    zombie(0x0049A470, 0x13, 0x312, Bits(0x40B33333), Bits(0xC38FA666), 0x500);  // 0x0049A3F0: (5.6, -287.3)
    CreatePlayerTasks();
    NewTask<WTask>(WorldTramp<&Impl::TaskTutFloor>, 0x0049AE20);  // 0x0049AE10
}

// 0x0049AE90: camera path 0x1E at the tick, look angles, views; the tick advances unless held (0x007DD154); past
// tick 0xE6 the mode ends (0x007DD164)
void World::Impl::TaskTutCamera(Task&) {
    CameraDirector& c = runner->camera;
    CameraContext& x = c.ctx[0];
    float pos[3], tg[3];
    int roll = 0;
    if (cameraBank.Evaluate(0x1E, static_cast<float>(c.tick), pos, tg, &roll, c.rollEnable != 0)) {
        memcpy(x.pos, pos, 12);
        memcpy(x.target, tg, 12);
    }
    ContextLookAngles(x, x.target, roll);
    BuildViews();
    if (front.tutHold == 0) {
        c.tick = static_cast<int>(static_cast<float>(c.tick) + 1.0f);  // __ftol(fild + 1.0f)
        if (0xE6 < c.tick) front.tutEnd = 1;
    }
}

// 0x00499FE0: the script (frame counter 0x007DD15C)
void World::Impl::TaskTutScript(Task&) {
    int& c = front.tutClock;
    if (c == 0x3C) {
        front.tutHold = 1;
        c = 0x3D;
        return;
    }
    if (c == 0x78) {
        TutCaption(0, 50.0f, 140.0f, 0x78);
        front.tutPause = 1;
    }
    if (c == 0xF0) {
        front.tutPause = 0;
        c = 0xF1;
        return;
    }
    if (c == 0x23F) {
        front.tutPause = 1;
        c = 0x240;
        return;
    }
    if (c == 0x2B7) {
        front.tutBullets = 6;
        front.tutReload = 2;
        Sound(0x3E16A9);
    }
    if (c == 0x32F) {
        front.tutPause = 0;
        c = 0x330;
        return;
    }
    if (c == 0x3FC) {
        TutCaption(2, 130.0f, 366.0f, 0x78);
        front.tutPause = 1;
    }
    if (c == 0x474) {
        front.tutPause = 0;
        c = 0x475;
        return;
    }
    auto zombie = [&](uint32_t fn, int kind, int motion, float x, float z, int yaw) {
        TutZombieTask* e = NewTask<TutZombieTask>(WorldTramp<&Impl::TaskTutZombie>, fn);
        e->anim.kind = static_cast<int16_t>(kind);
        e->anim.motion[0] = motion;
        EnemyInitPose(*e);
        e->anim.rotOrder = 1;
        e->nodeCb = 0x00411050;
        e->pos[0] = x;
        e->pos[1] = Bits(0x3FF33333);
        e->pos[2] = z;
        e->rot[0] = e->rot[2] = 0;
        e->rot[1] = yaw;
        e->clock = 0;
        return e;
    };
    if (c == 0x514) {
        front.tutHold = 0;
        zombie(0x0049A680, 7, 0x3FE, Bits(0x429A0000), Bits(0xC399199A), 0);  // 0x0049A600: (77, -306.2)
    }
    if (c == 0x541) {
        // 0x0049A760: skeleton 0x27, part list 0x00597BE4 (+0x1304 = anim +0x1170; 0x00412440: ids < 0x24 replace node
        // models, none here; id 0x2D is drawn on its node like 0x004124F0, see TaskTutZombie)
        zombie(0x0049A7F0, 0x27, 0x195, Bits(0x42990000), Bits(0xC3A7599A), 0x6000);  // (76.5, -334.7)
    }
    if (c == 0x550) {
        front.tutHold = 1;
        front.tutPause = 1;
        TutCaption(1, 60.0f, 190.0f, 0x78);
    }
    if (c == 0x5C8) {
        front.tutPause = 0;
        Sound(0x20000022);
    }
    if (c != 0x618) {
        if (c == 0x67C) {
            front.tutHold = 1;
            c = 0x67D;
            return;
        }
        if (c != 0x6B8) {
            ++c;
            return;
        }
    }
    front.tutHold = 0;
    ++c;
}

// 0x0049A8C0(kind, x, y, duration): a caption task 0x0049ADC0
void World::Impl::TutCaption(int kind, float x, float y, int duration) {
    TutTextTask* t = NewTask<TutTextTask>(WorldTramp<&Impl::TaskTutLabel>, 0x0049ADC0);
    t->x = x;
    t->duration = duration;
    t->sx = t->sy = 1.0f;
    t->y = y;
    t->counter = 0;
    const bool gun = player[0].inputKind == 1 || player[1].inputKind == 1;     // 0x009A5D85 / 0x009A5EB5
    const bool dev = player[0].deviceKind == 1 || player[1].deviceKind == 1;   // 0x009A5D84 / 0x009A5EB4
    switch (kind) {
    case 0: t->id = 0xA69; break;
    case 1: t->id = 0xA6A; break;
    case 2: t->id = 0xA6B; break;
    case 3: t->id = gun ? 0xA6C : (!dev ? 0xA6E : 0xA72); break;
    case 4: t->id = gun ? 0xA6F : (!dev ? 0xA71 : 0xA73); break;
    default: break;
    }
}

// 0x0049AD70 (label: ends with the mode, 0x007DD164) / 0x0049ADC0 (caption: ends after its duration)
void World::Impl::TaskTutLabel(Task& t) {
    TutTextTask& s = t.As<TutTextTask>();
    hudSprites.push_back(Simple(s.id, s.x, s.y, s.sx, s.sy));
    ++s.counter;
    if (s.fn == 0x0049AD70 ? front.tutEnd != 0 : s.duration < s.counter) ts.KillCurrent();
}

// 0x0049AC40: RELOAD prompt while 0x007DD158 == 1 (sound 0x115A9 at its start; the hint 0xA29 (gun) / 0xA28 from 120)
void World::Impl::TaskTutReload(Task& t) {
    TutTextTask& s = t.As<TutTextTask>();
    if (front.tutReload == 1) {
        if (s.counter % 0x3C < 0x2D) hudSprites.push_back(Simple(0xA27, s.x, s.y, 1.5f, 1.0f));
        if (s.counter == 0) Sound(0x115A9);
        const bool gun = player[0].inputKind == 1;  // 0x009A5D85
        if (0x77 < s.counter && s.counter % 0x3C < 0x2D)
            hudSprites.push_back(Simple(gun ? 0xA29 : 0xA28, s.x - F32(exe, 0x004C43A0), s.y + F32(exe, 0x00569130)));
        if (gun && s.counter == 0x78) Sound(0x215A9);
        ++s.counter;
    } else if (front.tutReload == 2) {
        ts.KillCurrent();
    }
}

// 0x0049AB80: life torches (0x007DD160 of them)
void World::Impl::TaskTutTorches(Task& t) {
    TutTextTask& s = t.As<TutTextTask>();
    for (int i = 0; i < front.tutLives; ++i)
        hudSprites.push_back(Simple(((s.counter + i * 4) / 3) % 7 + s.id, static_cast<float>(0x34 + 0x20 * i), 420.0f));
    ++s.counter;
    if (front.tutEnd != 0) ts.KillCurrent();
}

// 0x0049AA00: bullets (0x007DD150 of them)
void World::Impl::TaskTutBullets(Task&) {
    for (int i = 0; i < front.tutBullets; ++i) hudSprites.push_back(Simple(0xA74, static_cast<float>(0x10 + 0x18 * i), 372.0f));
}

// 0x0049AE20: the floor model 0x17FA at the origin
void World::Impl::TaskTutFloor(Task&) { Draw(0x17FA, Mat4::Identity()); }

// 0x0049AF10(obj, node, 0): two shot sounds and a flash task 0x00416E90 at the node's view-space hit point projected
// to depth 1
static void TutShot(World::Impl& w, EnemyTask& e, int node) {
    w.Sound(0x3416A9);
    w.Sound(0x216A9);

    TutShotTask* s = w.NewTask<TutShotTask>(WorldTramp<&World::Impl::TaskTutShot>, 0x00416E90);
    const EnemyNode& n = e.anim.nodes[node & 31];
    const float k = F32(w.exe, 0x004C4C64);  // -1
    s->pos[0] = (n.hit[0] / n.hit[2]) * k;
    s->pos[2] = -1.0f;
    s->pos[1] = (n.hit[1] / n.hit[2]) * k;
    mx::DirToAngles(s->pos[0], s->pos[1], -1.0f, &s->pitch, &s->yaw);  // 0x004016B0
    s->yaw += 0x8000;
    s->counter = 0;
    s->player = 0;
    s->pitch = -s->pitch;
}

// 0x00416E90: the shot flash (view space, models 0x00579F78[p] / 0x00579F7C[p] + counter), 10 frames
void World::Impl::TaskTutShot(Task& t) {
    TutShotTask& s = t.As<TutShotTask>();
    const int p = s.player & 1;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::RotY(m, s.yaw);
    mx::RotX(m, s.pitch);
    mx::Scale(m, 0.1f, 0.1f, 0.1f);
    Draw(exe.S16(0x00579F78 + 2 * static_cast<uint32_t>(p)) + s.counter, m, 8, -1.0f, false, -1, true);
    mx::Scale(m, 0.5f, 0.5f, 0.5f);
    Draw(exe.S16(0x00579F7C + 2 * static_cast<uint32_t>(p)) + s.counter, m, 8, -1.0f, false, -1, true);
    if (8 < s.counter++ || player[p].pending != 0) ts.KillCurrent();
}

// 0x0049AA60: the hit flash task 0x0049AA90 (sounds 0x316A9 / 0x1EA9), 0x30 frames
static void TutFlash(World::Impl& w) {
    TutFlashTask* f = w.NewTask<TutFlashTask>(WorldTramp<&World::Impl::TaskTutFlash>, 0x0049AA90);
    w.Sound(0x316A9);
    w.Sound(0x1EA9);
    f->counter = 0x30;
}

// 0x0049AA90: the red flash model 0x939 in front of the camera (pass-1 mode 0xC) and the view shake 0x009CA0E4
void World::Impl::TaskTutFlash(Task& t) {
    TutFlashTask& f = t.As<TutFlashTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, 0.0f, 0.0f, -1.0f);
    const float s = Bits(0x3CA3D70A);  // 0.02
    mx::Scale(m, s, s, s);
    Draw(0x939, m, 0xC, -1.0f, false, -1, true);
    if (0 < --f.counter) {
        // as Player_HitShake 0x00415270: fild(n * 0x1800) * double 0x004C4370; fcos; * (fild n * float 0x004EC8CC)
        double k2pi = 0;
        exe.Read(0x004C4370, &k2pi, 8);
        const float a = static_cast<float>(static_cast<double>(f.counter * 0x1800) * k2pi);
        const float k = static_cast<float>(f.counter) * F32(exe, 0x004EC8CC);
        shake = static_cast<int>(static_cast<float>(std::cos(static_cast<double>(a)) * static_cast<double>(k)));
        return;
    }
    f.counter = 0;
    shake = 0;
    ts.KillCurrent();
}

// The zombies (scripted by their own frame counter +0x1320, held while 0x007DD16C) and their sinking death 0x0049AFB0
void World::Impl::TaskTutZombie(Task& t) {
    TutZombieTask& e = t.As<TutZombieTask>();
    auto blend = [&](int motion, int n) { AnimBlendTo(e, motion, 0, n); };  // 0x004119A0
    auto shot = [&](int node) {
        --front.tutBullets;
        TutShot(*this, e, node);
    };
    auto dying = [&](int at) {
        if (e.clock == at) {
            e.sub = 0;
            e.fn = 0x0049AFB0;
        }
    };
    const int c = e.clock;
    bool advance = true;
    switch (e.fn) {
    case 0x0049A210:
        if (c == 0x3C) blend(0x3FE, 10);
        if (c == 0x96) { --front.tutBullets; blend(0x3D1, 5); TutShot(*this, e, 2); }
        if (c == 0xA0) { shot(1); blend(0x3D6, 5); }
        if (c == 0xAA) { shot(2); blend(0x3D1, 5); }
        if (c == 0xC3) { shot(6); blend(0x3D3, 5); }
        if (c == 0xCD) { shot(1); blend(0x3D6, 5); }
        if (c == 0xD7) {
            Sound(0x17A9);
            --front.tutBullets;
            front.tutReload = 1;
            TutShot(*this, e, 1);
            blend(0x3DC, 5);
        }
        dying(MotionTicks(0x3DC) + 0xD6);
        break;
    case 0x0049A470:
        if (c == 0x1C8) blend(0x318, 10);
        if (c == 0x21C) blend(0x30C, 10);
        if (c == 0x262) {
            TutFlash(*this);
            front.tutLives = 2;
        }
        if (c == 0x26C) blend(0x3E3, 10);
        if (c == 0x2A8) { shot(2); blend(0x3D1, 5); }
        if (c == 0x2B2) { shot(1); blend(0x3D6, 5); }
        if (c == 0x2D0) {
            Sound(0x17A9);
            shot(2);
            blend(0x3DC, 5);
        }
        dying(MotionTicks(0x3DC) + 0x2CF);
        break;
    case 0x0049A680:
        if (c == 0x50) { shot(6); blend(0x3D3, 5); }
        if (c == 0x5A) {
            Sound(0x17A9);
            shot(6);
            blend(0x3DF, 5);
        }
        dying(MotionTicks(0x3DF) + 0x59);
        break;
    case 0x0049A7F0: {
        const int t195 = MotionTicks(0x195);
        if (c == t195 - 1) blend(0x197, 0x1E);
        if (t195 - 1 <= c && c <= t195 + 0x1D) e.rot[1] -= 0x111;
        if (c == MotionTicks(0x197) - 2 + t195) blend(0x284, 0x1E);
        break;
    }
    case 0x0049AFB0:  // the death: 0x78 frames sinking 0.04 per frame, then the actor goes (0x004124B0, 0x00409CC0)
        advance = false;
        if (e.sub == 0) {
            e.sink = 0x78;
            e.savedYaw = e.rot[1];
            e.sub = 1;
        } else if (e.sub == 1) {
            e.pos[1] = e.pos[1] - F32(exe, 0x004C4D04);
            if (--e.sink <= 0) {
                EnemyDestroy(e);  // Actor_Destroy (kills the task)
                return;
            }
        }
        break;
    default: break;
    }
    // 0x0041DC70 (light: renderer), 0x00411090: skeleton + shadow 0x0040A590, 0x0041DCC0
    AnimDraw(e);
    if (e.fn == 0x0049A7F0 || (e.fn == 0x0049AFB0 && e.anim.kind == 0x27)) {
        // parts of 0x00597BE4 with id >= 0x24 on their node (0x004124F0 at the end of the skeleton draw)
        for (uint32_t a = 0x00597BE4;; a += 2) {
            const int16_t id = static_cast<int16_t>(exe.S16(a));
            if (id < 0) break;
            if (id < 0x24) continue;
            const uint32_t def = exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id));
            const int node = static_cast<int>(exe.U32(def)) & 15;
            Draw(static_cast<int>(exe.U32(def + 4)), e.anim.nodes[node].world, 8, -1.0f, (e.flags38 & 8) != 0, node);
        }
    }
    EnemyShadow(e);
    if (advance && front.tutPause == 0) {
        ++e.anim.counter[0];
        ++e.clock;
    }
}

}  // namespace game
