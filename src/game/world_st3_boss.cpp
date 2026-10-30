// game::World - the stage-3 boss (actor type 0x45, scene 0x0B: five heads rising from the water) and its tasks:
//   0x0041FC00 actor type function (boss block 0x007DC6F0.. init, rand interval) -> by +0x130C:
//     0: 0x0041FDB0 -> 0x00423050  intro head following the body (skeleton 0x49)
//     1: 0x004200F0 -> 0x00420550  intro body (skeleton 0x34), dives at event flag 3
//     2: 0x0041FE30 -> 0x004209B0  the five heads (skeleton 0x49, central head 2: 0x48), 0x77C4-byte block +0x1390
//     3: 0x00420180 -> 0x00420820  rocks the outer heads break through
//   name card 0x00424900, hit spark 0x004247D0 -> 0x004246F0, health bar 0x00435E50 (world_st1b_boss.cpp).
// Mode 0x007DC6F1 (0: scene 0x0B = stage 3, 1: scene 0x0D, 2: scene 2) selects the variant; only mode 0 is verified.
// notes/agents/stage3b.md. CONFIRMED = frame-exact on build\scene_capture_auto_st3.txt, CODE = read, not exercised.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/mstack.h"
#include "game/typing_boss_rules.h"
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

SpriteDef Sprite2D(int id, float x, float y, float depth, float sx, float sy, uint32_t flags, float alpha) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = 0;
    d.alpha = alpha;
    d.blend = -1;
    d.flags = flags;
    return d;
}

}  // namespace

// One 0x45 actor (all variants share the 0x13F4-byte enemy object; the head block +0x1390 is inline here)
struct HydraTask : EnemyTask {
    float f1340 = 0;          // +0x1340
    int8_t index = 0;         // +0x131B head / rock index (record s16 +0x22)
    // head block (+0x1390 -> 0x77C4 bytes)
    int ax[27] = {}, ay[27] = {}, az[27] = {};  // +0x00 / +0x6C / +0xD8 extra node angles (x, y, z)
    int8_t nodes = 0;         // +0x597 node count
    int8_t head = 0;          // +0x598 head node (the weak point)
    int8_t jawA = 0, jawB = 0;  // +0x599 / +0x59A (jaw nodes: both children of the head node)
    int8_t idleCount = 0;     // +0x59B idle motions
    int8_t attackCount = 0;   // +0x59C attacks
    int8_t side = 0;          // +0x59D (outer heads: 0 / 1 motion sets)
    int32_t zsum = 0;         // +0x5A4 sum of the z angles up to the head node (attack)
    uint32_t idleTab = 0;     // +0x7634 s16 motions
    uint32_t attackTab = 0;   // +0x7628 {s16 motion, s16 hit tick}
    int16_t flinch = 0;       // +0x7638
    int16_t death = 0;        // +0x763A
    int16_t hitTick = 0;      // +0x763C
    int16_t breath = 0;       // +0x7640
    int16_t introTimer = 0;   // +0x7648
    int16_t ready = -1;       // +0x764A (0: picked to attack, -1: idle)
    int32_t wobble = 0;       // +0x77B4 (mode 2)
    Mat4 mv[27];              // node model-view matrices of this frame's 0x00421F20 (drawn as such)
    bool mvValid = false;
    // variant 5 (0x00420360 / 0x004231C0, +0x131B = 8): the big head swimming along a recorded path
    float px[28] = {}, py[28] = {}, pz[28] = {};  // +0x2F4 / +0x360 / +0x3CC node start points (yaw walk)
    float qx[28] = {}, qy[28] = {}, qz[28] = {};  // +0x438 / +0x4A4 / +0x510 node start points (pitch walk)
    int8_t pass = 0;          // +0x59E pass (0..6), event table row
    int8_t hits = 0;          // +0x59F damaging hits of this rise
    int8_t splashed = 0;      // +0x5A0
    int8_t segCount = 0;      // +0x5A1 last path / camera segment
    std::vector<std::array<float, 3>> path;  // +0x5A8 path points (12 bytes each, up to 0x960)
    int16_t cursor = 0;       // +0x762C path point of the body
    int16_t headCursor = 0;   // +0x762E path point reached by the head node's walk
    int16_t pathLen = 0;      // +0x7630
    int16_t delay = 0;        // +0x763E
    int16_t hold = 0;         // +0x7642 camera hold after a bite
    int16_t camSeg = 0, camTick = 0;  // +0x7644 / +0x7646 held camera segment / tick
    int16_t laps = 0;         // +0x764C
    int zprev[28] = {}, yprev[28] = {};  // +0x7650 / +0x76BC
    float start[2] = {};      // +0x7794 / +0x779C lunge start x / z
    int phase = 0, phaseStep = 0;  // +0x77AC / +0x77B0 (death bob)
    uint32_t segTab = 0, camTab = 0, evTab = 0;  // +0x77B8 / +0x77BC / +0x77C0
    float amp = 0;            // +0x1344 death bob amplitude
};

struct HydraState {
    int8_t readyCount = 0;     // 0x007DC6F0
    int8_t mode = 0;           // 0x007DC6F1
    HydraTask* heads[5] = {};  // 0x007DC6F4 (index 0 is also the intro head, variant 0)
    uint32_t headSerial[5] = {};
    int8_t attacker = 0;       // 0x007DC71C
    int16_t countdown = 0;     // 0x007DC71E
    int16_t total = 0;         // 0x007DC720 hit points of the outer heads
    int8_t alive = 0;          // 0x007DC728
    HydraTask* body[2] = {};   // 0x007DC72C (variant 1, then the rocks by index)
    uint32_t bodySerial[2] = {};
    int8_t phase = 0;          // 0x007DC738 (0 intro, 1 fight, 2 dying, 3 dead)
    float lock[3] = {};        // 0x007DC7E0 smoothed lock-on point
    std::vector<std::array<float, 3>> water;  // model 0x1850's inline vertices as edited in memory (0x00424C10)
    bool waterLoaded = false;
    float bodyPos[3] = {};     // 0x007DC7F0
    int bodyYaw = 0;           // 0x007DC800
    int8_t level = 0;          // 0x007DC80C (attack interval row, 0..15)
    uint32_t counter = 0;      // 0x00811200 pseudo-random counter
    float card[8][5] = {};     // 0x007DC740 name card letters {x, y, z, angle (int bits), scale}
    TypingQuizChallenge quiz;
    enum class QuizPhase { Setup, Reveal, Answers, Cooldown, Attack };
    QuizPhase quizPhase = QuizPhase::Setup;
    uint32_t quizToken = 0;
    unsigned quizFrames = 0;
};

namespace {

HydraState& H(World::Impl& w) {
    if (!w.hydra) w.hydra = std::make_shared<HydraState>();
    return *w.hydra;
}

HydraTask* Alive(World::Impl& w, HydraTask* p, uint32_t serial) {
    if (!p) return nullptr;
    for (Task* t : w.ts.Live())
        if (t->serial == serial) return t->data.get() == p ? p : nullptr;
    return nullptr;
}

HydraTask* Head(World::Impl& w, int i) {
    HydraState& h = H(w);
    HydraTask* p = Alive(w, h.heads[i], h.headSerial[i]);
    if (!p && h.heads[i]) w.Note("stage-3 boss: freed head object read (0x007DC6F4)");
    return p;
}

// Mode 1 (the second swim, boss mode's stage-3 scene 17): 0x00424FE0 -> task 0x00424E10, the wake marks along the
// path rows 0x00589090 {s16 from, s16 to, u8 next, f32 x, y, z} (models 0x1987.. cycling, fading out over 20 points,
// sound 0x2916A9 every 8 points); removed when the head reaches behaviour 0xE
struct HydraMarks : WTask {
    int row = 0, frame = 0;  // +0x1334 / +0x1338
};

// 0x00424D90 -> task 0x00424C10: the water patch (model 0x1850) and ten pieces 0x183E.. at fixed points; removed by
// event flag 4. (Its vertex lift of the water mesh 0x009BEBA4 around the head is renderer data: not modelled.)
struct HydraWater : WTask {
    int done = 0;  // +0x38
};

void TaskHydraMarks(Task& t);
void TaskHydraWater(Task& t);

HydraTask* Body(World::Impl& w, int i) {
    HydraState& h = H(w);
    HydraTask* p = Alive(w, h.body[i], h.bodySerial[i]);
    if (!p && h.body[i]) w.Note("stage-3 boss: freed body object read (0x007DC72C)");
    return p;
}

// 0x00424E10
void TaskHydraMarks(Task& t) {
    World::Impl& w = *CurrentWorld();
    HydraMarks& k = t.As<HydraMarks>();
    const ExeImage& exe = w.exe;
    HydraTask* hd = Head(w, 0);
    if (!hd) return;
    if (hd->behaviour == 0xE) {
        w.ts.KillCurrent();  // Task_Kill 0x004A7040
        return;
    }
    for (int i = 0; i < 2; ++i) {
        uint8_t next = 0;
        exe.Read(0x00589094 + static_cast<uint32_t>(k.row) * 0x14, &next, 1);
        if (hd->laps == 0 && next == 1) ++k.row;
    }
    uint32_t r = 0x00589090 + static_cast<uint32_t>(k.row) * 0x14;
    const int from = exe.S16(r), to = exe.S16(r + 2), c = hd->cursor;
    if (!(from <= c && c <= to + 0x14)) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, F32(exe, r + 8), F32(exe, r + 0xC), F32(exe, r + 0x10));
    mx::RotY(m, w.runner->camera.ctx[0].yaw);  // 0x009A60D0
    mx::Scale(m, 2.0f, 2.0f, 2.0f);
    if (to < c) w.Draw(k.frame + 0x1987, m, 8, static_cast<float>((to - c) + 0x14) * F32(exe, 0x0055CBB8));  // 0x004185A0
    else w.Draw(k.frame + 0x1987, m);
    if (0xE < ++k.frame) k.frame = 0;
    if (c % 8 == 0) w.Sound(0x2916A9);
    r = 0x00589090 + static_cast<uint32_t>(k.row) * 0x14;
    if (exe.S16(r + 2) + 0x14 <= c && 3 < ++k.row) k.row = 0;
}

// 0x00424C10: the water mesh 0x1850 rises around head 0 while its body point is below 0x0055CBB4 (an in-memory edit of
// the loaded model: every inline vertex within 0x004C43A4 of the point is lifted to sin(ftol((r - d) * 0x0055CBB0)) *
// 0x0055CBA8 - 0x0055CBA0 when that is higher; 24-bit x87, fsin unrounded); kept until the model is reloaded
void HydraWaterLift(World::Impl& w, const HydraTask& hd) {
    HydraState& h = H(w);
    if (!h.waterLoaded) {
        for (const auto& r : w.InlineRecords(0x1850)) {
            std::array<float, 3> p;
            memcpy(p.data(), r.data(), 12);
            h.water.push_back(p);
        }
        h.waterLoaded = true;
    }
    const ExeImage& exe = w.exe;
    if (!(hd.body[1] < F32(exe, 0x0055CBB4))) return;
    const float r = F32(exe, 0x004C43A4), sc = F32(exe, 0x0055CBB0);
    const double amp = F64(exe, 0x0055CBA8), base = F64(exe, 0x0055CBA0);
    bool changed = false;
    for (auto& v : h.water) {
        const float dz = v[2] - hd.body[2], dx = v[0] - hd.body[0];
        const float d = std::sqrt(dx * dx + dz * dz);
        if (!(d < r)) continue;
        const int n = static_cast<int>((r - d) * sc);  // _ftol
        const float ang = static_cast<float>(static_cast<double>(n) * mx::kToRad);
        const float p = static_cast<float>(std::sin(static_cast<double>(ang)) * amp);
        const float y = static_cast<float>(static_cast<double>(p) - base);
        if (y > v[1]) {
            v[1] = y;
            changed = true;
        }
    }
    if (!changed && w.modelEdits.count(0x1850)) return;
    auto e = std::make_shared<ModelEdit>();
    for (size_t i = 0; i < h.water.size(); ++i) {
        ModelEdit::Vertex mv;
        mv.index = static_cast<int>(i);
        memcpy(mv.pos, h.water[i].data(), 12);
        e->vertices.push_back(mv);
    }
    w.modelEdits[0x1850] = std::move(e);
}

// 0x00424C10
void TaskHydraWater(Task& t) {
    World::Impl& w = *CurrentWorld();
    HydraWater& k = t.As<HydraWater>();
    if (w.runner->flags[4] == 1) {
        w.ts.KillCurrent();
        return;
    }
    HydraTask* hd = Head(w, 0);
    if (hd && hd->behaviour == 0xE) k.done = 1;
    if (hd && k.done == 0) HydraWaterLift(w, *hd);
    w.Draw(0x1850, Mat4::Identity());
    static const float kPts[10][2] = { { 7.0f, -24.5f }, { 15.0f, 2.0f }, { 0.0f, 0.0f }, { 55.0f, -0.5f }, { 0.0f, 0.0f },
                                       { 0.0f, 0.0f }, { 0.0f, 0.0f }, { -2.5f, -20.0f }, { -10.0f, 10.0f }, { 0.0f, 0.0f } };
    for (int i = 0; i < 10; ++i) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, kPts[i][0], 0.0f, kPts[i][1]);
        w.Draw(i + 0x183E, m);
    }
}

// 0x00421910(n): ++0x00811200 % (n + 1)
int Ctr(World::Impl& w, int n) {
    HydraState& h = H(w);
    ++h.counter;
    return static_cast<int>(h.counter % static_cast<uint32_t>(n + 1));
}

// 0x004207D0(n): boss voice / roar table by event mode
void Roar(World::Impl& w, int n) {
    const uint32_t t = w.cfg.eventMode == 2 ? 0x00588EA4 : 0x00588EC0;
    w.Sound(w.exe.U32(t + 4 * static_cast<uint32_t>(n)));
}

// attack interval 0x005890E0[level] = {s16 base, s16 range}: rand % (range + 1) + base
int16_t Interval(World::Impl& w, uint32_t site) {
    HydraState& h = H(w);
    const uint32_t row = 0x005890E0 + static_cast<uint32_t>(h.level) * 4;
    const int base = w.exe.S16(row), range = w.exe.S16(row + 2);
    const int r = w.Rand(site);
    return static_cast<int16_t>(r % (range + 1) + base);
}

void NoPostRoot(World::Impl&, EnemyTask&) {}  // +0x12F0 = 0x00420810
void NoNodeDraw(World::Impl&, EnemyTask&, int, const Mat4&) {}  // +0x12EC = 0x00420810 (heads draw in 0x004219E0)

