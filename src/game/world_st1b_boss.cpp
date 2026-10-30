// game::World - the stage-1 boss: actor 0x22 variant 1 (0x0049B0D0 -> 0x0049B620, behaviour table 0x00598008:
// 0x0049B640 intro, 0x0049B850 first phase, 0x0049C190 second phase, 0x0049C910 death) riding actor 0x23
// (0x0048FD90 -> 0x0048FE90, behaviour table 0x00597268: 0x0048FEB0 intro, 0x00490150 attacks, 0x00490B00 down,
// 0x00490C50 lying); the name card 0x00437A70 / 0x00437AC0, the health bar 0x00435E50 / 0x00435C80, the dust ring
// 0x00491700, the landing flash 0x0049DEA0 and the sound repeat task 0x00435EE0 / 0x00435E90.
// See notes/agents/stage1b.md. CONFIRMED / CODE as in world_st1b.cpp.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/typing_boss_rules.h"
#include "game/world_internal.h"

namespace game {

bool StartTypingChapterChoice(World::Impl& world);

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

int SourceBossRank(World::Impl& w) {
    // Source4376D0 mutates the campaign rank when a boss reads it.
    if (w.typingReferenceActive && w.cfg.gameType!=3) {
        if (w.cfg.eventMode==0) w.rank=std::min(w.rank,12);
        else if (w.cfg.eventMode==1) w.rank=std::min(w.rank,14);
    }
    return w.rank;
}

template <class T> T* AliveTask(World::Impl& w, T* p, uint32_t serial) {
    if (!p) return nullptr;
    for (Task* t : w.ts.Live())
        if (t->serial == serial) return t->data.get() == p ? p : nullptr;
    return nullptr;
}

// Actor 0x22 variant 1 fields beyond Actor22Task (+0x1324 = talk, +0x1338 / +0x133C = mouth there)
struct BossState : TaskData {
    int phase = 0;          // +0x1320 (1 once hp <= P s16 0xC: faster paths / attacks)
    int timer1328 = 0;      // +0x1328 letterbox timer of the taunt lines
    int damage = 0;         // +0x132C hits on the mount this frame (taken in 0x0049D220)
    int count = 0;          // +0x1330
    int steps = 0;          // +0x1334
    float pathT = 0;        // +0x1340 tick on path +0x1350
    float pathStep = 0;     // +0x1344
    float f1348 = 0;        // +0x1348
    int path = 0;           // +0x1350 (camera path 0x104 + path / 0x140 + path)
    int level = 8;          // +0x1354 aggression 0..15
    int hit1358 = 0;        // +0x1358
    int mountHits = 0;      // +0x135C
    int attack1360 = 0;     // +0x1360 the mount's attack landed
    int hits1364 = 0;       // +0x1364
    int landed1368 = 0;     // +0x1368
    int flag136C = 0;       // +0x136C
    float dist = 0;         // +0x1370 the mount's distance to the players (written by the mount)
    float off[3] = {};      // +0x13C0
    float saved[3] = {};    // +0x13CC
    EnemyTask* mount = nullptr;  // +0x1394
    uint32_t mountSerial = 0;
    unsigned typingWordPhase = 0;
    float typingPathFraction = 0;
    int typingInstructions = 0;
};

}  // namespace

// Actor 0x23 (0x0048FD90): the mount (skeleton 0x44)
struct MountTask : EnemyTask {
    int phase = 0;          // +0x1320
    int saved1350 = 0;      // +0x1350 sub-state before a stagger
    float f13C0[3] = {};    // +0x13C0
    int count1330 = 0;      // +0x1330 (variant-1 intro 0x00490D00)
    Actor22Task* rider = nullptr;  // +0x1394
    uint32_t riderSerial = 0;
};

namespace {

BossState& Boss(Actor22Task& a) { return *static_cast<BossState*>(a.boss.get()); }

int JudgmentPath(const World::Impl& w, int native) {
    // Reference rider paths125/126/166/1A1 correspond to103/104/144/17F.
    return w.typingReferenceActive ? 0x4000 + native + 0x22 : native;
}

// 0x00409B70(dy): view position of the body point, hit target, body point + dy, lock-on candidate
void ViewPos(World::Impl& w, EnemyTask& e, float dy) {
    mx::Transform(w.cam.view, e.body, e.viewPos);
    w.RegisterTarget(&e);
    e.body[1] = dy + e.body[1];
    w.SyncLock(e);
    w.lockOn.Register(e.lock, w.playerPos);  // 0x00408EC0
    static const bool dbg = getenv("HOTD2_BODYDBG") != nullptr;
    if (dbg)
        fprintf(stderr, "%d body fn %X body %.6f %.6f %.6f bv %.6f %.6f %.6f pos %.6f %.6f %.6f\n", w.frame, e.fn, e.body[0], e.body[1], e.body[2], e.bodyView[0], e.bodyView[1], e.bodyView[2],
                e.pos[0], e.pos[1], e.pos[2]);
}

// 0x0049DE60(x, z, px, pz, &yaw): yaw facing away from (px, pz) (Cam_DirToAngles of the difference)
int FaceYaw(float x, float z, float px, float pz) {
    int pitch = 0, yaw = 0;
    mx::DirToAngles(x - px, 0.0f, z - pz, &pitch, &yaw);
    return yaw & 0xFFFF;
}

void ReleaseSlots(World::Impl& w, EnemyTask& e) {
    if (e.slot120 != -1 && e.lock) {  // 0x004092B0
        w.lockOn.Remove(e.lock);
        e.slot120 = -1;
    }
    if (e.actorSlot != -1) {  // 0x004092D0
        if (e.actorSlot >= 0 && e.actorSlot < 14) w.actorSlots[e.actorSlot] = nullptr;
        e.actorSlot = -1;
    }
}

// 0x0049DB90(path, t): the rider's offset on camera path `path` (y - 3) in the mount's frame
void PathPlace(World::Impl& w, BossState& b, int path, float t) {
    path = JudgmentPath(w, path);
    float p[3] = {}, tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(path, t, p, tg, &roll, false)) w.Note("boss path " + std::to_string(path) + " missing");
    b.off[0] = p[0];
    b.off[1] = p[1] - F32(w.exe, 0x004C49C0);
    b.off[2] = p[2];
}

// 0x0049DBE0(path, t): target point = camera path point in the players' frame (10 in front)
void PathTarget(World::Impl& w, BossState& b, int path, float t) {
    path = JudgmentPath(w, path);
    float p[3] = {}, tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(path, t, p, tg, &roll, false)) w.Note("boss path " + std::to_string(path) + " missing");
    Mat4 m = Mat4::Identity();
    mx::Translate(m, w.playerPos[0], w.playerPos[1], w.playerPos[2]);
    mx::RotY(m, w.runner->camera.playerYaw + 0x8000);  // 0x009C71F0
    mx::Translate(m, 0.0f, 0.0f, -10.0f);
    mx::Transform(m, p, b.off);
}

float PathLen(const World::Impl& w, int path) { return F32(w.exe, 0x00570C0C + static_cast<uint32_t>(path % 0x21) * 8); }

// 0x0049DC90: next path by aggression (table 0x00570D64), the distance to the players (60) and rand
void PickPath(World::Impl& w, BossState& b) {
    const int r1 = w.Rand(0x0049DC98);
    const bool near = b.dist <= F64(w.exe, 0x00570F50);
    const int th = w.exe.S16(0x00570D64 + static_cast<uint32_t>(b.level) * 2);
    const int r2 = w.Rand(0x0049DCDA);
    const int idx = r2 % 10 + ((near ? 1 : 0) + (th <= r1 % 100 ? 2 : 0)) * 10;
    const int s = w.typingReferenceActive ? TypingJudgmentFirstPath(idx) :
        w.exe.S16(0x00570D14 + static_cast<uint32_t>(idx) * 2);
    const int r3 = w.Rand(0x0049DCFE) & 1;
    b.pathT = 0;
    b.path = s + r3 * 0x21;
}

// 0x0049DD90: 20 steps back to the nearer end of the path's middle section
void StepBack(World::Impl& w, BossState& b) {
    const uint32_t i = static_cast<uint32_t>(b.path % 0x21) * 8;
    const float a = F32(w.exe, 0x00570C10 + i), l = F32(w.exe, 0x00570C0C + i);
    float f;
    if (a <= b.pathT) {
        if (b.pathT < (a + l) * 0.5f) {
            b.steps = 0x14;
            b.pathStep = (b.pathT - a) * F32(w.exe, 0x0056B180);
            return;
        }
        f = l;
    } else {
        if (b.pathT < a * 0.5f) {
            b.steps = 0x14;
            b.pathStep = b.pathT * F32(w.exe, 0x0056B180);
            return;
        }
        f = a;
    }
    b.steps = 0x14;
    b.pathStep = (f - b.pathT) * F32(w.exe, 0x0055CBB8);
}

void Stagger(World::Impl& w, Actor22Task& a, uint32_t site) {
    a.flags34 |= 0x40000100;
    const int r = w.Rand(site) & 1;
    w.AnimBlendTo(a, w.exe.S16(0x00570BFC + static_cast<uint32_t>(r) * 2), 0, 3);
    if (a.variant == 1) w.Sound(0x2A18A9);
    else if (a.variant == 2) w.Sound(0x2F23A9);
}

// 0x0049D640: shots of this frame: points (node 2: 120, else 10), node blood, 30 damage (25 with two players);
// returns the player (2: both, -1: none)
int BossShots(World::Impl& w, Actor22Task& a, BossState& b) {
    int res = -1;
    for (int p = 0; p < 2; ++p) {
        const uint32_t bit = 1u << (p + 1);
        int node = 0;
        if ((a.flags34 & bit) && (node = a.hitNode[p]) != 0) {
            res = res + 1 + p;
            w.AddScore(p, node == 2 ? 0x78 : 10);
            w.SpawnNodeBlood(a, node);  // 0x00407200
            int dmg = w.playersInGame != 2 ? 30 : 25;
            float factor = 1.0f, boost = 0.0f;
            if (w.runner->cfg.gameType == 1 && ItemDamage(w, p, &factor, &boost)) {  // game type 1: x the item factor (-1: x 2)
                const float f = static_cast<float>(dmg);
                dmg = static_cast<int>(factor == -1.0f ? f + f : f * factor);  // _ftol
            }
            if (w.cfg.portTyping && a.typingStrike) {
                a.typingStrike = false;
                dmg = int(TypingJudgmentWordDamage(w.playersInGame == 2));
            } else dmg = static_cast<int>(TypingDamage(a, static_cast<float>(dmg)));
            a.hp = static_cast<int16_t>(a.hp - dmg);
            ++b.hit1358;
            ++b.hits1364;
        }
        a.hitNode[p] = 0;
        a.anim.nodes[node & 31].flags &= ~bit & ~8u;
    }
    return res;
}

void BossDraw(World::Impl& w, Actor22Task& a, BossState& b);

// 0x0049D220: first-phase damage (also the mount's relayed hits +0x132C); at P s16 0xE the second phase starts
void BossHits1(World::Impl& w, Actor22Task& a, BossState& b) {
    static const bool kHandled[12] = { false, true, false, false, false, true, false, true, false, false, false, true };
    if (a.sub < 1 || a.sub > 11 || !kHandled[a.sub]) return;
    a.hp = static_cast<int16_t>(a.hp - b.damage);
    const int thrE = a.record.ParamS16(0xE), thrC = a.record.ParamS16(0xC);
    auto nextPhase = [&](uint32_t site) {
        Stagger(w, a, site);
        ++a.behaviour;
        a.sub = 0;
        a.rot[1] = FaceYaw(a.pos[0], a.pos[2], w.playerPos[0], w.playerPos[2]);
        a.hp = static_cast<int16_t>(thrE);
        w.bossBar = static_cast<float>(a.hp) / static_cast<float>(a.hpMax);
    };
    if (!(a.flags34 & 8) || (a.flags34 & 0x40000000)) {
        if (thrE < a.hp) return;
        nextPhase(0x0049D3E7);
    } else {
        BossShots(w, a, b);
        if (thrE < a.hp) {
            if (b.phase == 0 && a.hp <= thrC) b.phase = 1;
            Stagger(w, a, 0x0049D372);
            ++a.sub;
            return;
        }
        nextPhase(0x0049D2AD);
    }
    BossDraw(w, a, b);
    ++a.anim.counter[0];
}

// 0x0049D4B0: second-phase damage; at 0 hp the boss dies (1500 points)
void BossHits2(World::Impl& w, Actor22Task& a, BossState& b) {
    const int s = a.sub;
    if (!(s == 3 || s == 5 || s == 7 || s == 9 || s == 10)) return;
    if (!(a.flags34 & 8) || (a.flags34 & 0x40000000)) return;
    const int res = BossShots(w, a, b);
    if (a.hp < 1) {
        w.bossActive = 0;  // 0x009CA0EA
        const int p = res == 2 ? (w.Rand(0x0049D51F) & 1) : res;
        if (p >= 0) w.AddScore(p, 0x5DC);
        if (w.cfg.portTyping) w.TypingController(std::clamp(p,0,1)).addBasePoints(500);
        BossRecords(w);  // 0x00425F40: boss-mode (game type 3) records (world_modes_boss.cpp)
        const int r = w.Rand(0x0049D542) & 1;
        w.AnimBlendTo(a, w.exe.S16(0x00570BFC + static_cast<uint32_t>(r) * 2), 0, 3);
        if (a.variant == 1) w.Sound(0x2818A9);
        else if (a.variant == 2) w.Sound(0x2D23A9);
        a.behaviour = 3;
        a.flags34 |= 0x40000100;
        a.sub = 0;
        BossDraw(w, a, b);
        ++a.anim.counter[0];
        return;
    }
    Stagger(w, a, 0x0049D5C3);
    ++a.sub;
    b.level -= 2;
}