// Actor_ReleaseSlot 0x004092D0 + task end 0x004A7040
void ReleaseAndKill(World::Impl& w, HydraTask& a) {
    if (a.actorSlot >= 0 && a.actorSlot < 14) {
        w.actorSlots[a.actorSlot] = nullptr;
        a.actorSlot = -1;
    }
    w.ts.KillCurrent();
}

// 0x00411090 with the boss light 0x0041DC70 / 0x0041DCC0 (renderer): skeleton + floor shadow
void DrawSkeleton(World::Impl& w, HydraTask& a) {
    w.AnimDraw(a);
    w.EnemyShadow(a);
}

// Common object setup of 0x0040A8A0-style ctors: Anim_InitEnemy 0x00410440 (+ actor slot)
void InitObject(World::Impl& w, HydraTask& a, int kind, int motion) {
    a.anim.kind = static_cast<int16_t>(kind);
    a.anim.motion[0] = motion;
    w.EnemyInitPose(a);
    a.postRootFn = NoPostRoot;
    a.nodeCb = 0x00411050;
    a.anim.rotOrder = 5;
}

// ---------------------------------------------------------------------------------------------------------------------
// Variant 0: 0x0041FDB0 -> 0x00423050
// ---------------------------------------------------------------------------------------------------------------------
void TaskIntroHead(Task& t) {  // 0x00423050
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    h.heads[0] = &a;
    h.headSerial[0] = t.serial;
    DrawSkeleton(w, a);
    HydraTask* b = Body(w, 0);
    const int bs = b ? b->behaviour : 0;
    if (bs == 2) {
        if (h.mode == 0) {
            Mat4 m = Mat4::Identity();
            mx::RotY(m, 0x6400);
            const float in[3] = { 0.0f, 3.0f, 55.5f };
            float c[3];
            mx::Transform(m, in, c);
            a.f1340 = a.f1340 * F32(w.exe, 0x0055CB40);
            a.pos[0] = c[0] + a.f1340 + h.bodyPos[0];
            a.pos[1] = c[1] + h.bodyPos[1];
            a.pos[2] = c[2] + h.bodyPos[2];
            a.rot[1] = 0x5400;
        }
        ++a.anim.counter[0];
    } else if (bs == 3) {
        if (h.mode == 0) {
            const double ang = F64(w.exe, 0x0055CB08), k = F64(w.exe, 0x0055CB00);
            a.pos[0] = static_cast<float>(std::sin(ang) * k) + a.pos[0];
            const float z = static_cast<float>(std::cos(ang) * k) - static_cast<float>(F64(w.exe, 0x0055CAF8));
            a.pos[2] = z + a.pos[2];
        } else {
            a.pos[0] = a.pos[0] - F32(w.exe, 0x004C49C0);
        }
        if (0x27 < a.counter1330++) {
            ReleaseAndKill(w, a);
            return;
        }
    }
}

void TaskIntroHeadCtor(Task& t) {  // 0x0041FDB0
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    h.heads[0] = &a;
    h.headSerial[0] = t.serial;
    InitObject(w, a, 0x49, w.runner->scene == 0xB ? 0x5A : 0x5B);
    a.flags34 |= 0x88000;
    a.anim.counter[0] = 0;
    a.f1340 = 15.0f;
    a.fn = 0x00423050;
    t.fn = TaskIntroHead;
}

// ---------------------------------------------------------------------------------------------------------------------
// Variant 1: 0x004200F0 -> 0x00420550
// ---------------------------------------------------------------------------------------------------------------------
void TaskIntroBody(Task& t) {  // 0x00420550
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    h.body[0] = &a;
    h.bodySerial[0] = t.serial;
    if (0 < a.behaviour) DrawSkeleton(w, a);
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, a.pos[0], a.pos[1] + F32(w.exe, 0x004C4D10), a.pos[2]);
        mx::Scale(m, 10.0f, 1.0f, 10.0f);
        w.Draw(0x10D0, m);
    }
    bool resetCounter = false;
    switch (a.behaviour) {
    case 0:
        if (w.runner->flags[3] == 1) a.behaviour = 1;
        resetCounter = true;
        break;
    case 1: {
        ++a.anim.counter[0];
        const int c = a.counter1330++;
        if (h.mode == 0) {
            if (0x6C < c) {
                Roar(w, 4);
                a.behaviour = 2;
                a.counter1330 = 0;
            }
            a.rot[1] += 0x75;
            break;
        }
        if (c < 0x46) break;
        Roar(w, 4);
        a.behaviour = 2;
        resetCounter = true;
        break;
    }
    case 2: {
        ++a.anim.counter[0];
        const int c = a.counter1330++;
        if (h.mode == 0) {
            if (c != 0x36) break;
            w.AnimSetMotion(a, 0x24F);
            a.behaviour = 3;
            a.rot[1] = 0x5400;
        } else {
            if (c != 0x1D) break;
            w.AnimBlendTo(a, 0x264, 0x11, 8);
            a.behaviour = 3;
        }
        w.Sound(0x20000015);
        resetCounter = true;  // (0x00420810(4, 0x14): no-op)
        break;
    }
    case 3: {
        if (h.mode == 0) {
            const double ang = F64(w.exe, 0x0055CB08), k = F64(w.exe, 0x0055CB00);
            a.pos[0] = static_cast<float>(std::sin(ang) * k) + a.pos[0];
            const float z = static_cast<float>(std::cos(ang) * k) - static_cast<float>(F64(w.exe, 0x0055CAF8));
            a.pos[2] = z + a.pos[2];
        } else {
            a.pos[0] = a.pos[0] - F32(w.exe, 0x004C49C0);
        }
        if (0x27 < a.counter1330++) {
            ReleaseAndKill(w, a);
            return;
        }
        break;
    }
    default:
        break;
    }
    if (resetCounter) a.counter1330 = 0;
    memcpy(h.bodyPos, a.pos, 12);
}

void TaskIntroBodyCtor(Task& t) {  // 0x004200F0
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    h.body[0] = &a;
    h.bodySerial[0] = t.serial;
    InitObject(w, a, a.anim.kind, 0x23D);
    a.nodeCb = 0x004208F0;  // the node model (scaled in game type 1 with item 0xC)
    a.anim.drawFlags |= 4;
    a.flags34 |= 0x8000;
    a.anim.counter[0] = 0;
    memcpy(h.bodyPos, a.pos, 12);
    h.bodyYaw = a.rot[1];
    a.fn = 0x00420550;
    t.fn = TaskIntroBody;
}

// ---------------------------------------------------------------------------------------------------------------------
// Variant 3: 0x00420180 -> 0x00420820 (rocks)
// ---------------------------------------------------------------------------------------------------------------------
void TaskRock(Task& t) {  // 0x00420820
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    const int i = a.index & 1;
    h.body[i] = &a;
    h.bodySerial[i] = t.serial;
    DrawSkeleton(w, a);
    const int s = a.behaviour;
    if (s < 0) return;
    if (s < 2) {
        ++a.anim.counter[0];
    } else if (s == 2) {
        if (0xF0 < ++a.counter1330) {
            ReleaseAndKill(w, a);
            return;
        }
        a.vel[0] = a.vel[0] + F32(w.exe, 0x004E30E0);
        a.vel[1] = a.vel[1] - F32(w.exe, 0x0055CB10);
        a.vel[2] = a.vel[2] - F32(w.exe, 0x004C4C88);
        a.pos[0] = a.vel[0] + a.pos[0];
        a.pos[1] = a.vel[1] + a.pos[1];
    }
}

// modes 1 / 2: pos = translation of Identity . Translate(pos) . RotY(0x1200) . Translate(offset)
void PlaceRock(HydraTask& a, uint32_t ox, uint32_t oy, uint32_t oz) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotY(m, 0x1200);
    mx::Translate(m, Bits(ox), Bits(oy), Bits(oz));
    mx::GetTranslation(m, a.pos);
}

void TaskRockCtor(Task& t) {  // 0x00420180
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    a.index = static_cast<int8_t>(a.hp);
    const int i = a.index & 1;
    h.body[i] = &a;
    h.bodySerial[i] = t.serial;
    a.hp = 1;
    InitObject(w, a, a.anim.kind, 0x21E);
    a.anim.drawFlags &= ~2u;
    a.flags34 |= 0x88000;
    if (a.index != 0) {
        if (h.mode == 0) {
            a.pos[0] = a.pos[0] + F32(w.exe, 0x004C4D10);
            a.pos[1] = a.pos[1] + F32(w.exe, 0x0055CAF0);
            a.pos[2] = a.pos[2] + F32(w.exe, 0x0055CADC);
        } else {
            PlaceRock(a, 0x3E99999A, 0x400CCCCD, 0x408CCCCD);  // (0.3, 2.2, 4.4) on the yaw 0x1200
        }
        a.anim.counter[0] = 0x1E;
    } else {
        if (h.mode == 0) {
            a.pos[0] = a.pos[0] - F32(w.exe, 0x0055CAF0);
            a.pos[1] = static_cast<float>(a.pos[1] + F64(w.exe, 0x0055CAE8));
            a.pos[2] = static_cast<float>(a.pos[2] + F64(w.exe, 0x0055CAE0));
        } else {
            PlaceRock(a, 0xC00CCCCD, 0x3FE66666, 0x4089999A);  // (-2.2, 1.8, 4.3) on the yaw 0x1200
        }
    }
    a.fn = 0x00420820;
    t.fn = TaskRock;
}

// ---------------------------------------------------------------------------------------------------------------------
// Name card 0x00424900 (created by head 0 at event flag 1): 8 letters (models 0x00589120) turn in and fly off, the two
// title sprites 0xBC / 0xCA from tick 0x50, 300 frames
// ---------------------------------------------------------------------------------------------------------------------
struct CardTask : WTask {
    int16_t state = 0;  // +0x1310
    int tick = 0;       // +0x1320
};

void TaskCard(Task& t) {
    World::Impl& w = *CurrentWorld();
    CardTask& k = t.As<CardTask>();
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    auto letter = [&](int i) {
        float* l = h.card[i];
        int ang;
        memcpy(&ang, &l[3], 4);
        Mat4 m = Mat4::Identity();  // view space (identity on the stack)
        mx::Translate(m, l[0], l[1], l[2]);
        mx::RotY(m, ang);
        mx::Scale(m, l[4], l[4], l[4]);
        const int model = w.typingReferenceActive ? w.typingReference.SourceModel(i == 0 ? 0x81E : i == 6 ? 0x1905 : 0x81F)
                                                 : static_cast<int>(exe.U32(0x00589120 + 4 * static_cast<uint32_t>(i)));
        w.Draw(model, m, 8, -1.0f, false, -1, true);
    };
    if (k.state == 0) {
        k.tick = 0;
        for (int i = 0; i < 8; ++i) {
            float* l = h.card[i];
            l[0] = 0.18f;
            l[1] = -0.07f;
            l[2] = -1.0f - static_cast<float>(i) * F32(exe, 0x004C4CC0);
            const int z = 0;
            memcpy(&l[3], &z, 4);
            l[4] = 0.03f;
        }
        ++k.state;
    } else {
        bool second = false;
        if (k.state == 1) {
            if (k.tick != 0x50) {
                for (int i = 0; i < 8; ++i) {
                    float* l = h.card[i];
                    int ang;
                    memcpy(&ang, &l[3], 4);
                    if (i < 6 && i * 5 + 0xF <= k.tick) {
                        ang += i == 0 ? -0x300 : -0x200;
                        if (ang < -0x7FFF) ang = -0x8000;
                        if (ang == -0x4200) l[2] = F32(exe, 0x004C4C64) - static_cast<float>(8 - i) * F32(exe, 0x004C4CC0);
                        memcpy(&l[3], &ang, 4);
                    }
                    int bend;
                    memcpy(&bend, &l[3], 4);
                    w.PageBend(bend);  // 0x004759C0: page model 0x7EE bent by this letter's angle
                    letter(i);
                }
            } else {
                k.state = 2;
                second = true;
            }
        } else if (k.state == 2) {
            second = true;
        }
        if (second) {
            if (k.tick == 300) {
                w.ts.KillCurrent();
                return;
            }
            for (int i = 0; i < 8; ++i) {
                float* l = h.card[i];
                if (i == 6) {
                    if (l[4] < F32(exe, 0x0055CB9C)) {
                        l[4] = l[4] + F32(exe, 0x0055CB98);
                        l[0] = l[0] - static_cast<float>(F64(exe, 0x0055CB90));
                        l[1] = l[1] + static_cast<float>(F64(exe, 0x0055CB88));
                    }
                } else {
                    const float s = l[4] - F32(exe, 0x004E30E0);
                    l[4] = s;
                    if (s <= 0.0f) l[4] = 0.0f;
                }
                letter(i);
            }
        }
    }
    if (1 < k.state && k.state < 4 && 0x4F < k.tick) {  // Spr_Draw 0x0041C630 x 2
        float a = static_cast<float>(k.tick - 0x50) * F32(exe, 0x0055CB80);
        if (1.0f <= a) a = 1.0f;
        if (w.typingReferenceActive) {
            w.TypingSprite(0xD1,Bits(0x43AC0000),Bits(0x42C00000),a,1,0,0x004081AD);
            w.TypingSprite(0xDF,Bits(0x43F60000),Bits(0x42C00000),a,1,0,0x004081D7);
        } else {
            w.hudSprites.push_back(Sprite2D(0xBC, Bits(0x43AC0000), Bits(0x42C00000), 1.0f, 1.0f, 1.0f, 0, a));
            w.hudSprites.push_back(Sprite2D(0xCA, Bits(0x43F60000), Bits(0x42C00000), 1.0f, 1.0f, 1.0f, 0, a));
        }
    }
    ++k.tick;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hit spark 0x004246F0 (0x004247D0(head, node)): models 0xE25.. at the node's view-space hit point, 15 frames
// ---------------------------------------------------------------------------------------------------------------------
struct SparkTask : WTask {
    HydraTask* obj = nullptr;  // +0x34
    uint32_t objSerial = 0;
    int16_t n = 0;     // +0x44
    int16_t node = 0;  // +0x46
};

void TaskSpark(Task& t) {
    World::Impl& w = *CurrentWorld();
    SparkTask& k = t.As<SparkTask>();
    HydraTask* o = Alive(w, k.obj, k.objSerial);
    if (!o) {
        w.Note("stage-3 boss spark: head freed (0x004246F0 reads its node)");
        w.ts.KillCurrent();
        return;
    }
    const EnemyNode& nd = o->anim.nodes[k.node & 31];
    const float z = nd.radius + nd.hit[2];
    float f = 1.0f;
    if (F32(w.exe, 0x004C4C60) <= z) f = -z * F32(w.exe, 0x004C4C5C) + F32(w.exe, 0x004C4C58);
    const float g = static_cast<float>(F64(w.exe, 0x0055CB78));
    Mat4 m = Mat4::Identity();
    mx::Translate(m, nd.hit[0], nd.hit[1], z);
    mx::RotY(m, 0x8000);
    const float s = f * g + f * g;
    mx::Scale(m, s, s, s);
    w.Draw(k.n + 0xE25, m, 8, -1.0f, false, -1, true);
    if (0xE < ++k.n) w.ts.KillCurrent();
}

// ---------------------------------------------------------------------------------------------------------------------
// The heads: 0x0041FE30 -> 0x004209B0
// ---------------------------------------------------------------------------------------------------------------------
const SkeletonNode* SkelNode(const Skeleton& s, int index) {
    for (const SkeletonNode& d : s.nodes)
        if (d.index == index) return &d;
    return nullptr;
}

void ZeroExtra(HydraTask& a) {
    for (int i = 0; i < a.nodes && i < 27; ++i) a.ax[i] = a.ay[i] = a.az[i] = 0;
}

// 0x00421F20 (states 4..6, heads): the node chain again with the extra angles (attack lunge / flinch decay), node
// matrices and view-space hit points; states 10..14 (variant 5, 0x004231C0) not ported
void HeadChain(World::Impl& w, HydraTask& a) {
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    const Skeleton& s = w.EnemySkeleton(a.anim.kind);
    const float fi = static_cast<float>(a.index);
    const int cy = static_cast<int>(F32(exe, 0x0055CB30) - fi * F32(exe, 0x0055CB34));
    const int cx = static_cast<int>(F32(exe, 0x0055CB2C) - fi * F32(exe, 0x005308E8));
    const int cz = 0;  // local_20 (only cases 10..12 add to it)
    float t = 0;
    int zsum = 0;
    // world chain (draws) and the original's model-view chain from the camera matrix (hit points)
    Mat4 m = Mat4::Identity(), mv = w.cam.view;
    Mat4* mvs = a.mv;
    a.mvValid = true;
    for (Mat4* q : { &m, &mv }) {
        mx::Translate(*q, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotZ(*q, a.rot[2]);
        mx::RotY(*q, a.rot[1]);
        mx::RotX(*q, a.rot[0]);
    }
    const int tick = a.anim.tick[0];
    for (int i = 0; i < a.nodes; ++i) {
        EnemyNode& n = a.anim.nodes[i & 31];
        switch (a.behaviour) {
        case 4:
            if (h.mode == 2 && a.index == 2 && i == a.jawA) {  // 0x00422C70: jaw wobble (stage 6)
                if (a.wobble < 0) ++a.wobble;
                if (0 <= a.wobble) {
                    a.wobble += 0x200;
                    const float s = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(a.wobble * mx::kToRad))));
                    const float v = s * F32(exe, 0x0055CB3C);  // fmul / fsub(r) under the 24-bit FPU, __ftol
                    a.az[a.jawA % 27] = static_cast<int>(v - F32(exe, 0x0055CB38));
                    a.az[a.jawB % 27] = static_cast<int>(F32(exe, 0x0055CB38) - v);
                    if (0x7FFF < a.wobble) a.wobble = -300 - Ctr(w, 0x96);
                }
            }
            break;
        case 5: {
            const int c = a.index;
            if (c != 2) {
                bool upd = false;
                if (c == 1 || c == 3) {
                    if (a.hitTick != 0) {
                        if (tick < a.hitTick) t = static_cast<float>(static_cast<double>(tick) / a.hitTick);
                        else {
                            const int len = w.MotionTicks(a.anim.motion[0]);
                            t = static_cast<float>(static_cast<double>(len - tick) / (len - a.hitTick));
                        }
                    }
                    upd = true;
                } else if (0x28 < tick && tick < a.hitTick) {
                    t = static_cast<float>(static_cast<double>(tick - 0x28) / (a.hitTick - 0x28));
                    upd = true;
                } else if (a.hitTick <= tick) {
                    if (a.hitTick != 0) {
                        const int len = w.MotionTicks(a.anim.motion[0]);
                        t = static_cast<float>(static_cast<double>(len - tick) / (len - a.hitTick));
                    }
                    upd = true;
                }
                if (upd) {
                    a.ax[i] = static_cast<int>(static_cast<float>(cx) * t);
                    a.ay[i] = static_cast<int>(static_cast<float>(cy) * t);
                    a.az[i] = static_cast<int>(static_cast<float>(cz) * t);
                }
            }
            if (i <= a.head) {
                zsum += n.cur[2];
                if (i == a.head) a.zsum = zsum;
            }
            break;
        }
        case 6: case 7: {
            const float k = F32(exe, 0x004C43A0) - a.f1340;
            const float e = F32(exe, 0x0055CB28);
            a.ax[i] = static_cast<int>(k * static_cast<float>(a.ax[i]) * e);
            a.ay[i] = static_cast<int>(k * static_cast<float>(a.ay[i]) * e);
            a.az[i] = static_cast<int>(k * static_cast<float>(a.az[i]) * e);
            break;
        }
        default:
            w.Note("stage-3 boss head chain state " + std::to_string(a.behaviour) + " (0x00421F20) not ported");
            break;
        }
        if (a.index == 2) {
            if (i == 0x1A) m = a.anim.nodes[24].world, mv = mvs[24];
        } else if (i == a.jawB) {
            m = a.anim.nodes[a.head & 31].world, mv = mvs[a.head & 31];
        }
        float r[3] = {};
        bool root = false;
        if (i == 0) {
            float* rt = a.anim.root;
            float* prev = a.anim.prevRoot;
            root = true;
            if ((tick & 1) == 0) {
                memcpy(r, rt, 12);
                memcpy(prev, rt, 12);
            } else {
                for (int c = 0; c < 3; ++c) r[c] = (rt[c] - prev[c]) * 0.5f + prev[c];
            }
        }
        const SkeletonNode* d = SkelNode(s, i);
        for (Mat4* q : { &m, &mv }) {
            if (root) mx::Translate(*q, r[0], r[1], r[2]);
            if (d) mx::Translate(*q, d->offset[0], d->offset[1], d->offset[2]);
            mx::RotZ(*q, a.az[i]);
            mx::RotY(*q, a.ay[i]);
            mx::RotX(*q, a.ax[i]);
            mx::RotZ(*q, n.cur[2]);
            mx::RotY(*q, n.cur[1]);
            mx::RotX(*q, n.cur[0]);
        }
        n.world = m;
        if (i < 27) mvs[i] = mv;
        mx::Transform(mv, n.local, n.hit);  // 0x004A8CA0 -> node +0x28 (model-view), 0x004A8A80 -> +0x68
    }
}

// The model-view node matrices of 0x004110D0 for the draw in the other states (the port's AnimDraw keeps world
// matrices; the heads draw from model-view ones like the original): same chain, no extra angles, nothing stored
void HeadMvChain(World::Impl& w, HydraTask& a) {
    const Skeleton& s = w.EnemySkeleton(a.anim.kind);
    Mat4 mv = w.cam.view;
    mx::Translate(mv, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotZ(mv, a.rot[2]);
    mx::RotY(mv, a.rot[1]);
    mx::RotX(mv, a.rot[0]);
    mx::Scale(mv, a.anim.scale, a.anim.scale, a.anim.scale);
    const float* r = a.anim.root;
    if (!(a.anim.drawFlags & 2)) mx::Translate(mv, r[0], r[1], r[2]);
    else mx::Translate(mv, 0.0f, r[1], 0.0f);
    for (int i = 0; i < a.nodes && i < 27; ++i) {
        const EnemyNode& n = a.anim.nodes[i & 31];
        if (a.index == 2) {
            if (i == 0x1A) mv = a.mv[24];
        } else if (i == a.jawB) {
            mv = a.mv[a.head & 31];
        }
        if (i != 0)
            if (const SkeletonNode* d = SkelNode(s, i)) mx::Translate(mv, d->offset[0], d->offset[1], d->offset[2]);
        mx::RotZ(mv, n.cur[2]);
        mx::RotY(mv, n.cur[1]);
        mx::RotX(mv, n.cur[0]);
        a.mv[i] = mv;
    }
    a.mvValid = true;
}

// 0x004219E0: nodes 1.. drawn from their matrices; the breath effect on the second jaw node while attacking
void HeadDraw(World::Impl& w, HydraTask& a) {
    w.LightChar();  // the callers wrap 0x004219E0 in 0x0041DC70 / 0x0041DCC0 (renderer state)
    struct Restore { World::Impl& w; ~Restore() { w.LightScene(); } } restore{ w };
    for (int i = 1; i < a.nodes; ++i) {
        const EnemyNode& n = a.anim.nodes[i & 31];
        const bool vs = a.mvValid && i < 27;  // states 4..6: the model-view chain (as the original), else world
        w.Draw(n.model, vs ? a.mv[i] : n.world, 8, -1.0f, false, i, vs);
        if (a.behaviour == 5 && i == a.jawB && a.breath < 0x25) {
            ++a.breath;
            Mat4 m = vs ? a.mv[i] : n.world;
            if (a.index == 2) {
                mx::Translate(m, 7.0f, Bits(0xC0900000), 0.0f);
                mx::RotY(m, 0x4000);
                mx::RotX(m, 0x2000);
                mx::Scale(m, 1.5f, 1.5f, 1.5f);
            } else {
                mx::Translate(m, 7.0f, -0.5f, 0.0f);
                mx::RotY(m, 0x4000);
            }
            w.Draw(a.breath % 0x27 + 0x97A, m, 8, -1.0f, false, -1, vs);
            w.Draw((a.breath & 31) + 0x199C, m, 8, -1.0f, false, -1, vs);
        }
    }
}

// 0x00422D30 (heads 0 / 4 in phase 0, mode 0): wait for flag 0, break the rock, rise, the name card (head 0, flag 1),
// fight at flag 2
void HeadIntro(World::Impl& w, HydraTask& a) {
    HydraState& h = H(w);
    const int r = a.index != 0 ? 1 : 0;
    auto& cnt = a.anim.counter[0];
    if (a.behaviour == 0) {
        HydraTask* rock = Body(w, r);
        const int rs = rock ? rock->behaviour : 0;
        if (rs == 0) {
            ++cnt;
            if (w.runner->flags[0] != 1 || !a.anim.ended) return;
            w.AnimSetMotion(a, r == 0 ? 0x57 : 0x59);
            if (rock) {
                w.AnimSetMotion(*rock, r == 0 ? 0x21D : 0x21F);
                rock->behaviour = 1;
            }
            return;
        }
        if (rs != 1) return;
        const int tick = a.anim.tick[0];
        if (r == 0) {
            if (tick == 0x17) w.Sound(0x3D16A9);
            if (tick < 0x21) {
                ++cnt;
                return;
            }
        } else {
            if (tick == 0x3F) w.Sound(0x3D16A9);
            if (tick < 0x49) {
                ++cnt;
                return;
            }
        }
        rock->behaviour = 2;
        a.behaviour = 2;
        a.counter1330 = 0;
        if (r == 0) {
            rock->vel[0] = Bits(0xC019999A);
            rock->vel[1] = Bits(0x3FB33333);
            rock->vel[2] = Bits(0xBE4CCCCD);
            w.Sound(0x20000012);
        } else {
            rock->vel[0] = Bits(0xBFB33333);
            rock->vel[1] = Bits(0x3FE66666);
            rock->vel[2] = Bits(0xBE4CCCCD);
            w.Sound(0x1DA9);
        }
        return;
    }
    if (a.behaviour == 2) {
        ++cnt;
        if (!a.anim.ended) return;
        a.side = static_cast<int8_t>(a.index % 2);
        if (a.side == 0) {
            w.AnimBlendTo(a, w.exe.S16(0x00588EE0 + 2 * static_cast<uint32_t>(a.index)), 0, 4);
            a.idleTab = 0x00588EE0;
            a.attackTab = 0x00588F1C;
        } else {
            w.AnimBlendTo(a, w.exe.S16(0x00588EF8 + 2 * static_cast<uint32_t>(a.index)), 0, 4);
            a.idleTab = 0x00588EF8;
            a.attackTab = 0x00588F2C;
        }
        a.counter1330 = 0;
        a.behaviour = 3;
        return;
    }
    if (a.behaviour == 3) {
        ++cnt;
        if (w.runner->flags[1] != 0 && a.index == 0 && a.counter1330 == 0) {
            CardTask* c = w.NewTask<CardTask>(TaskCard, 0x00424900);
            c->state = 0;
            a.counter1330 = 1;
        }
        if (w.runner->flags[2] == 1) {
            a.counter1330 = 0;
            a.behaviour = 4;
            a.flags34 &= ~0x8000u;
            if (a.index == 0) w.SpawnBossBar(320.0f, 35.0f);
            HydraTask* h0 = Head(w, 0);
            HydraTask* h4 = Head(w, 4);
            if (h0 && h4 && h0->behaviour == 4 && h4->behaviour == 4) h.phase = 1;
        }
    }
}

// 0x00421930: the other motion set (transition motion 0x00588F50[side])
void HeadSwitchSide(World::Impl& w, HydraTask& a) {
    w.AnimSetMotion(a, w.exe.S16(0x00588F50 + 2 * static_cast<uint32_t>(a.side)));
    a.side = static_cast<int8_t>(1 - a.side);
    if (a.side == 0) {
        a.idleTab = 0x00588EE0;
        a.attackTab = 0x00588F1C;
        a.flinch = w.exe.S16(0x00588F48 + 2 * static_cast<uint32_t>(Ctr(w, 1)));
    } else {
        a.idleTab = 0x00588EF8;
        a.attackTab = 0x00588F2C;
        a.flinch = w.exe.S16(0x00588F48 + 2 * static_cast<uint32_t>(Ctr(w, 1)) + 2);
    }
    a.anim.counter[0] = 0;
}

void HeadIdle(World::Impl& w, HydraTask& a) {  // 0x00421910(idle count - 1) -> 0x00411930(idle table[k])
    const int k = Ctr(w, a.idleCount - 1);
    w.AnimSetMotion(a, w.exe.S16(a.idleTab + 2 * static_cast<uint32_t>(k)));
    a.anim.counter[0] = 0;
}

void HeadKillLock(World::Impl& w, HydraTask& a) {
    if (a.slot120 != -1) w.lockOn.Release(a.slot120);  // 0x009A5EC0[+0x120 * 8] = 0
}

char QuizInitial(std::string_view text) {
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) return 0;
    const char c = text[first];
    return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c;
}