// 0x0049D770 (variants 1 / 2): both skeletons; the second object's motion by the path speed (rank, phase)
void BossDraw(World::Impl& w, Actor22Task& a, BossState& b) {
    w.AnimDraw(a);
    w.EnemyShadow(a);
    EnemyTask& s = *a.second;
    const Mat4 n1 = (a.anim.nodes[1].world * w.cam.view) * w.cam.inverse;
    s.pos[0] = n1.m[12];
    s.pos[1] = n1.m[13];
    s.pos[2] = n1.m[14];
    mx::MatrixToAngles(n1, &s.rot[0], &s.rot[1], &s.rot[2]);  // 0x004019E0
    w.AnimDraw(s);
    w.EnemyShadow(s);
    if (a.hp < 1) return;
    if (w.typingReferenceActive) SourceBossRank(w);
    const float v = w.typingReferenceActive ? TypingJudgmentFirstPhaseStep() :
        F32(w.exe, 0x00570D84 + static_cast<uint32_t>(w.rank + b.phase * 0x10) * 4);
    int idx;
    if (v > 2.5) idx = 4;
    else if (v > 2.0) idx = 3;
    else if (v > 1.5) idx = 2;
    else if (v > 1.0) idx = 1;
    else idx = 0;
    a.secondWant = idx;
    if (a.secondHave != a.secondWant) w.AnimSetMotion(s, w.exe.S16(0x00570C00 + static_cast<uint32_t>(idx) * 2));
    a.secondHave = a.secondWant;
    ++s.anim.counter[0];
}

// ---------------------------------------------------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------------------------------------------------