bool BeginTypingQuiz(World::Impl& w, HydraState& h) {
    int bankCount = 0;
    for (const auto& bank : TypingWordBanks()) if (bank.group == 3) ++bankCount;
    if (!bankCount) return false;
    const auto* bank = FindTypingWordBank(3, 1 + w.Rand(0x00405085) % bankCount);
    if (!bank) return false;
    const auto records = TypingWords(*bank);
    if (records.size() <= 9) return false;
    const unsigned correct = unsigned(w.Rand(0x0040508D)) % 3u;
    std::array<uint32_t, 3> choices{};
    std::array<char, 3> initials{};
    for (unsigned choice = 0; choice < choices.size(); ++choice) {
        const uint32_t first = choice == correct ? 1 : 9;
        const uint32_t count = choice == correct ? 8 : uint32_t(records.size() - 9);
        uint32_t selected = first;
        const auto available = [&](uint32_t index) {
            const char initial = QuizInitial(records[index].text);
            return initial && std::find(initials.begin(), initials.begin() + choice, initial) == initials.begin() + choice;
        };
        for (int attempt = 0; attempt < 16; ++attempt) {
            selected = first + unsigned(w.Rand(0x004B0BBA)) % count;
            if (available(selected)) break;
        }
        if (!available(selected)) {
            for (uint32_t index = first + count; index-- > first;)
                if (available(index)) { selected = index; break; }
        }
        choices[choice] = selected;
        initials[choice] = QuizInitial(records[selected].text);
    }
    if (!h.quiz.begin(records, choices)) return false;
    ++h.quizToken;
    h.quizPhase = HydraState::QuizPhase::Reveal;
    h.quizFrames = 120; // original question enters at -960, advancing eight units per frame
    w.typingQuestion = h.quiz.question().text;
    w.typingQuestionSeconds = 0;
    w.typingHud.questionSlide = -960;
    w.typingHud.questionVisible = false;
    return true;
}

void ResolveTypingQuiz(World::Impl& w, HydraState& h, TypingQuizOutcome outcome, size_t answeredChoice = 3) {
    if (outcome == TypingQuizOutcome::None) return;
    ++h.quizToken;
    w.typingQuestion.clear();
    w.typingQuestionSeconds = 0;
    h.quizPhase = HydraState::QuizPhase::Cooldown;
    h.quizFrames = 1;
    if (outcome == TypingQuizOutcome::Correct) {
        h.total = static_cast<int16_t>(std::max(0, int(h.total) - int(TypingTowerCorrectDamage(h.mode == 2))));
        w.bossBar = float(h.total) / float(TypingTowerStartingLife(h.mode == 2));
        h.quizFrames = 600 + unsigned(w.Rand(0x0040429C)) % 51u;
        if (h.total == 0) {
            h.alive = 0;
            h.phase = 2;
            h.readyCount = 0;
            w.TypingController(w.typingPlayer).addBasePoints(700);
            w.AddScore(w.typingPlayer, 700);
            for (int i = 0; i < 5; ++i) if (auto* dying = Head(w, i)) {
                dying->ready = -1;
                dying->behaviour = 7;
                dying->counter1330 = 0;
                HeadKillLock(w, *dying);
                w.AnimBlendTo(*dying, dying->death, 8, 8);
            }
        }
    }
    if (h.phase == 1 && answeredChoice < kTypingQuizHeadIndices.size()) {
        if (auto* head = Head(w, int(kTypingQuizHeadIndices[answeredChoice]))) {
            head->behaviour = 6;
            head->f1340 = 0;
            w.AnimBlendTo(*head, head->flinch, 8, 8);
            Roar(w, 1);
        }
    }
    if (h.level < 15) ++h.level;
}

void TickTypingQuiz(World::Impl& w, HydraState& h) {
    if (w.paused || w.runner->camera.modeCur != 2 ||
        (w.player[0].state != 5 && w.player[1].state != 5)) return;
    switch (h.quizPhase) {
    case HydraState::QuizPhase::Setup:
        BeginTypingQuiz(w, h);
        break;
    case HydraState::QuizPhase::Reveal: {
        for (unsigned index : kTypingQuizHeadIndices) {
            const auto* head = Head(w, int(index));
            if (!head || head->behaviour != 4) return;
        }
        if (h.quizFrames) --h.quizFrames;
        w.typingHud.questionSlide = -8 * int(h.quizFrames);
        w.typingHud.questionVisible = true;
        if (h.quizFrames) break;
        h.quiz.revealComplete(std::clamp(w.rank, 0, w.cfg.eventMode == 0 ? 12 : w.cfg.eventMode == 1 ? 14 : 15));
        h.quizPhase = HydraState::QuizPhase::Answers;
        w.typingQuestionSeconds = float(h.quiz.remainingFrames()) / 60.0f;
        break;
    }
    case HydraState::QuizPhase::Answers: {
        const auto outcome = h.quiz.tick();
        w.typingQuestionSeconds = float(h.quiz.remainingFrames()) / 60.0f;
        ResolveTypingQuiz(w, h, outcome);
        if (outcome == TypingQuizOutcome::Timeout) {
            if (auto* head = Head(w, int(kTypingQuizHeadIndices[h.quiz.correctChoice()]))) {
                head->ready = 0;
                ++h.readyCount;
                h.quizPhase = HydraState::QuizPhase::Attack;
            }
        }
        break;
    }
    case HydraState::QuizPhase::Cooldown:
        if (h.quizFrames) --h.quizFrames;
        if (h.quizFrames) break;
        if (auto* head = Head(w, int(kTypingQuizHeadIndices[h.quiz.correctChoice()]))) {
            head->ready = 0;
            ++h.readyCount;
            h.quizPhase = HydraState::QuizPhase::Attack;
        }
        break;
    case HydraState::QuizPhase::Attack:
        break;
    }
}

// 0x004209B0
void TaskHead(Task& t) {
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    if (a.lock) a.slot120 = static_cast<int8_t>(a.lock->slot);
    h.heads[a.index] = &a;
    h.headSerial[a.index] = t.serial;
    auto& cnt = a.anim.counter[0];
    // ---- shots (fight phase, not flinching / dying)
    if (!w.cfg.portTyping && h.phase == 1 && a.behaviour != 7 && a.behaviour != 6 && (a.flags34 & 8)) {
        const uint32_t f = a.flags34 & ~8u;
        a.flags34 = f;
        int p;
        if ((f & 2) && (f & 4)) p = w.Rand(0x00420A2D) & 1;
        else p = (f & 2) == 0 ? 1 : 0;
        const bool onHead = ((a.flags34 & 2) && a.hitNode[0] == a.head) || ((a.flags34 & 4) && a.hitNode[1] == a.head);
        bool damage = false;
        if (onHead) {
            const EnemyNode& j1 = a.anim.nodes[a.jawA & 31];
            const EnemyNode& j2 = a.anim.nodes[a.jawB & 31];
            int d = ((j1.cur[2] - j2.cur[2]) - a.az[a.jawB]) + a.az[a.jawA];
            if (d < 0) d = ((j2.cur[2] - j1.cur[2]) - a.az[a.jawA]) + a.az[a.jawB];
            const uint16_t zs = static_cast<uint16_t>(a.zsum);
            damage = 0x1700 < d && !(h.mode != 2 && a.index == 2) && !(0x1700 < zs && zs < 0xC000);
        }
        if (damage) {
            w.AddScore(p, 10);
            w.SpawnNodeBlood(a, a.hitNode[p]);  // 0x00407200
            a.flags34 &= ~8u;
            float factor = 1.0f;
            if (w.runner->cfg.gameType == 1) {
                // 0x00420B56: the shooter's item factor 0x009A224C + p * 0x14 (both guns: rand & 1; gun bit 2: P1, else
                // P2); -1.0 (float 0x004C4C64) counts as float 0x004E30F0
                int q;
                if ((a.flags34 & 2) && (a.flags34 & 4)) q = w.Rand(0x00420B67) & 1;
                else q = (a.flags34 & 2) ? 0 : 1;
                float boost = 0.0f;
                if (!ItemDamage(w, q, &factor, &boost)) factor = 1.0f;
                if (factor == F32(w.exe, 0x004C4C64)) factor = F32(w.exe, 0x004E30F0);
            }
            // fild (45 / n) x factor on the x87 stack, _ftol (exact in double)
            int dmg = static_cast<int>(static_cast<double>(45 / (w.players != 2 ? 3 : 5)) * static_cast<double>(factor));
            if (a.typingStrike) dmg = static_cast<int>(TypingDamage(a, static_cast<float>(dmg)));
            a.hp = static_cast<int16_t>(a.hp - dmg);
            h.total = static_cast<int16_t>(h.total - dmg);
            if (a.hp < 0) h.total = static_cast<int16_t>(h.total - a.hp);
            w.bossBar = static_cast<float>(h.total) * F32(exe, h.mode == 2 ? 0x0055CB18 : 0x0055CB14);
            if (w.bossBar < 0.0f) w.bossBar = 0.0f;
            if (h.level < 0xF) ++h.level;
            if (a.hp < 1) {
                w.Sound(0x1C17A9);
                a.behaviour = 7;
                --h.alive;
                a.counter1330 = 0;
                HeadKillLock(w, a);
                w.AnimBlendTo(a, a.death, 8, 8);
                if (h.alive == 0) {
                    a.counter1330 = 0;
                    h.phase = 2;
                    w.bossBar = 0.0f;
                    const uint32_t g = a.flags34 & 2;
                    if (g == 0 || !(a.flags34 & 4)) w.AddScore(g == 0 ? 1 : 0, 0x5DC);
                    else w.AddScore(w.Rand(0x00420D05) & 1, 0x5DC);
                }
            } else {
                Roar(w, 1);
                a.behaviour = 6;
                if (a.index != 2) a.flinch = exe.S16(0x00588F48 + 2 * static_cast<uint32_t>(Ctr(w, 1) + a.side * 2));
                w.AnimBlendTo(a, a.flinch, 8, 8);
                h.attacker = a.index;
                a.f1340 = 0;
            }
            if (0 <= a.ready) {
                if (0 < h.readyCount) --h.readyCount;
                a.ready = -1;
            }
        } else {
            w.Sound(0x1216A9);
            SparkTask* k = w.NewTask<SparkTask>(TaskSpark, 0x004246F0);  // 0x004247D0
            k->obj = &a;
            k->objSerial = t.serial;
            k->node = a.hitNode[p];
            k->n = 0;
        }
        h.counter += static_cast<uint32_t>(cnt % 10);
        a.flags34 &= ~6u;
    }
    // ---- skeleton
    bool started = false;
    DrawSkeleton(w, a);
    if (a.behaviour == 4 || a.behaviour == 5 || a.behaviour == 6) HeadChain(w, a);
    else HeadMvChain(w, a);
    HeadDraw(w, a);
    if (h.phase == 1 && a.behaviour == 0) {
        a.behaviour = 4;
        a.flags34 &= ~0x8000u;
    }
    const auto playersIn = [&] { return w.player[0].state == 5 || w.player[1].state == 5; };
    bool scheduler = false;
    switch (h.phase) {
    case 0:
        if (h.mode == 2) {
            if (w.runner->flags[0] == 1) {  // stage 6 (notes/agents/stage6.md)
                w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
                w.bossBar = 1.0f;
                h.phase = 1;
            }
            if (a.introTimer++ == 0x4B) Roar(w, 3);
            ++cnt;
        } else if (w.runner->cfg.gameType == 3) {  // boss mode (LAB_00420F35): no rocks; card at flag 1, fight at flag 2
            ++cnt;
            if (w.runner->flags[1] == 1 && a.index == 0 && a.counter1330 == 0) {
                CardTask* c = w.NewTask<CardTask>(TaskCard, 0x00424900);
                c->state = 0;
                a.counter1330 = 1;
            }
            if (w.runner->flags[2] == 1) {
                h.phase = 1;
                a.counter1330 = 0;
                a.behaviour = 4;
                a.flags34 &= ~0x8000u;
                w.bossActive = 1;  // 0x009CA0EA
                if (a.index == 0) {
                    w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
                    w.bossBar = 1.0f;               // 0x009C8E10
                }
            }
            if (a.index == 2) {
                ++a.introTimer;
                if (a.introTimer == 0x3C || a.introTimer == 0x226) Roar(w, 3);
                if (a.introTimer == 0x122) Roar(w, 5);
            }
        } else {
            if (a.index == 0 || a.index == 4) HeadIntro(w, a);
            else ++cnt;
            if (a.index == 2) {
                ++a.introTimer;
                if (a.introTimer == 0x3C || a.introTimer == 0x226) Roar(w, 3);
                if (a.introTimer == 0x122) Roar(w, 5);
            }
        }
        break;
    case 1:
        switch (a.behaviour) {
        case 4:
            ++cnt;
            if (0 < a.ready) --a.ready;
            if (a.anim.ended) {
                const int r = Ctr(w, 1);
                if (r == 0 || a.index == 2 || a.ready != -1) {
                    HeadIdle(w, a);
                    started = true;
                } else {
                    HeadSwitchSide(w, a);
                }
            }
            if (w.cfg.portTyping || h.mode == 2 || a.index != 2) {
                if (a.ready == 0 && started && w.runner->camera.modeCur == 2 && playersIn()) {
                    const int k = Ctr(w, a.attackCount - 1);
                    a.behaviour = 5;
                    const uint32_t e = a.attackTab + 4 * static_cast<uint32_t>(k);
                    w.AnimSetMotion(a, exe.S16(e));
                    a.anim.counter[0] = 0;
                    a.breath = 0;
                    a.hitTick = exe.S16(e + 2);
                    if (w.players == 2) a.target = static_cast<int8_t>(Ctr(w, 1));
                    else {
                        if (w.playersInGame == 0) a.target = 0;
                        if (w.playersInGame == 1) a.target = 1;
                    }
                }
                scheduler = a.index == 2;
            } else {
                scheduler = true;
            }
            break;
        case 5: {
            if (a.breath == 3) Roar(w, 4);
            if (a.breath == 0xF) Roar(w, 2);
            const int old = cnt++;
            if (old == a.hitTick) {
                if (w.runner->camera.modeCur == 2 && 0 < w.players && w.player[a.target & 1].state == 5) {
                    w.PlayerTakeHit(a.target, 1, 9);  // Player_TakeHit 0x00415300
                    h.level = static_cast<int8_t>(h.level - 3);
                }
                if (0 < h.readyCount) --h.readyCount;
                a.ready = -1;
                if (h.level < 0) h.level = 0;
                h.countdown = Interval(w, 0x0042127B);
                h.attacker = a.index;
                if (w.cfg.portTyping) {
                    h.quiz = {};
                    h.quizPhase = HydraState::QuizPhase::Setup;
                    h.quizFrames = 0;
                }
            }
            if (a.anim.ended) {
                const int r = Ctr(w, 1);
                if (r == 0 || a.index == 2) HeadIdle(w, a);
                else HeadSwitchSide(w, a);
                a.behaviour = 4;
                ZeroExtra(a);
            }
            scheduler = a.index == 2;
            break;
        }
        case 6:
            if (w.cfg.portTyping) {
                ++cnt;
                if (a.anim.flags[0] & 1) a.f1340 += 1.0f;
                if (a.anim.ended) {
                    HeadIdle(w, a);
                    a.behaviour = 4;
                    ZeroExtra(a);
                    h.quiz = {};
                    h.quizPhase = HydraState::QuizPhase::Setup;
                    h.quizFrames = 0;
                }
                break;
            }
            if (!(a.anim.flags[0] & 1)) {
                ++cnt;
                if (a.anim.ended) HeadIdle(w, a);
                a.behaviour = 4;
                ZeroExtra(a);
            } else {
                ++cnt;
                a.f1340 = a.f1340 + 1.0f;
            }
            scheduler = a.index == 2;
            break;
        case 7: {
            if (!a.anim.ended) ++cnt;
            const int tick = a.anim.tick[0];
            if ((a.index == 2 && tick == 0x70) || (a.index != 2 && tick == 0x4B)) {
                if (!(a.anim.flags[0] & 1)) {
                    w.Sound(0xB16A9);
                    w.shakeTimer = 0x18;  // 0x009C8E8C (+ 0x00420810(4, 0x14): no-op)
                }
            }
            scheduler = a.index == 2;
            break;
        }
        default:
            scheduler = a.index == 2;
            break;
        }
        if (w.cfg.portTyping && a.index == 2) TickTypingQuiz(w, h);
        if (!w.cfg.portTyping && scheduler && w.runner->camera.modeCur == 2 && playersIn()) {  // LAB_0042141B (central head)
            --h.countdown;
            if (h.countdown < 1 && h.readyCount < 2) {
                const int i = Ctr(w, 4);
                HydraTask* c = Head(w, i);
                if (c && (h.mode == 2 || i != 2) && c->behaviour != 7 && c->ready != 0) {
                    bool conflict = false;
                    for (int j = 0; j < 5; ++j) {
                        HydraTask* o = Head(w, j);
                        if (j != i && o && o->ready == 0 && 2 < std::abs(j - i)) conflict = true;
                    }
                    if (!conflict) {
                        c->ready = 0;
                        h.countdown = Interval(w, 0x00421519);
                        ++h.readyCount;
                    }
                }
            }
        }
        break;
    case 2:
        if (a.index == 2) {
            if (a.anim.tick[0] == 0x70) {
                w.Sound(0xB16A9);
                w.shakeTimer = 0x18;  // (+ 0x00420810(6, 0x1E): no-op)
            }
            if (++a.counter1330 == 0xB4) {
                --w.attackers;
                --w.liveEnemies;
                h.phase = 3;
                HeadKillLock(w, a);
                a.counter1330 = 0;
                if (h.mode != 2) {
                    if (a.actorSlot >= 0 && a.actorSlot < 14) {
                        w.actorSlots[a.actorSlot] = nullptr;
                        a.actorSlot = -1;
                    }
                    w.EnemyDestroy(a);  // 0x00409CC0
                    return;
                }
            }
        } else if (a.behaviour == 7 && a.anim.tick[0] == 0x38 && !(a.anim.flags[0] & 1)) {
            w.Sound(0xB16A9);
            w.shakeTimer = 0x18;
        }
        if (a.behaviour != 7) {
            a.behaviour = 7;
            a.counter1330 = 0;
            w.AnimBlendTo(a, a.death, 8, 8);
        }
        if (!a.anim.ended) ++cnt;
        break;
    case 3:
        if (h.mode != 2 || 0xEF < ++a.counter1330) {
            if (a.actorSlot >= 0 && a.actorSlot < 14) {
                w.actorSlots[a.actorSlot] = nullptr;
                a.actorSlot = -1;
            }
            w.EnemyDestroy(a);
            return;
        }
        break;
    default:
        break;
    }
    // ---- target point / lock-on
    memcpy(a.body, a.pos, 12);
    mx::Transform(w.cam.view, a.pos, a.viewPos);
    if (a.behaviour != 7) w.RegisterTarget(&a);  // 0x00405160
    bool lock;
    if (h.phase == 0) lock = a.index == 2;
    else lock = h.phase == 1 && 0 <= a.ready;
    if (!lock) lock = h.readyCount == 0 && a.index == h.attacker && a.behaviour != 7;
    if (lock) {
        float p[3];
        mx::Transform(w.cam.inverse, a.anim.nodes[a.head & 31].hit, p);
        const float k = F32(exe, 0x004C4D08);
        for (int c = 0; c < 3; ++c) h.lock[c] = (p[c] - h.lock[c]) * k + h.lock[c];
        memcpy(a.body, h.lock, 12);
        static const bool dbgLock = getenv("HOTD2_HYDRA_LOCK") != nullptr;
        if (dbgLock)
            fprintf(stderr, "%d lock head %d p %.6f %.6f %.6f -> %.6f %.6f %.6f\n", w.frame, a.index, p[0], p[1], p[2], h.lock[0],
                    h.lock[1], h.lock[2]);
        if (a.behaviour != 7) {
            w.SyncLock(a);
            w.lockOn.Register(a.lock, w.playerPos);  // 0x00408EC0
        }
    }
    if (h.mode != 2 && h.alive == 1 && a.index == 2) {  // the central head dies with the last outer one
        a.behaviour = 7;
        --h.alive;
        a.counter1330 = 0;
        HeadKillLock(w, a);
        w.AnimBlendTo(a, a.death, 8, 8);
        h.phase = 2;
    }
    w.SyncLock(a);
    static const int dbg = getenv("HOTD2_HYDRA_DEBUG") ? atoi(getenv("HOTD2_HYDRA_DEBUG")) : -1;
    if (dbg == a.index) {  // debug trace: node 17 / 24 hit point, angles of nodes 1 / head, anim state
        const EnemyNode& hn = a.anim.nodes[a.head & 31];
        fprintf(stderr, "%d head %d st %d m %d t %d c %d f %X hit %.6f %.6f %.6f n1 %d %d %d nh %d %d %d ax %d ay %d\n", w.frame, a.index,
                a.behaviour, a.anim.motion[0], a.anim.tick[0], a.anim.counter[0], a.anim.flags[0], hn.hit[0], hn.hit[1],
                hn.hit[2], a.anim.nodes[1].cur[0], a.anim.nodes[1].cur[1], a.anim.nodes[1].cur[2], hn.cur[0], hn.cur[1], hn.cur[2],
                a.ax[5], a.ay[5]);
        if (getenv("HOTD2_HYDRA_NODES")) {
            fprintf(stderr, "  pos %.6f %.6f %.6f rot %d %d %d root %.6f %.6f %.6f f1340 %.6f\n", a.pos[0], a.pos[1], a.pos[2],
                    a.rot[0], a.rot[1], a.rot[2], a.anim.root[0], a.anim.root[1], a.anim.root[2], a.f1340);
            for (int i = 0; i < a.nodes && i < 27; ++i)
                fprintf(stderr, "  n%d cur %d %d %d x %d %d %d hit %.6f %.6f %.6f\n", i, a.anim.nodes[i].cur[0], a.anim.nodes[i].cur[1],
                        a.anim.nodes[i].cur[2], a.ax[i], a.ay[i], a.az[i], a.anim.nodes[i].hit[0], a.anim.nodes[i].hit[1],
                        a.anim.nodes[i].hit[2]);
        }
    }
}

void TaskHeadCtor(Task& t) {  // 0x0041FE30
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    a.index = static_cast<int8_t>(a.hp);
    h.heads[a.index % 5] = &a;
    h.headSerial[a.index % 5] = t.serial;
    a.hp = static_cast<int16_t>(h.mode != 2 ? 0x2D : 0x1E);
    int kind, motion;
    if (a.index == 2) {
        kind = 0x48;
        motion = exe.S16(0x00588F10);
        a.wobble = -300;
        h.lock[0] = a.pos[0];
        h.lock[1] = a.pos[1] + F32(exe, 0x0055CAD8);
        h.lock[2] = a.pos[2];
    } else {
        kind = 0x49;
        a.side = static_cast<int8_t>(a.index % 2);
        if (a.side == 0) {
            motion = exe.S16(0x00588EE0 + 2 * static_cast<uint32_t>(a.index));
            a.idleTab = 0x00588EE0;
            a.attackTab = 0x00588F1C;
        } else {
            motion = exe.S16(0x00588EF8 + 2 * static_cast<uint32_t>(a.index));
            a.idleTab = 0x00588EF8;
            a.attackTab = 0x00588F2C;
        }
    }
    InitObject(w, a, kind, motion);
    a.nodeCb = 0;
    a.nodeFn = NoNodeDraw;
    a.flags34 |= 0x8000;
    a.anim.counter[0] = 0;
    a.radius = F32(exe, 0x004C4D28 + static_cast<uint32_t>(kind) * 4);
    if (a.index == 2) {
        a.nodes = 0x1B, a.head = 0x18, a.jawA = 0x19, a.jawB = 0x1A, a.idleCount = 6, a.attackCount = 3;
        a.idleTab = 0x00588F10;
        a.attackTab = 0x00588F3C;
        a.flinch = 0x49;
        a.death = 0x41;
    } else {
        a.nodes = 0x14, a.head = 0x11, a.jawA = 0x12, a.jawB = 0x13, a.idleCount = 0xB, a.attackCount = 4;
        a.death = 0x4C;
        a.flinch = exe.S16(0x00588F48 + 2 * static_cast<uint32_t>(a.side));
    }
    ZeroExtra(a);
    a.breath = 0;
    a.introTimer = 0;
    a.ready = -1;
    if (a.index == 2) {
        ++w.attackers;
        ++w.liveEnemies;
    }
    a.lock = &w.lockTargets[w.nextLockTarget++];
    if (w.runner->cfg.gameType != 3) {
        if (a.index == 0) {
            a.anim.motion[0] = 0x58;
            a.anim.counter[0] = 0;
        } else if (a.index == 4) {
            a.anim.motion[0] = 0x58;
            a.anim.counter[0] = 0x1E;
        }
    }
    w.SyncLock(a);
    w.lockOn.AddDirect(a.lock);  // 0x00408E80
    a.slot120 = static_cast<int8_t>(a.lock->slot);
    a.fn = 0x004209B0;
    t.fn = TaskHead;
}

// ---------------------------------------------------------------------------------------------------------------------
// Variant 5: 0x00420360 -> 0x004231C0 (+0x131B = 8). After the five heads: the big head (skeleton 0x48) swims along a
// path recorded from the camera paths of table +0x77B8 (0x00424190, one segment per frame in state 8); the camera
// follows the camera paths of +0x77BC and turns to the head node (0x00424250 and the end of 0x004231C0, camera hold
// 0x009CA094); at the event table +0x77C0 it rises (state 11), lunges at the camera (state 12, Player_TakeHit at the
// hit tick) and takes damage only on node 0x18 with the jaw open (> 0x1700) in states 11 / 12; 120 hp, -10 a hit (-6
// with two players); two damaging hits (three with two players) end a rise. Death: state 14, bobbing in the water
// until event flag 4. Splashes: task 0x00424800 (0x004248B0).
// ---------------------------------------------------------------------------------------------------------------------
struct SplashTask : WTask {
    float pos[3] = {};  // +0x38
    int16_t kind = 0;   // +0x44
    int16_t n = 0;      // +0x4C model
};

void TaskSplash(Task& t) {  // 0x00424800
    World::Impl& w = *CurrentWorld();
    SplashTask& k = t.As<SplashTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    if (k.kind == 0) {
        mx::Scale(m, 3.0f, 3.0f, 3.0f);
    } else {
        mx::Translate(m, 0.0f, 4.5f, 0.0f);
        mx::Scale(m, 1.0f, 2.0f, 1.0f);
    }
    w.Draw(k.n++, m);
    if (k.n == 0x1356 || k.n == 0xA2) w.ts.KillCurrent();
}

void SpawnSplash(World::Impl& w, float x, float y, float z, int kind) {  // 0x004248B0
    SplashTask* k = w.NewTask<SplashTask>(TaskSplash, 0x00424800);
    k->pos[0] = x;
    k->pos[1] = y;
    k->pos[2] = z;
    k->kind = static_cast<int16_t>(kind);
    k->n = static_cast<int16_t>(kind == 0 ? 0x1339 : 0x94);
}

// World position of a node: (model-view node matrix) x inverse(view) (0x004A9230 view, 0x004A8D20, 0x004A92A0, 0x004A8CC0)
void NodeWorldPos(World::Impl& w, const Mat4& mv, float out[3]) {
    Mat4 top = mx::Inverse(w.cam.view);
    mx::MulLeft(top, mv);
    mx::GetTranslation(top, out);
}

// The splash at a node (state 10 / 14 of 0x004231C0): its world x / z, the given y
void SplashAtNode(World::Impl& w, HydraTask& a, int node, float y, int kind) {
    float p[3];
    NodeWorldPos(w, a.mv[node % 27], p);
    SpawnSplash(w, p[0], y, p[2], kind);
}

int WrapPoint(const HydraState& h) { return h.mode != 0 ? 0xDD : 0x11E; }

const std::array<float, 3>& PathAt(const HydraTask& a, int k) {
    static const std::array<float, 3> zero = { 0.0f, 0.0f, 0.0f };
    return k >= 0 && k < static_cast<int>(a.path.size()) ? a.path[static_cast<size_t>(k)] : zero;
}

int Wrap16(int v) {  // and 0xFFFF; > 0x8000: - 0x10000
    v &= 0xFFFF;
    return v > 0x8000 ? v - 0x10000 : v;
}