// A 2D sprite of a world task (Spr_DrawSimple 0x0041C6D0 / Spr_Draw 0x0041C630; the retained list 0x0041C800 ->
// 0x0041C760 adds flags 0x700): alpha 1 unless given, blend -1
SpriteDef Sprite2D(int id, float x, float y, float depth, float sx, float sy, uint32_t flags, float alpha = 1.0f) {
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

// 0x00435C80 (0x00435E50(x, y)): the boss health bar (0x009C8E10), 2D only; ends 120 frames after 0 or at -1
struct BarTask : WTask {
    float x = 0, y = 0;  // +0x34 / +0x38
    float cur = 0;       // +0x40
    float lag = 1.0f;    // +0x44
    int counter = 0;     // +0x48
};

void TaskBar(Task& t) {
    World::Impl& w = *CurrentWorld();
    BarTask& k = t.As<BarTask>();
    const float v = w.bossBar;
    if (v == -1.0f) {
        w.ts.KillCurrent();
        return;
    }
    if (v <= k.cur) {
        k.cur = v;
    } else {
        const float c = k.cur + (w.typingReferenceActive ? .01f : static_cast<float>(F64(w.exe, 0x0055D190)));
        k.cur = c;
        if (v <= c) k.cur = v;
        k.lag = k.cur;
    }
    const float l = k.lag - (w.typingReferenceActive ? .001f : static_cast<float>(F64(w.exe, 0x0055E180)));
    k.lag = l;
    if (l < k.cur) k.lag = k.cur;
    if (k.counter == 0 && v == 0.0f) k.counter = 1;
    if (k.counter != 0) {
        const int c = k.counter;
        k.counter = c + 1;
        if (c == 0x78) {
            w.ts.KillCurrent();
            return;
        }
        if ((c + 1) % 10 < 5) return;
    }
    // 0x0041C800(id, x, y, depth, sx, sy, 0, flags, layer 2): the lag, the difference, the current value, the frame
    const float sy = Bits(0x3F4CCCCD);
    if (w.typingReferenceActive) {
        // Source423600/4237C0: the smaller typing bar is eight pixels below
        // its supplied y. TypingSprite also makes its prepared texture resident.
        const auto bar = [&](int id, float x, float depth, float sx, uint32_t flags) {
            w.TypingSprite(id,x,k.y+8.0f,1,depth,flags,0x00423600);
            w.hudSprites.back().sx = sx;
            w.hudSprites.back().sy = sy;
        };
        bar(0xCA,k.x-(1.0f-2.0f*k.lag)*86.4f,Bits(0x3C185F07),(1.0f-k.lag)*10.8f,0x29);
        bar(0xCB,k.x-(1.0f-2.0f*k.cur)*86.4f,Bits(0x3C16BB99),(k.lag-k.cur)*10.8f,0x29);
        bar(0xC9,k.x-86.4f,Bits(0x3C15182B),k.cur*10.8f,0x29);
        bar(0xCC,k.x,Bits(0x3C1A0275),.72f,0xA);
        return;
    }
    auto bar = [&](int id, float x, float depth, float sx, uint32_t flags) {
        w.hudSprites.push_back(Sprite2D(id, x, k.y, depth, sx, sy, flags | 0x700));
    };
    bar(0xB6, k.x - (1.0f - (k.lag + k.lag)) * 144.0f, Bits(0x3F804189), (1.0f - k.lag) * 18.0f, 0x29);  // 0x004C4380 / 0x005308E8 / 0x0055D178
    bar(0xB7, k.x - (1.0f - (k.cur + k.cur)) * 144.0f, Bits(0x3F8020C5), (k.lag - k.cur) * 18.0f, 0x29);
    bar(0xB5, k.x - 144.0f, 1.0f, k.cur * 18.0f, 0x29);
    w.hudSprites.push_back(Sprite2D(0xB8, k.x, k.y, Bits(0x3F80624E), Bits(0x3F99999A), sy, 10 | 0x700));
}

// 0x00491700: dust ring (model 0x17C8) scaled / faded by camera path 0x147, 80 frames
struct RingTask : WTask {
    float pos[3] = {};  // +0x40
    int yaw = 0;        // +0x68
    int t = 1;          // +0x1320
};

void TaskRing(Task& t) {
    World::Impl& w = *CurrentWorld();
    RingTask& k = t.As<RingTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    k.yaw += 8;
    mx::RotY(m, k.yaw);
    float s[3] = {}, tg[3] = {};
    int roll = 0;
    w.cameraBank.Evaluate(0x147, static_cast<float>(k.t), s, tg, &roll, false);
    mx::Scale(m, s[0], s[1], s[2]);
    w.Draw(0x17C8, m, 8, 1.0f - tg[0]);  // 0x004185A0
    if (0x50 < ++k.t) w.ts.KillCurrent();
}

// 0x0049DEA0: models 0x94 + n at a point for 15 frames
struct FlashTask : WTask {
    float pos[3] = {};  // +0x40
    int n = 0;          // +0x1320
};

void TaskFlash(Task& t) {
    World::Impl& w = *CurrentWorld();
    FlashTask& k = t.As<FlashTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    w.Draw(k.n + 0x94, m);
    if (0xE < ++k.n) w.ts.KillCurrent();
}

// 0x00435E90 (0x00435EE0(id, count, interval)): the sound again every `interval` frames, count - 1 more times
struct RepeatTask : WTask {
    uint32_t id = 0;     // +0x34
    int16_t left = 0;    // +0x38
    int16_t interval = 0;  // +0x3A
    int16_t timer = 1;   // +0x3C
};

void TaskRepeat(Task& t) {
    World::Impl& w = *CurrentWorld();
    RepeatTask& k = t.As<RepeatTask>();
    if (k.left != 0 && !w.runner->skipping && w.skip2230 == 0) {
        if (k.timer == k.interval) {
            k.timer = 0;
            w.Sound(k.id);
            --k.left;
        }
        ++k.timer;
        return;
    }
    w.ts.KillCurrent();
}

// 0x00437AC0 (0x00437A70(record 0x00570EC8)): the boss name card: waits for event flag rec+0; then for rec s16 2
// ticks the camera position / target follow path rec s16 1 (camera hold 0x009CA094) while 8 letters (models 0x7ED /
// 0x7EE, rec s16 3 for letter 6) turn in and fly off; from tick 0x50 the two title sprites of the record fade in
struct CardTask : WTask {
    uint32_t rec = 0;    // +0x130C
    int state = 0;       // c+0x00
    int tick = 0;        // c+0x04
    float l[8][5] = {};  // c+0x08 letters {x, y, z, angle (int), scale}
    int ang[8] = {};
    int model[8] = {};   // c+0xA8
    float savedPos[3] = {}, savedTarget[3] = {};  // c+0xC8 / c+0xD4
};

struct TypingCardRecord {
    uint32_t nativeRecord;
    int flag, camera, model, sprites[2];
    float startX, targetX, targetY, titleX[2], titleY[2];
};

const TypingCardRecord* TypingCard(uint32_t record) {
    // Verified source records58ADC0,5B32F8/3338,5B4D20/4D60,5B3750,593960.
    // All end ticks are300, start y=-.07/z=-1, title depth1. Native records
    // have identical scalar fields but different camera/model/sprite ids.
    static constexpr TypingCardRecord cards[] = {
        {0x00570EC8,2,0x40,0x18D1,{0xCF,0xDD}, .18f,.06f,.01f,{310,526},{84,84}},
        {0x005966B8,9,0x75,0x18D5,{0xD0,0xDE},-.23f,-.52f,0,{14,264},{104,104}},
        {0x005966F8,9,0x79,0x18D5,{0xD0,0xDE},-.23f,-.52f,0,{14,264},{104,104}},
        {0x005972F8,30,0xC5,0x1927,{0xD2,0xE0},-.23f,-.52f,0,{14,224},{104,104}},
        {0x00597338,30,0xCD,0x1927,{0xD2,0xE0},-.23f,-.52f,0,{14,224},{104,104}},
        {0x00596AC0,22,0xE6,0x195E,{0xD3,0xE1},-.23f,-.52f,0,{14,264},{104,104}},
        {0x005898C8,50,0xF4,0x19DA,{0xD4,0xE2}, .18f,.06f,.01f,{310,526},{80,84}}
    };
    for (const auto& card : cards) if (card.nativeRecord == record) return &card;
    return nullptr;
}

void TaskCard(Task& t) {
    World::Impl& w = *CurrentWorld();
    CardTask& k = t.As<CardTask>();
    const ExeImage& exe = w.exe;
    const uint32_t r = k.rec;
    const auto* source = w.typingReferenceActive ? TypingCard(r) : nullptr;
    const int cover = source ? w.typingReference.SourceModel(0x81E) : 0x7ED;
    const int page = source ? w.typingReference.SourceModel(0x81F) : 0x7EE;
    const auto integer = [&](uint32_t offset) {
        if (!source) return int(exe.S16(r+offset));
        if (offset == 0) return source->flag;
        if (offset == 2) return 0x4000+source->camera;
        if (offset == 4) return 300;
        return w.typingReference.SourceModel(source->model);
    };
    const auto scalar = [&](uint32_t offset) {
        if (!source) return F32(exe,r+offset);
        switch (offset) {
        case 8: return source->startX;
        case 0xC: return -.07f;
        case 0x10: return -1.0f;
        case 0x14: return source->targetX;
        case 0x18: return source->targetY;
        default: return 0.0f;
        }
    };
    CameraContext& c0 = w.runner->camera.ctx[0];
    auto camFromPath = [&] {  // Cam_Evaluate 0x004041E0 -> 0x009A60C0 / 0x009A60D8
        int roll = 0;
        w.cameraBank.Evaluate(integer(2), static_cast<float>(k.tick), c0.pos, c0.target, &roll, false);
    };
    auto drawLetter = [&](int i) {
        Mat4 m = Mat4::Identity();  // view space (0x004A9E10 on the model-view stack)
        mx::Translate(m, k.l[i][0], k.l[i][1], k.l[i][2]);
        mx::RotY(m, k.ang[i]);
        const float s = k.l[i][4];
        mx::Scale(m, s, s, s);
        w.Draw(k.model[i], m, 8, -1.0f, false, -1, true);
    };
    bool common = true;
    if (k.state == 0) {
        k.state = 999;  // 0x0041D5D0(0x7ED / 0x7EE): model load requests (not resident in stage 5)
        if (w.residency) {
            w.resQueue.push_back({0x50,uint32_t(cover)});
            w.resQueue.push_back({0x50,uint32_t(page)});
            if (source) w.resQueue.push_back({0x50,uint32_t(integer(6))});
        }
    }
    if (k.state == 999) {
        if (w.runner->flags[integer(0) & 0xFF] != 0) k.state = 1;
    } else if (k.state == 1) {
        w.runner->camera.forceState6 = true;  // 0x009CA094
        memcpy(k.savedPos, c0.pos, 12);
        memcpy(k.savedTarget, c0.target, 12);
        k.tick = 1;
        for (int i = 0; i < 8; ++i) {
            k.l[i][0] = scalar(8);
            k.l[i][1] = scalar(0xC);
            k.l[i][2] = scalar(0x10) - static_cast<float>(i) * F32(exe, 0x004C4CC0);
            k.ang[i] = 0;
            k.l[i][4] = Bits(0x3CF5C28F);
            k.model[i] = i == 0 ? cover : (i == 6 ? integer(6) : page);
        }
        ++k.state;
    } else if (k.state == 2 && k.tick != 0x50) {
        camFromPath();
        for (int i = 0; i < 8; ++i) {
            if (i < 6 && i * 5 + 0xF <= k.tick) {
                k.ang[i] -= i == 0 ? 0x300 : 0x200;
                if (k.ang[i] < -0x7FFF) k.ang[i] = -0x8000;
                if (k.ang[i] == -0x4200) k.l[i][2] = scalar(0x10) - static_cast<float>(8 - i) * F32(exe, 0x004C4CC0);
            }
            w.PageBend(k.ang[i]);  // 0x004759C0: page model 0x7EE bent by this letter's angle
            drawLetter(i);
        }
    } else if (k.state == 2 || k.state == 3) {
        k.state = 3;
        if (k.tick == integer(4)) {
            w.letterbox = 1;
            w.runner->camera.forceState6 = false;
            memcpy(c0.pos, k.savedPos, 12);
            memcpy(c0.target, k.savedTarget, 12);
            w.modelEdits.erase(page);  // 0x0041D610(0x7ED / 0x7EE): the models are released (a reload is unbent)
            w.ts.KillCurrent();
            return;
        }
        camFromPath();
        for (int i = 0; i < 8; ++i) {
            float* p = k.l[i];
            if (i == 6) {
                if (p[4] < F32(exe, 0x0055CB9C)) {
                    const float s = p[4] + F32(exe, 0x0055CB98);
                    p[4] = s;
                    if (s <= F32(exe, 0x0055CB9C)) {
                        p[0] = (scalar(0x14) - scalar(8)) * F32(exe, 0x0055E1C0) + p[0];
                        p[1] = (scalar(0x18) - scalar(0xC)) * F32(exe, 0x0055E1C0) + p[1];
                    } else {
                        p[4] = 0.06f;
                        p[0] = scalar(0x14);
                        p[1] = scalar(0x18);
                    }
                }
            } else {
                const float s = p[4] - F32(exe, 0x004E30E0);
                p[4] = s;
                if (s <= 0.0f) p[4] = 0.0f;
            }
            drawLetter(i);
        }
    }
    if (common && 1 < k.state && k.state < 4 && 0x4F < k.tick) {  // 0x00437E4D: Spr_Draw 0x0041C630 x 2
        float a = static_cast<float>(k.tick - 0x50) * F32(exe, 0x0055CB80);
        if (1.0f <= a) a = 1.0f;
        if (source) {
            for (int i=0;i<2;++i) w.TypingSprite(source->sprites[i],source->titleX[i],source->titleY[i],a,1,0,0x00424F80);
        } else for (uint32_t o : { 0x20u, 0x30u })
            w.hudSprites.push_back(Sprite2D(static_cast<int>(exe.U32(r + o)), F32(exe, r + o + 4), F32(exe, r + o + 8), F32(exe, r + o + 0xC), 1.0f, 1.0f, 0, a));
    }
    ++k.tick;
}

// ---------------------------------------------------------------------------------------------------------------------
// The mount (actor 0x23)
// ---------------------------------------------------------------------------------------------------------------------

void MountDraw(World::Impl& w, MountTask& m) {  // 0x004916D0
    w.AnimDraw(m);
    w.EnemyShadow(m);
}

void SoundV(World::Impl& w, const EnemyTask& e, uint32_t v0, uint32_t v1) {
    if (e.variant == 0) w.Sound(v0);
    else if (e.variant == 1) w.Sound(v1);
}

void Shake(World::Impl& w) {
    if (w.shakeTimer == 0) w.shakeTimer = 0x18;
}

// 0x00491420: shots on the mount: pushed back (first phase, walking), 10 points, sparks (0x00407BC0 0x5A / 0x5C),
// one point of damage relayed to the rider (+0x132C) per hit
void MountShots(World::Impl& w, MountTask& m, Actor22Task* rider) {
    if (!(m.flags34 & 8)) return;
    const int gt = w.runner->cfg.gameType;
    if (gt != 2 && m.behaviour == 1 && m.sub == 1) {
        Mat4 r = Mat4::Identity();
        mx::RotY(r, m.rot[1] + 0x8000);
        const float z[3] = { 0.0f, 0.0f, -1.0f };
        float d[3];
        mx::Transform(r, z, d);
        const float k = F32(w.exe, 0x0057041C + static_cast<uint32_t>(w.rank) * 4);
        m.pos[0] = d[0] * k + m.pos[0];
        m.pos[2] = d[2] * k + m.pos[2];
    }
    float dmg = 0.0f;
    for (int p = 0; p < 2; ++p) {
        int node = m.hitNode[p];
        // PC48AB54 maps the typing head hit onto the mount's armored node9.
        if (w.cfg.portTyping && w.cfg.eventMode == 7 && w.cfg.gameType == 2 && node == 2) node = 9;
        if (node != 0) {
            if (gt == 2) {
                if (!(w.cfg.portTyping && w.cfg.eventMode == 7 && w.cfg.gameType == 2)) --m.hp;
            } else {
                w.AddScore(p, 10);
                if (m.behaviour == 1 && rider) {
                    ++Boss(*rider).mountHits;
                    float factor = 1.0f, boost = 0.0f;
                    if (gt == 1 && ItemDamage(w, p, &factor, &boost))  // game type 1: the item factor (-1: 0x004E30F0)
                        dmg = dmg + (factor == -1.0f ? F32(w.exe, 0x004E30F0) : factor);
                    else
                        dmg = dmg + 1.0f;
                }
            }
            const EnemyNode& n = m.anim.nodes[node & 31];
            const float hp[3] = { n.hit[0], n.hit[1], n.hit[2] + n.radius };
            float pt[3];
            mx::Transform(w.cam.inverse, hp, pt);
            switch (node) {
            case 1: case 2: case 3: case 4: case 6: case 7: case 9: case 0xB: case 0xE:
                w.SpawnEffectScaled(pt, 0x5A, p);
                w.Sound(0x1216A9);
                break;
            default:
                w.SpawnEffectScaled(pt, 0x5C, p);
                w.Sound(0x1316A9);
                break;
            }
        }
        m.hitNode[p] = 0;
        const uint32_t bit = 1u << (p + 1);
        m.anim.nodes[node & 31].flags &= ~bit & ~8u;
        m.flags34 &= ~bit;
    }
    if (gt != 2 && rider) Boss(*rider).damage += static_cast<int>(dmg);
    if (w.cfg.portTyping && w.cfg.eventMode == 7 && w.cfg.gameType == 2) {
        // PC 0x48AC89: a word interrupts the charge, without taking HP.
        if (m.flags34 & 0x1000u) m.flags34 &= ~0x1000u;
        else m.sub = 6;
    }
}

void TaskMount(Task& t);

// 0x0048FEB0: the mount's entrance (camera 0x2F)
void MountBeh0(World::Impl& w, MountTask& m) {
    const CameraDirector& c = w.runner->camera;
    switch (m.sub) {
    case 0:
        if (w.runner->cfg.gameType == 3) {  // boss mode: no entrance, standing at once (sub-state 4)
            w.AnimSetMotion(m, 0x38D);
            m.pos[0] = Bits(0xC482599A);
            m.pos[1] = w.ground;
            m.pos[2] = Bits(0xC3EDD99A);
            m.rot[1] = 0xC6DD;
            m.rot[2] = 0;
            m.rot[0] = 0;
            m.sub = 4;
            MountDraw(w, m);
            ++m.anim.counter[0];
            return;
        }
        if (c.camId == (w.typingReferenceActive ? 0x403F : 0x2F) && c.tick < 0x203) return;
        m.pos[0] = Bits(0xC484A000);
        m.pos[1] = w.ground;
        m.pos[2] = Bits(0xC3EE0000);
        m.rot[1] = 0xC6DD;
        m.rot[2] = 0;
        m.rot[0] = 0;
        ++m.sub;
        [[fallthrough]];
    case 1:
        if (c.camId == (w.typingReferenceActive ? 0x403F : 0x2F) && 600 < c.tick) {
            w.AnimBlendTo(m, 0x386, 0, 2);
            ++m.sub;
        }
        break;
    case 2:
        if (m.anim.tick[0] == 0x32) SoundV(w, m, 0x18A9, 0x2423A9);
        if (m.anim.tick[0] == 0x46) {
            RingTask* r = w.NewTask<RingTask>(TaskRing, 0x00491700);
            memcpy(r->pos, m.pos, 12);
            r->t = 1;
            w.shakeTimer = 0x30;
            w.Sound(0x2316A9);
            w.Sound(0x2B16A9);
        }
        if (m.anim.tick[0] == w.MotionTicks(m.anim.motion[0]) - 0x19) {
            w.AnimBlendTo(m, 0x38E, 0, 5);
            w.Sound(0x417A9);
            ++m.sub;
        }
        break;
    case 3:
        if (c.camId == (w.typingReferenceActive ? 0x403F : 0x2F) && c.tick == static_cast<int32_t>(w.exe.U32(0x00576DF4))) {
            w.AnimSetMotion(m, 0x38D);
            m.pos[0] = Bits(0xC482599A);
            m.pos[1] = w.ground;
            m.pos[2] = Bits(0xC3EDD99A);
            ++m.sub;
        }
        break;
    case 4:
        if (m.rider && m.rider->behaviour == 1) {
            m.f13C0[0] = m.pos[0] - F32(w.exe, 0x0055D2AC);
            m.f13C0[1] = m.pos[1];
            m.f13C0[2] = m.pos[2];
            ++m.behaviour;
            m.sub = 0;
        }
        break;
    default:
        break;
    }
    MountDraw(w, m);
    ++m.anim.counter[0];
}

// 0x00490150: walk at the players, attack in range (0x005703E0 rows {motion, hit tick, range, sound tick, kind}),
// back off, stagger with the rider; down when the rider's hp reaches the mount's (90)
void MountBeh1(World::Impl& w, MountTask& m, Actor22Task* rider) {
    const ExeImage& exe = w.exe;
    const float dz = m.pos[2] - w.playerPos[2], dx = m.pos[0] - w.playerPos[0];
    const float d = std::sqrt(dz * dz + dx * dx);
    if (rider) Boss(*rider).dist = d;
    MountShots(w, m, rider);
    if (rider && rider->hp <= m.hp) {
        --w.liveEnemies;
        w.AnimBlendTo(m, 0x38A, 0, 3);
        ++m.behaviour;
        m.sub = 0;
        MountDraw(w, m);
        ++m.anim.counter[0];
        m.flags34 &= ~8u;
        w.RegisterTarget(&m);
        w.SyncLock(m);  // the camera reads +0x100 directly (no 0x00409B70 lift this frame)
        return;
    }
    const uint32_t ph = static_cast<uint32_t>(m.phase) * 2;
    if (rider && (rider->flags34 & 0x40000000) && !(m.flags34 & 0x40000000)) {
        m.flags34 |= 0x40000000;
        w.AnimBlendTo(m, exe.S16(0x005703D8 + ph), 0, 3);
        m.saved1350 = m.sub;
        m.sub = 6;
    }
    auto row = [&] { return static_cast<uint32_t>(m.phase + m.attack131A * 2) * 12; };
    auto hitPlayers = [&](int kind) {  // Player_TakeHit 0x00415300 for the players in game
        if (w.playersInGame == 0) w.PlayerTakeHit(0, 1, kind);
        else if (w.playersInGame == 1) w.PlayerTakeHit(1, 1, kind);
        else if (w.playersInGame == 2) {
            w.PlayerTakeHit(0, 1, kind);
            w.PlayerTakeHit(1, 1, kind);
        }
    };
    const int tick = m.anim.tick[0], mo = m.anim.motion[0];
    switch (m.sub) {
    case 0:
        m.attack131A = static_cast<int8_t>(w.Rand(0x0049025F) & 1);
        w.AnimBlendTo(m, exe.S16(0x005703C4 + ph), 0, 5);
        ++m.sub;
        [[fallthrough]];
    case 1: {
        m.rot[1] = FaceYaw(m.pos[0], m.pos[2], w.playerPos[0], w.playerPos[2]);
        if (w.typingReferenceActive) {
            // Source48981E/489848 adds this typing-specific approach to the
            // animation's movement. The original uses double coefficients.
            Mat4 frame=Mat4::Identity();
            mx::RotY(frame,m.rot[1]+0x8000);
            const float forward[3]={0,0,-1}; float direction[3];
            mx::Transform(frame,forward,direction);
            const double speed=double(5-SourceBossRank(w))*.005+.1;
            m.pos[0]=float(double(direction[0])*speed+double(m.pos[0]));
            m.pos[2]=float(double(direction[2])*speed+double(m.pos[2]));
        }
        const int t2 = m.anim.tick[0], m2 = m.anim.motion[0];
        if ((m2 == 0x393 && (t2 == 0x37 || t2 == 0x81 || t2 == 0xCF || t2 == 0x11A)) ||
            (m2 == 0x394 && (t2 == 0x2D || t2 == 0x67 || t2 == 0xA7 || t2 == 0xE1))) {
            Shake(w);
            SoundV(w, m, 0x218A9, 0x2523A9);
        }
        const uint32_t e = row();
        if (F32(exe, 0x005703E4 + e) < d || (m.flags34 & 0x40000000)) break;
        w.AnimBlendTo(m, exe.S16(0x005703E0 + e), 0, 5);
        Shake(w);
        SoundV(w, m, 0x218A9, 0x2523A9);
        ++m.sub;
        m.flags34 |= 0x10000000;
        break;
    }
    case 2: {
        const uint32_t e = row();
        if (tick == exe.S16(0x005703E8 + e) && !(m.flags34 & 0x40000000)) SoundV(w, m, 0x18A9, 0x2423A9);
        if (tick == exe.S16(0x005703E2 + e) && !(m.flags34 & 0x40000000)) {
            if (rider) ++Boss(*rider).attack1360;
            uint8_t kb = 0;
            exe.Read(0x005703EA + e, &kb, 1);
            hitPlayers(static_cast<int8_t>(kb));
        }
        if (m.anim.ended) {
            m.flags34 &= ~0x10000000u;
            w.AnimBlendTo(m, exe.S16(0x005703CC + ph), exe.S16(0x00570414 + ph), 5);
            ++m.sub;
        }
        break;
    }
    case 3:
        if ((mo == 0x390 && (tick == 0x4B || tick == 0xA6)) || (mo == 0x391 && (tick == 0x3C || tick == 0x84))) {
            Shake(w);
            SoundV(w, m, 0x418A9, 0x2723A9);
        }
        if (F32(exe, 0x00570488) <= d && !(m.flags34 & 0x40000000)) {
            w.AnimBlendTo(m, exe.S16(0x005703D0 + ph), 0, 5);
            w.Sound(0x417A9);
            ++m.sub;
        }
        break;
    case 4:
        if (mo == 0x38E) {
            if (tick == 0x72) ++m.sub;
        } else if (mo == 0x38F && tick == 0x5B) {
            ++m.sub;
        }
        break;
    case 5:
        if (w.playersInGame < 0) break;
        m.attack131A = static_cast<int8_t>(w.Rand(0x0049064E) & 1);
        if (rider && Boss(*rider).phase == 1) m.phase = 1;  // 0x004913F0
        w.AnimBlendTo(m, exe.S16(0x005703C4 + static_cast<uint32_t>(m.phase) * 2),
                      exe.S16(0x00570410 + static_cast<uint32_t>(m.phase) * 2), 0xF);
        m.sub = 1;
        break;
    case 6: {
        const int a = exe.S16(0x005703D8 + ph), b2 = exe.S16(0x005703DC + ph);
        if (mo == a) {
            if ((mo == 0x388 && (tick == 0x39 || tick == 0x5A)) || (mo == 0x389 && (tick == 0x2F || tick == 0x48))) {
                Shake(w);
                SoundV(w, m, 0x418A9, 0x2723A9);
            }
        }
        if (mo == b2) {
            if ((mo == 0x38B && (tick == 0xF || tick == 0x1E || tick == 0x28)) ||
                (mo == 0x38C && (tick == 0xD || tick == 0x18 || tick == 0x1E)))
                w.Sound(0x3C16A9);
        }
        if (mo == a && tick == w.MotionTicks(mo) - 1) w.AnimBlendTo(m, b2, 0, 5);
        if (m.anim.motion[0] != b2 || m.anim.tick[0] != w.MotionTicks(m.anim.motion[0]) - 1) break;
        const uint32_t f = m.flags34;
        m.flags34 = f & ~0x40000000u;
        switch (m.saved1350) {
        case 1: case 5:
            w.AnimBlendTo(m, exe.S16(0x005703C0 + static_cast<uint32_t>(m.phase + m.saved1350 * 2) * 2), 0, 5);
            m.sub = static_cast<int16_t>(m.saved1350);
            break;
        case 2: case 3:
            m.flags34 = f & 0xAFFFFFFFu;
            w.AnimBlendTo(m, exe.S16(0x005703CC + ph), exe.S16(0x00570418 + ph), 0x14);
            m.sub = 3;
            break;
        case 4:
            w.AnimBlendTo(m, exe.S16(0x005703C4 + ph), 0, 5);
            m.sub = 1;
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    if (m.variant == 0) {
        if (m.pos[0] < F32(exe, 0x00570478)) m.pos[0] = Bits(0xC4820000);
        if (F32(exe, 0x00570474) < m.pos[2]) m.pos[2] = Bits(0xC3E70000);
        if (m.pos[2] < F32(exe, 0x00570470)) m.pos[2] = Bits(0xC3FA0000);
    } else if (m.variant == 1) {
        if (m.pos[0] < F32(exe, 0x00570484)) m.pos[0] = Bits(0x44098000);  // stage 5
        if (F32(exe, 0x00570480) < m.pos[0]) m.pos[0] = Bits(0x44188000);
        if (m.pos[2] < F32(exe, 0x0057047C)) m.pos[2] = Bits(0xC4960000);
    }
    MountDraw(w, m);
    ++m.anim.counter[0];
    m.flags34 &= ~8u;
    ViewPos(w, m, 6.0f);
}

// 0x00490B00: falls; at the motion end out of the counts, lock-on and actor slot; then lying
void MountBeh2(World::Impl& w, MountTask& m, Actor22Task* rider) {
    if (w.runner->cfg.gameType != 2) MountShots(w, m, rider);
    MountDraw(w, m);
    if (m.anim.tick[0] == 0x68) {
        w.shakeTimer = 0x18;
        w.Sound(0x2216A9);
    }
    if (m.anim.tick[0] == w.MotionTicks(m.anim.motion[0])) {
        --w.attackers;
        ReleaseSlots(w, m);
        m.sub = 0;
        ++m.behaviour;
        m.flags34 |= 0x8000;
        return;
    }
    ++m.anim.counter[0];
    if (w.runner->cfg.gameType != 2) {
        mx::Transform(w.cam.view, m.body, m.viewPos);
        m.flags34 &= ~8u;
        w.RegisterTarget(&m);
    }
}

// 0x00490C50: lying until camera P s16 6 reaches tick P s16 8
void MountBeh3(World::Impl& w, MountTask& m, Actor22Task* rider) {
    const CameraDirector& c = w.runner->camera;
    if (c.camId == m.record.ParamS16(6) && m.record.ParamS16(8) <= c.tick) {
        w.EnemyDestroy(m);  // 0x00409CC0
        return;
    }
    MountDraw(w, m);
    if (m.sub == 0 && w.runner->cfg.gameType != 2 && rider && rider->behaviour == 3 && rider->sub == 2) {
        if (F32(w.exe, 0x0057048C) < m.pos[0]) m.pos[0] = Bits(0xC4886000);
        ++m.sub;
    }
}

// 0x00490D00 (mount variant 1, stage 5): placed after camera 0xCE tick 0xB4; at camera tick 0x00577070 a 700-frame
// wait, the roar motion 0x38E, then idle 0x38D; the fight when the rider's behaviour is 1
void MountBeh0V1(World::Impl& w, MountTask& m) {
    const CameraDirector& c = w.runner->camera;
    switch (m.sub) {
    case 0:
        if (c.camId == (w.typingReferenceActive ? 0x40E0 : 0xCE) && c.tick < 0xB4) return;
        m.pos[0] = Bits(0x44120000);
        m.pos[1] = Bits(0xC28DCCCD);
        m.pos[2] = Bits(0xC4924000);
        m.rot[1] = 0x8000;
        m.rot[2] = 0;
        m.rot[0] = 0;
        ++m.sub;
        [[fallthrough]];
    case 1:
        if (c.tick == static_cast<int32_t>(w.exe.U32(0x00577070))) {
            ++m.sub;
            m.count1330 = 0;
        }
        break;
    case 2:
        if (++m.count1330 == 700) {
            w.AnimBlendTo(m, 0x38E, 0, 2);
            w.Sound(0x417A9);
        }
        if (m.anim.motion[0] == 0x38E && m.anim.tick[0] == w.MotionTicks(0x38E) - 1) {
            w.AnimBlendTo(m, 0x38D, 0, 10);
            ++m.sub;
        }
        break;
    case 3:
        if (m.rider && m.rider->behaviour == 1) {
            m.f13C0[0] = m.pos[0] - F32(w.exe, 0x0055D2AC);
            m.f13C0[1] = m.pos[1];
            m.f13C0[2] = m.pos[2];
            ++m.behaviour;
            m.sub = 0;
        }
        break;
    default:
        break;
    }
    MountDraw(w, m);
    ++m.anim.counter[0];
}

// 0x00490EB0 (mount variant 2, training lesson 4): placed at (0x0057045C[difficulty], 23, -1839) facing 0x4000; when
// the lesson runs (flag 0xF1) the health bar and the start motion 0x005703D0, then the charge (behaviour 1)
void MountBeh0V2(World::Impl& w, MountTask& m) {
    const ExeImage& exe = w.exe;
    const uint32_t ph = static_cast<uint32_t>(m.phase) * 2;
    if (m.sub == 0) {
        m.pos[0] = w.cfg.portTyping && w.cfg.eventMode == 7 && w.cfg.gameType == 2 ? 35.0f :
            F32(exe, 0x0057045C + static_cast<uint32_t>(w.runner->cfg.playerVariant) * 4);
        m.pos[1] = Bits(0x41B80000);
        m.pos[2] = Bits(0xC4E5E000);
        m.rot[1] = 0x4000;
        m.rot[2] = 0;
        m.rot[0] = 0;
        ++m.sub;
    } else if (m.sub != 1) {
        if (m.sub != 2) {
            MountDraw(w, m);
            ++m.anim.counter[0];
            return;
        }
    }
    if (m.sub == 1) {
        if (w.runner->flags[0xF1] == 0) {
            MountDraw(w, m);
            return;
        }
        if (w.cfg.portTyping && w.cfg.eventMode == 7 && w.cfg.gameType == 2) {
            w.rank = 2;
            w.typing.setWordLimits(1500,100); // PC 0x48A58F -> 0x4B2830.
            w.TypingWords(-1,100);
            m.typingFlags = 0x30004402u;
        } else w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
        w.AnimSetMotion(m, exe.S16(0x005703D0 + ph));  // 0x00411930
        ++m.sub;
    }
    if (m.anim.motion[0] == exe.S16(0x005703D0 + ph) && w.MotionTicks(m.anim.motion[0]) - 1 <= m.anim.tick[0]) {
        w.AnimBlendTo(m, exe.S16(0x005703C4 + ph), exe.S16(0x00570410 + ph), 0xF);
        ++m.behaviour;
        m.sub = 0;
    }
    MountDraw(w, m);
    ++m.anim.counter[0];
}

// PC 0x48A670: Speed2 keeps the mount alive. A word makes it stagger;
// a low grade takes its real attack path and the accepted hit ends the course.
void MountDrill(World::Impl& w,MountTask& m) {
    if (w.runner->flags[0xF2]) { MountDraw(w,m); return; }
    MountShots(w,m,nullptr);
    const ExeImage& exe=w.exe;
    const uint32_t ph=static_cast<uint32_t>(m.phase)*2;
    const auto row=[&] {return static_cast<uint32_t>(m.phase+m.attack131A*2)*12;};
    const auto attack=[&] {
        w.AnimBlendTo(m,exe.S16(0x005703E0+row()),0,5);
        w.Sound(0x218A9);Shake(w);
        m.flags34|=0x10000000u;
        m.sub=2;
    };
    switch(m.sub) {
    case 0:
        m.attack131A=static_cast<int8_t>(w.Rand(0x0048A702)&1);
        w.AnimBlendTo(m,exe.S16(0x005703D0+ph),0,5);
        m.typingFlags&=~0x400u;
        w.TypingAttackTimer(2);
        m.sub=1;
        [[fallthrough]];
    case 1:
        m.rot[1]=FaceYaw(m.pos[0],m.pos[2],w.playerPos[0],w.playerPos[2]);
        if(w.typingAttackDelay==0)attack();
        break;
    case 2: {
        const uint32_t e=row();const int tick=m.anim.tick[0];
        if(tick==exe.S16(0x005703E8+e)&&!(m.flags34&0x40000000u))w.Sound(0x18A9);
        if(tick==exe.S16(0x005703E2+e)&&!(m.flags34&0x40000000u)) {
            uint8_t kind=0;exe.Read(0x005703EA+e,&kind,1);
            w.PlayerTakeHit(w.typingPlayer,1,static_cast<int8_t>(kind));
        }
        if(m.anim.ended) {
            m.flags34&=~0x10000000u;
            w.AnimBlendTo(m,exe.S16(0x005703D4+ph),exe.S16(0x00570414+ph),5);
            m.sub=3;
        }
        break;
    }
    case 6: {
        m.typingFlags|=0x400u;
        const int hits=++w.player[w.typingPlayer].hits;
        w.rank=hits<=3?2:hits<=6?3:std::min(15,hits/5+3);
        const auto& grades=w.typingDrillGame.rankCounts;
        if(grades[2]||grades[3]||grades[4]) {attack();break;}
        const int selector=hits<5?100:hits<35?(hits/5+1)*100:(hits/5)*100;
        w.TypingWords(-1,selector);
        w.AnimBlendTo(m,(w.frame&1)?0x389:0x388,0,3); // source motions947/948, exact payload aliases.
        m.sub=7;
        break;
    }
    case 7:
        if(m.anim.tick[0]>=w.MotionTicks(m.anim.motion[0])-1) {
            m.typingFlags&=~0x400u;
            w.AnimBlendTo(m,(w.frame&1)?0x38F:0x38E,0,5); // source953/954.
            m.sub=1;w.TypingAttackTimer(2);
        }
        break;
    default:break;
    }
    m.pos[0]=std::clamp(m.pos[0],32.0f,38.0f);
    m.pos[2]=std::clamp(m.pos[2],-1842.0f,-1836.0f);
    MountDraw(w,m);++m.anim.counter[0];
    m.flags34&=~8u;
    ViewPos(w,m,6.0f);
}

// 0x00490FD0 (mount variant 2): the charge at the players; shots take its hp (0x00491420, game type 2), the health bar
// 0x009C8E10; the attack row (0x005703E0, by phase and the random attack) in range; down at hp 0
void MountBeh1V2(World::Impl& w, MountTask& m) {
    if(w.cfg.portTyping && w.cfg.eventMode==7 && w.cfg.gameType==2) { MountDrill(w,m); return; }
    const ExeImage& exe = w.exe;
    const float dz = m.pos[2] - w.playerPos[2], dx = m.pos[0] - w.playerPos[0];
    if (w.runner->flags[0xF2] != 0) {
        MountDraw(w, m);
        return;
    }
    MountShots(w, m, nullptr);
    if (m.hp < 1) {
        --w.liveEnemies;
        w.bossBar = 0.0f;
        w.AnimBlendTo(m, 0x38A, 0, 3);
        ++m.behaviour;
        m.sub = 0;
        MountDraw(w, m);
        ++m.anim.counter[0];
        return;
    }
    const uint32_t ph = static_cast<uint32_t>(m.phase) * 2;
    auto row = [&] { return static_cast<uint32_t>(m.phase + m.attack131A * 2) * 12; };
    bool next = false;
    auto approach = [&] {  // LAB_004911D9
        m.rot[1] = FaceYaw(m.pos[0], m.pos[2], w.playerPos[0], w.playerPos[2]);  // 0x0049DE60
        const int t2 = m.anim.tick[0];
        if (t2 == 0x37 || t2 == 0x81 || t2 == 0xCF || t2 == 0x11A) {
            Shake(w);
            w.Sound(0x218A9);
        }
        const uint32_t e = row();
        if (F32(exe, 0x005703E4 + e) < std::sqrt(dz * dz + dx * dx)) return;
        w.AnimBlendTo(m, exe.S16(0x005703E0 + e), 0, 5);
        Shake(w);
        w.Sound(0x218A9);
        m.flags34 |= 0x10000000;
        next = true;
    };
    if (m.sub == 0) {
        m.attack131A = static_cast<int8_t>(w.Rand(0x004911A4) & 1);
        w.AnimBlendTo(m, exe.S16(0x005703C4 + ph), 0, 5);
        ++m.sub;
        approach();
    } else if (m.sub == 1) {
        approach();
    } else if (m.sub == 2) {
        const uint32_t e = row();
        const int tick = m.anim.tick[0];
        if (tick == exe.S16(0x005703E8 + e) && !(m.flags34 & 0x40000000)) w.Sound(0x18A9);
        if (tick == exe.S16(0x005703E2 + e) && !(m.flags34 & 0x40000000)) {
            uint8_t kb = 0;
            exe.Read(0x005703EA + e, &kb, 1);
            const int kind = static_cast<int8_t>(kb);
            if (w.playersInGame == 0) w.PlayerTakeHit(0, 1, kind);
            else if (w.playersInGame == 1) w.PlayerTakeHit(1, 1, kind);
            else if (w.playersInGame == 2) {
                w.PlayerTakeHit(0, 1, kind);
                w.PlayerTakeHit(1, 1, kind);
            }
        }
        if (m.anim.ended) {
            m.flags34 &= ~0x10000000u;
            w.AnimBlendTo(m, exe.S16(0x005703D4 + ph), exe.S16(0x00570414 + ph), 5);
            next = true;
        }
    }
    if (next) ++m.sub;
    if (m.hp != 0) w.bossBar = static_cast<float>(m.hp) / static_cast<float>(m.hpMax);
    MountDraw(w, m);
    ++m.anim.counter[0];
    m.flags34 &= ~8u;
    ViewPos(w, m, 6.0f);  // 0x00409B70
}

// 0x0048FE90
void TaskMount(Task& t) {
    World::Impl& w = *CurrentWorld();
    MountTask& m = t.As<MountTask>();
    if (m.lock) m.slot120 = static_cast<int8_t>(m.lock->slot);
    Actor22Task* rider = AliveTask(w, m.rider, m.riderSerial);
    switch (m.behaviour) {
    case 0:
        if (m.variant == 1) MountBeh0V1(w, m);  // 0x00597278[0] (stage 5)
        else if (m.variant == 2) MountBeh0V2(w, m);  // 0x00597288[0] (training lesson 4)
        else MountBeh0(w, m);
        break;
    case 1:
        if (m.variant == 2) MountBeh1V2(w, m);  // 0x00597288[1]
        else MountBeh1(w, m, rider);
        break;
    case 2: MountBeh2(w, m, rider); break;
    case 3: MountBeh3(w, m, rider); break;
    default: w.Note("boss mount behaviour " + std::to_string(m.behaviour) + " not ported"); break;
    }
    w.SyncLock(m);  // lock-on readers (camera 0x00403050) read +0x100 / +0x121 live
}

// 0x0048FD90: skeleton 0x44, motion 0x38D, node 2 hidden and not hittable, counted as live enemy and attacker,
// lock-on slot direct (0x00408E80)
void TaskMountCtor(Task& t) {
    World::Impl& w = *CurrentWorld();
    MountTask& m = t.As<MountTask>();
    m.behaviour = 0;
    m.sub = 0;
    m.actorSlot = -1;
    m.slot120 = -1;
    m.variant = m.record.ParamS8(1);
    m.anim.kind = 0x44;
    m.anim.motion[0] = 0x38D;
    w.EnemyInitPose(m);  // 0x00410440
    m.anim.rotOrder = 1;
    m.anim.drawFlags |= 4;
    m.radius = F32(w.exe, 0x004C4D28 + 0x44 * 4);
    m.anim.nodes[2].model = 0;
    m.anim.nodes[2].radius = 0;
    ++w.attackers;
    ++w.liveEnemies;
    m.target = -1;
    m.c131D = -1;
    w.SyncLock(m);
    w.lockOn.AddDirect(m.lock);
    m.slot120 = static_cast<int8_t>(m.lock->slot);
    m.phase = 0;
    if (m.variant > 2) w.Note("boss mount variant " + std::to_string(m.variant) + " not ported");
    t.fn = TaskMount;
    m.fn = m.variant == 1 ? 0x00490CE0 : m.variant == 2 ? 0x00490E90 : 0x0048FE90;
    TaskMount(t);
}

// ---------------------------------------------------------------------------------------------------------------------
// The rider (actor 0x22 variant 1)
// ---------------------------------------------------------------------------------------------------------------------

// The mount of record P word 0x10 (0x00408A20), linked both ways (+0x1394)
void SpawnMountOf(World::Impl& w, Actor22Task& a, BossState& b) {
    SpawnRecord r;
    const uint32_t addr = static_cast<uint32_t>(a.record.ParamS32(0x10));
    if (r.Read(w.mem, addr)) {  // 0x00408A20
        MountTask* m = w.NewTask<MountTask>(TaskMountCtor, w.actorType[r.type & 0x7F]);
        m->lock = &w.lockTargets[w.nextLockTarget++];
        m->flags34 = r.flags | 1;  // 0x00408970
        m->flags38 = 0;
        memcpy(m->pos, r.pos, 12);
        memcpy(m->rot, r.rot, 12);
        m->lo = static_cast<int16_t>(r.lo);
        m->hp = m->hpMax = static_cast<int16_t>(r.state);
        m->record = r;
        m->rider = &a;
        m->riderSerial = w.CurrentSerial();
        b.mount = m;
        b.mountSerial = w.ts.NextSerial() - 1;
        if (r.type != 0x23) w.Note("boss rider: mount record of actor type " + std::to_string(r.type));
    } else {
        w.Note("boss rider: mount record unreadable");
    }
}

// 0x0049B640: intro on path 0x103 (camera ticks), the mount spawned (record P word 0x10); at camera tick
// 0x00576DF4 event flag 2 (the name card), then after 0x00576DF8 frames the fight
void BossBeh0(World::Impl& w, Actor22Task& a, BossState& b) {
    const CameraDirector& c = w.runner->camera;
    const ExeImage& exe = w.exe;
    bool place = false;
    if (a.sub == 0) {
        SpawnMountOf(w, a, b);
        if (w.runner->cfg.gameType == 3) {  // boss mode: no intro path; the name card flag, then the fight count
            w.AnimSetMotion(a, exe.S16(0x00570BE2));
            w.runner->flags[2] = 1;  // 0x009C7202
            a.pos[0] = Bits(0xC483C000);
            a.pos[1] = Bits(0x41F00000);
            a.pos[2] = Bits(0xC3EF8CCD);
            a.rot[1] = 0xC6DD;
            a.rot[2] = 0;
            a.rot[0] = 0;
            b.count = 0;
            a.sub = 2;
            BossDraw(w, a, b);
            ++a.anim.counter[0];
            return;
        }
        ++a.sub;
        place = true;
    } else if (a.sub == 1) {
        place = true;
    } else if (a.sub == 2) {
        if (static_cast<int32_t>(exe.U32(0x00576DF8)) <= ++b.count) {
            a.hp = a.record.ParamS16(0xA);
            b.phase = 0;
            BarTask* bar = w.NewTask<BarTask>(TaskBar, 0x00435C80);  // 0x00435E50(320, 35)
            bar->x = 320.0f;
            bar->y = 35.0f;
            bar->cur = 0;
            bar->lag = 1.0f;
            bar->counter = 0;
            w.bossBar = 1.0f;
            ++w.attackers;
            ++w.liveEnemies;
            w.AnimBlendTo(a, exe.S16(0x00570BE4), 0, 3);
            w.bossActive = 1;  // 0x009CA0EA
            ++a.behaviour;
            a.sub = 0;
        }
    }
    if (place) {
        float pos[3] = {}, tg[3] = {};
        int roll = 0;
        const float t = static_cast<float>(c.tick);
        if (w.cameraBank.Evaluate(JudgmentPath(w, 0x103), t, pos, tg, &roll, false)) {  // 0x0049DB40 -> 0x004042D0
            memcpy(a.pos, pos, 12);
            for (int k = 0; k < 3; ++k) a.rot[k] = static_cast<int>(tg[k]);
        }
        a.rot[1] = 0xC6DD;
        if (t == F32(exe, 0x00570F2C)) w.AnimBlendTo(a, exe.S16(0x00570BE2), 0, 8);
        if (t == F32(exe, 0x00570F28)) {
            a.mouthMode = 1;
            a.mouthTick = 0;
        }
        if (t == F32(exe, 0x00570F24)) a.mouthMode = 0;
        if (c.tick == static_cast<int32_t>(exe.U32(0x00576DF4))) {
            w.runner->flags[2] = 1;
            a.pos[0] = Bits(0xC483C000);
            a.pos[1] = Bits(0x41F00000);
            a.pos[2] = Bits(0xC3EF8CCD);
            ++a.sub;
            b.count = 0;
        }
    }
    BossDraw(w, a, b);
    ++a.anim.counter[0];
}

// 0x0049D180: aggression from this frame's events
void BossCounts(BossState& b) {
    if (b.hit1358 != 0) {
        b.hit1358 = 0;
        --b.level;
    }
    if (7 < b.mountHits) {
        ++b.level;
        b.mountHits -= 8;
    }
    if (b.attack1360 != 0) {
        b.level += 3;
        b.attack1360 = 0;
        ++b.landed1368;
        b.flag136C = 1;
    }
    if (b.level < 0) b.level = 0;
    if (0xF < b.level) b.level = 0xF;
}

// 0x0049B850: first phase: the rider moves along paths 0x104 + n relative to the mount (speed by rank / phase),
// jumps back, attacks (taunts 0x1B / 0x1C after the mount hit a player)
void BossBeh1(World::Impl& w, Actor22Task& a, BossState& b) {
    const ExeImage& exe = w.exe;
    EnemyTask* m = AliveTask(w, b.mount, b.mountSerial);
    if (a.variant == 1 && a.talk != 0) {
        const int old = b.timer1328--;
        if (old < 1) {
            a.talk = 0;
            w.letterbox = 1;
        }
    }
    BossCounts(b);
    BossHits1(w, a, b);
    if (a.behaviour == 2) return;
    b.damage = 0;
    auto place = [&] { PathPlace(w, b, b.path + 0x104, b.pathT); };
    auto atEnd = [&] { return a.anim.tick[0] == w.MotionTicks(a.anim.motion[0]) - 1; };
    const int idle = exe.S16(0x00570BE4);
    switch (a.sub) {
    case 0:
        b.damage = 0;
        b.timer1328 = 0;
        a.talk = 0;
        b.path = 0;
        b.level = 8;
        b.hit1358 = b.mountHits = b.attack1360 = b.hits1364 = b.landed1368 = b.flag136C = 0;
        b.pathT = 0;
        PickPath(w, b);
        place();
        ++a.sub;
        [[fallthrough]];
    case 1:
        if (a.flags34 & 0x40000000) break;
        place();
        b.pathT += w.typingReferenceActive ? TypingJudgmentFirstPhaseStep() :
            F32(exe, 0x00570D84 + static_cast<uint32_t>(w.rank + b.phase * 0x10) * 4);
        if (m && m->sub == 2) {
            if (b.pathT < PathLen(w, b.path)) {
                StepBack(w, b);
                a.sub = 5;
                b.flag136C = 0;
            } else {
                a.flags34 &= 0xBFFFFEFFu;
                b.pathT = 0;
                b.path = w.Rand(0x0049DD35) % 3 + 0x1B;  // 0x0049DD30
                a.sub = 7;
                b.flag136C = 0;
            }
        } else if (PathLen(w, b.path) <= b.pathT) {
            PickPath(w, b);
        }
        break;
    case 2:
        place();
        if (atEnd()) {
            a.flags34 &= ~0x40000000u;
            w.AnimBlendTo(a, idle, 0, 3);
            StepBack(w, b);
            a.sub = 3;
        }
        break;
    case 3:
        if (b.steps < 1) {
            PickPath(w, b);
            a.sub = 4;
            break;
        }
        place();
        --b.steps;
        b.pathT = b.pathStep + b.pathT;
        break;
    case 4:
        place();
        if (m && !(m->flags34 & 0x40000000)) {
            a.sub = 1;
            a.flags34 &= ~0x100u;
        }
        break;
    case 5:
        if (a.flags34 & 0x40000000) break;
        if (b.steps == 0) {
            a.flags34 &= 0xBFFFFEFFu;
            b.pathT = 0;
            b.path = w.Rand(0x0049DD35) % 3 + 0x1B;  // 0x0049DD30
            b.f1348 = 0;
            a.sub = 7;
            break;
        }
        place();
        --b.steps;
        b.pathT = b.pathStep + b.pathT;
        break;
    case 6:
        place();
        if (a.anim.ended && (a.flags34 & 0x40000000)) {
            a.flags34 &= ~0x40000000u;
            w.AnimBlendTo(a, idle, 0, 3);
            a.sub = 3;
        }
        break;
    case 7: {
        if (a.flags34 & 0x40000000) break;
        const uint32_t i = static_cast<uint32_t>(b.phase) * 4;
        if (b.f1348 == F32(exe, 0x00570EBC + i)) {
            w.AnimBlendTo(a, exe.S16(0x00570BF4 + i), 0, 3);
            if (a.variant == 1) w.Sound(0x20000085);
            else if (a.variant == 2) w.Sound(0x20000192);
        }
        if (a.anim.motion[0] == exe.S16(0x00570BF4 + i) && atEnd()) w.AnimBlendTo(a, idle, 0, 3);
        place();
        const float inc = b.phase == 0 ? 1.0f : F32(exe, 0x00565EEC);
        b.pathT = b.pathT + inc;
        if (PathLen(w, b.path) <= b.pathT) {
            PickPath(w, b);
            a.sub = 10;
        }
        b.f1348 = b.f1348 + 1.0f;
        break;
    }
    case 8:
        place();
        if (!a.anim.ended || !(a.flags34 & 0x40000000)) break;
        a.flags34 &= ~0x40000000u;
        w.AnimBlendTo(a, idle, 0, 3);
        ++a.sub;
        break;
    case 9:
    case 13:
        if (b.pathT < PathLen(w, b.path)) {
            place();
            b.pathT = b.pathT + F32(exe, 0x004E30F0);
            break;
        }
        PickPath(w, b);
        a.sub = 4;
        break;
    case 10:
        place();
        if (m && (m->flags34 & 0x10000000)) break;
        a.flags34 &= ~0x100u;
        if (b.flag136C == 0) {
            a.sub = 1;
            break;
        }
        w.AnimBlendTo(a, exe.S16(0x00570BEC), 0, 3);
        if (a.variant == 1) w.Sound(0x3018A9);
        else if (a.variant == 2) w.Sound(0x3323A9);
        b.flag136C = 0;
        b.pathT = 0;
        b.path = w.Rand(0x0049DD65) % 3 + 0x1E;  // 0x0049DD60
        a.sub = 11;
        if (a.variant == 1 && b.hits1364 == 0) {
            if (b.landed1368 == 1 || b.landed1368 == 2) {
                a.talk = 1;
                b.timer1328 = 0x78;
                w.letterbox = 5;
                w.Voice(b.landed1368 == 1 ? 0x1B : 0x1C);  // 0x00435B80
            }
        }
        break;
    case 11:
        if (a.flags34 & 0x40000000) break;
        if (a.anim.motion[0] == exe.S16(0x00570BEC) && atEnd()) w.AnimBlendTo(a, idle, 0, 3);
        place();
        b.pathT = b.pathT + 1.0f;
        if (b.pathT < PathLen(w, b.path)) break;
        w.AnimBlendTo(a, idle, 0, 3);
        PickPath(w, b);
        a.sub = 1;
        break;
    case 12:
        place();
        if (!a.anim.ended || !(a.flags34 & 0x40000000)) break;
        a.flags34 &= ~0x40000000u;
        w.AnimBlendTo(a, idle, 0, 3);
        ++a.sub;
        break;
    default:
        break;
    }
    a.rot[1] = FaceYaw(a.pos[0], a.pos[2], w.playerPos[0], w.playerPos[2]);
    if (m) {
        Mat4 q = Mat4::Identity();
        mx::Translate(q, m->pos[0], m->pos[1], m->pos[2]);
        mx::RotY(q, m->rot[1] + 0x8000);
        mx::Transform(q, b.off, a.pos);
    }
    w.bossBar = static_cast<float>(a.hp) / static_cast<float>(a.hpMax);
    BossDraw(w, a, b);
    ++a.anim.counter[0];
    mx::Transform(w.cam.view, a.body, a.viewPos);
    a.flags34 &= ~0xEu;
    w.RegisterTarget(&a);
}

// 0x0049C190: second phase (off the mount): rises, then jumps along paths 0x140 + n in the players' frame and
// attacks (Player_TakeHit kind 7 at the path's tick 0x00577238[n] - 5)
void BossBeh2(World::Impl& w, Actor22Task& a, BossState& b) {
    const ExeImage& exe = w.exe;
    if (b.level < 0) b.level = 0;
    if (0xF < b.level) b.level = 0xF;
    BossHits2(w, a, b);
    if (a.behaviour == 3) return;
    auto newPath = [&](uint32_t site) {
        const int r = w.Rand(site);
        const int group=w.typingReferenceActive ? TypingJudgmentSecondPathGroup(b.level) :
            exe.S16(0x00570E04 + static_cast<uint32_t>(b.level)*2);
        const int index=r%10+group*10;
        const int s = w.typingReferenceActive ? TypingJudgmentSecondPath(index) :
            exe.S16(0x00570E24 + static_cast<uint32_t>(index)*2);
        b.path = s;
        PathTarget(w, b, s + 0x140, static_cast<float>(b.count));
    };
    auto lerpTo = [&](int n) {  // 0x00401E60
        a.pos[0] = Lerp(a.pos[0], b.off[0], 1, n - b.count);
        a.pos[1] = Lerp(a.pos[1], b.off[1], 1, n - b.count);
        a.pos[2] = Lerp(a.pos[2], b.off[2], 1, n - b.count);
        const int c = b.count++;
        if (n < c) {
            memcpy(a.pos, b.off, 12);
            return true;
        }
        return false;
    };
    switch (a.sub) {
    case 0:
        b.count = 0;
        a.acc[1] = Bits(0x3E19999A);
        a.vel[1] = 0;
        if (w.typingReferenceActive) {
            a.typingFlags &= ~0x04000000u;
            ++a.sub;
            break; // Source4B3F2C returns before the first rise update.
        }
        ++a.sub;
        [[fallthrough]];
    case 1: {
        const float v = a.acc[1] + a.vel[1];
        a.vel[1] = v;
        const float y = v + a.pos[1];
        a.pos[1] = y;
        if (F32(exe, 0x00564420) <= y) {
            a.acc[1] = Bits(0x3E19999A);
            a.vel[1] = 0;
            b.count = 0;
            newPath(0x0049C23B);
            ++a.sub;
        }
        break;
    }
    case 2:
        if (lerpTo(0x3C)) {
            w.AnimBlendTo(a, exe.S16(0x00570BEE), 0, 3);
            a.flags34 &= ~0x40000000u;
            w.SyncLock(a);
            w.lockOn.AddDirect(a.lock);  // 0x00408E80
            a.slot120 = static_cast<int8_t>(a.lock->slot);
            b.count = 0;
            if (w.typingReferenceActive && w.cfg.eventMode == 0) {
                b.typingInstructions=0;
                a.sub=14;
            } else a.sub = 7;
        }
        break;
    case 3: {
        const float tick=w.typingReferenceActive ? TypingJudgmentAdvancePathClock(b.count,b.typingPathFraction,SourceBossRank(w)) :
            static_cast<float>(b.count);
        PathTarget(w, b, b.path + 0x140, tick);
        memcpy(a.pos, b.off, 12);
        const int c = w.typingReferenceActive ? b.count : ++b.count;
        const int len = static_cast<int32_t>(exe.U32(0x00577238 + static_cast<uint32_t>(b.path) * 4));
        if (c != (w.typingReferenceActive ? len : len-5)) {
            if (len < c) {
                b.count = 0;
                newPath(0x0049C42D);
                a.sub = 5;
            }
            break;
        }
        if (w.playersInGame == 0) w.PlayerTakeHit(0, 1, 7);
        else if (w.playersInGame == 1) w.PlayerTakeHit(1, 1, 7);
        else if (w.playersInGame == 2) {
            w.PlayerTakeHit(0, 1, 7);
            w.PlayerTakeHit(1, 1, 7);
        }
        b.level += 4;
        if (w.typingReferenceActive) {
            b.count=0; newPath(0x0049C42D); a.sub=5;
        }
        break;
    }
    case 4:
        b.count = 0;
        [[fallthrough]];
    case 6:
        PathTarget(w, b, 0x144, 0.0f);
        a.sub = 9;
        break;
    case 5:
        if (lerpTo(0x3C)) {
            w.AnimBlendTo(a, exe.S16(0x00570BF0), 0, 3);
            b.count = 0;
            a.sub = 7;
        }
        break;
    case 7:
        if (-1 < w.playersInGame && (w.typingReferenceActive ? 179 : 99) < b.count++) {
            w.AnimBlendTo(a, exe.S16(0x00570BE6), 0, 3);
            if (a.flags34 & 0x100) a.flags34 &= ~0x100u;
            b.count = 0;
            a.sub = 3;
            w.Sound(0x4317A9);
        }
        break;
    case 14:
        // Source4B40A4: chapter-one phase instructions precede its first
        // approach. The rematch skips this interlude; Boss Mode skips its wait.
        if (w.cfg.gameType != 3 && ++b.typingInstructions < 180) {
            for (int i=0;i<4;++i) w.TypingSprite(0xB89+i,128.0f*float(i+1),334,1,.1f,0xA,0x00424D30);
            break;
        }
        b.count=0; newPath(0x0049C23B); a.sub=7;
        break;
    case 8:
        w.AnimBlendTo(a, exe.S16(0x00570BEE), 0, 3);
        a.flags34 &= 0xBFFFFEFFu;
        b.count = 0;
        PathTarget(w, b, 0x144, 0.0f);
        a.sub = 10;
        break;
    case 9:
        if (lerpTo(0x3C)) {
            w.AnimBlendTo(a, exe.S16(0x00570BEE), 0, 3);
            ++a.sub;
            a.flags34 &= 0xBFFFFEFFu;
            b.count = 0;
        }
        break;
    case 10:
        PathTarget(w, b, 0x144, static_cast<float>(b.count));
        memcpy(a.pos, b.off, 12);
        if (b.count++ < 0x3D) break;
        w.AnimBlendTo(a, exe.S16(0x00570BE6), 0, 3);
        a.flags34 |= 0x40000100;
        b.count = 0;
        newPath(0x0049C761);
        a.sub = 12;
        break;
    case 11:
        w.AnimBlendTo(a, exe.S16(0x00570BE6), 0, 3);
        b.count = 0;
        newPath(0x0049C761);
        a.sub = 12;
        break;
    case 12:
        if (lerpTo(10)) {
            b.count = 0;
            a.flags34 &= 0xBFFFFEFFu;
            a.sub = 3;
        }
        break;
    default:
        break;
    }
    if (w.typingReferenceActive) {
        if (a.sub==3) a.flags34 &= ~0x100u;
        else a.flags34 |= 0x100u;
    }
    a.rot[1] = FaceYaw(a.pos[0], a.pos[2], w.playerPos[0], w.playerPos[2]);
    w.bossBar = static_cast<float>(a.hp) / static_cast<float>(a.hpMax);
    BossDraw(w, a, b);
    ++a.anim.counter[0];
    const uint32_t f = a.flags34;
    a.flags34 = f & ~0xEu;
    if (!(f & 0x100)) ViewPos(w, a, 2.0f);
}

// 0x0049C910: death: out of the counts / lock-on; when the camera settles the falling motion, the camera circles
// the boss (+8 per frame) and the boss drops to the ground (landing flash 0x0049DEA0); after 300 frames the camera
// is restored and event flag 3 set
void BossBeh3(World::Impl& w, Actor22Task& a, BossState& b) {
    const ExeImage& exe = w.exe;
    CameraDirector& cd = w.runner->camera;
    if (cd.camId == a.record.ParamS16(6) && a.record.ParamS16(8) <= cd.tick) {
        w.EnemyDestroy(a);  // 0x00409CC0
        return;
    }
    BossDraw(w, a, b);
    int& ctr = a.anim.counter[0];
    CameraContext& c0 = cd.ctx[0];
    switch (a.sub) {
    case 0:
        w.bossBar = 0.0f;
        w.SpawnNodeBlood(a, 2);  // 0x00407200
        --w.liveEnemies;
        --w.attackers;
        ReleaseSlots(w, a);
        ReleaseSlots(w, *a.second);
        ++a.sub;
        b.count = 0;
        [[fallthrough]];
    case 1:
        ++ctr;
        if (cd.settled) {  // 0x009C6F2D
            w.letterbox = 5;
            w.AnimSetMotion(a, exe.S16(0x00570BE8));
            ++a.sub;
            if (a.variant == 1) {
                a.pos[0] = Bits(0xC4854666);
                a.pos[1] = Bits(0x42200000);
                a.pos[2] = Bits(0xC3F5199A);
            } else if (a.variant == 2) {  // stage 5 (notes/agents/stage5.md)
                a.pos[1] = Bits(0x42200000);
                if (a.pos[2] < F32(exe, 0x00570F48)) a.pos[2] = Bits(0xC49AC000);
                else if (F32(exe, 0x00570F44) < a.pos[2]) a.pos[2] = Bits(0xC4938000);
            }
        }
        break;
    case 2:
        b.count = 0;
        cd.forceState6 = true;  // 0x009CA094
        memcpy(b.off, c0.pos, 12);
        memcpy(b.saved, c0.target, 12);
        ++a.sub;
        [[fallthrough]];
    case 3:
        if (0x1B < ctr) {
            if (a.variant == 1) w.Voice(0x1D);
            a.mouthMode = 2;
            a.vel[1] = 0;
            a.acc[1] = 0;
            ++a.sub;
        }
        ++ctr;
        break;
    case 4: {
        if (ctr < 0x84) ++ctr;
        if (w.ground + F32(exe, 0x00570F40) < a.pos[1]) {
            const float acc = static_cast<float>(a.acc[1] - F64(exe, 0x00570F38));
            a.acc[1] = acc;
            const float v = acc + a.vel[1];
            a.vel[1] = v;
            a.pos[1] = v + a.pos[1];
        } else {
            if (ctr < 0x8D) a.pos[1] = w.ground + F32(exe, 0x00570C78 + static_cast<uint32_t>(ctr) * 4);
            const int c = ++ctr;
            if (c == 0x92) {
                FlashTask* f = w.NewTask<FlashTask>(TaskFlash, 0x0049DEA0);
                memcpy(f->pos, a.pos, 12);
                f->n = 0;
                w.Sound(0x2A16A9);
            }
            if (ctr == w.MotionTicks(a.anim.motion[0])) {
                ++a.sub;
                a.mouthMode = 0;
            }
        }
        if (a.pos[1] <= w.ground + F32(exe, 0x00570F30)) a.pos[1] = w.ground + F32(exe, 0x00570F30);
        break;
    }
    case 5:
        if (299 < b.count) {
            cd.forceState6 = false;
            if (w.runner->cfg.gameType == 0) {
                memcpy(c0.pos, b.off, 12);
                memcpy(c0.target, b.saved, 12);
            }
            if (a.variant == 1) w.runner->flags[3] = 1;
            else if (a.variant == 2) w.runner->flags[0] = 1;
            ++a.sub;
        }
        break;
    default:
        break;
    }
    if (1 < a.sub && a.sub < 6) {
        ++b.count;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
        mx::RotY(m, a.rot[1] + b.count * 8);
        const float e[3] = { 10.0f, 0.0f, -20.0f }, z[3] = {};
        mx::Transform(m, e, c0.pos);
        mx::Transform(m, z, c0.target);
        if (a.variant == 1) c0.pos[1] = 30.0f;
        else if (a.variant == 2) c0.pos[1] = Bits(0xC2026666);
        ContextLookAngles(c0, c0.target, 0);  // 0x00403AC0
    }
}

// ---- stage 5: the rematch (rider variant 2 = 0x0049CDF0 -> table 0x00598018, mount variant 1 = 0x00490CE0 -> table
// 0x00597278); only the intros differ, the other behaviours are the shared ones above (notes/agents/stage5.md)

// 0x0049CE10: the rider on camera path 0x17F (camera ticks) facing back, the mount spawned (record P word 0x10); from
// camera tick 0x00577070 a timed taunt (mouth, motion 0x412), then 60 frames onto the mount's path 0x104 start; at
// frame 0x348 the fight (hp P s16 0xA, health bar, counts)
void BossBeh0V2(World::Impl& w, Actor22Task& a, BossState& b) {
    const ExeImage& exe = w.exe;
    const CameraDirector& c = w.runner->camera;
    switch (a.sub) {
    case 0:
        SpawnMountOf(w, a, b);
        ++a.sub;
        [[fallthrough]];
    case 1: {
        float pos[3] = {}, tg[3] = {};
        int roll = 0;
        if (w.cameraBank.Evaluate(JudgmentPath(w, 0x17F), static_cast<float>(c.tick), pos, tg, &roll, false)) {  // 0x0049DB40
            memcpy(a.pos, pos, 12);
            for (int k = 0; k < 3; ++k) a.rot[k] = static_cast<int>(tg[k]);
        }
        a.rot[1] += 0x8000;
        if (c.tick == static_cast<int32_t>(exe.U32(0x00577070))) {
            ++a.sub;
            b.count = 0;
        }
        break;
    }
    case 2: {
        const int old = b.count;
        const int n = ++b.count;
        if (n < 0x191) {
            if (n == 400 || n == 0x78) {
                a.mouthMode = 1;
                a.mouthTick = 0;
            } else if (n == 0x17C) {
                a.mouthMode = 0;
            }
        } else if (old == 0x22F) {
            a.mouthMode = 0;
        } else if (n == 600) {
            a.mouthMode = 1;
            a.mouthTick = 0;
            w.AnimBlendTo(a, 0x412, 0, 3);
        }
        if (a.anim.motion[0] == 0x412 && a.anim.tick[0] == w.MotionTicks(0x412) - 1) {
            EnemyTask* m = AliveTask(w, b.mount, b.mountSerial);
            w.AnimBlendTo(a, exe.S16(0x00570BE2), 0, 8);
            PathPlace(w, b, 0x104, 0.0f);  // 0x0049DB90
            if (m) {
                Mat4 q = Mat4::Identity();
                mx::Translate(q, m->pos[0], m->pos[1], m->pos[2]);
                mx::RotY(q, m->rot[1] + 0x8000);
                const float o[3] = { b.off[0], b.off[1], b.off[2] };
                mx::Transform(q, o, b.off);
            }
            ++a.sub;
            b.path = 0;
        }
        break;
    }
    case 3: {
        ++b.count;
        const int n = 0x3C - b.path;
        a.pos[0] = Lerp(a.pos[0], b.off[0], 1, n);  // 0x00401E60
        a.pos[1] = Lerp(a.pos[1], b.off[1], 1, n);
        a.pos[2] = Lerp(a.pos[2], b.off[2], 1, n);
        if (0x3C < b.path++) {
            memcpy(a.pos, b.off, 12);
            ++a.sub;
            b.path = 0;
        }
        break;
    }
    case 4:
        if (++b.count == 0x348) {
            a.hp = a.record.ParamS16(0xA);
            b.phase = 0;
            BarTask* bar = w.NewTask<BarTask>(TaskBar, 0x00435C80);  // 0x00435E50(320, 35)
            bar->x = 320.0f;
            bar->y = 35.0f;
            bar->cur = 0;
            bar->lag = 1.0f;
            bar->counter = 0;
            w.bossBar = 1.0f;
            ++w.attackers;
            ++w.liveEnemies;
            w.AnimBlendTo(a, exe.S16(0x00570BE4), 0, 3);
            ++a.behaviour;
            b.count = 0;
            a.sub = 0;
        }
        break;
    default:
        break;
    }
    a.rot[1] = FaceYaw(a.pos[0], a.pos[2], w.playerPos[0], w.playerPos[2]);  // 0x0049DE60
    BossDraw(w, a, b);
    ++a.anim.counter[0];
}

// 0x0049B620 -> 0x00598008[behaviour]
void TaskBoss22(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor22Task& a = t.As<Actor22Task>();
    BossState& b = Boss(a);
    if (a.lock) a.slot120 = static_cast<int8_t>(a.lock->slot);
    switch (a.behaviour) {
    case 0:
        if (a.variant == 2) BossBeh0V2(w, a, b);  // 0x00598018[0] (stage 5)
        else BossBeh0(w, a, b);
        break;
    case 1: BossBeh1(w, a, b); break;
    case 2: BossBeh2(w, a, b); break;
    case 3: BossBeh3(w, a, b); break;
    default: w.Note("boss behaviour " + std::to_string(a.behaviour) + " not ported"); break;
    }
    if (w.cfg.portTyping && a.behaviour >= 1 && a.behaviour <= 2 && b.typingWordPhase != unsigned(a.behaviour)) {
        b.typingWordPhase = unsigned(a.behaviour);
        const bool rematch=w.typingReferenceActive && w.cfg.eventMode!=0;
        w.typing.setWordSelection(0, a.behaviour == 1 ? (rematch?700:500) : (rematch?500:400));
        w.typing2.setWordSelection(0, a.behaviour == 1 ? (rematch?700:500) : (rematch?500:400));
    }
    w.SyncLock(a);  // lock-on readers (camera 0x00403050) read +0x100 / +0x121 live
}

// ---------------------------------------------------------------------------------------------------------------------
// Stage clear (actor type 0x61 = 0x00434EF0, type 0x62 = 0x00435930)
// ---------------------------------------------------------------------------------------------------------------------

// 0x004356A0 -> 0x00435760: a rescued civilian on the results screen (skeleton = the rescued kind, list
// 0x0055DF50[stage]); the first one plays motion 0x180 from camera tick 0x104 when bonus lives are given
struct ClearCharTask : EnemyTask {
    int done = 0;   // +0x1324 (motion 0x180 reached tick 0x80: frozen)
    int bonus = 0;  // +0x1350
};

// 0x004357F0; game type 1 with item 0xC (0x009C88AC == 1): the head scaled (1.5, 1, 1.5), the hands / feet (nodes 5 / 8 /
// 0xC / 0xF) (2, 1, 2) for the node's own model (pushed matrix)
void ClearCharNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    Mat4 s = m;
    if (w.runner->cfg.gameType == 1 && ItemFlag(w, 4)) {
        if (idx == 2) mx::Scale(s, 1.5f, 1.0f, 1.5f);
        else if (idx == 5 || idx == 8 || idx == 0xC || idx == 0xF) mx::Scale(s, 2.0f, 1.0f, 2.0f);
    }
    w.Draw(e.anim.nodes[idx & 31].model, s, 8, -1.0f, false, idx);
    if (e.c131B == 0 && idx == 5 && e.anim.motion[0] == 0x180 && 0x1D < e.anim.tick[0] && e.anim.tick[0] < 0x58) {
        Mat4 q = m;
        mx::Translate(q, 1.0f, -1.0f, 0.0f);
        mx::RotX(q, 0x4000);
        mx::RotZ(q, 0);
        mx::RotY(q, 0);
        w.Draw(0x10C3, q, 8, -1.0f, false, idx);
    }
}

void ClearCharParts(World::Impl& w, ClearCharTask& c) {  // parts 0x0055DF68 row (exe data; 0x004124F0)
    for (uint32_t a = c.parts; a != 0; a += 2) {
        const int16_t id = w.exe.S16(a);
        if (id < 0) break;
        if (id < 0x24) continue;
        const uint32_t def = w.exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id));
        const int node = static_cast<int>(w.exe.U32(def)) & 31;
        Mat4 q = c.anim.nodes[node].mv;  // node +0x28; game type 1 with item 0xC: scaled as in 0x004124F0
        const int pn = static_cast<int>(w.exe.U32(def));
        if (w.runner->cfg.gameType == 1 && ItemFlag(w, 4)) {
            if (pn == 2) mx::Scale(q, 1.5f, 1.0f, 1.5f);
            else if (pn == 5 || pn == 8 || pn == 12 || pn == 15) mx::Scale(q, 2.0f, 1.0f, 2.0f);
        }
        w.Draw(static_cast<int>(w.exe.U32(def + 4)), q, 8, -1.0f, false, node, true);
    }
}

// 0x00435760
void TaskClearChar(Task& t) {
    World::Impl& w = *CurrentWorld();
    ClearCharTask& c = t.As<ClearCharTask>();
    if (c.c131B == 0 && c.bonus != 0) {
        if (w.runner->camera.tick == 0x104) w.AnimBlendTo(c, 0x180, 0, 0x14);
        if (c.anim.motion[0] == 0x180 && c.anim.tick[0] == 0x80) c.done = 1;
    }
    w.AnimDraw(c);
    ClearCharParts(w, c);  // anim +0x1174 (0x004124F0) at the end of the skeleton draw, before the shadow
    w.EnemyShadow(c);      // 0x0040A590 (in 0x00411090)
    if (c.done == 0) ++c.anim.counter[0];
}

// 0x004356A0
void TaskClearCharInit(Task& t) {
    World::Impl& w = *CurrentWorld();
    ClearCharTask& c = t.As<ClearCharTask>();
    c.actorSlot = -1;
    c.slot120 = -1;
    w.EnemyInitPose(c);  // 0x00410440
    c.anim.rotOrder = 1;
    c.anim.counter[0] = w.Rand(0x004356DB);
    c.nodeFn = ClearCharNode;  // 0x004357F0
    c.flags34 |= 0x8000;
    c.postRootFn = [](World::Impl&, EnemyTask&) {};  // +0x12F0 = 0x00420810 (0x00410440's default: no floor snap)
    const uint32_t row = 0x0055DF68 + static_cast<uint32_t>(c.anim.kind - 0x20) * 6;
    if (w.exe.S16(row) != -1) {
        c.parts = row;  // 0x00412440: part list (drawn by ClearCharParts)
        for (uint32_t a = row;; a += 2) {
            const int16_t id = w.exe.S16(a);
            if (id < 0) break;
            const uint32_t def = w.exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id));
            if (id < 0x24) c.anim.nodes[static_cast<int>(w.exe.U32(def)) & 31].model = static_cast<int32_t>(w.exe.U32(def + 4));
            if (id > 0x11 && w.residency) w.resQueue.push_back({ 0x50, w.exe.U32(def + 4) });  // 0x0041D5D0: load request
        }
    }
    if (c.anim.kind == 0x20) {
        const int m = c.anim.motion[0];
        if (0x17B < m && (m < 0x17E || m == 0x17F)) c.pos[1] = c.pos[1] - F32(w.exe, 0x0055E174);
    }
    t.fn = TaskClearChar;
    c.fn = 0x00435760;
    TaskClearChar(t);
}