// 0x00424190(segment, obj): the path points of one segment of +0x77B8 {s16 camera path, s16 first tick, s16 last tick}
// (0x004042D0 positions) appended at +0x1334
void BuildSegment(World::Impl& w, HydraTask& a, int seg) {
    const uint32_t e = a.segTab + static_cast<uint32_t>(seg) * 6;
    for (int t = w.exe.S16(e + 2); t <= w.exe.S16(e + 4); ++t) {
        float p[3] = {}, tg[3] = {};
        int roll = 0;
        const int path=w.exe.S16(e)+(w.typingReferenceActive ? 0x4022 : 0);
        if (!w.cameraBank.Evaluate(path, static_cast<float>(t), p, tg, &roll, false))
            w.Note("stage-3 boss path camera " + std::to_string(w.exe.S16(e)) + " missing (0x00424190)");
        if (a.counter1334 >= 0 && a.counter1334 < static_cast<int>(a.path.size()))
            a.path[static_cast<size_t>(a.counter1334)] = { p[0], p[1], p[2] };
        ++a.counter1334;
    }
}

// 0x00424250: body position by state, path cursor, camera position from the camera segments (held after a bite)
void Move5(World::Impl& w, HydraTask& a) {
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    CameraContext& c0 = w.runner->camera.ctx[0];
    if (a.behaviour == 0xC) {
        if (a.hitTick != 0) {
            float f = F32(exe, 0x004C4380);
            if (h.mode == 1) {
                if (a.pass == 0) f = F32(exe, 0x004C4CB8);
                if (a.pass == 4) f = F32(exe, 0x004C43AC);
            }
            const float n = static_cast<float>(a.hitTick);
            a.pos[0] = ((c0.pos[0] - a.start[0]) * a.f1340 / n) * f + a.start[0];
            a.pos[2] = ((c0.pos[2] - a.start[1]) * a.f1340 / n) * f + a.start[1];
        }
        a.f1340 = a.f1340 + F32(exe, 0x004C4D10);
    } else if (a.behaviour == 0xD) {
        const auto& p = PathAt(a, a.cursor);
        a.pos[0] = (p[0] - a.start[0]) * a.f1340 + a.start[0];
        a.pos[2] = (p[2] - a.start[1]) * a.f1340 + a.start[1];
    } else {
        {
            const auto& p = PathAt(a, a.cursor);
            a.pos[0] = p[0];
            a.pos[2] = p[2];
        }
        if (h.mode == 1) {
            const int c = a.cursor;
            if (0x2F2 < c && c < 0x335) {
                const auto& p = PathAt(a, c);
                const float n = static_cast<float>(c - 0x2F3);
                a.pos[0] = (F32(exe, 0x0055CB70) - p[0]) * n * F32(exe, 0x0055CB6C) + p[0];
                a.pos[2] = (F32(exe, 0x0055CB68) - p[2]) * n * F32(exe, 0x0055CB6C) + p[2];
            }
            if (0x334 < c && c < 0x399) {
                const auto& p = PathAt(a, c);
                const float n = static_cast<float>(c - 0x335);
                a.pos[0] = (F32(exe, 0x0055CB64) - p[0]) * n * F32(exe, 0x0055CB60) + p[0];
                a.pos[2] = (F32(exe, 0x0055CB5C) - p[2]) * n * F32(exe, 0x0055CB60) + p[2];
            }
        }
        if (h.mode == 0) {
            a.pos[1] = -15.0f;  // 0xC1700000
            if (a.cursor < 0x42) a.pos[2] = static_cast<float>(0x41 - a.cursor) * F32(exe, 0x0055CB58) + a.pos[2];
        } else if (a.pass == 0 && ((0x840 < a.cursor && a.anim.motion[0] != 0x3D) || (200 < a.cursor && a.cursor < 0x140))) {
            a.pos[1] = (F32(exe, 0x0055CB54) - a.pos[1]) * F32(exe, 0x0055CB50) + a.pos[1];
        } else {
            a.pos[1] = -12.5f;  // 0xC1480000
        }
    }
    if (0 < a.delay) --a.delay;
    if (a.delay < 1) {
        ++a.cursor;
        if (a.pathLen <= a.cursor) {
            ++a.laps;
            a.cursor = static_cast<int16_t>(WrapPoint(h));
        }
    }
    int seg, tick;
    if (0 < a.hold) {
        --a.hold;
        tick = a.camTick;
        seg = a.camSeg;
    } else {
        tick = a.counter1334;
        seg = a.counter1330;
    }
    {
        float tg[3] = {};
        int roll = 0;
        const int cam = exe.S16(a.camTab + static_cast<uint32_t>(seg) * 6)+(w.typingReferenceActive ? 0x4010 : 0);
        if (!w.cameraBank.Evaluate(cam, static_cast<float>(tick), c0.pos, tg, &roll, false))  // Cam_Evaluate 0x004041E0
            w.Note("stage-3 boss camera " + std::to_string(cam) + " missing (0x00424250)");
    }
    if (h.mode == 0) c0.pos[1] = static_cast<float>(c0.pos[1] + F64(exe, 0x0055CAF8));
    if (a.behaviour != 9) {
        ++a.timer1338;
        ++a.counter1334;
        if (exe.S16(a.camTab + static_cast<uint32_t>(a.counter1330) * 6 + 4) < a.counter1334) {
            ++a.counter1330;
            if (a.segCount < a.counter1330) {
                a.counter1330 = 1;
                if (h.mode == 0) {
                    a.counter1334 = 0x29;
                    a.timer1338 = 0x11E;
                } else {
                    a.counter1334 = 0xDD;
                    a.timer1338 = 0xDD;
                }
                return;
            }
            a.counter1334 = exe.S16(a.camTab + static_cast<uint32_t>(a.counter1330) * 6 + 2);
        }
    }
}