// 0x00434EF0: 420 frames: the rescued civilians (0x004356A0), bonus lives 0x0055E044[stage][rescued] for both
// players, then event flag 0xFE; every frame the background tiles and the view-space result text / numbers
struct ClearTask : WTask {
    int16_t timer = 0;  // +0x11C
    int16_t sub = 0;    // +0x1312
};

// A view-space model of the result screen: matrix push, identity, translate (x, y, -1), uniform scale, draw, pop
void ClearText(World::Impl& w, int model, float x, float y, float s) {
    if (model == 0) return;  // (an empty slot of the text tables draws nothing)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, x, y, -1.0f);
    mx::Scale(m, s, s, s);
    w.Draw(model, m, 8, -1.0f, false, -1, true);
}

// 0x004362E0(p, x, y, k): player p's score as digit models 0x165F + d, from x + p * k, leading zeros hidden
void ClearScore(World::Impl& w, int p, float x, float y, float k) {
    const int score = w.player[p].score;
    const float x0 = static_cast<float>(p) * k + x;
    const float s = Bits(0x3D23D70A);
    static const uint32_t kOff[6] = { 0, 0x0055E15C, 0x0055E194, 0x0055E190, 0x0055E18C, 0x0055E188 };
    int div = 100000;
    for (int i = 0; i < 6; ++i, div /= 10)
        if (i == 5 || div - 1 < score) ClearText(w, (score / div) % 10 + 0x165F, i == 0 ? x0 : x0 + F32(w.exe, kOff[i]), y, s);
}

// 0x00436620(p, x, y, k): player p's hit rate hits * 100 / shots (shots 0 -> 1; outside 0..100 -> 0) and "%" 0x1679
void ClearRate(World::Impl& w, int p, float x, float y, float k) {
    World::Impl::Player& P = w.player[p];
    if (P.shots == 0) P.shots = 1;
    int pct = P.hits * 100 / P.shots;
    if (pct > 100 || pct < 0) pct = 0;
    const float x0 = static_cast<float>(p) * k + x;
    const float s = Bits(0x3D23D70A);
    if (pct > 99) ClearText(w, (pct / 100) % 10 + 0x165F, x0, y, s);
    if (pct > 9) ClearText(w, (pct / 10) % 10 + 0x165F, x0 + F32(w.exe, 0x0055E15C), y, s);
    ClearText(w, pct % 10 + 0x165F, x0 + F32(w.exe, 0x0055E194), y, s);
    ClearText(w, 0x1679, x0 + F32(w.exe, 0x0055E190), y, s);
}

void ClearDraw(World::Impl& w, const struct ClearTask& k, int count, int bonus);

void TaskClear(Task& t) {
    World::Impl& w = *CurrentWorld();
    ClearTask& k = t.As<ClearTask>();
    const ExeImage& exe = w.exe;
    const int st = w.cfg.eventMode >= 0 && w.cfg.eventMode < 10 ? w.cfg.eventMode : 0;
    const uint32_t list = exe.U32(0x0055DF50 + 4 * static_cast<uint32_t>(st));
    const int count = w.rescueCount[st];
    int8_t bonus = 0;
    exe.Read(0x0055E044 + static_cast<uint32_t>(st * 8 + std::min(count, 7)), &bonus, 1);
    if (w.cfg.portStageResults && w.gameMode == 6 && w.cfg.eventMode >= 0 && w.cfg.eventMode < 6 &&
        w.cfg.gameType != 2 && !w.stageResults.shown) {
        // Hold this task for the score page, then run the original rescue scene on dismissal.
        // It owns the civilian actors, timed life award and event-completion flag.
        w.BeginStageResults(bonus);
        return;
    }
    if (k.sub == 0) {
        // 0x0041D3A0(0, 0, -1): sounds stopped
        w.Sound(0x10000003);
        w.canShoot = 0;
        w.flags5900 |= 0x10;
        k.timer = 0x1A4;
        auto spawn = [&](uint32_t e, int i, int kind, int motion, bool b) {
            ClearCharTask* c = w.NewTask<ClearCharTask>(TaskClearCharInit, 0x004356A0);
            c->c131B = static_cast<int8_t>(i);
            c->pos[0] = F32(exe, e + 4);
            c->pos[1] = F32(exe, e + 8);
            c->pos[2] = F32(exe, e + 0xC);
            c->rot[1] = static_cast<int32_t>(exe.U32(e + 0x10));
            c->anim.kind = static_cast<int16_t>(kind);
            c->anim.motion[0] = motion;
            if (b) c->bonus = 1;
        };
        if (count == 0) {
            int i = 0;
            for (uint32_t e = list; exe.S16(e) != -1; e += 20, ++i) {
                const int r = w.Rand(0x0043504A);
                spawn(e, i, exe.S16(e), r % 3 + 0x18B, false);
            }
        } else {
            for (int i = 0; i < count && i < 10; ++i) {
                const uint32_t e = list + static_cast<uint32_t>(i) * 20;
                spawn(e, i, w.rescueKinds[st][i], exe.S16(e + 2), 0 < bonus);
            }
        }
        ++k.sub;
    } else if (k.sub == 1) {
        if (k.timer < 0x79) ++k.sub;
    } else if (k.sub == 2) {
        for (int p = 0; p < 2; ++p) {
            int v = w.player[p].lives + bonus;
            const int cap = w.runner->cfg.gameType == 1 ? ItemLivesMax(w, p) : 5;  // game type 1: items +5 (0x009A2245), else 0x009A2440
            if (cap <= v) v = cap;
            w.player[p].lives = static_cast<int16_t>(v);
        }
        ++k.sub;
    }
    ClearDraw(w, k, count, bonus);  // 0x00435118
    if (--k.timer < 1) {
        w.runner->flags[0xFE] = 1;  // 0x009C72FE
        w.flags5900 &= ~0x10u;
        w.ts.KillCurrent();
    }
}