// 0x00421F20 for variant 5 (+0x131B = 8): in states 10..13 the nodes up to the head take their yaw (and in mode 0
// between path points 0x440..0x579 their pitch) from the path points ahead (walks of 3 / 4 points per node), state 12
// turns node 1 to the camera and lets the others relax, state 14 a sine yaw wave; nodes before the head rotate X, Y, Z
// (no extra angles), no root translation. World chain (draws / shadow consumers) and model-view chain (hit points).
void BodyChain(World::Impl& w, HydraTask& a) {
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    const Skeleton& s = w.EnemySkeleton(a.anim.kind);
    const CameraContext& c0 = w.runner->camera.ctx[0];
    const int wrap = WrapPoint(h);
    int k54 = a.cursor, k44 = a.cursor;  // local_54 / local_44
    int yawAcc = 0, pitchAcc = 0;        // local_4c / local_20
    float yTmp = 0;                      // local_3c (only stored into +0x360, which nothing reads)
    Mat4 m = Mat4::Identity(), mv = w.cam.view;
    a.mvValid = true;
    for (Mat4* q : { &m, &mv }) {
        mx::Translate(*q, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotZ(*q, a.rot[2]);
        mx::RotY(*q, a.rot[1]);
        mx::RotX(*q, a.rot[0]);
    }
    const int st = a.behaviour;
    const int head = a.head;
    for (int i = 0; i < a.nodes && i < 27; ++i) {
        EnemyNode& n = a.anim.nodes[i & 31];
        if (st == 10 || st == 11 || st == 12) {
            if (i == 1) {
                n.cur[0] = 0;
                n.cur[2] = 0;
                const auto& p = PathAt(a, a.cursor);
                a.px[1] = p[0];
                a.py[1] = p[1];
                a.pz[1] = p[2];
                a.qx[1] = a.px[1];
                a.qy[1] = p[1];
                a.qz[1] = p[2];
                if (st == 12) {
                    const float dx = a.pos[0] - c0.pos[0], dz = a.pos[2] - c0.pos[2];
                    const int ang = -TruncToAngle(std::atan2(static_cast<double>(dx), static_cast<double>(dz)));  // x 0x0055CB20
                    const int d = Wrap16(0xC000 - ang - a.yprev[1]);
                    a.yprev[1] += d / 6;
                    n.cur[1] = a.yprev[1];
                    if (h.mode == 0 && a.pass == 4) {
                        a.yprev[1] = TruncToAngle(std::atan2(static_cast<double>(dx), static_cast<double>(dz))) - 0x4000;
                        n.cur[1] = a.yprev[1];
                    }
                } else {
                    n.cur[1] = 0;
                }
            }
            if (st == 12) {
                bool tail = true;  // LAB_0042240C / LAB_00422414
                if (1 < i && i <= head) {
                    const int d = Wrap16(-a.yprev[i]);
                    if (h.mode == 1 && a.pass == 4) {
                        const float f = static_cast<float>(i / 2 + 4);
                        if (f != 0.0f) a.yprev[i] = static_cast<int>(static_cast<float>(d) / f + static_cast<float>(a.yprev[i]));
                    } else {
                        a.yprev[i] += d / 10;
                    }
                    n.cur[1] = a.yprev[i];
                    if (h.mode == 0 && a.pass == 4) {
                        a.yprev[i] = 0;
                        n.cur[1] = 0;
                    }
                }
                if (h.mode != 0) tail = false;
                if (tail && 0 < i && i <= head && a.pass == 3 && 0x57A <= a.cursor) {
                    const int d = Wrap16(n.cur[2] - a.zprev[i]);
                    a.zprev[i] += d / 16;
                    n.cur[2] = a.zprev[i];
                }
            } else if (0 < i && i <= head &&
                       (h.mode != 0 || a.anim.motion[0] != 0x3D || i < 0x10 || a.counter1330 == 2 || a.counter1330 == 6)) {
                // yaw: towards the path point 3 (4 in pass 2, mode 0) ahead of the previous node's point
                const int lim = (h.mode == 0 && a.pass == 2) ? 4 : 3;
                int k = k54;
                float x = 0, z = 0;
                for (int c = 0; c < 0x3C; ++c) {
                    x = PathAt(a, k)[0];
                    z = PathAt(a, k)[2];
                    if (lim <= c) break;
                    if (a.pathLen <= ++k) k = wrap;
                }
                k54 = k;
                const float dx = x - a.px[i], dz = z - a.pz[i];
                const int yawA = TruncToAngle(std::atan2(static_cast<double>(dx), static_cast<double>(dz)));
                a.px[i + 1] = x;
                a.py[i + 1] = yTmp;
                a.pz[i + 1] = z;
                // pitch: towards the path point 3 ahead of the previous node's pitch point
                {
                    int c = 0;
                    for (;;) {
                        if (a.pathLen <= ++k44) k44 = wrap;
                        if (0x3C <= ++c) break;
                        if (3 <= c) break;
                    }
                }
                const auto& p = PathAt(a, k44);
                const float X = p[0], Y = p[1], Z = p[2];
                const float ddz = Z - a.qz[i], ddx = X - a.qx[i], ddy = -(Y - a.qy[i]);
                const float hl = std::sqrt(ddx * ddx + ddz * ddz);
                const int pitch = TruncToAngle(std::atan2(static_cast<double>(ddy), static_cast<double>(hl)));
                a.qx[i + 1] = X;
                a.qy[i + 1] = Y;
                a.qz[i + 1] = Z;
                yTmp = Y;
                n.cur[1] = ((yawA + 0x4000) & 0xFFFF) - yawAcc;
                if (h.mode == 0 && 0x43F < a.cursor && a.cursor < 0x57A) n.cur[2] = pitch - pitchAcc;
                if (a.cursor == 0x57A) a.splashed = 0;
                pitchAcc += n.cur[2];
                a.yprev[i] = n.cur[1];
                yawAcc += n.cur[1];
                a.zprev[i] = n.cur[2];
                if (i == head) a.headCursor = static_cast<int16_t>(k54);
            }
        } else if (st == 0xD) {
            if (0 < i && i <= head) {
                bool go;
                if (h.mode == 0) go = a.anim.motion[0] != 0x3D || i < 0x10 || a.counter1330 == 2 || a.counter1330 == 6;
                else go = !(h.mode == 1 && 0xF < i && a.anim.motion[0] == 0x3D);
                if (go) {
                    const int lim = a.pass == 2 ? 4 : 3;
                    int k = k54;
                    float x = 0, z = 0;
                    for (int c = 0; c < 0x3C; ++c) {
                        x = PathAt(a, k)[0];
                        z = PathAt(a, k)[2];
                        if (lim <= c) break;
                        if (a.pathLen <= ++k) k = wrap;
                    }
                    k54 = k;
                    const float dx = x - a.px[i], dz = z - a.pz[i];
                    const int a16 = (TruncToAngle(std::atan2(static_cast<double>(dx), static_cast<double>(dz))) + 0x4000) & 0xFFFF;
                    const int d = Wrap16(a16 - yawAcc - a.yprev[i]);
                    a.yprev[i] = d / 32 + a.yprev[i];
                    n.cur[1] = a.yprev[i];
                    yawAcc += a.yprev[i];
                    a.px[i + 1] = x;
                    a.pz[i + 1] = z;
                    if (i == head) a.headCursor = static_cast<int16_t>(k);
                }
            }
        } else if (st == 0xE) {
            if (0 < i && i <= head) {
                const int tt = static_cast<int>(static_cast<float>(static_cast<float>(i) * F32(exe, 0x004E1FDC)) / static_cast<float>(head));
                const float ang = static_cast<float>(static_cast<double>(tt) * mx::kToRad);
                const float sv = static_cast<float>(std::sin(static_cast<double>(ang)) * F32(exe, 0x0055CB1C));
                n.cur[1] = static_cast<int>(sv + static_cast<float>(n.cur[1]));
            }
        }
        if (i == a.jawB) m = a.anim.nodes[head & 31].world, mv = a.mv[head % 27];
        const SkeletonNode* d = SkelNode(s, i);
        for (Mat4* q : { &m, &mv }) {
            if (d) mx::Translate(*q, d->offset[0], d->offset[1], d->offset[2]);
            if (i < head) {
                mx::RotX(*q, n.cur[0]);
                mx::RotY(*q, n.cur[1]);
                mx::RotZ(*q, n.cur[2]);
            } else {
                mx::RotZ(*q, a.az[i]);
                mx::RotY(*q, a.ay[i]);
                mx::RotX(*q, a.ax[i]);
                mx::RotZ(*q, n.cur[2]);
                mx::RotY(*q, n.cur[1]);
                mx::RotX(*q, n.cur[0]);
            }
        }
        n.world = m;
        a.mv[i] = mv;
        mx::Transform(mv, n.local, n.hit);
    }
}

// 0x004219E0 for variant 5: nodes 1.. ; the breath at the head node in state 11; the water wakes along the body
// (mode 0, states 10..13); the jaw splash at the water line in pass 3
void BodyDraw(World::Impl& w, HydraTask& a) {
    w.LightChar();  // the callers wrap 0x004219E0 in 0x0041DC70 / 0x0041DCC0 (renderer state)
    struct Restore { World::Impl& w; ~Restore() { w.LightScene(); } } restore{ w };
    HydraState& h = H(w);
    const int st = a.behaviour;
    for (int i = 1; i < a.nodes && i < 27; ++i) {
        const EnemyNode& n = a.anim.nodes[i & 31];
        const Mat4& mat = a.mv[i];
        w.Draw(n.model, mat, 8, -1.0f, false, i, true);
        if (st == 0xB && i == a.head) {
            ++a.breath;
            Mat4 m = mat;
            mx::Translate(m, 7.0f, -0.5f, 0.0f);
            mx::RotY(m, 0x4000);
            mx::Scale(m, 1.5f, 1.5f, 1.5f);
            w.Draw(a.breath % 0x27 + 0x97A, m, 8, -1.0f, false, -1, true);
            w.Draw((a.breath & 31) + 0x199C, m, 8, -1.0f, false, -1, true);
        }
        const int mo = a.anim.motion[0];
        if (h.mode == 0 && 10 <= st && st <= 13 && (mo != 0x3D || 0x36 < a.anim.tick[0]) && (mo != 0x3B || i < 0x10) &&
            (mo != 0x3C || i < 0x10) && i < a.head && 0x5E < a.cursor && (a.cursor < 0x51A || 0x579 < a.cursor)) {
            const int model = static_cast<int>(w.seedCounter % 0x18) + 0x8CE;  // 0x009A32A0
            Mat4 m = mat;
            mx::Translate(m, 4.0f, -1.5f, Bits(0xC04CCCCD));
            mx::RotZ(m, 0x4000);
            mx::RotY(m, 0x8000);
            mx::RotX(m, 0x4000);
            w.Draw(model, m, 8, -1.0f, false, -1, true);
            m = mat;
            mx::Translate(m, 4.0f, -1.5f, Bits(0x404CCCCD));
            mx::RotZ(m, 0x4000);
            mx::RotY(m, 0);
            mx::RotX(m, 0x4000);
            mx::Scale(m, -1.0f, 1.0f, 1.0f);
            w.Draw(model, m, 8, -1.0f, false, -1, true);
        }
        if (a.pass == 3 && 0x43E < a.cursor && i == a.jawB) {
            float p[3];
            NodeWorldPos(w, mat, p);
            const float lim = F32(w.exe, 0x004C4C78);
            if (h.mode == 0 && st != 0xE && a.splashed == 0 && p[1] < lim) {
                w.Sound(0x4116A9);
                SpawnSplash(w, p[0], -15.0f, p[2], 0);  // (+ 0x00420810(6, 0x28): no-op)
                a.splashed = 1;
            }
            if (a.splashed == 1 && lim < p[1]) a.splashed = 0;
        }
    }
}

void Body5Death(World::Impl& w, HydraTask& a) {
    HydraState& h = H(w);
    CameraDirector& cd = w.runner->camera;
    const uint32_t g = a.flags34 & 2;
    if (g == 0 || !(a.flags34 & 4)) w.AddScore(g == 0 ? 1 : 0, 0x5DC);
    else w.AddScore(w.Rand(0x0042336D) & 1, 0x5DC);
    if (w.cfg.portTyping) w.TypingController(a.typingPlayer).addBasePoints(700);
    w.bossBar = 0.0f;
    a.behaviour = 0xE;
    a.cooldown133C = 0;
    w.AnimSetMotion(a, 0x41);
    if (h.mode == 0) {
        a.pos[0] = Bits(0xC4546CCD);
        a.pos[1] = Bits(0xC0A00000);
        a.pos[2] = Bits(0xC584E000);
    } else {
        a.pos[0] = Bits(0xC4C88333);
        a.pos[1] = Bits(0xC1400000);
        a.pos[2] = Bits(0xC575799A);
    }
    a.rot[1] = static_cast<int>(0xFFFFCEE4u);
    a.phase = 0;
    a.phaseStep = 0x400;
    a.amp = 1.25f;  // 0x3FA00000
    --w.attackers;
    --w.liveEnemies;
    cd.forceState6 = false;  // 0x009CA094
    cd.settled = true;       // 0x009C6F2D
    cd.returning = false;    // 0x009C6F2C
    HeadKillLock(w, a);
    Roar(w, 7);
    if (w.runner->cfg.gameType == 3) w.bossActive = 0;  // 0x009CA0EA
}

// The pass after a rise / lunge: +0x59E + 1, past 6 back to 1 (mode 0) or 0
void NextPass(World::Impl& w, HydraTask& a) {
    ++a.pass;
    if (6 < a.pass) a.pass = static_cast<int8_t>(H(w).mode == 0 ? 1 : 0);
}

// 0x004231C0
void TaskBody5(Task& t) {
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    CameraDirector& cd = w.runner->camera;
    CameraContext& c0 = cd.ctx[0];
    if (a.lock) a.slot120 = static_cast<int8_t>(a.lock->slot);
    h.heads[0] = &a;  // 0x007DC6F4
    h.headSerial[0] = t.serial;
    auto& cnt = a.anim.counter[0];
    // ---- shots
    if (a.flags34 & 8) {
        const uint32_t f = a.flags34;
        int p;
        if ((f & 2) && (f & 4)) p = w.Rand(0x004231FE) & 1;
        else p = (f & 2) == 0 ? 1 : 0;
        bool damage = w.cfg.portTyping && a.typingStrike;
        if (!damage && (a.behaviour == 0xB || a.behaviour == 0xC) &&
            (((a.flags34 & 2) && a.hitNode[0] == 0x18) || ((a.flags34 & 4) && a.hitNode[1] == 0x18))) {
            const int j1 = a.anim.nodes[a.jawA & 31].cur[2], j2 = a.anim.nodes[a.jawB & 31].cur[2];
            int d = j1 - j2;
            if (d < 0) d = j2 - j1;
            damage = 0x1700 < d;
        }
        if (damage) {
            w.AddScore(p, 10);
            w.SpawnNodeBlood(a, a.hitNode[p]);  // 0x00407200
            ++a.hits;
            float factor = 1.0f;
            if (w.runner->cfg.gameType == 1) {  // 0x004232C1: the shooter's item factor (-1.0: float 0x004E30F0)
                float boost = 0.0f;
                if (!ItemDamage(w, p, &factor, &boost)) factor = 1.0f;
                if (factor == F32(exe, 0x004C4C64)) factor = F32(exe, 0x004E30F0);
            }
            int dmg = static_cast<int>(static_cast<double>(factor) * static_cast<double>(F32(exe, w.players == 2 ? 0x0055CB4C : 0x0055CB48)));
            if (w.cfg.portTyping && a.typingStrike) {
                a.typingStrike = false;
                dmg = -int(TypingTowerSwimmingWordDamage());
            } else if (a.typingStrike) dmg = static_cast<int>(TypingDamage(a, static_cast<float>(dmg)));
            a.hp = static_cast<int16_t>(a.hp + dmg);
            w.bossBar = static_cast<float>(a.hp) * F32(exe, 0x0055CB44);
            if (w.bossBar < 0.0f) w.bossBar = 0.0f;
            if (a.hp < 1) Body5Death(w, a);
        } else {
            w.Sound(0x1216A9);
            SparkTask* k = w.NewTask<SparkTask>(TaskSpark, 0x004246F0);  // 0x004247D0
            k->obj = &a;
            k->objSerial = t.serial;
            k->node = a.hitNode[p];
            k->n = 0;
        }
        a.flags34 &= ~0xEu;
    }
    switch (a.behaviour) {
    case 8:
        BuildSegment(w, a, a.counter1330);
        if (a.segCount < ++a.counter1330) {
            a.counter1330 = 0;
            a.pathLen = static_cast<int16_t>(a.counter1334);
            a.counter1334 = 0;
            a.pos[0] = PathAt(a, a.cursor)[0];
            a.pos[2] = PathAt(a, a.cursor)[2];
            a.behaviour = 9;
            if (w.cfg.portTyping) { w.typing.setWordSelection(0,150); w.typing2.setWordSelection(0,150); }
        }
        break;
    case 9:
        a.behaviour = 10;
        cd.forceState6 = true;  // 0x009CA094
        break;
    case 10: {
        if (h.mode == 0) {
            if (a.cursor == 0x5F) w.Sound(0x4116A9);  // (+ 0x00420810(4, 0x1E): no-op)
        }
        if (h.mode == 1 && a.cursor == 10) w.NewTask<HydraWater>(TaskHydraWater, 0x00424C10);  // 0x00424D90
        if (a.cursor == 0x3C) {
            w.Voice(0x83);  // 0x00435B80
            w.letterbox = 5;  // 0x009CA0F4
        }
        if (a.cursor == 0xB4) w.letterbox = 1;
        const int mo = a.anim.motion[0];
        const int tk = a.anim.tick[0];
        if (h.mode == 0) {
            if (((mo == 0x3D && tk == 0x36) || ((mo == 0x3B || mo == 0x3C) && tk == a.hitTick + 4)) && a.pass != 3) {
                float p[3];
                NodeWorldPos(w, a.mv[a.jawB % 27], p);
                w.Sound(0x4116A9);  // (+ 0x00420810(6, 0x1E): no-op)
                SpawnSplash(w, p[0], -15.0f, p[2], 0);
            }
            if (a.counter1330 == 4 && a.counter1334 == 0x2B2 && a.cursor != 0x523) a.cursor = 0x523;
        } else {
            if (mo == 0x3D && tk == 0x36) {
                float p[3];
                NodeWorldPos(w, a.mv[a.head % 27], p);
                w.Sound(0xB16A9);
                w.shakeTimer = 0x30;  // 0x009C8E8C
                SpawnSplash(w, p[0], -13.5f, p[2], 1);
            }
            if (a.counter1330 == 6 && a.counter1334 == 0x325 && a.cursor != 0x6F4) a.cursor = 0x6B8;
        }
        Move5(w, a);
        if (a.cursor == 0xB4) {
            w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
            w.bossBar = 1.0f;
        }
        if (exe.S16(a.evTab + static_cast<uint32_t>(a.pass) * 6 + 2) == a.cursor) {
            a.behaviour = 0xB;
            a.hits = 0;
            w.AnimSetMotion(a, 0x42);
            Roar(w, 3);
        }
        if (h.mode != 0 || 0 < a.pass) {
            if (a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) - 1 && a.anim.motion[0] != 0x42) {
                w.AnimSetMotion(a, 0x42);
                cnt = 0;
            }
            if (a.anim.motion[0] != 0x42 || a.anim.tick[0] != 0) ++cnt;
        }
        break;
    }
    case 0xB: {
        Move5(w, a);
        const bool done = w.typingReferenceActive ? a.hits>=1 :
            w.players == 2 ? 2 < a.hits : (w.players < 2 && 1 < a.hits);
        if (done) {
            Roar(w, 1);
            NextPass(w, a);
            a.behaviour = 10;
            a.start[0] = a.pos[0];
            a.start[1] = a.pos[2];
            a.f1340 = 0;
            w.AnimSetMotion(a, 0x3D);
            break;
        }
        if (h.mode == 0) {
            const int c = a.pass;
            bool attack;
            if (c != 6) attack = exe.S16(a.evTab + static_cast<uint32_t>(c) * 6 + 4) == a.cursor;
            else attack = a.cursor < exe.S16(a.evTab + 0x26) && exe.S16(a.evTab + 0x28) == a.cursor;
            if (attack) {
                a.behaviour = 0xC;
                if (a.pass % 2 == 0) {
                    w.AnimBlendTo(a, 0x3C, 0, 4);
                    a.hitTick = 0x26;
                } else {
                    w.AnimBlendTo(a, 0x3B, 0, 4);
                    a.hitTick = 0x2F;
                }
                a.start[0] = a.pos[0];
                a.start[1] = a.pos[2];
                a.f1340 = 0;
                w.Sound(0x4216A9);
                Roar(w, 4);
            }
        } else if (exe.S16(a.evTab + static_cast<uint32_t>(a.pass) * 6 + 4) == a.cursor) {
            a.behaviour = 0xC;
            const int mo = exe.S16(0x0058907C + static_cast<uint32_t>(a.pass) * 2);
            w.AnimSetMotion(a, mo);
            a.hitTick = static_cast<int16_t>(mo == 0x3B ? 0x2F : 0x2D);
            a.start[0] = a.pos[0];
            a.start[1] = a.pos[2];
            a.f1340 = 0;
            Roar(w, 4);
            a.pos[1] = -13.5f;  // 0xC1580000
        }
        if (a.anim.tick[0] != 0x1E) ++cnt;
        break;
    }
    case 0xC: {
        Move5(w, a);
        const bool done = w.typingReferenceActive ? a.hits>=1 :
            w.players == 2 ? 2 < a.hits : (w.players < 2 && 1 < a.hits);
        if (done) {
            w.Sound(0x241AA9);
            NextPass(w, a);
            a.behaviour = 10;
            w.AnimBlendTo(a, 0x3D, 8, 0xC);
            a.start[0] = a.pos[0];
            a.start[1] = a.pos[2];
            a.f1340 = 0;
            a.camSeg = static_cast<int16_t>(a.counter1330);
            a.camTick = static_cast<int16_t>(a.counter1334);
            break;
        }
        if (a.anim.tick[0] == a.hitTick) {
            a.hold = 0x1E;
            if (cd.modeCur == 2 && 0 < w.players && (w.player[0].state == 5 || w.player[1].state == 5)) {
                if (w.players == 2) {
                    w.PlayerTakeHit(0, 1, 9);  // Player_TakeHit 0x00415300
                    w.PlayerTakeHit(1, 1, 9);
                } else {
                    if (w.playersInGame == 0) a.target = 0;
                    if (w.playersInGame == 1) a.target = 1;
                    w.PlayerTakeHit(a.target, 1, 9);
                }
            }
            NextPass(w, a);
            a.start[0] = a.pos[0];
            a.start[1] = a.pos[2];
            a.behaviour = 10;
            a.f1340 = 0;
            a.camSeg = static_cast<int16_t>(a.counter1330);
            a.camTick = static_cast<int16_t>(a.counter1334);
        }
        ++cnt;
        break;
    }
    case 0xD:
        Move5(w, a);
        if (h.mode == 0 && a.anim.tick[0] == a.hitTick + 4 && a.pass != 3) w.Sound(0x4116A9);
        if (!(a.f1340 < F32(exe, 0x004C4380))) a.behaviour = 10;
        a.f1340 = a.f1340 + F32(exe, 0x004C4C88);
        ++cnt;
        break;
    case 0xE: {
        if (!a.anim.ended) ++cnt;
        if (w.runner->flags[4] == 1) {  // 0x009C7204
            if (a.actorSlot >= 0 && a.actorSlot < 14) {
                w.actorSlots[a.actorSlot] = nullptr;
                a.actorSlot = -1;
            }
            BossRecords(w);  // 0x00425F40: boss-mode (game type 3) records (world_modes_boss.cpp)
            w.EnemyDestroy(a);
            return;
        }
        const int tk = a.anim.tick[0];
        if (h.mode == 0) {
            if (0x46 <= tk && tk <= 0x6E)
                a.pos[1] = static_cast<float>(0x6E - tk) * F32(exe, 0x004C4C58) - F32(exe, 0x004C4398);
            if (tk == 0x6E) {
                float p[3];
                NodeWorldPos(w, a.mv[a.jawB % 27], p);
                w.Sound(0x4116A9);  // (+ 0x00420810(6, 0x1E): no-op)
                SpawnSplash(w, p[0], -15.0f, p[2], 0);
            }
            if (0x6E <= a.anim.tick[0]) {
                const float ang = static_cast<float>(static_cast<double>(a.phase) * mx::kToRad);
                a.pos[1] = static_cast<float>(std::sin(static_cast<double>(ang)) * a.amp) - F32(exe, 0x004C4398);
                a.phase += a.phaseStep;
                if (a.phase % 0x10000 == 0) a.amp = a.amp * F32(exe, 0x004E30F8);
            }
        } else if (tk == 0x6E) {
            float p[3];
            NodeWorldPos(w, a.mv[a.jawB % 27], p);
            w.Sound(0xB16A9);
            w.shakeTimer = 0x30;
            SpawnSplash(w, p[0], -12.0f, p[2], 1);
        }
        break;
    }
    default:
        break;
    }
    // ---- skeleton, chain, draw (0x00411090 without the boss light, 0x00421F20, 0x004219E0)
    DrawSkeleton(w, a);
    if (a.behaviour != 0xE) a.rot[1] = 0x8000;
    BodyChain(w, a);
    if (a.behaviour != 8 && a.behaviour != 9) BodyDraw(w, a);
    memcpy(a.body, a.pos, 12);
    mx::Transform(w.cam.view, a.pos, a.viewPos);
    mx::Transform(w.cam.inverse, a.anim.nodes[a.head & 31].hit, a.body);  // camera matrix 0x009A6040[ctx]
    const int st = a.behaviour;
    if (10 <= st && st <= 13) {  // the camera turns to the head
        int pitch = 0, yaw = 0;
        game::DirToAngles(c0.pos[0] - a.body[0], c0.pos[1] - a.body[1], c0.pos[2] - a.body[2], &pitch, &yaw);  // 0x004016B0
        if (a.hold < 1) {
            const int y16 = c0.yaw & 0xFFFF;
            const int d = Wrap16(yaw - y16);
            c0.yaw = y16 + d / 8;
            const int mo = a.anim.motion[0];
            if (mo == 0x3B || mo == 0x3C || mo == 0x3E || mo == 0x3F || mo == 0x3D) {
                c0.pitch += (pitch - c0.pitch) / 4;
                if (c0.pitch < 0) c0.pitch = 0;
            } else {
                c0.pitch = 0;
            }
        } else {
            c0.pitch = 0;
        }
        w.RegisterTarget(&a);  // 0x00405160
    }
    w.SyncLock(a);
}