// 0x00435118: the result screen of 0x00434EF0 (drawn every frame before the timer counts down)
void ClearDraw(World::Impl& w, const ClearTask& k, int count, int bonus) {
    const ExeImage& exe = w.exe;
    int id = 0xA2A;  // 17 background tiles 128 x 128 (the middle of row 1 left out), Spr_DrawSimple depth 1.1
    for (int row = 0; row < 4; ++row)
        for (int x = 0; x < 0x280; x += 0x80)
            if (row != 1 || x < 0x80 || 0x180 < x) w.hudSprites.push_back(Sprite2D(id++, static_cast<float>(x), static_cast<float>(row << 7), Bits(0x3F8CCCCD), 1.0f, 1.0f, 0));
    const float s1 = Bits(0x3D8F5C29), s2 = Bits(0x3D4CCCCD), s3 = Bits(0x3CF5C28F), s4 = Bits(0x3D23D70A);
    for (int i = 0; i < 9; ++i)  // title text 0x0055DFF8
        ClearText(w, exe.S16(0x0055DFF8 + 2 * i), static_cast<float>(i) * F32(exe, 0x004C4C88) - F32(exe, 0x004C4C58), Bits(0x3E6147AE), s1);
    if (k.timer < 0x187) {  // rescued count, counting up every 20 frames
        int n = (0x186 - k.timer) / 0x14;
        if (count < n) n = count;
        ClearText(w, n + 0x165F, 0.25f, Bits(0x3E6147AE), s1);
    }
    if (k.timer < 0xB5)
        for (int i = 0; i < 12; ++i)  // bonus text 0x0055E00C
            ClearText(w, exe.S16(0x0055E00C + 2 * i), static_cast<float>(i) * F32(exe, 0x0055E170) - F32(exe, 0x0055E16C), s2, s2);
    if (k.timer < 0x97) ClearText(w, bonus + 0x165F, Bits(0x3E6F9DB2), s2, s2);
    for (int i = 0; i < 8; ++i)  // text 0x0055E034 (x: float * float - double 0x0055E160, rounded to float)
        ClearText(w, exe.S16(0x0055E034 + 2 * i),
                  static_cast<float>(static_cast<double>(static_cast<float>(i + 5) * F32(exe, 0x0055E168)) - F64(exe, 0x0055E160)), Bits(0xBE75C28F), s3);
    for (int p = 0; p < 2; ++p) {
        if (w.player[p].state != 5) continue;
        ClearText(w, p == 0 ? 0x1660 : 0x1661, Bits(p == 0 ? 0xBEB39C0F : 0x3E18C7E3), Bits(0xBDE147AE), s4);
        for (int i = 0; i < 7; ++i)  // score text 0x0055E024
            ClearText(w, exe.S16(0x0055E024 + 2 * i),
                      p == 0 ? static_cast<float>(i) * F32(exe, 0x0055E15C) - F32(exe, 0x0055E158) : static_cast<float>(i) * F32(exe, 0x0055E15C) + F32(exe, 0x0055E154),
                      Bits(0xBDE147AE), s4);
        ClearScore(w, p, Bits(0xBE961E4F), Bits(0xBE2E147B), 0.5f);
        if (0x13 < w.player[p].shots) ClearRate(w, p, Bits(0xBE78D4FE), Bits(0xBE75C28F), Bits(p == 0 ? 0x3F000000 : 0x3EBFB15B));
    }
}

}  // namespace

// Actor types 0x61 (0x00434EF0, the stage clear) / 0x62 (0x00435930: the rescued civilians' models loaded, then ends)
bool World::Impl::SpawnStageClear(Task& t, SpawnTask& s) {
    if (s.rec.type == 0x62) {
        // 0x00435930: texture 0x16A (0x0041D6D0: texFiles), pol 0x7C (the result text and digits 0x165F.., drawn by
        // 0x00434EF0) and pol 0x85 + kind per civilian shown (0x0041D650, ring type 3), flushed at once (0x0041D9D0);
        // the part lists 0x00412400 are not modelled
        // (queued behind the pending requests, e.g. part models released by the rescued civilians, and flushed in order)
        resQueue.push_back({ 0x54, 0x16A });
        resQueue.push_back({ 0x52, 0x7C });
        const int st = cfg.eventMode >= 0 && cfg.eventMode < 10 ? cfg.eventMode : 0;
        auto kindFiles = [&](int kind) {
            resQueue.push_back({ 0x52, static_cast<uint32_t>(kind + 0x85) });  // 0x0041D650
            // 0x00412400(0x0055DF68 row of the kind): every part model requested (0x0041D5D0)
            for (uint32_t a = 0x0055DF68 + static_cast<uint32_t>(kind - 0x20) * 6;; a += 2) {
                const int16_t id = exe.S16(a);
                if (id < 0) break;
                resQueue.push_back({ 0x50, exe.U32(exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id)) + 4) });
            }
        };
        if (rescueCount[st] == 0) {
            for (uint32_t e = exe.U32(0x0055DF50 + 4 * static_cast<uint32_t>(st)); exe.S16(e) != -1; e += 20) kindFiles(exe.S16(e));
        } else {
            for (int i = 0; i < rescueCount[st] && i < 10; ++i) kindFiles(rescueKinds[st][i]);
        }
        Resource(0x59, 0);  // 0x0041D9D0: the queued requests of types < 8, in order
        ts.KillCurrent();
        return true;
    }
    if (s.rec.type != 0x61) return false;
    auto d = std::make_unique<ClearTask>();
    d->fn = 0x00434EF0;
    t.data = std::move(d);
    t.fn = TaskClear;
    TaskClear(t);
    return true;
}