void TaskBody5Ctor(Task& t) {  // 0x00420360
    World::Impl& w = *CurrentWorld();
    HydraTask& a = t.As<HydraTask>();
    HydraState& h = H(w);
    const ExeImage& exe = w.exe;
    h.heads[0] = &a;
    h.headSerial[0] = t.serial;
    a.index = 8;
    a.hp = 0x78;
    InitObject(w, a, 0x48, 0x42);  // Anim_InitEnemy 0x00410440
    a.pos[1] = a.pos[1] + F32(exe, 0x004C49C0);
    a.anim.rotOrder = 5;
    a.radius = F32(exe, 0x004C4E48);
    a.nodeCb = 0;
    a.nodeFn = NoNodeDraw;  // +0x12EC = 0x00420810
    a.behaviour = 8;
    a.flags34 |= 0x80080000u;
    a.nodes = 0x1B, a.head = 0x18, a.jawA = 0x19, a.jawB = 0x1A;
    a.cursor = 0;
    a.headCursor = 0;
    if (h.mode == 0) {
        a.segTab = 0x00588F54;
        a.camTab = 0x00588FBC;
        a.evTab = 0x00589024;
        a.segCount = 7;
    } else {
        a.segTab = 0x00588F84;
        a.camTab = 0x00588FEC;
        a.evTab = 0x00589050;
        a.segCount = 8;
        w.NewTask<HydraMarks>(TaskHydraMarks, 0x00424E10);  // 0x00424FE0
    }
    a.pass = 0;
    a.delay = 0;
    a.hits = 0;
    a.hold = 0;
    a.breath = 0;
    a.splashed = 0;
    a.laps = 0;
    a.path.assign(0x960, { 0.0f, 0.0f, 0.0f });
    for (int i = 0; i < 27; ++i) {
        a.ax[i] = a.ay[i] = a.az[i] = 0;
        a.px[i] = a.pos[0];
        a.py[i] = a.pos[1];
        a.pz[i] = a.pos[2];
        a.zprev[i] = a.yprev[i] = 0;
        a.anim.nodes[i].cur[0] = a.anim.nodes[i].cur[1] = a.anim.nodes[i].cur[2] = 0;
    }
    ++w.attackers;
    ++w.liveEnemies;
    a.lock = &w.lockTargets[w.nextLockTarget++];
    w.SyncLock(a);
    w.lockOn.AddDirect(a.lock);  // 0x00408E80
    a.slot120 = static_cast<int8_t>(a.lock->slot);
    a.fn = 0x004231C0;
    t.fn = TaskBody5;
}

// ---------------------------------------------------------------------------------------------------------------------
// Actor type 0x44 state 14 (0x004736D0 -> task 0x004758E0, block 0x378): a scaled model (P s16 4, scale P floats 8 / 0xC
// / 0x10) at the record, lifetime P s16 0 area changes (0x00466640); Mdl_SelectAlt when 0x009A2BB4 != 0
// ---------------------------------------------------------------------------------------------------------------------
struct Decor44Task : ActorTask {
    int16_t life = 0;      // +0x11C
    int8_t area = 0;       // +0x196
    int8_t areaCount = 0;  // +0x197
    float pos[3] = {};     // +0x19C
    float scale[3] = {};   // +0x1A8
    float dz = 0;          // +0x1C8
    int rot[3] = {};       // +0x1CC
    int16_t model = 0;     // +0x28C
};

void TaskDecor44(Task& t) {
    World::Impl& w = *CurrentWorld();
    Decor44Task& k = t.As<Decor44Task>();
    auto destroy = [&] {  // Actor_Destroy 0x00409CC0
        k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
        for (World::Impl::Target& tg : w.targetCopy)
            if (tg.obj == &k) tg.obj = nullptr;
        w.ts.KillCurrent();
    };
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) return destroy();
    if (static_cast<int8_t>(w.runner->variant) != k.area) {
        if (k.life < ++k.areaCount) return destroy();
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.dz + k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::Scale(m, k.scale[0], k.scale[1], k.scale[2]);
    const bool alt = w.runner->cfg.gameType != 2 && w.altModels != 0;  // 0x004185E0
    w.Draw(k.model, m, 8, -1.0f, alt);
}

}  // namespace

void World::Impl::SpawnDecor44(const SpawnRecord& r) {  // 0x004736D0
    Decor44Task* k = NewTask<Decor44Task>(TaskDecor44, 0x004758E0);
    memcpy(k->pos, r.pos, 12);
    k->rot[0] = r.rot[0];
    k->rot[1] = r.rot[1];
    k->rot[2] = r.rot[2];
    k->area = static_cast<int8_t>(runner->variant);
    k->areaCount = 0;
    k->life = r.ParamS16(0);
    k->model = r.ParamS16(4);
    for (int i = 0; i < 3; ++i) {
        const int32_t v = r.ParamS32(8 + 4 * i);
        memcpy(&k->scale[i], &v, 4);
    }
}

// 0x0041FC00: actor type 0x45. Boss block init (unless variant 5), then the variant's function for the next frame.
void World::Impl::SpawnHydra(Task& t, SpawnTask& s) {
    HydraState& h = H(*this);
    const SpawnRecord r = s.rec;
    auto d = std::make_unique<HydraTask>();
    SetTypingSpawnOrigin(*d, r);
    d->record = r;
    d->flags34 = r.flags | 1;  // 0x00408970
    memcpy(d->pos, r.pos, 12);
    memcpy(d->rot, r.rot, 12);
    d->hp = d->hpMax = static_cast<int16_t>(r.state);
    d->lo = static_cast<int16_t>(r.lo);
    if (s.op == 0x09) {
        d->anim.kind = static_cast<int16_t>(static_cast<int8_t>(r.params[0]));  // +0x1F4
        d->variant = static_cast<int8_t>(r.params[1]);                         // +0x130C
    } else {
        Note("actor 0x45 spawned by op " + std::to_string(s.op) + " (+0x130C / +0x1F4 not set)");
    }
    const int gt = runner->cfg.gameType;
    const int scene = runner->scene;
    if (gt == 3) h.mode = scene != 0xF ? 1 : 0;
    else if (scene == 2) h.mode = 2;
    else if (scene == 0xB) h.mode = 0;
    else if (scene == 0xD) h.mode = 1;
    if (d->variant != 5) {
        h.phase = 0;
        h.alive = 5;
        h.total = static_cast<int16_t>(h.mode != 2 ? 0xB4 : 0x96);
        h.quiz = {};
        h.quizPhase = HydraState::QuizPhase::Setup;
        h.quizFrames = 0;
        ++h.quizToken;
        typingQuestion.clear();
        typingQuestionSeconds = 0;
        h.level = static_cast<int8_t>(rank);  // 0x0040A8A0
        const uint32_t row = 0x005890E0 + static_cast<uint32_t>(h.level) * 4;
        const int r1 = Rand(0x0041FC9E);
        h.readyCount = 0;
        h.attacker = 2;
        h.countdown = static_cast<int16_t>(r1 % (exe.S16(row + 2) + 1) + exe.S16(row));
        if (h.mode == 0) h.counter = 0x86DEB2C;
        else if (h.mode == 1) h.counter = 0x1084A3C;
        else if (h.mode == 2) h.counter = 0x1553267;
    }
    d->flags34 = 1;  // +0x34 = 1
    d->actorSlot = -1;
    d->slot120 = -1;
    d->target = 0;  // +0x121: not set by 0x0041FC00 (cleared task memory)
    d->behaviour = 0;
    d->sub = 0;
    d->counter1330 = 0;
    d->f1340 = 0;
    d->counter1334 = 0;
    switch (d->variant) {
    case 0: d->fn = 0x0041FDB0; t.fn = TaskIntroHeadCtor; break;
    case 1: d->fn = 0x004200F0; t.fn = TaskIntroBodyCtor; break;
    case 2: d->fn = 0x0041FE30; t.fn = TaskHeadCtor; break;
    case 3: d->fn = 0x00420180; t.fn = TaskRockCtor; break;
    case 5: d->fn = 0x00420360; t.fn = TaskBody5Ctor; break;
    default:
        Note("stage-3 boss variant " + std::to_string(d->variant) + " (0x0041EBB0 / 0x00420360) not ported");
        d->fn = d->variant == 5 ? 0x00420360 : 0x0041EBB0;
        t.fn = WorldTramp<&World::Impl::TaskPlaceholder>;
        break;
    }
    t.data = std::move(d);
}

bool TypingBoss3(World::Impl& w, ActorTask& actor, TypingTargetSpec& out, bool complete) {
    auto* a = dynamic_cast<HydraTask*>(&actor);
    if (!a) return false;
    out.boss = true;
    if (a->hp < 1 || (a->flags34 & 0x4000000u)) return true;
    HydraState& h = H(w);
    if (a->variant == 2) {
        if (w.cfg.portTyping) {
            if (h.phase != 1 || !h.quiz.acceptingAnswers()) return true;
            const auto found = std::find(kTypingQuizHeadIndices.begin(), kTypingQuizHeadIndices.end(), unsigned(a->index));
            if (found == kTypingQuizHeadIndices.end()) return true;
            const size_t choice = size_t(found - kTypingQuizHeadIndices.begin());
            out.retain = true;
            out.token = 0x300000u + h.quizToken * 8u + unsigned(a->index);
            out.word = &h.quiz.answers()[choice];
            out.node = a->head;
            memcpy(out.view, a->anim.nodes[a->head & 31].hit, 12);
            if (complete) ResolveTypingQuiz(w, h, h.quiz.answer(choice), choice);
            return true;
        }
        if (h.phase != 1 || a->behaviour == 7 || (h.mode != 2 && a->index == 2)) return true;
        out.retain = true;
        out.token = 0x300000u | (static_cast<uint32_t>(h.phase) << 16) | (static_cast<uint32_t>(a->index) << 8);
        memcpy(out.view, a->anim.nodes[a->head & 31].hit, 12);
        if (a->behaviour == 6 || (a->flags34 & 0x8100u)) return true;
        const EnemyNode& j1 = a->anim.nodes[a->jawA & 31];
        const EnemyNode& j2 = a->anim.nodes[a->jawB & 31];
        int d = ((j1.cur[2] - j2.cur[2]) - a->az[a->jawB]) + a->az[a->jawA];
        if (d < 0) d = ((j2.cur[2] - j1.cur[2]) - a->az[a->jawA]) + a->az[a->jawB];
        const uint16_t z = static_cast<uint16_t>(a->zsum);
        if (d <= 0x1700 || (0x1700 < z && z < 0xC000)) return true;
        out.node = a->head;
    } else if (a->variant == 5) {
        if (a->behaviour < 8 || a->behaviour >= 0xE) return true;
        out.retain = true;
        out.token = 0x350000u | static_cast<uint32_t>(h.mode);
        memcpy(out.view, a->anim.nodes[0x18].hit, 12);
        if (w.cfg.portTyping) {
            out.node = 0x18;
            if (complete) TypingDeliver(*a, out.node);
            return true;
        }
        if ((a->behaviour != 0xB && a->behaviour != 0xC) || (a->flags34 & 0x8100u)) return true;
        const int d = std::abs(a->anim.nodes[a->jawA & 31].cur[2] - a->anim.nodes[a->jawB & 31].cur[2]);
        if (d <= 0x1700) return true;
        out.node = 0x18;
    } else {
        return true;
    }
    if (complete) TypingDeliver(*a, out.node);
    return true;
}

}  // namespace game