// 0x0045ED90 (op 0x4F past the mode's last scene): 0x009C71C0 = 1, hits counted again (0x009A5C48 = 0); in game mode
// 6 (not sub-state 8) sub-state 5 (the next stage); 0x0041D350 (sounds)
void World::Impl::StageEnd() {
    if(cfg.portTyping&&cfg.eventMode==7&&cfg.gameType==2) {
        // Source 6D supervises the course after the common script halts.
        runner->globals[0x009C71C0]=1;
        return;
    }
    // The PC's last chapter has no actor61 report. Its HUD/ranking getter
    // still includes difficulty, accuracy and rescue bonuses (0x004843B0).
    if (cfg.portTyping && typingReferenceActive && cfg.eventMode == 5) {
        typing.finishChapter(static_cast<TypingDifficulty>(std::clamp(cfg.difficulty, 0, 4)),
                             static_cast<uint32_t>(std::max<int>(0, rescueCount[5])));
        if (player[1].state != 9) typing2.finishChapter(static_cast<TypingDifficulty>(std::clamp(cfg.difficulty,0,4)),
                             static_cast<uint32_t>(std::max<int>(0,rescueCount[5])));
    }
    runner->globals[0x009C71C0] = 1;
    runner->globals[0x009A5C48] = 0;
    if (gameMode == 6 && subState != 8) {
        subState = 5;
        SoundStopAll();  // 0x0041D350
        BeginStageResults();  // final chapter / boss routes without the rescue-page actor
    }
}

// Game mode 6 sub-states 5 (0x00460390) / 6 (0x004603B0): one more stage frame, then the next event mode with
// sub-state 1 (7 after stage 6); sub-state 1 for a later stage (0x0045FFF0 -> Stage_Start 0x00460030) is the
// mid-game start of world_st2.cpp (WorldConfig::carry), not chained inside one run
void World::Impl::Mode6StageEnd() {
    if(cfg.portTyping&&cfg.eventMode==7&&cfg.gameType==2) { StageFrame(); return; }
    auto notPlaying = [&](int p) { return exe.U32(0x00579CD4 + 0x14 * static_cast<uint32_t>(player[p].state)) != 0; };
    if (subState == 5) {
        stageEndCounter = 1;  // 0x007DCCCC
        ++subState;
    } else if (subState == 6) {
        if (--stageEndCounter == 0) {
            if (runner->cfg.gameType == 2 || runner->cfg.gameType == 3) {
                if (!modes) Note("stage end in game type " + std::to_string(runner->cfg.gameType) + " without the persistent data");
                else ModesStageEnd(*this);  // 0x004603B0 game types 2 / 3 (world_modes_end.cpp)
            } else {
                if (StartTypingChapterChoice(*this)) return;
                nextEventMode = cfg.eventMode + 1;  // 0x009A1A08 (+ 0x00413160)
                subState = nextEventMode != 6 ? 1 : 7;
            }
            // 0x0049DFD0 (resources)
            return;
        }
    } else {
        Note("next stage start inside one run (mode 6 sub-state 1, 0x0045FFF0) not ported");
        StageFrame();
        return;
    }
    if (notPlaying(0) && notPlaying(1)) {
        if (gameMode == 6 && (runner->cfg.gameType < 2 || 3 < runner->cfg.gameType) && !CanContinue()) subState = 0xB;
        else subState = 3;
    }
    StageFrame();
    // 0x009A5C44 + 1
}
// 0x00435E50(x, y): the boss health bar task 0x00435C80, 0x009C8E10 = 1
void World::Impl::SpawnBossBar(float x, float y) {
    BarTask* bar = NewTask<BarTask>(TaskBar, 0x00435C80);
    bar->x = x;
    bar->y = y;
    bar->cur = 0;
    bar->lag = 1.0f;
    bar->counter = 0;
    bossBar = 1.0f;
}
// 0x00435EE0(id, count, interval): the sound now and count - 1 more times (task 0x00435E90)
void World::Impl::SpawnSoundRepeat(uint32_t id, int count, int interval) {
    RepeatTask* k = NewTask<RepeatTask>(TaskRepeat, 0x00435E90);
    k->left = static_cast<int16_t>(count - 1);
    k->id = id;
    k->interval = static_cast<int16_t>(interval);
    k->timer = 1;
    Sound(id);
}

// 0x00437A70(record): the boss name card task (also used by the stage-2 boss, world_st2c_boss.cpp)
void World::Impl::SpawnNameCard(uint32_t rec) {
    CardTask* c = NewTask<CardTask>(TaskCard, 0x00437AC0);
    c->rec = rec;
    c->state = 0;
}

// 0x0049B0D0 for variant 1 (the spawn task becomes the actor): Actor22 setup, the name card task, behaviour 0 now
bool World::Impl::SpawnBoss22(Task& t, const SpawnRecord& r) {
    auto d = std::make_unique<Actor22Task>();
    SetTypingSpawnOrigin(*d, r);
    d->record = r;
    d->flags34 = r.flags | 1;  // 0x00408A20
    memcpy(d->pos, r.pos, 12);
    memcpy(d->rot, r.rot, 12);
    d->hp = d->hpMax = static_cast<int16_t>(r.state);
    d->lo = static_cast<int16_t>(r.lo);
    d->behaviour = 0;
    d->sub = 0;
    d->actorSlot = -1;
    d->slot120 = -1;
    d->target = 0;  // +0x121 is not set by 0x0049B0D0 (cleared task memory)
    d->variant = r.ParamS8(1);
    d->anim.kind = 0x45;
    d->anim.motion[0] = r.ParamS16(2);
    EnemyInitPose(*d);  // 0x00410440
    d->anim.rotOrder = 5;
    d->nodeCb = 0x0049D980;
    d->anim.drawFlags |= 4;
    d->anim.counter[0] = r.ParamS16(4);
    d->radius = F32(exe, 0x004C4D28 + 0x45 * 4);
    d->anim.nodes[2].model = 0x2A9;
    d->mouthMode = d->mouthTick = 0;
    d->lock = &lockTargets[nextLockTarget++];
    auto s = std::make_unique<EnemyTask>();  // 0x004A74E0(0x13F4): skeleton 0x46, motion 0x10
    s->actorSlot = -1;
    s->slot120 = -1;
    s->anim.kind = 0x46;
    s->anim.motion[0] = 0x10;
    EnemyInitPose(*s);
    s->anim.rotOrder = 5;
    s->nodeCb = 0x00411050;
    s->flags34 |= 0x88000;
    d->secondWant = d->secondHave = 1;
    d->second = std::move(s);
    d->boss = std::make_unique<BossState>();
    const bool v2 = d->variant == 2;  // stage 5: 0x0049CDF0, no name card here
    d->fn = v2 ? 0x0049CDF0 : 0x0049B620;
    t.data = std::move(d);
    t.fn = TaskBoss22;
    if (!v2) {
        CardTask* c = NewTask<CardTask>(TaskCard, 0x00437AC0);  // 0x00437A70(0x00570EC8)
        c->rec = 0x00570EC8;
        c->state = 0;
    }
    TaskBoss22(t);
    return true;
}

// Actor 0x23 from its own record (op 0x0B, 0x00408A20; training lesson 4 spawns the variant-2 mount without a rider)
bool SpawnMount23(World::Impl& w, Task& t, const SpawnRecord& r) {
    auto m = std::make_unique<MountTask>();
    w.SetTypingSpawnOrigin(*m, r);
    m->lock = &w.lockTargets[w.nextLockTarget++];
    m->flags34 = r.flags | 1;  // 0x00408970
    m->flags38 = 0;
    memcpy(m->pos, r.pos, 12);
    memcpy(m->rot, r.rot, 12);
    m->lo = static_cast<int16_t>(r.lo);
    m->hp = m->hpMax = static_cast<int16_t>(r.state);
    m->record = r;
    m->rider = nullptr;
    m->fn = 0x0048FD90;
    t.data = std::move(m);
    t.fn = TaskMountCtor;
    TaskMountCtor(t);
    return true;
}

// A phrase remains assigned while Judgment moves or staggers; completion waits
// for the native damage window, then enters its original shot/reaction path.
bool TypingBoss1(World::Impl& w, ActorTask& actor, TypingTargetSpec& out, bool complete) {
    if (auto* mount=dynamic_cast<MountTask*>(&actor)) {
        if(w.cfg.portTyping && w.cfg.eventMode==7 && w.cfg.gameType==2 && mount->variant==2) {
            if(mount->behaviour!=1 || (mount->typingFlags&0x400u))return true;
            out.node=9;out.danger=true;out.retain=true;out.token=0x230002u;
            memcpy(out.view,mount->anim.nodes[9].hit,12);
            if(complete){TypingDeliver(*mount,9);mount->typingStrike=false;}
        }
        return true;
    }
    if (auto* a = dynamic_cast<Actor22Task*>(&actor)) {
        if (!a->boss) return false;
        out.boss = true;
        if (a->hp < 1 || a->behaviour < 1 || a->behaviour > 2 || (a->flags34 & 0x4000000u)) return true;
        BossState& b = Boss(*a);
        out.retain = true;
        out.token = 0x100000u | (static_cast<uint32_t>(a->behaviour) << 12) | (static_cast<uint32_t>(b.phase) << 8);
        memcpy(out.view, a->anim.nodes[2].hit, 12);
        const int s = a->sub;
        const bool first = a->behaviour == 1 && (s == 1 || s == 5 || s == 7 || s == 11);
        const bool second = a->behaviour == 2 && (s == 3 || s == 5 || s == 7 || s == 9 || s == 10);
        if ((!first && !second) || (a->flags34 & 0x40008100u)) return true;
        out.node = 2;
        if (complete) TypingDeliver(*a, out.node);
        return true;
    }
    // Kuarl's armored mount is not Judgment's weak point.
    return dynamic_cast<MountTask*>(&actor) != nullptr;
}

}  // namespace game
