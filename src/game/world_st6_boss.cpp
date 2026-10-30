// game::World - the final boss (actor type 0x2D variant 1: 0x00426A70 -> task 0x00426C30, state table 0x005898A8), its
// eight orbs (0x00429D00 -> 0x00429D30, state table 0x00589908), the summoned creature (0x0042C830 -> 0x0042C8D0) and
// their effect tasks. Status and verification (world_test --stage-start 70806 on build\scene_capture_auto_st6.txt):
// notes/agents/stage6.md.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/shoot_camera.h"
#include "game/shooting.h"
#include "game/typing_boss_rules.h"
#include "game/world_st6.h"

namespace game {

using namespace st6;

void CreatureFirst(Task& t);   // 0x0042C830
void CreatureFirst0(Task& t);  // 0x0042C0B0
void CreatureFirst2(Task& t);  // 0x0042CD30
void Creature3First(Task& t);  // 0x0042D490 (patterns 3 / 4)

namespace {

// The boss object (block 0x13F4; Actor2DTask = the actor 0x2D layout of world_st5.h)
struct FinalBossTask : Actor2DTask {
    int pattern = 0;      // +0x1320 next attack pattern (sub-state of states 3 / 4)
    int rank = 0;         // +0x1324 0..15 (0x00428D60)
    int hits = 0;         // +0x1328
    int freeze = 0;       // +0x132C (1: the animation counter holds)
    int t = 0;            // +0x1330
    int f1334 = 0;        // +0x1334
    int hitBy = -1;       // +0x133C (0x00428DA0: -1 none, 0 / 1 player, 2 both)
    float f1340 = 0;      // +0x1340
    int place = 0;        // +0x1350 position index (table 0x0055CCE0)
    int prevPlace = 0;    // +0x1354
    int f1358 = 0;        // +0x1358
    int f135C = 0;        // +0x135C
    int step = 0;         // +0x1368
    float tgt[3] = {};    // +0x13C0
    Mat4 mv1 = Mat4::Identity(), mv5 = Mat4::Identity();  // +0x2C4 / +0x504: node 1 / 5 model-view of the last draw
    std::array<TypingWordRecord, 3> typingAnswers{};
    uint32_t typingDialogueToken = 0;
    unsigned typingRevealFrames = 0;
    bool typingDialogueActive = false, typingAnswerActors = false, typingAnswerHit = false;
};

struct EmperorAnswerTask : ActorTask {
    FinalBossTask* boss = nullptr;
    uint32_t bossSerial = 0;
    unsigned choice = 0;
};

// Orb object (0x00429D00, block 0x13F4)
struct OrbTask : EnemyTask {
    int idx = 0;                  // +0x131B
    FinalBossTask* boss = nullptr;  // +0x1394
    uint32_t bossSerial = 0;
    int t = 0;          // +0x1330
    int f1334 = 0;      // +0x1334
    int f1338 = 0;      // +0x1338
    int mode = 0;       // +0x133C (draw mode)
    float scale = 0;    // +0x1340
    int spin = 0;       // +0x1350
    int trailEnd = 0;   // +0x1368 (1: its trail task ends)
    float tgt[3] = {};  // +0x13C0
};

// The summoned creature (0x0042C830 -> 0x0042C8D0, skeleton 0x4F)
struct CreatureTask : EnemyTask {
    FinalBossTask* boss = nullptr;  // +0x1394
    int anim1320 = 0;   // +0x1320 (1: the animation runs)
    int lit = 0;        // +0x1324 (1: nodes drawn, attachments on)
    int t = 0;          // +0x1330
    int f1334 = 0;      // +0x1334
    float tgt[3] = {};  // +0x13C0 (patterns 2 / 3 / 4: the players' point taken; pattern 0: the path point)
    Mat4 mv1 = Mat4::Identity();  // node 1 model-view (0x0042CCC0)
    // patterns 0 / 2 (two-player capture 2p_auto_b, notes/agents/twoplayer2.md)
    int billboardNode = 1;         // the node drawn as the flame billboard 0x199C.. while not lit (pattern 2: 0x16)
    uint32_t orbTable = 0x0055D238;  // node -> orb slot (pattern 2: 0x0055D268)
    int side = 0;                  // +0x1350 (pattern 0: path 0x192 + side)
    Mat4 mv24 = Mat4::Identity();  // node 24 model-view (pattern 2: +0xFB4)
    std::unique_ptr<EnemyTask> extra;  // pattern 0: the second skeleton (0x4E) on node 1 (+0x13B0, Mem_Alloc block)
};

// The explosion task 0x004298C0
struct BlastTask : EnemyTask {
    int c1320 = 0, s1324 = 0, s1328 = 0;
    float f1340 = 0;
};

float RF(const World::Impl& w, uint32_t va) { return F32(w.exe, va); }

// node model-view (node +0x28): the model-view chain of AnimDraw (EnemyNode::mv, as the original builds it from the view)
Mat4 NodeMV(World::Impl&, const EnemyTask& e, int i) {
    return e.anim.nodes[i & 31].mv;
}

// 0x0055CCE0 + i * 12: the boss positions
void PlacePos(World::Impl& w, int i, float out[3]) {
    const uint32_t e = 0x0055CCE0 + static_cast<uint32_t>(i) * 12;
    for (int k = 0; k < 3; ++k) out[k] = RF(w, e + 4 * k);
}

// 0x00429620(from, to): yaw / pitch from `to` toward `from` (0x004016B0)
void FaceFrom(const float from[3], const float to[3], int* yaw, int* pitch) {
    int p = 0, y = 0;
    DirToAngles(from[0] - to[0], from[1] - to[1], from[2] - to[2], &p, &y);
    *yaw = y & 0xFFFF;
    *pitch = p & 0xFFFF;
}

// 0x00401E60(cur, target, 1, n) on the three coordinates
void LerpTo(float pos[3], const float tgt[3], int n) {
    for (int k = 0; k < 3; ++k) pos[k] = Lerp(pos[k], tgt[k], 1, n);
}

// 0x00428D60
void RankAdd(FinalBossTask& b, int d) {
    b.rank += d;
    if (b.rank < 0) b.rank = 0;
    else if (0xF < b.rank) b.rank = 0xF;
}

// 0x00409B70(r) for the boss / creature: view position, hit target, lock-on candidate
void ViewPosR(World::Impl& w, EnemyTask& e, float r) {
    mx::Transform(w.cam.view, e.body, e.viewPos);
    w.RegisterTarget(&e);
    e.body[1] = r + e.body[1];
    w.SyncLock(e);
    w.lockOn.Register(e.lock, w.playerPos);  // 0x00408EC0
}

void BossDraw(World::Impl& w, FinalBossTask& b) {  // 0x00428F70
    st5::Draw2D(w, b);
    b.mv1 = NodeMV(w, b, 1);
    b.mv5 = NodeMV(w, b, 5);
}

// 0x00429710: no orb slot active
bool OrbsIdle(St6State& s) {
    for (const auto& o : s.orbs)
        if (o.active != 0) return false;
    return true;
}

// 0x00428F00: player p's shot passes within +0x1374 of node 1's point (2.3121, 0.1097, 0)
bool WeakPoint(World::Impl& w, FinalBossTask& b, int p) {
    Mat4 m = b.mv1;
    mx::Translate(m, Bits(0x4013F972), Bits(0x3DE0AA65), 0.0f);
    float v[3];
    mx::GetTranslation(m, v);
    return ShotRayDistance(w.player[p].ray, v[0], v[1], v[2]) <= b.f1374;
}

// Preserve each of Emperor's scripted health thresholds when a phrase lands.
float EmperorDamage(World::Impl& w, FinalBossTask& boss, ActorTask& source) {
    const float base = static_cast<float>(w.exe.S16(0x0055CCD6 + static_cast<uint32_t>(w.players) * 2));
    if (!source.typingStrike) return base;
    if (w.cfg.portTyping) {
        source.typingStrike = false;
        return float(TypingEmperorWordDamage(w.players == 2));
    }
    const int floor = boss.behaviour == 3 ? boss.record.ParamS16(0xC) :
                      boss.behaviour == 4 ? boss.record.ParamS16(0xE) : 0;
    return std::min(TypingDamage(source, base), static_cast<float>(std::max(0, static_cast<int>(boss.hp) - floor)));
}

// Hit flash task 0x00429760 (0x00429730): model 0x276 + frame at the node's view-space hit point, 25 frames
struct FlashTask : WTask {
    FinalBossTask* boss = nullptr;
    uint32_t bossSerial = 0;
    int frame = 0;  // +0x38
    int node = 1;   // +0x3C
};

bool Alive(World::Impl& w, const void* p, uint32_t serial) {
    for (Task* t : w.ts.Live())
        if (t->serial == serial) return t->data.get() == p;
    return false;
}

void TaskFlash(Task& t) {
    World::Impl& w = *CurrentWorld();
    FlashTask& k = t.As<FlashTask>();
    if (Alive(w, k.boss, k.bossSerial)) {
        const EnemyNode& n = k.boss->anim.nodes[k.node & 31];
        const float z = n.radius + n.hit[2];
        float s = 1.0f;
        if (RF(w, 0x004C4C60) <= z) s = -z * RF(w, 0x004C4C5C) + RF(w, 0x004C4C58);
        s = static_cast<float>(s * F64(w.exe, 0x004C4C48));
        Mat4 m = Mat4::Identity();
        mx::Translate(m, n.hit[0], n.hit[1], z);
        mx::Scale(m, s, s, s);
        w.Draw(k.frame + 0x276, m, 8, -1.0f, false, -1, true);
    } else {
        w.Note("final boss hit flash: boss freed (0x00429760 reads its node)");
    }
    if (0x18 < ++k.frame) w.ts.KillCurrent();
}

void SpawnFlash(World::Impl& w, FinalBossTask& b) {  // 0x00429730
    FlashTask* k = w.NewTask<FlashTask>(TaskFlash, 0x00429760);
    k->boss = &b;
    k->bossSerial = w.CurrentSerial();
    k->node = 1;
    k->frame = 0;
}

// 0x00428DA0: shots on node 1 (the weak point) while the sub-state is 1..5; true when damaged
bool BossHits(World::Impl& w, FinalBossTask& b) {
    if (w.cfg.portTyping && b.typingAnswerHit) {
        b.typingAnswerHit = false;
        b.hp = static_cast<int16_t>(b.hp - TypingEmperorWordDamage(w.players == 2));
        b.hitBy = 0;
        RankAdd(b, 1);
        b.light[0] = 1.0f;
        b.light[1] = 0.25f;
        b.light[2] = 0.5f;
        SpawnFlash(w, b);
        w.Sound(0x1625A9);
        return true;
    }
    int who = -1;
    bool dmg = false;
    if (0 < b.sub && b.sub < 6 && (b.flags34 & 8) && !(b.flags34 & 0x40000000)) {
        for (int p = 0; p < 2; ++p) {
            const int node = b.hitNode[p];
            if (node == 1 && !(b.flags34 & 0x100) && (b.typingStrike || WeakPoint(w, b, p))) {
                who = who + 1 + p;
                b.hp = static_cast<int16_t>(b.hp - EmperorDamage(w, b, b));
                RankAdd(b, 1);
                b.light[0] = 1.0f;
                b.light[1] = 0.25f;
                b.light[2] = 0.5f;
                SpawnFlash(w, b);
                w.Sound(0x1625A9);
                dmg = true;
            } else if (node != 0 && b.sub != 5) {
                w.Sound(0x1216A9);
            }
            b.hitNode[p] = 0;
            const uint32_t bit = 2u << p;
            EnemyNode& n = b.anim.nodes[node & 31];
            n.flags = n.flags & ~bit & ~8u;
            b.flags34 &= ~bit;
        }
    }
    b.hitBy = who;
    return dmg;
}

// 0x00429530(path, t): path position + the camera 0xDF position at 1810 (y - 10, path y - 22.2)
void PathTarget(World::Impl& w, FinalBossTask& b, int path, float tm) {
    float p[3] = {};
    int ang[3];
    // Source4162E0 receives 1AA..1B1 rather than the gun game's188..18F.
    st5::EvalPath(w, w.typingReferenceActive ? path+0x4022 : path, tm, p, ang);
    p[1] = p[1] - RF(w, 0x0055D19C);
    float c[3] = {}, tg[3] = {};
    int roll = 0;
    w.cameraBank.Evaluate(w.typingReferenceActive ? 0x40F2 : 0xDF, Bits(0x44E24000), c, tg, &roll, false);
    c[1] = c[1] - RF(w, 0x004C4398);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, c[0], c[1], c[2]);
    mx::Transform(m, p, b.tgt);
}

void SetRadii(World::Impl& w, FinalBossTask& b, bool on) {  // nodes 3 / 4 radius (+0x434 / +0x4C4)
    const uint32_t tab = w.exe.U32(0x004D032C + static_cast<uint32_t>(b.anim.kind) * 4);
    b.anim.nodes[3].radius = on ? RF(w, tab + 0x4C) : 0.0f;
    b.anim.nodes[4].radius = on ? RF(w, tab + 0x60) : 0.0f;
}

int Len(World::Impl& w, int motion) { return w.MotionTicks(motion); }

// shared tail of states 3 / 4 / 5
void BossTail(World::Impl& w, FinalBossTask& b, bool flat) {
    float from[3] = { b.pos[0], b.pos[1], b.pos[2] }, to[3] = { w.playerPos[0], w.playerPos[1], w.playerPos[2] };
    if (flat) from[1] = to[1] = 0.0f;
    FaceFrom(from, to, &b.rot[1], &b.rot[0]);
    w.bossBar = static_cast<float>(b.hp) / static_cast<float>(b.hpMax);
    BossDraw(w, b);
}

void NewPattern(World::Impl& w, FinalBossTask& b, uint32_t site) {  // LAB_0042743F
    PlacePos(w, b.place, b.tgt);
    b.sub = 0;
    b.step = 0;
    const int r = w.Rand(site);
    b.pattern = w.typingReferenceActive ? int(TypingEmperorFirstPhasePattern(unsigned(r))) :
        static_cast<int>(w.exe.U32(0x0055CD1C + static_cast<uint32_t>((r >> 4) % 10 + b.rank * 10) * 4)) + 1;
    w.Sound(0x1425A9);
}

void NextState(World::Impl& w, FinalBossTask& b, uint32_t site1, uint32_t site2) {  // LAB_004273D0 / state 3 case 4
    b.flags34 |= 0x100;
    w.Sound(0x1425A9);
    ++b.behaviour;
    PlacePos(w, b.place, b.tgt);
    b.sub = 0;
    b.step = 0;
    const int u = w.Rand(site1) & 3;
    const int r = w.Rand(site2);
    b.pattern = static_cast<int>(w.exe.U32(0x0055CFC0 + static_cast<uint32_t>(r % 10 + u * 10) * 4));
}

void HitPlayers(World::Impl& w, int kind) {  // Player_TakeHit for the players in the game (0x009C7000)
    if (w.playersInGame == 0) w.PlayerTakeHit(0, 1, kind);
    else if (w.playersInGame == 1) w.PlayerTakeHit(1, 1, kind);
    else if (w.playersInGame == 2) {
        w.PlayerTakeHit(0, 1, kind);
        w.PlayerTakeHit(1, 1, kind);
    }
}

void SortOrbs(St6State& s) {  // 0x00429680
    memcpy(s.sorted, s.orbs, sizeof(s.sorted));
    for (int i = 0; i < 7; ++i)
        for (int j = i; j >= 0; --j) {
            if (s.sorted[j + 1].z <= s.sorted[j].z) break;
            std::swap(s.sorted[j], s.sorted[j + 1]);
        }
}

// ---- states (0x005898A8) --------------------------------------------------------------------------------------------
// 0: 0x00426C50 the boss on the floor bridge (camera 0xDF): path 0x184 (1311 before tick 0x51E, then the tick), the
// shock 0x00429830 at tick 0x667, rises on event flag 0x31
void State0(World::Impl& w, FinalBossTask& b) {
    if (w.runner->camera.camId == (w.typingReferenceActive ? 0x40F2 : 0xDF)) {
        const int tick = w.runner->camera.tick;
        float p[3];
        int ang[3];
        bool set = true;
        if (tick < 0x51E) {
            st5::EvalPath(w, w.typingReferenceActive ? 0x41A6 : 0x184, Bits(0x44A3C000), p, ang);
            p[1] = static_cast<float>(p[1] + F64(w.exe, 0x0055D170));
        } else if (0x628 < tick) {
            set = false;
            if (tick == 0x667) {
                struct Shock : WTask {
                    int c = 0;
                };
                w.NewTask<Shock>([](Task& t) {  // 0x00429830
                    World::Impl& ww = *CurrentWorld();
                    auto& k = t.As<Shock>();
                    Mat4 m = Mat4::Identity();
                    mx::Translate(m, 0.0f, 0.0f, Bits(0xC109999A));
                    mx::RotX(m, 0xF000);
                    ww.Draw(k.c / 2 + 0x161B, m, 8, -1.0f, false, -1, true);
                    if (++k.c == 0xF) ww.shakeTimer = 0x30;  // 0x009C8E8C
                    if (0x20 < ++k.c) ww.ts.KillCurrent();
                }, 0x00429830);
            }
            if (w.runner->flags[0x31] == 1) {
                b.rot[1] = 0x8A20;
                b.rot[0] = 0;
                b.rot[2] = 0;
                b.pos[1] = static_cast<float>(b.pos[1] - F64(w.exe, 0x0055D168));
                ++b.behaviour;
                BossDraw(w, b);
                return;
            }
        } else {
            int e = static_cast<int>(w.exe.U32(0x00577348));
            if (tick <= e) e = tick;
            st5::EvalPath(w, w.typingReferenceActive ? 0x41A6 : 0x184, static_cast<float>(e), p, ang);
            p[1] = static_cast<float>(p[1] + F64(w.exe, 0x0055D160));
        }
        if (set) {
            b.pos[0] = Bits(0x443C3805);
            b.pos[1] = p[1];
            b.pos[2] = Bits(0xC61A405C);
            b.rot[0] = 0x8000;
            b.rot[1] = 0xA20;
            b.rot[2] = 0;
        }
    }
    BossDraw(w, b);
}

// 1: 0x00426D70 rising on path 0x185 (camera 0xDF) until tick 1810: event flag 0x32, orbs out (+0x136C = 1)
void State1(World::Impl& w, FinalBossTask& b) {
    if (w.runner->camera.camId == (w.typingReferenceActive ? 0x40F2 : 0xDF)) {
        if (b.anim.motion[0] == 0xA1 && b.anim.tick[0] == Len(w, 0xA1) - 1) w.AnimBlendTo(b, 0xA3, 0, 0x14);
        const int tick = w.runner->camera.tick;
        if (tick == static_cast<int>(w.exe.U32(0x005770B4))) {
            w.runner->flags[0x32] = 1;
            ++b.behaviour;
            b.t = 0;
            b.c136C = 1;
        }
        int e = static_cast<int>(w.exe.U32(0x0057734C));
        if (tick <= e) e = tick;
        float p[3];
        int ang[3];
        if (st5::EvalPath(w, w.typingReferenceActive ? 0x41A7 : 0x185, static_cast<float>(e), p, ang)) {
            memcpy(b.pos, p, 12);
            for (int k = 0; k < 3; ++k) b.rot[k] = ang[k];
        }
    }
    BossDraw(w, b);
    ++b.anim.counter[0];
}

// 2: 0x00426E60 the name card time (300 frames; sounds at 0xA0 / 0xF0), then the fight: hp, bar, lock-on, position 0
void State2(World::Impl& w, FinalBossTask& b) {
    const int c = ++b.t;
    if (c == 0xA0) {
        w.Sound(0x1225A9);
    } else if (c == 0xF0) {
        w.Sound(0x25A9);
    } else if (static_cast<int>(w.exe.U32(0x005770BC)) <= c) {
        b.hp = b.record.ParamS16(10);  // (params +10)
        w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
        ++w.attackers;
        ++w.liveEnemies;
        w.SyncLock(b);
        w.lockOn.AddDirect(b.lock);  // 0x00408E80
        w.bossActive = 1;            // 0x009CA0EA
        b.flags34 |= 0x100;
        w.Sound(0x1425A9);
        ++b.behaviour;
        b.sub = 0;
        b.step = 0;
        b.pattern = 1;
        b.hitBy = -1;
        b.rank = w.rank;  // 0x0040A8A0
        b.freeze = 0;
        b.place = 0;
        PlacePos(w, 0, b.pos);
        b.place = 1;
        PlacePos(w, 1, b.tgt);
        b.t = 0;
    }
    BossDraw(w, b);
    ++b.anim.counter[0];
}

// 3: 0x00426FD0 the first fight phase: patterns 1 / 2 (orbs) and 3 (charge)
void State3(World::Impl& w, FinalBossTask& b) {
    St6State& s = State(w);
    const bool hit = BossHits(w, b);
    const int motion = b.anim.motion[0], tick = b.anim.tick[0];
    const int16_t lowHp = b.record.ParamS16(0xC);
    enum { kTail, kNewPattern, kNextState } go = kTail;
    switch (b.sub) {
    case 0:
        LerpTo(b.pos, b.tgt, 0x28 - b.t);
        if (0x28 < b.t++) {
            memcpy(b.pos, b.tgt, 12);
            b.prevPlace = b.place;
            b.t = 0;
            if (4 < ++b.place) b.place = 0;
            b.sub = static_cast<int16_t>(b.pattern);
            b.step = 0;
            w.AnimBlendTo(b, 0xA4, 0, 2);
        }
        break;
    case 1:
        if (b.step == 0) {
            if (motion == 0xA4) {
                if (tick == Len(w, 0xA4) - 1) {
                    SetRadii(w, b, false);
                    b.flags34 &= ~0x100u;
                    w.AnimBlendTo(b, 0xA5, 0, 2);
                }
                break;
            }
            if (motion != 0xA5) break;
            if (tick == 0x37) {
                b.c136C = 2;
                w.Sound(0x125A9);
                if (w.typingReferenceActive) w.TypingWords(0,350); // Source413B36.
            }
            if (tick != Len(w, 0xA5) - 1) break;
            SortOrbs(s);
            const int n = w.playersInGame != 2 ? 4 : 6;
            for (int k = 0; k < n; ++k) {
                const int o = s.sorted[k].idx & 7;
                s.orbs[o].active = 1;
                s.orbs[o].order = static_cast<int8_t>(k);
            }
            b.c136C = 3;
            w.Sound(0x25A9);
            b.f1334 = 0;
            w.AnimBlendTo(b, 0x97, 0, 2);
            ++b.step;
        } else if (b.step == 1) {
            if (motion == 0x97) {
                if (hit && !(b.flags34 & 0x40000000)) {
                    b.flags34 |= 0x40000000;
                    w.AnimBlendTo(b, 0xB0, 0, 5);
                    w.Sound(0x2225A9);
                }
                if (OrbsIdle(s) && !(b.flags34 & 0x40000000)) {
                    b.c136C = 1;
                    SetRadii(w, b, true);
                    b.flags34 |= 0x100;
                    w.AnimBlendTo(b, 0xA6, 0, 2);
                    ++b.step;
                }
            } else if (motion == 0xB0 && tick == Len(w, 0xB0) - 1) {
                b.flags34 &= ~0x40000000u;
                w.AnimBlendTo(b, 0x97, 0, 10);
            }
        } else if (b.step == 2) {
            if (motion == 0xA6 && tick == 0x26) {
                b.f1334 = 0;
                w.AnimBlendTo(b, 0xA3, 0, 0x14);
            }
            if (b.anim.motion[0] != 0xA3 || b.f1334++ != 0x14) break;
            go = b.hp <= lowHp ? kNextState : kNewPattern;
        }
        break;
    case 2:
        if (b.step == 0) {
            if (motion == 0xA4) {
                if (tick == Len(w, 0xA4) - 1) w.AnimBlendTo(b, 0xA7, 0, 2);
            } else if (motion == 0xA7) {
                if (tick == 0x1E) {
                    SetRadii(w, b, false);
                    b.c136C = 2;
                    b.flags34 &= ~0x100u;
                    w.Sound(0x125A9);
                    w.Sound(0x1225A9);
                    if (w.typingReferenceActive) {
                        w.TypingWords(0,150); // Source413F0D: the orb-word window.
                        b.flags34 |= 0x100u;
                    }
                }
                if (tick == Len(w, 0xA7) - 1) {
                    b.c136C = 3;
                    b.f1334 = 0;
                    w.AnimBlendTo(b, 0x99, 0, 2);
                    ++b.step;
                }
            }
        } else if (b.step == 1) {
            if (motion != 0x99) {
                if (motion == 0xB0 && tick == Len(w, 0xB0) - 1) {
                    b.flags34 &= ~0x40000000u;
                    w.AnimBlendTo(b, 0x99, 0, 10);
                }
                break;
            }
            if (hit && !(b.flags34 & 0x40000000)) {
                b.flags34 |= 0x40000000;
                w.AnimBlendTo(b, 0xB0, 0, 5);
                w.Sound(0x2225A9);
            }
            if (!OrbsIdle(s) || (b.flags34 & 0x40000000)) break;
            b.c136C = 1;
            w.Sound(0x25A9);
            SetRadii(w, b, true);
            b.flags34 |= 0x100;
            w.AnimBlendTo(b, 0xA8, 0, 2);
            ++b.step;
        } else if (b.step == 2) {
            if (motion == 0xA8 && tick == Len(w, 0xA8) - 1) {
                b.f1334 = 0;
                w.AnimBlendTo(b, 0xA3, 0, 0x14);
            }
            if (b.anim.motion[0] != 0xA3 || b.f1334++ != 0x14) break;
            go = b.hp <= lowHp ? kNextState : kNewPattern;
        }
        break;
    case 3: {
        auto toHold = [&] {  // LAB_0042790D
            b.t = 0;
            b.flags34 |= 0x100;
            w.AnimBlendTo(b, 0xB0, 0, 0xF);
            w.Sound(0x2225A9);
            w.Sound(0x1C25A9);
            b.freeze = 0;
            b.step = 5;
        };
        const int limit = w.typingReferenceActive ? int(TypingEmperorChargeHitQuota(unsigned(w.players))) :
            w.exe.S16(0x0055CF9A + static_cast<uint32_t>(w.players) * 2);
        switch (b.step) {
        case 0:
            // Source414042/4140D4/41411C reposts this selection throughout
            // the preparation pose, then leaves it stable during the lunge.
            if (w.typingReferenceActive) w.TypingWords(0,850);
            if (motion == 0xA4) {
                if (tick == Len(w, 0xA4) - 1) {
                    SetRadii(w, b, false);
                    b.flags34 &= ~0x100u;
                    w.AnimBlendTo(b, 0xAA, 0, 2);
                }
                break;
            }
            if (motion != 0xAA) break;
            if (tick == 0xB) {
                b.c136C = 2;
                w.Sound(0x125A9);
                w.Sound(0x1225A9);
            }
            if (b.c136C == 2 && b.mode1360 == 0) {
                bool any = false;
                for (const auto& o : s.orbs)
                    if (o.active == 1) any = true;
                if (any) {
                    b.mode1360 = 1;
                    b.c1338 = 0;
                }
            }
            if (tick == 0x37) {
                w.Sound(0x1625A9);
                break;
            }
            if (tick != Len(w, 0xAA) - 1) break;
            memcpy(b.tgt, w.playerPos, 12);
            b.t = 0;
            b.f1334 = w.typingReferenceActive ? int(TypingEmperorChargeFrames(b.rank)) :
                w.exe.S16(0x0055CFA0 + static_cast<uint32_t>(b.rank) * 2);
            b.hits = 0;
            w.AnimBlendTo(b, 0x9B, 0, 10);
            w.Sound(0x1B25A9);
            ++b.step;
            break;
        case 1:
            if (hit) ++b.hits;
            if (motion == 0x9B && tick == 0x1E) b.freeze = 1;
            if (b.hits < limit) {
                LerpTo(b.pos, b.tgt, b.f1334 - b.t);
                const float dz = b.pos[2] - b.tgt[2], dy = b.pos[1] - b.tgt[1], dx = b.pos[0] - b.tgt[0];
                if (std::sqrt(dz * dz + dy * dy + dx * dx) <= RF(w, 0x0055CCD4)) {
                    b.freeze = 0;
                    ++b.step;
                }
                if (w.typingReferenceActive) b.typingFlags |= 0x200u; // source41425A: charging word window
                break;
            }
            toHold();
            break;
        case 2:
            if (hit) ++b.hits;
            if (0x29 < tick || b.hits < limit) {
                if (tick != 0x2A) {
                    if (tick == Len(w, b.anim.motion[0]) - 1) {
                        PlacePos(w, b.prevPlace, b.tgt);
                        b.t = 0;
                        b.flags34 |= 0x100;
                        w.AnimBlendTo(b, 0x9A, 0, 2);
                        ++b.step;
                    }
                    break;
                }
                HitPlayers(w, 5);
                RankAdd(b, -3);
                b.flags34 |= 0x100;
                w.Sound(0x1C25A9);
                w.Sound(0x1D23A9);
                break;
            }
            toHold();
            break;
        case 3:
            LerpTo(b.pos, b.tgt, 0x28 - b.t);
            if (b.t++ < 0x29) break;
            memcpy(b.pos, b.tgt, 12);
            b.t = 0;
            b.c136C = 1;
            w.Sound(0x25A9);
            b.mode1360 = 2;
            SetRadii(w, b, true);
            w.AnimBlendTo(b, 0xA9, 0, 2);
            ++b.step;
            break;
        case 4:
            if (motion == 0xA9 && tick == 0xF) {
                b.f1334 = 0;
                w.AnimBlendTo(b, 0xA3, 0, 0x14);
            }
            if (b.anim.motion[0] != 0xA3 || b.f1334++ != 0x14) break;
            if (b.hp <= lowHp) NextState(w, b, 0x00427BED, 0x00427C01);
            else go = kNewPattern;
            break;
        case 5:
            if (motion == 0xB0 && tick == Len(w, 0xB0) - 1) {
                PlacePos(w, b.prevPlace, b.tgt);
                b.t = 0;
                b.flags34 |= 0x100;
                w.AnimBlendTo(b, 0x9A, 0, 0x14);
                b.step = 3;
            }
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    if (go == kNextState) NextState(w, b, 0x0042742F, 0x00427C01);
    else if (go == kNewPattern) NewPattern(w, b, 0x00427481);
    // Source413E97 exposes the second orb pattern only while its chest is
    // vulnerable; the default400 bit keeps the other owner windows hidden.
    if (w.typingReferenceActive && b.sub == 2 && b.step == 1 && !(b.flags34 & 0x100u))
        b.typingFlags |= 0x200u;
    BossTail(w, b, false);
    if (b.freeze == 0) ++b.anim.counter[0];
    b.flags34 &= ~0xEu;
    ViewPosR(w, b, 5.0f);
}

// 4: 0x00427D40 the second phase: a summoned creature (pattern 0..4), the orb ring attack
void State4(World::Impl& w, FinalBossTask& b) {
    St6State& s = State(w);
    const bool hit = BossHits(w, b);
    const int motion = b.anim.motion[0], tick = b.anim.tick[0];
    if (b.sub == 0) {
        LerpTo(b.pos, b.tgt, 0x28 - b.t);
        if (0x28 < b.t++) {
            memcpy(b.pos, b.tgt, 12);
            b.prevPlace = b.place;
            b.t = 0;
            if (4 < ++b.place) b.place = 0;
            s.f2448 = 1;
            b.sub = 4;
            b.step = 0;
            w.AnimBlendTo(b, 0xA4, 0, 2);
            float pp[3];
            PlacePos(w, b.prevPlace, pp);
            int yaw = 0, pitch = 0;
            FaceFrom(w.playerPos, pp, &yaw, &pitch);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, w.playerPos[0], w.playerPos[1], w.playerPos[2]);
            mx::RotY(m, yaw);
            mx::RotX(m, pitch);
            const uint32_t e = 0x0055D120 + static_cast<uint32_t>(b.pattern) * 12;
            const float l[3] = { RF(w, e), RF(w, e + 4), RF(w, e + 8) };
            float at[3];
            mx::Transform(m, l, at);
            if (b.pattern >= 0 && b.pattern <= 4) {
                // first runs: 0x0042C0B0 (0) / 0x0042C830 (1) / 0x0042CD30 (2) / 0x0042D490 (3, 4)
                static const uint32_t kFirst[5] = { 0x0042C0B0, 0x0042C830, 0x0042CD30, 0x0042D490, 0x0042D490 };
                static void (*const kFn[5])(Task&) = { CreatureFirst0, CreatureFirst, CreatureFirst2, Creature3First, Creature3First };
                CreatureTask* c = w.NewTask<CreatureTask>(kFn[b.pattern], kFirst[b.pattern]);
                c->flags34 |= 1;
                if (w.typingReferenceActive) c->typingFlags = 0x30000008u; // source414D69: its own word post
                memcpy(c->pos, at, 12);
                c->boss = &b;
                c->rot[0] = -pitch;
                c->rot[2] = 0;
                c->rot[1] = yaw + 0x8000;
            } else {
                w.Note("final boss creature pattern " + std::to_string(b.pattern) + " (0x00428307 jump table: garbage object) not ported");
            }
        }
    } else if (b.sub == 4) {
        if (b.step == 0) {
            if (motion == 0xA4) {
                if (tick == Len(w, 0xA4) - 1) {
                    SetRadii(w, b, false);
                    b.flags34 &= ~0x100u;
                    w.AnimBlendTo(b, 0xAB, 0, 2);
                    w.Sound(0x1225A9);
                }
            } else if (motion == 0xAB) {
                if (tick == Len(w, 0xAB) - 1) {
                    b.t = 0;
                    b.c136C = 2;
                    for (auto& o : s.orbs) o.active = 1;
                    w.Sound(0x125A9);
                    w.AnimBlendTo(b, 0xAD, 0, 2);
                }
            } else if (motion == 0xAD && OrbsIdle(s)) {
                for (auto& o : s.orbs) o.active = 1;
                b.c136C = 3;
                w.AnimBlendTo(b, 0xAE, 0, 2);
                b.flags34 |= 0x10000;
                ++b.step;
            }
        } else if (b.step == 1) {
            if (motion == 0xAE) {
                if (tick == Len(w, 0xAE) - 1) w.AnimBlendTo(b, 0xAF, 0, 2);
            } else if (motion == 0xAF) {
                if (hit && !(b.flags34 & 0x40000000)) {
                    b.flags34 |= 0x40000000;
                    w.AnimBlendTo(b, 0xB0, 0, 5);
                    w.Sound(0x2225A9);
                }
                if (OrbsIdle(s) && !(b.flags34 & 0x40000000)) {
                    b.flags34 &= ~0x10000u;
                    b.c136C = 1;
                    w.Sound(0x25A9);
                    SetRadii(w, b, true);
                    b.flags34 |= 0x100;
                    w.AnimBlendTo(b, 0xAC, 0, 2);
                    ++b.step;
                }
            } else if (motion == 0xB0 && tick == Len(w, 0xB0) - 1) {
                b.flags34 &= ~0x40000000u;
                w.AnimBlendTo(b, 0xAF, 0, 10);
            }
        } else if (b.step == 2) {
            if (motion == 0xAC && tick == Len(w, 0xAC) - 1) {
                b.f1334 = 0;
                w.AnimBlendTo(b, 0xA3, 0, 0x14);
            }
            if (b.anim.motion[0] == 0xA3 && b.f1334++ == 0x14) {
                if (b.record.ParamS16(0xE) < b.hp) {
                    PlacePos(w, b.place, b.tgt);
                    b.sub = 0;
                    b.step = 0;
                    b.f1358 = 0;
                    b.f135C = 0;
                    const int r = w.Rand(0x00427E7F);
                    b.pattern = static_cast<int>(w.exe.U32(0x0055CFC0 + static_cast<uint32_t>(r % 10 + b.pattern * 10) * 4));
                    w.Sound(0x1425A9);
                } else {
                    b.flags34 |= 0x100;
                    w.Sound(0x1425A9);
                    b.place = 0;
                    b.behaviour = 5;
                    b.step = 0;
                    b.sub = 5;
                }
            }
        }
    }
    BossTail(w, b, false);
    ++b.anim.counter[0];
    b.flags34 &= ~0xEu;
    ViewPosR(w, b, 5.0f);
}

void BossDeath(World::Impl& w, FinalBossTask& b) {  // state 5, hp < 1
    w.bossActive = 0;
    w.flags5900 &= ~2u;
    int p = b.hitBy;
    if (p == 2) p = w.Rand(0x0042844F) & 1;
    w.AddScore(p, 0x9C4);
    if (w.cfg.portTyping) {
        w.TypingController(std::clamp(p,0,1)).addBasePoints(1500);
        w.typingQuestion.clear();
        w.typingQuestionSeconds = 0;
        b.typingDialogueActive = false;
        ++b.typingDialogueToken;
    }
    if (w.runner->cfg.gameType == 1) ItemsFinalBoss(w);  // the item reward (world_modes_items.cpp)
    BossRecords(w);  // 0x00425F40 (game type 3 only)
    b.flags34 |= 0x100;
    b.behaviour = 6;
    b.sub = 0;
    b.step = 0;
    b.pattern = 0;
    w.AnimSetMotion(b, 0x9F);
    b.c136C = 3;
    w.Sound(0x325A9);
    b.f1358 = 0;
    b.f135C = 0;
    BossDraw(w, b);
    ++b.anim.counter[0];
}

// 5: 0x00428400 the last phase: the boss circles on paths 0x188.. with the orbs (0x0055D060 rows), charge at the end
void State5(World::Impl& w, FinalBossTask& b) {
    const bool hit = BossHits(w, b);
    if (b.hp < 1) {
        BossDeath(w, b);
        return;
    }
    const uint32_t row = 0x0055D060 + static_cast<uint32_t>(b.place) * 0x18;
    switch (b.step) {
    case 0:
        b.f1374 = Bits(0x3FCCCCCD);
        PathTarget(w, b, b.place + 0x188, 60.0f);
        b.t = 0;
        b.f1340 = 60.0f;
        ++b.step;
        [[fallthrough]];
    case 1:
        LerpTo(b.pos, b.tgt, 0x28 - b.t);
        if (b.t++ < 0x29) break;
        memcpy(b.pos, b.tgt, 12);
        b.t = 0;
        w.AnimBlendTo(b, 0xA4, 0, 2);
        ++b.step;
        break;
    case 2: {
        const int motion = b.anim.motion[0], tick = b.anim.tick[0];
        if (motion == 0x9D) {
            if (tick == Len(w, 0x9D) - 1) {
                b.t = 0;
                b.c1364 = 1;
                w.AnimBlendTo(b, 0x9E, 0, 2);
                w.Sound(0x1925A9);
            }
            break;
        }
        if (motion != 0x9E) {
            if (motion == 0xA4 && tick == Len(w, 0xA4) - 1) {
                b.c136C = 2;
                w.Sound(0x125A9);
                w.Sound(0x225A9);
                w.AnimBlendTo(b, 0x9D, 0, 2);
            }
            break;
        }
        const int c = b.t;
        b.alpha1370 = static_cast<float>(b.alpha1370 - F64(w.exe, 0x0055D180));
        b.t = c + 1;
        if (c < 0x3C) break;
        b.alpha1370 = 0.0f;
        b.hits = 0;
        b.flags34 &= ~0x100u;
        b.freeze = 0;
        for (int i = 0; i < 16; ++i)
            if (i != 1) b.anim.nodes[i].radius = 0.0f;  // +0x284 + i * 0x90: node radius
        w.AnimBlendTo(b, 0xA2, 0, 2);
        ++b.step;
        break;
    }
    case 3: {
        if (hit) ++b.hits;
        PathTarget(w, b, b.place + 0x188, b.f1340);
        memcpy(b.pos, b.tgt, 12);
        const float f = (w.typingReferenceActive ? TypingEmperorFinalPathStep(b.place) : RF(w,row)) + b.f1340;
        b.f1340 = f;
        if (RF(w, row + 0xC) - RF(w, 0x0055D178) <= f) b.f1358 = 1;
        if (b.freeze != 0 || b.f1340 < RF(w, row + 0xC)) break;
        const int quota=w.typingReferenceActive ? int(TypingEmperorFinalHitQuota()) :
            w.exe.S16(0x0055D070 + static_cast<uint32_t>(w.players + b.place * 0xC) * 2);
        if (b.hits < quota) {
            b.freeze = 0;
            ++b.step;
        } else {
            b.f1334 = w.exe.S16(row + 0x10);
            if (7 < ++b.place) b.place = 0;
            b.f1340 = 0.0f;
            PathTarget(w, b, b.place + 0x188, 0.0f);
            w.Sound(0x2625A9);
            b.f1358 = 0;
            b.step += 2;
        }
        break;
    }
    case 4: {
        PathTarget(w, b, b.place + 0x188, b.f1340);
        memcpy(b.pos, b.tgt, 12);
        const float f = (w.typingReferenceActive ? TypingEmperorFinalPathStep(b.place) : RF(w,row)) + b.f1340;
        b.f1340 = f;
        if (b.freeze == 0 && RF(w, row + 8) <= f) {
            b.freeze = 1;
            HitPlayers(w, 7);
            RankAdd(b, -3);
            // Source415373 replaces the unanswered dialogue on a missed orbit.
            if (w.typingReferenceActive) b.typingDialogueActive=false;
            b.f1358 = 0;
            b.f135C = 1;
            w.Sound(0x2725A9);
            break;
        }
        if (f <= RF(w, row + 4)) break;
        if (7 < ++b.place) b.place = 0;
        b.f1340 = 0.0f;
        b.hits = 0;
        b.freeze = 0;
        b.f1358 = 0;
        b.f135C = 0;
        --b.step;
        break;
    }
    case 5:
        LerpTo(b.pos, b.tgt, b.f1334 - b.t);
        if (b.t++ <= b.f1334) break;
        memcpy(b.pos, b.tgt, 12);
        b.f1340 = 0.0f;
        b.hits = 0;
        b.freeze = 0;
        b.f1358 = 0;
        b.step -= 2;
        break;
    default:
        break;
    }
    BossTail(w, b, true);
    ++b.anim.counter[0];
    b.flags34 &= ~0xEu;
    ViewPosR(w, b, 5.0f);
}

void TaskBlast(Task& t);

// 6: 0x00428AE0 down: the camera 0xE2 path 0x186, the explosion 0x004298C0 at tick 200, removed 30 frames later
void State6(World::Impl& w, FinalBossTask& b) {
    if (b.alpha1370 < 1.0f) b.alpha1370 = static_cast<float>(b.alpha1370 + F64(w.exe, 0x0055D188));
    switch (b.sub) {
    case 0:
        --w.liveEnemies;
        --w.attackers;
        w.bossBar = 0.0f;
        w.runner->camera.settled = true;    // 0x009C6F2D
        w.runner->camera.returning = false;  // 0x009C6F2C
        if (b.slot120 != -1) {
            w.lockOn.Remove(b.lock);  // 0x004092B0
            b.slot120 = -1;
        }
        if (b.actorSlot != -1) st5::ReleaseSlot(w, b);  // 0x004092D0
        ++b.sub;
        b.alpha1370 = 0.0f;
        b.pattern = 0;
        [[fallthrough]];
    case 1:
        if (w.runner->camera.camId != (w.typingReferenceActive ? 0x40F5 : 0xE2)) break;
        ++b.sub;
        [[fallthrough]];
    case 2: {
        const int tick = w.runner->camera.tick;
        if (tick == 100) w.AnimBlendTo(b, 0xA1, 0xC, 10);
        if (b.anim.motion[0] == 0xA1 && b.anim.tick[0] == 0x20) b.pattern = 1;
        if (tick < 0xC9) {
            float p[3];
            int ang[3];
            if (st5::EvalPath(w, w.typingReferenceActive ? 0x41A8 : 0x186, static_cast<float>(tick), p, ang)) {
                memcpy(b.pos, p, 12);
                for (int k = 0; k < 3; ++k) b.rot[k] = ang[k];
            }
            if (tick == 200) {
                BlastTask* k = w.NewTask<BlastTask>(TaskBlast, 0x004298C0);
                memcpy(k->pos, b.pos, 12);
                ++b.sub;
                b.t = 0;
            }
        }
        break;
    }
    case 3:
        if (0x1D < ++b.t) {
            b.c136C = 4;
            State(w).bossGhost = std::make_shared<FinalBossTask>(b);  // (freed memory stays readable)
            State(w).bossGhostOf = &b;
            w.EnemyDestroy(b);  // 0x00409CC0
            return;
        }
        break;
    default:
        break;
    }
    BossDraw(w, b);
    if (b.pattern == 0) ++b.anim.counter[0];
}

// 0x00429C90: the v of every inline vertex of the blast streak model 0x16B6 (model slot 0x009BD204; it runs while the
// slot flag 0x009BD20C & 0x8000 is set: assumed, like the other slots' 0x8000 of loaded models) -= double 0x0055D1B0
// (24-bit: the difference rounded to float), stored with bit 0 set. The loaded model keeps the edit (also for a later
// blast) until a stage start reloads it (World::waterUv / modelEdits are cleared there).
void BlastUvScroll(World::Impl& w) {
    const int id = 0x16B6;
    const auto& rec = w.InlineRecords(id);
    if (rec.empty()) return;
    std::vector<std::array<float, 2>>& uv = w.waterUv[id];
    if (uv.size() != rec.size()) {
        uv.resize(rec.size());
        for (size_t i = 0; i < rec.size(); ++i) memcpy(uv[i].data(), &rec[i][6], 8);
    }
    const double step = F64(w.exe, 0x0055D1B0);
    auto edit = std::make_shared<ModelEdit>();
    for (size_t i = 0; i < uv.size(); ++i) {
        float v = static_cast<float>(static_cast<double>(uv[i][1]) - step);
        uint32_t b;
        memcpy(&b, &v, 4);
        b |= 1;
        memcpy(&uv[i][1], &b, 4);
        ModelEdit::Uv u;
        u.index = static_cast<int>(i);
        u.uv[0] = uv[i][0];
        u.uv[1] = uv[i][1];
        edit->uvs.push_back(u);
    }
    w.modelEdits[id] = std::move(edit);
}

// 0x004298C0: the explosion (camera curves 0x191 / 0x190 for the shell scales), 0.4 per frame up to 0x60
void TaskBlast(Task& t) {
    World::Impl& w = *CurrentWorld();
    BlastTask& k = t.As<BlastTask>();
    float alpha = 1.0f;
    if (0x50 <= k.c1320) alpha = 1.0f - (k.f1340 - RF(w, 0x004E1FC4)) * RF(w, 0x0055D1A8);
    float p191[3] = {}, t191[3] = {}, p190[3] = {}, t190[3] = {};
    int roll = 0;
    w.cameraBank.Evaluate(w.typingReferenceActive ? 0x41B3 : 0x191, k.f1340, p191, t191, &roll, false);
    w.cameraBank.Evaluate(w.typingReferenceActive ? 0x41B2 : 0x190, k.f1340, p190, t190, &roll, false);
    if (k.s1324 == 0 && k.c1320 == 0x1A) {
        k.s1324 = 1;
        w.Sound(0xD25A9);
        w.Sound(0x1825A9);
    } else if (k.s1328 == 0 && k.c1320 == 5) {
        k.s1328 = 1;
        w.Sound(0x1725A9);
    }
    // the whole blast in a white light from the camera: 0x0041DC10(1.0, camera pitch / yaw, 1, 1, 1) .. 0x0041DCC0
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);
    struct Restore { World::Impl& w; ~Restore() { w.LightScene(); } } restore{ w };
    Mat4 base = w.cam.view;  // the model-view top: 0x004A9F70 makes it a billboard (drawn in view space)
    mx::Translate(base, k.pos[0], k.pos[1], k.pos[2]);
    mx::ClearRotation(base);  // 0x004A9F70
    mx::Scale(base, 3.0f, 3.0f, 3.0f);
    if (0x1A < k.c1320) {
        BlastUvScroll(w);  // 0x00429C90
        Mat4 m = base;
        mx::Scale(m, t190[0], t190[1], t190[2]);
        w.Draw(0x16B6, m, 8, alpha, false, -1, true);
    }
    if (k.c1320 < 0x19) {
        Mat4 m = base;
        mx::Scale(m, t191[0], t191[1], t191[2]);
        w.Draw(0xB00, m, 8, -1.0f, false, -1, true);
    }
    if (0x1A < k.c1320) {
        Mat4 m = base;
        mx::Scale(m, p191[0], p191[1], p191[2]);
        w.Draw(0xB00, m, 8, alpha, false, -1, true);
        Mat4 q = base;
        mx::ClearRotation(q);  // 0x004A9F70 again: the shell scale 3 is dropped
        mx::RotZ(q, 0x8000);
        mx::Scale(q, 30.0f, 30.0f, 30.0f);
        w.Draw(0x19B3 - w.frameCounter5C50 % 0x18, q, 8, alpha, false, -1, true);
    }
    if (0x19 < k.c1320) {
        if (k.c1320 == 0x1A) {
            w.Draw(0x18C4, base, 8, -1.0f, false, -1, true);
        } else {
            const float f = static_cast<float>(k.c1320 - 0x1B) * RF(w, 0x0055D1A4) + 1.0f;
            Mat4 m = base;
            mx::Scale(m, f, f, f);
            w.Draw(0x18C5, m, 8, alpha, false, -1, true);
        }
        w.Draw(k.c1320 + 0x190D, base, 8, alpha, false, -1, true);
    }
    k.f1340 = k.f1340 + RF(w, 0x0055D1A0);
    k.c1320 = static_cast<int>(k.f1340);
    if (0x60 < k.c1320) w.ts.KillCurrent();
}

void TaskEmperorAnswer(Task& t) {
    World::Impl& w = *CurrentWorld();
    auto& answer = t.As<EmperorAnswerTask>();
    if (!Alive(w, answer.boss, answer.bossSerial) || answer.boss->behaviour > 5 || answer.boss->hp < 1) {
        w.ts.KillCurrent();
        return;
    }
    memcpy(answer.viewPos, answer.boss->viewPos, 12);
}

void BeginEmperorDialogue(World::Impl& w, FinalBossTask& b, uint32_t serial) {
    int bankCount = 0;
    for (const auto& bank : TypingWordBanks()) if (bank.group == 8) ++bankCount;
    if (!bankCount) return;
    const auto* bank = FindTypingWordBank(8, 1 + w.Rand(0x00415604) % bankCount);
    if (!bank) return;
    const auto words = TypingWords(*bank);
    if (words.size() < 4) return;
    std::copy_n(words.begin() + 1, b.typingAnswers.size(), b.typingAnswers.begin());
    ++b.typingDialogueToken;
    b.typingRevealFrames = 120;
    b.typingDialogueActive = true;
    w.typingQuestion = words.front().text;
    w.typingQuestionSeconds = 0; // Dialogue has no independent quiz deadline.
    w.typingHud.questionSlide = -960;
    w.typingHud.questionVisible = false;
    if (!b.typingAnswerActors) {
        b.typingAnswerActors = true;
        for (unsigned choice = 0; choice < b.typingAnswers.size(); ++choice) {
            auto* answer = w.NewTask<EmperorAnswerTask>(TaskEmperorAnswer, 0x00415730);
            answer->boss = &b;
            answer->bossSerial = serial;
            answer->choice = choice;
            answer->typingFlags = 0x34100008u;
        }
    }
}

void TickEmperorDialogue(World::Impl& w, FinalBossTask& b, uint32_t serial) {
    if (b.behaviour != 5 || b.step < 3 || b.hp < 1 || w.paused ||
        w.runner->camera.modeCur != 2 || (w.player[0].state != 5 && w.player[1].state != 5)) return;
    if (!b.typingDialogueActive) BeginEmperorDialogue(w, b, serial);
    else if (b.typingRevealFrames) {
        --b.typingRevealFrames;
        w.typingHud.questionSlide = -8 * int(b.typingRevealFrames);
        w.typingHud.questionVisible = true;
    }
}

void TaskBoss(Task& t) {  // 0x00426C30
    World::Impl& w = *CurrentWorld();
    FinalBossTask& b = t.As<FinalBossTask>();
    if (w.typingReferenceActive) b.typingFlags &= ~0x200u; // source41347D resets the per-frame bypass
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);
    switch (b.behaviour) {
    case 0: State0(w, b); break;
    case 1: State1(w, b); break;
    case 2: State2(w, b); break;
    case 3: State3(w, b); break;
    case 4: State4(w, b); break;
    case 5: State5(w, b); break;
    case 6: State6(w, b); break;
    default: break;
    }
    if (w.cfg.portTyping) TickEmperorDialogue(w, b, t.serial);
}

// ---- orbs (0x00589908) ----------------------------------------------------------------------------------------------
FinalBossTask* OrbBoss(World::Impl& w, OrbTask& o) {
    if (o.boss && Alive(w, o.boss, o.bossSerial)) return o.boss;
    St6State& s = State(w);
    if (o.boss && s.bossGhostOf == o.boss && s.bossGhost) return static_cast<FinalBossTask*>(s.bossGhost.get());
    if (o.boss) w.Note("final boss orb: boss freed (its block is read)");
    return nullptr;
}

// 0x0042BCD0(z): the orb's place on the ring around the boss's node-1 point; returns the ring angle
int OrbPlace(World::Impl& w, OrbTask& o, FinalBossTask& b, float z) {
    Mat4 m = w.cam.inverse;  // 0x004A9230(0x009A6040[ctx])
    mx::MulLeft(m, b.mv1);   // 0x004A92A0(+0x2C4)
    mx::Translate(m, Bits(0x4013F972), Bits(0x3DE0AA65), 0.0f);
    float c[3];
    mx::GetTranslation(m, c);
    Mat4 q = Mat4::Identity();
    mx::Translate(q, c[0], c[1], c[2]);
    mx::RotY(q, b.rot[1]);
    mx::RotX(q, b.rot[0]);
    const int a = (o.idx * 16 + o.spin) * 0x200;
    mx::RotZ(q, a);
    const int ry = static_cast<int>(static_cast<float>(static_cast<float>(a - 0x4000) * RF(w, 0x0055D228)) + RF(w, 0x0055D22C));
    mx::RotY(q, ry);
    const float l[3] = { 0.0f, 0.0f, z };
    mx::Transform(q, l, o.tgt);
    return ry;
}

void OrbDraw(World::Impl& w, OrbTask& o, int model) {  // 0x0042BC10 (0x72C) / 0x754
    Mat4 m = w.cam.view;  // the matrix-stack top (model-view chain)
    mx::Translate(m, o.pos[0], o.pos[1], o.pos[2]);
    mx::RotY(m, static_cast<int>(w.seedCounter << 8));
    mx::Scale(m, o.scale, o.scale, o.scale);
    // renderer state: 0x0041DC10(1.0, active context pitch / yaw, white) ... 0x0041DCC0, the env-map uv pass 0x00418660
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);
    w.envMapDraw = true;
    w.Draw(model, m, 8, -1.0f, false, -1, true);
    w.envMapDraw = false;
    w.LightScene();
}

void OrbRegister(World::Impl& w, OrbTask& o) {  // 0x0042BB80
    o.flags34 &= ~0xEu;
    mx::Transform(w.cam.view, o.pos, o.viewPos);
    w.RegisterTarget(&o);
}

// Spark 0x0042BE90 (0x0042BE50): models 0x17B0.. at the orb's view position, 5 frames
void OrbSpark(World::Impl& w, OrbTask& o) {
    struct Spark : WTask {
        float p[3] = {};
        int n = 0;
    };
    Spark* k = w.NewTask<Spark>([](Task& t) {
        World::Impl& ww = *CurrentWorld();
        auto& s = t.As<Spark>();
        Mat4 m = Mat4::Identity();
        mx::Translate(m, s.p[0], s.p[1], s.p[2]);
        ww.Draw(s.n + 0x17B0, m, 8, -1.0f, false, -1, true);
        if (3 < s.n++) ww.ts.KillCurrent();
    }, 0x0042BE90);
    k->p[0] = o.viewPos[0];
    k->p[1] = o.viewPos[1];
    k->p[2] = static_cast<float>(o.viewPos[2] + F64(w.exe, 0x004ECB70));
    k->n = 0;
}

// Trail 0x0042BF30 (0x0042BEE0): 20 past positions, four drawn (1, 4, 10, 18) fading; ends when the orb's +0x1368 is 1
struct TrailTask : WTask {
    float hist[20][3] = {};
    float scale = 0;  // +0x128
    OrbTask* orb = nullptr;
    uint32_t orbSerial = 0;
    int endSeen = 0;
};

void TaskTrail(Task& t) {
    World::Impl& w = *CurrentWorld();
    TrailTask& k = t.As<TrailTask>();
    const bool alive = Alive(w, k.orb, k.orbSerial);
    // an orb is only removed with +0x1368 = 1 (state 8): its freed block still says so
    if (!alive || k.orb->trailEnd == 1) {
        w.ts.KillCurrent();
        return;
    }
    // renderer state: 0x0041DC10(1.0, active context pitch / yaw, white) ... 0x0041DCC0 around the loop, 0x00418660
    // (env-map uv) before each draw
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);
    for (int i = 0x13; i >= 0; --i) {
        if (i == 0) {
            if (alive) memcpy(k.hist[0], k.orb->pos, 12);
            continue;
        }
        if (i == 1 || i == 4 || i == 10 || i == 0x12) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, k.hist[i][0], k.hist[i][1], k.hist[i][2]);
            mx::RotY(m, static_cast<int>(w.seedCounter << 8));
            mx::Scale(m, k.scale, k.scale, k.scale);
            // a negative factor (the oldest copy) leaves alpha below 0: nothing passes the alpha test; the port's -1
            // means "not the alpha path", so it is clamped to 0
            const float a = RF(w, 0x004C43A8) - static_cast<float>(i) * RF(w, 0x0055D230);
            w.envMapDraw = true;
            w.Draw(0x72B, m, 8, a < 0.0f ? 0.0f : a);
            w.envMapDraw = false;
        }
        memcpy(k.hist[i], k.hist[i - 1], 12);
    }
    w.LightScene();
}

void OrbTrail(World::Impl& w, OrbTask& o) {  // 0x0042BEE0
    TrailTask* k = w.NewTask<TrailTask>(TaskTrail, 0x0042BF30);
    memcpy(k->hist[0], o.pos, 12);
    k->scale = o.scale;
    k->orb = &o;
    k->orbSerial = w.CurrentSerial();
    o.trailEnd = 0;
}

bool OrbShot(World::Impl& w, OrbTask& o) {
    if (!(o.flags34 & 8)) return false;
    OrbSpark(w, o);
    w.Sound(0x1116A9);
    return true;
}

// the flying shot toward the screen point (state 3 case 2 / state 4 case 4)
void OrbFly(World::Impl& w, OrbTask& o, float ky, float kx) {
    const int left = o.f1334 - o.t;
    float l[3] = { static_cast<float>(left) * o.tgt[0] * kx, static_cast<float>(left) * o.tgt[1] * ky, o.tgt[2] };
    float d[3];
    mx::Transform(w.cam.inverse, l, d);
    const float v[3] = { d[0] - o.pos[0], d[1] - o.pos[1], d[2] - o.pos[2] };
    const float len = std::sqrt(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]);
    const float n[3] = { v[0] / len, v[1] / len, v[2] / len };
    const float dz = o.pos[2] - d[2], dy = o.pos[1] - d[1], dx = o.pos[0] - d[0];
    const float dist = std::sqrt(dz * dz + dy * dy + dx * dx);
    for (int k = 0; k < 3; ++k) {
        o.vel[k] = (dist / static_cast<float>(left)) * n[k];
        o.pos[k] = o.vel[k] + o.pos[k];
    }
}

// the return to the ring (state 3 case 4 / state 4 case 6)
void OrbReturn(World::Impl& w, OrbTask& o, FinalBossTask& b) {
    OrbPlace(w, o, b, -15.0f);
    float v[3];
    mx::Transform(w.cam.view, o.tgt, v);
    const float f = static_cast<float>(0x1E - o.t);
    const float l[3] = { (f * RF(w, 0x004C43AC) + 1.0f) * v[0], (f * RF(w, 0x0055D218) + 1.0f) * v[1], v[2] };
    float d[3];
    mx::Transform(w.cam.inverse, l, d);
    const float u[3] = { d[0] - o.pos[0], d[1] - o.pos[1], d[2] - o.pos[2] };
    const float len = std::sqrt(u[2] * u[2] + u[1] * u[1] + u[0] * u[0]);
    const float n[3] = { u[0] / len, u[1] / len, u[2] / len };
    const float dz = o.pos[2] - d[2], dy = o.pos[1] - d[1], dx = o.pos[0] - d[0];
    const float dist = std::sqrt(dz * dz + dy * dy + dx * dx);
    for (int k = 0; k < 3; ++k) {
        o.vel[k] = (dist / f) * n[k];
        o.pos[k] = o.vel[k] + o.pos[k];
    }
}

// 0x00415500: hits the player on that side of the screen (view position on screen), -100 points; lives kept >= 1
void OrbHitsPlayer(World::Impl& w, const float v[3], int kind) {
    const float fx = -((w.focal * v[0]) / v[2]), fy = -((w.focal * v[1]) / v[2]);
    if (!(fx <= -304.0f || 304.0f <= fx || fy <= -224.0f || 224.0f <= fy)) {
        const int p = w.players == 2 ? (0.0f <= fx ? 1 : 0) : w.playersInGame;
        w.PlayerTakeHit(p, 1, kind);
        w.AddScore(p, -100);
    }
    if (w.runner->camera.modeCur != 2 && w.runner->camera.modeReq != 2) {
        if (w.player[0].lives < 1) w.player[0].lives = 1;
        if (w.player[1].lives < 1) w.player[1].lives = 1;
    }
}

void OrbState3(World::Impl& w, OrbTask& o, FinalBossTask& b) {  // 0x0042A100
    St6State& s = State(w);
    auto& slot = s.orbs[o.idx & 7];
    const bool shot = OrbShot(w, o);
    ++o.spin;
    switch (o.sub) {
    case 0:
        if (b.c136C != 3) break;
        if (slot.active == 1) {
            o.t = 0;
            o.f1338 = w.exe.S16(0x0055D1B8 + static_cast<uint32_t>(b.rank) * 2);
            ++o.sub;
            break;
        }
        o.sub = 5;
        break;
    case 1:
        if (o.t / o.f1338 == slot.order) {
            const float k = static_cast<float>(F64(w.exe, 0x0055D220));
            o.tgt[0] = (o.viewPos[0] * k) / o.viewPos[2];
            o.tgt[2] = Bits(0xC0200000);
            o.t = 0;
            o.tgt[1] = (o.viewPos[1] * k) / o.viewPos[2];
            o.f1334 = w.exe.S16(0x0055D1D8 + static_cast<uint32_t>(b.rank) * 2);
            OrbTrail(w, o);
            w.Sound(0x1125A9);
            ++o.sub;
        }
        break;
    case 2:
        if (!shot) {
            if (o.t < o.f1334 - 2) {
                OrbFly(w, o, RF(w, 0x0055D218), RF(w, 0x004C43AC));
            } else {
                OrbHitsPlayer(w, o.viewPos, 7);
                RankAdd(b, -3);
                o.t = 0;
                o.trailEnd = 1;
                o.sub = 3;
            }
        } else {
            o.t = 0;
            o.sub = 4;
        }
        break;
    case 3:
        if (o.f1338 / 3 <= o.t) {
            o.t = 0;
            OrbTrail(w, o);
            o.sub = 4;
        }
        break;
    case 4:
        if (o.t < 0x1E) {
            OrbReturn(w, o, b);
            break;
        }
        slot.active = 0;
        o.trailEnd = 1;
        o.sub = 5;
        break;
    case 5:
        OrbPlace(w, o, b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        if (b.c136C == 1) o.behaviour = 2;
        break;
    default:
        break;
    }
    OrbDraw(w, o, 0x72C);
    OrbRegister(w, o);
    ++o.t;
}

void OrbState4(World::Impl& w, OrbTask& o, FinalBossTask& b) {  // 0x0042A6F0 (pairs: even orb i and odd orb i + 1)
    St6State& s = State(w);
    const int i = o.idx & 7;
    auto& me = s.orbs[i];
    const bool odd = (o.idx & 1) != 0;
    const bool shot = OrbShot(w, o);
    ++o.spin;
    auto& prev = s.orbs[(i + 7) & 7];
    auto& next = s.orbs[(i + 1) & 7];
    switch (o.sub) {
    case 0: {
        o.mode = 0;
        OrbPlace(w, o, b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        memcpy(me.pos, o.tgt, 12);
        if (odd) memcpy(o.tgt, prev.pos, 12);
        ++o.sub;
        o.t = 0;
        break;
    }
    case 1:
        if (!odd) {
            if (next.order != 0) {
                me.order = 1;
                ++o.sub;
            }
        } else {
            memcpy(o.tgt, prev.pos, 12);
            LerpTo(o.pos, o.tgt, 10 - o.t);
            if (7 < o.t++) {
                me.order = 1;
                ++o.sub;
            }
        }
        break;
    case 2:
        if (!odd) {
            int pitch = 0, yaw = 0;
            DirToAngles(next.pos[0] - o.pos[0], next.pos[1] - o.pos[1], next.pos[2] - o.pos[2], &pitch, &yaw);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, o.pos[0], o.pos[1], o.pos[2]);
            mx::RotY(m, yaw);
            mx::RotX(m, pitch);
            mx::RotX(m, 0x4000);
            mx::Translate(m, 0.0f, static_cast<float>(0x28 - o.t) * RF(w, 0x0055CBB8), 0.0f);
            const float white[3] = { 1.0f, 1.0f, 1.0f };
            w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);  // 0x0041DC10(1, context pitch / yaw, white)
            w.envMapDraw = true;  // 0x00418660 (env-map uv)
            w.Draw(o.t + 0x72D, m);  // beam
            w.envMapDraw = false;
            w.LightScene();  // 0x0041DCC0
            if (0x28 < o.t++) {
                o.radius = Bits(0x402CCCCD);
                me.active = 1;
                ++o.sub;
            }
            o.mode = 1;
            break;
        }
        if (prev.order != 0) ++o.sub;
        o.mode = 0;
        break;
    case 3:
        if (!odd) {
            if (b.c136C == 3) {
                mx::Transform(w.cam.view, o.pos, o.viewPos);
                const float k = static_cast<float>(F64(w.exe, 0x0055D220));
                o.tgt[0] = (o.viewPos[0] * k) / o.viewPos[2];
                o.tgt[2] = Bits(0xC0200000);
                o.t = 0;
                o.tgt[1] = (o.viewPos[1] * k) / o.viewPos[2];
                o.f1334 = w.exe.S16(0x0055D1F8 + static_cast<uint32_t>(b.rank) * 2);
                w.Sound(0xE25A9);
                ++o.sub;
            }
            o.mode = 2;
        } else {
            if (b.c136C == 3) ++o.sub;
            o.mode = 1;
        }
        break;
    case 4:
        if (!odd) {
            if (shot) {
                o.radius = Bits(0x3FE66666);
                me.order = 0;
                o.t = 0;
                o.sub = 6;
            } else if (o.t < o.f1334 - 2) {
                OrbFly(w, o, RF(w, 0x004C4C58), RF(w, 0x0055D218));
            } else {
                OrbHitsPlayer(w, o.viewPos, 7);
                RankAdd(b, -3);
                o.t = 0;
                o.sub = 5;
            }
            o.mode = 2;
            ++o.t;
            break;
        }
        if (prev.order == 0) {
            memcpy(o.pos, prev.pos, 12);
            o.radius = Bits(0x3FE66666);
            o.t = 0;
            o.sub = 6;
        }
        o.mode = 1;
        ++o.t;
        break;
    case 5:
        if (o.f1338 / 3 <= o.t) {
            o.radius = Bits(0x3FE66666);
            o.t = 0;
            me.order = 0;
            o.sub = 6;
        }
        o.mode = 2;
        ++o.t;
        break;
    case 6:
        if (o.t < 0x1E) OrbReturn(w, o, b);
        else {
            me.active = 0;
            o.sub = 7;
        }
        o.mode = 0;
        ++o.t;
        break;
    case 7:
        OrbPlace(w, o, b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        if (b.c136C == 1) o.behaviour = 2;
        o.mode = 0;
        break;
    default:
        break;
    }
    if (o.mode == 0) {
        OrbDraw(w, o, 0x72C);
        OrbRegister(w, o);
    } else if (o.mode == 1) {
        o.flags34 &= ~0xEu;
    } else if (o.mode == 2) {
        OrbDraw(w, o, 0x754);
        OrbRegister(w, o);
    }
}

void OrbState5(World::Impl& w, OrbTask& o, FinalBossTask& b) {  // 0x0042B230: gather at the boss's node 5
    St6State& s = State(w);
    OrbShot(w, o);
    ++o.spin;
    auto node5 = [&] {
        Mat4 m = w.cam.inverse;
        mx::MulLeft(m, b.mv5);
        mx::GetTranslation(m, o.tgt);
    };
    bool hide = false;
    switch (o.sub) {
    case 0:
        OrbPlace(w, o, b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        node5();
        ++o.sub;
        o.t = 0;
        break;
    case 1:
        node5();
        LerpTo(o.pos, o.tgt, 0x28 - o.t);
        if (0x28 < o.t++) {
            memcpy(o.pos, o.tgt, 12);
            s.orbs[o.idx & 7].active = 1;
            ++o.sub;
        }
        break;
    case 2:
        if (b.c136C == 1) {
            o.t = 0;
            ++o.sub;
        }
        hide = true;
        break;
    case 3:
        if (o.t++ / 3 == o.idx) {
            node5();
            memcpy(o.pos, o.tgt, 12);
            ++o.sub;
            o.t = 0;
        }
        hide = true;
        break;
    case 4:
        OrbPlace(w, o, b, -15.0f);
        LerpTo(o.pos, o.tgt, 0x28 - o.t);
        if (0x28 < o.t++) {
            memcpy(o.pos, o.tgt, 12);
            o.behaviour = 2;
        }
        break;
    default:
        break;
    }
    if (hide) {
        o.flags34 &= ~0xEu;
        return;
    }
    OrbDraw(w, o, 0x72C);
    OrbRegister(w, o);
}

void OrbState6(World::Impl& w, OrbTask& o, FinalBossTask& b) {  // 0x0042B650: to the creature's points, back
    St6State& s = State(w);
    auto& slot = s.orbs[o.idx & 7];
    OrbShot(w, o);
    ++o.spin;
    bool hide = false;
    switch (o.sub) {
    case 0:
        OrbPlace(w, o, b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        memcpy(o.tgt, slot.pos, 12);
        ++o.sub;
        o.t = 0;
        break;
    case 1:
        LerpTo(o.pos, o.tgt, 10 - o.t);
        if (10 < o.t++) {
            memcpy(o.pos, o.tgt, 12);
            slot.active = 0;
            ++o.sub;
        }
        break;
    case 2:
        if (b.c136C == 3) ++o.sub;
        break;
    case 3:
        if (s.f2448 == 0) {
            Mat4 m = w.cam.inverse;
            mx::MulLeft(m, b.mv5);
            mx::GetTranslation(m, o.tgt);
            memcpy(o.pos, slot.pos, 12);
            ++o.sub;
            o.t = 0;
        }
        hide = true;
        break;
    case 4:
        OrbPlace(w, o, b, -15.0f);
        LerpTo(o.pos, o.tgt, 0x14 - o.t);
        if (0x14 < o.t++) {
            memcpy(o.pos, o.tgt, 12);
            slot.active = 0;
            ++o.sub;
        }
        break;
    case 5:
        OrbPlace(w, o, b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        if (b.c136C == 1) o.behaviour = 2;
        break;
    default:
        break;
    }
    if (hide) {
        o.flags34 &= ~0xEu;
        return;
    }
    OrbDraw(w, o, 0x72C);
    OrbRegister(w, o);
}

void OrbState7(World::Impl& w, OrbTask& o, FinalBossTask& b) {  // 0x0042B9D0: the ring of the last phase
    OrbShot(w, o);
    if (o.sub == 0) {
        OrbTrail(w, o);
        ++o.sub;
    }
    if (b.c136C == 2) o.spin += 2;
    else if (b.c136C == 3) o.spin += 1;
    else if (b.c136C == 4) {
        o.behaviour = 8;
        o.trailEnd = 1;
    }
    OrbPlace(w, o, b, -15.0f);
    memcpy(o.pos, o.tgt, 12);
    OrbDraw(w, o, 0x72C);
    if (b.f1358 != 0) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, o.viewPos[0], o.viewPos[1], o.viewPos[2]);
        mx::Scale(m, 0.8f, 0.8f, 0.8f);
        w.Draw(w.frameCounter5C50 % 0x18 + 0x199C, m, 8, -1.0f, false, -1, true);
    }
    if (b.f135C != 0) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, o.viewPos[0], o.viewPos[1], o.viewPos[2]);
        mx::Scale(m, Bits(0x3ECCCCCD), 4.0f, Bits(0x3ECCCCCD));
        w.Draw(w.frameCounter5C50 % 0x18 + 0x1633, m, 8, -1.0f, false, -1, true);
    }
    OrbRegister(w, o);
}

void TaskOrb(Task& t) {  // 0x00429D30
    World::Impl& w = *CurrentWorld();
    OrbTask& o = t.As<OrbTask>();
    FinalBossTask* b = OrbBoss(w, o);
    if (!b) {
        if (o.behaviour == 8) {
            o.trailEnd = 1;
            w.EnemyDestroy(o);  // 0x0042BB60
            return;
        }
        w.Note("final boss orb without its boss (state " + std::to_string(o.behaviour) + ")");
        return;
    }
    St6State& s = State(w);
    switch (o.behaviour) {
    case 0:  // 0x00429D50
        if (b->c136C != 0) {
            o.spin = 0;
            o.t = 0;
            o.scale = 0.0f;
            OrbPlace(w, o, *b, -15.0f);
            memcpy(o.pos, o.tgt, 12);
            ++o.behaviour;
        }
        break;
    case 1: {  // 0x00429DB0 appear
        ++o.t;
        mx::Transform(w.cam.view, o.pos, o.viewPos);
        if (9 < o.t && o.t < 0xF0) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, o.viewPos[0], o.viewPos[1], o.viewPos[2]);
            mx::Scale(m, 0.5f, 0.5f, 0.5f);
            w.Draw(0x19BC - o.t % 0x21, m, 8, -1.0f, false, -1, true);
        }
        if (199 < o.t && o.t < 0xE6) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, o.viewPos[0], o.viewPos[1], o.viewPos[2]);
            mx::Scale(m, Bits(0x3ECCCCCD), 4.0f, Bits(0x3ECCCCCD));
            w.Draw(o.t % 0x18 + 0x1633, m, 8, -1.0f, false, -1, true);
        }
        if (199 < o.t) {
            if (o.scale < 1.0f) o.scale = o.scale + RF(w, 0x004C4C88);
            OrbDraw(w, o, 0x72C);
        }
        if (0xEF < o.t) {
            ++o.behaviour;
            o.sub = 0;
        }
        break;
    }
    case 2: {  // 0x00429F70 ring
        auto& slot = s.orbs[o.idx & 7];
        OrbShot(w, o);
        ++o.spin;
        OrbPlace(w, o, *b, -15.0f);
        memcpy(o.pos, o.tgt, 12);
        OrbDraw(w, o, 0x72C);
        OrbRegister(w, o);
        if (b->c136C == 2) {
            const int bs = b->sub;
            if (bs >= 1 && bs <= 3) {
                slot.idx = static_cast<int8_t>(o.idx);
                slot.active = 0;
                slot.order = 0;
                slot.z = o.viewPos[2];
                if (bs == 1) o.t = 0;
                o.behaviour = static_cast<int16_t>(bs + 2);
                o.sub = 0;
            } else if (bs == 4) {
                o.behaviour = 6;
                o.sub = 0;
            } else if (bs == 5) {
                o.behaviour = 7;
                o.t = 0;
                o.sub = 0;
            }
        } else if (b->c136C == 4) {
            o.behaviour = 8;
            o.sub = 0;
        }
        break;
    }
    case 3: OrbState3(w, o, *b); break;
    case 4: OrbState4(w, o, *b); break;
    case 5: OrbState5(w, o, *b); break;
    case 6: OrbState6(w, o, *b); break;
    case 7: OrbState7(w, o, *b); break;
    case 8:  // 0x0042BB60
        o.trailEnd = 1;
        w.EnemyDestroy(o);
        return;
    default:
        break;
    }
}

}  // namespace

// ---- the summoned creature (pattern 1: 0x0042C830 -> 0x0042C8D0) ---------------------------------------------------

namespace {

// 0x0042C6D0 (patterns 0 / 1) and 0x0042D330 (pattern 2: flame on node 0x16, orb table 0x0055D268)
void CreatureNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    CreatureTask& c = static_cast<CreatureTask&>(e);
    if (c.lit == 0) {
        if (idx == c.billboardNode) {
            Mat4 q = c.anim.nodes[idx].mv;  // the node's model-view: 0x004A9F70 makes it a view-space billboard
            mx::ClearRotation(q);  // 0x004A9F70
            mx::RotZ(q, 0x8000);
            mx::Scale(q, 10.0f, 10.0f, 10.0f);
            w.Draw(w.frameCounter5C50 % 0x18 + 0x199C, q, 8, -1.0f, false, idx, true);
        }
    } else {
        w.envMapDraw = true;  // 0x00418660 (env-map uv) then 0x00418560
        w.Draw(c.anim.nodes[idx & 31].model, m, 8, -1.0f, false, idx);
        w.envMapDraw = false;
    }
    if (idx == 1) c.mv1 = NodeMV(w, c, 1);
    if (idx == 24) c.mv24 = NodeMV(w, c, 24);
    const int8_t slot = static_cast<int8_t>(w.exe.U32(c.orbTable + static_cast<uint32_t>(idx)) & 0xFF);
    if (slot != -1 && idx != 0) {
        float p[3];
        Mat4 q = w.cam.inverse;  // 0x004A9230(camera matrix), 0x004A92A0(node model-view): model-view x camera
        mx::MulLeft(q, m);
        mx::GetTranslation(q, p);
        memcpy(State(w).orbs[slot & 7].pos, p, 12);
    }
}

// 0x0042CBA0: shots on node 1 (sub-states 2..4); true when damaged (the boss loses hp)
bool CreatureHits(World::Impl& w, CreatureTask& c, FinalBossTask* b) {
    bool dmg = false;
    if (1 < c.sub && c.sub < 5 && (c.flags34 & 8) && !(c.flags34 & 0x40000000)) {
        for (int p = 0; p < 2; ++p) {
            const int node = c.hitNode[p];
            if (node == 1) {
                Mat4 m = c.mv1;
                mx::Translate(m, 0.0f, 4.0f, 1.0f);
                float v[3];
                mx::GetTranslation(m, v);
                if (c.typingStrike || ShotRayDistance(w.player[p].ray, v[0], v[1], v[2]) <= 3.5f) {
                    if (b) {
                        b->hp = static_cast<int16_t>(b->hp - EmperorDamage(w, *b, c));
                        RankAdd(*b, 1);
                    }
                    w.Sound(0x1117A9);
                    dmg = true;
                } else {
                    w.Sound(0x1116A9);
                }
            } else if (node != 0) {
                w.Sound(0x1116A9);
            }
            c.hitNode[p] = 0;
            const uint32_t bit = 2u << p;
            EnemyNode& n = c.anim.nodes[node & 31];
            n.flags = n.flags & ~bit & ~8u;
            c.flags34 &= ~bit;
        }
    }
    return dmg;
}

void TaskCreature(Task& t) {  // 0x0042C8D0
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    if (c.lock) c.slot120 = static_cast<int8_t>(c.lock->slot);
    FinalBossTask* b = c.boss;  // (the boss outlives it)
    bool inc = false;
    switch (c.sub) {
    case 0:
        if (b->c136C != 3) break;
        c.anim1320 = 0;
        c.lit = 1;
        c.t = 0;
        w.targetTaken[0] = 1;  // 0x009A2BA0
        w.SyncLock(c);
        w.lockOn.AddDirect(c.lock);  // 0x00408E80
        inc = true;
        break;
    case 1:
        if (0x1E < c.t++) {
            c.t = 0;
            c.f1334 = 0;
            c.anim1320 = 1;
            ++c.sub;
        }
        break;
    case 2: {
        if (0x32 < ++c.f1334 && CreatureHits(w, c, b)) {
            c.sub = 5;
            break;
        }
        if (c.anim.tick[0] != 0x23) break;
        const float dz = c.pos[2] - w.playerPos[2], dx = c.pos[0] - w.playerPos[0];
        c.vel[1] = 4.0f;
        c.acc[1] = Bits(0xBD8B60B6);
        const float sp = (std::sqrt(dz * dz + dx * dx) - RF(w, 0x004C43A4)) * RF(w, 0x0055D2A4);
        float s, co;
        const double a = static_cast<double>(c.rot[1]) * mx::kToRad;
        s = static_cast<float>(std::sin(a));
        co = static_cast<float>(std::cos(a));
        c.vel[0] = s * sp;
        c.vel[2] = co * sp;
        w.Sound(0x2A16A9);
        inc = true;
        break;
    }
    case 3:
    case 4: {
        bool step = false;
        if (CreatureHits(w, c, b)) {
            c.sub = 5;
        } else if (c.sub == 3) {
            if (c.anim.tick[0] == 0x41) c.anim1320 = 0;
            if (c.vel[1] < 0.0f) c.anim1320 = 1;
            if (c.anim.tick[0] == 0x55) {
                c.anim1320 = 0;
                step = true;
            }
        } else {
            if (c.pos[1] < w.playerPos[1] + RF(w, 0x0055CAD8)) {
                HitPlayers(w, 6);
                if (b) RankAdd(*b, -3);
                step = true;
            }
        }
        if (step) ++c.sub;
        for (int k = 0; k < 3; ++k) c.pos[k] = c.vel[k] + c.pos[k];
        c.vel[1] = c.acc[1] + c.vel[1];
        break;
    }
    case 5:
        if (b) b->flags34 &= ~0x10000u;
        w.targetTaken[0] = 0;
        State(w).f2448 = 0;
        w.EnemyDestroy(c);  // 0x00409CC0
        return;
    default:
        break;
    }
    if (inc) ++c.sub;
    {
        float from[3] = { c.pos[0], 0.0f, c.pos[2] }, to[3] = { w.playerPos[0], 0.0f, w.playerPos[2] };
        FaceFrom(from, to, &c.rot[1], &c.rot[0]);
    }
    c.anim.attachHidden = c.lit != 1;  // 0x0042D930: attachment entry 0 on with +0x1324
    {
        // 0x0041DC10(1.0, active context pitch / yaw, white) ... 0x0041DCC0 (renderer state)
        const float white[3] = { 1.0f, 1.0f, 1.0f };
        w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);
        w.animLight = 0;
        w.AnimDraw(c);     // 0x00411090 = 0x004110D0
        w.EnemyShadow(c);  // + 0x0040A590
        w.animLight = 1;
        w.LightScene();
    }
    if (c.anim1320 != 0) ++c.anim.counter[0];
    const uint32_t f = c.flags34;
    c.flags34 = f & ~0xEu;
    if (!(f & 0x100)) ViewPosR(w, c, 15.0f);
}

// ---- patterns 3 / 4: 0x0042D490 -> 0x0042D530 (skeleton 0x51): flies at the players' point and strikes ----------

// 0x0042D820: shots on node 2 (sub-states 2 / 3); true when damaged (the boss loses hp)
bool Creature3Hits(World::Impl& w, CreatureTask& c, FinalBossTask* b) {
    bool dmg = false;
    if (c.sub < 2 || 3 < c.sub || !(c.flags34 & 8) || (c.flags34 & 0x40000000)) return false;
    for (int p = 0; p < 2; ++p) {
        const int node = c.hitNode[p];
        if (node == 2) {
            if (b) {
                b->hp = static_cast<int16_t>(b->hp - EmperorDamage(w, *b, c));
                RankAdd(*b, 1);
            }
            w.Sound(0x316A9);
            dmg = true;
        } else if (node != 0) {
            w.Sound(0x1116A9);
        }
        c.hitNode[p] = 0;
        const uint32_t bit = 2u << p;
        EnemyNode& n = c.anim.nodes[node & 31];
        n.flags = n.flags & ~bit & ~8u;
        c.flags34 &= ~bit;
    }
    return dmg;
}

void TaskCreature3(Task& t) {  // 0x0042D530
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    if (c.lock) c.slot120 = static_cast<int8_t>(c.lock->slot);
    FinalBossTask* b = c.boss;
    switch (c.sub) {
    case 0:
        if (b->c136C != 3) break;
        c.anim1320 = 1;
        c.lit = 1;
        c.t = 0;
        w.targetTaken[0] = 1;  // 0x009A2BA0
        w.SyncLock(c);
        w.lockOn.AddDirect(c.lock);  // 0x00408E80
        w.Sound(0x4D17A9);
        ++c.sub;
        break;
    case 1:
        if (c.anim.motion[0] == 0x79 && c.anim.tick[0] == 0xD) c.anim1320 = 0;
        if (0x1E < c.t++) {
            memcpy(c.tgt, w.playerPos, 12);  // 0x009C71E0
            c.t = 0;
            c.f1334 = w.exe.S16(0x0055D284 + static_cast<uint32_t>(b->rank) * 2);
            ++c.sub;
        }
        break;
    case 2: {
        if (Creature3Hits(w, c, b)) {
            c.sub = 4;
            break;
        }
        for (int k = 0; k < 3; ++k) c.pos[k] = Lerp(c.pos[k], c.tgt[k], 1, c.f1334 - c.t);  // 0x00401E60
        const float dz = c.pos[2] - c.tgt[2], dy = c.pos[1] - c.tgt[1], dx = c.pos[0] - c.tgt[0];
        if (!(std::sqrt((dz * dz + dx * dx) + dy * dy) <= RF(w, 0x0055D2AC))) break;
        c.anim1320 = 1;
        ++c.sub;
        break;
    }
    case 3:
        if (c.anim.tick[0] < 0x2C) {
            if (Creature3Hits(w, c, b)) c.sub = 4;
        } else if (c.anim.tick[0] == 0x2C) {
            HitPlayers(w, 8);
            if (b) RankAdd(*b, -3);
            ++c.sub;
        } else if (c.anim.tick[0] == w.MotionTicks(c.anim.motion[0]) - 1) {
            ++c.sub;
        }
        break;
    case 4:
        if (b) b->flags34 &= ~0x10000u;
        w.Sound(0x4E17A9);
        w.targetTaken[0] = 0;
        State(w).f2448 = 0;
        w.EnemyDestroy(c);  // 0x00409CC0
        return;
    default:
        break;
    }
    c.anim.attachHidden = c.lit != 1;  // 0x0042D930
    {
        const float white[3] = { 1.0f, 1.0f, 1.0f };
        w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);
        w.animLight = 0;
        w.AnimDraw(c);     // 0x00411090
        w.EnemyShadow(c);
        w.animLight = 1;
        w.LightScene();
    }
    if (c.anim1320 != 0) ++c.anim.counter[0];
    const uint32_t f = c.flags34;
    c.flags34 = f & ~0xEu;
    if (!(f & 0x100)) ViewPosR(w, c, 15.0f);
}

}  // namespace

void Creature3First(Task& t) {  // 0x0042D490
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    c.actorSlot = -1;
    c.slot120 = -1;
    c.target = 0;  // (+0x121 never written: cleared task memory)
    c.behaviour = 0;
    c.sub = 0;
    c.variant = 0;
    c.anim.kind = 0x51;
    c.anim.motion[0] = 0x79;
    w.EnemyInitPose(c);  // 0x00410440
    c.anim.rotOrder = 5;
    c.nodeFn = CreatureNode;  // 0x0042C6D0
    c.postRootFn = [](World::Impl&, EnemyTask&) {};
    c.anim.drawFlags |= 4;
    c.anim.counter[0] = 0;
    c.radius = RF(w, 0x004C4D28 + 0x51 * 4);
    c.anim1320 = 0;
    c.lit = 0;
    c.lock = &w.lockTargets[w.nextLockTarget++];
    c.fn = 0x0042D530;
    t.fn = TaskCreature3;
    TaskCreature3(t);
}

void CreatureFirst(Task& t) {  // 0x0042C830
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    c.actorSlot = -1;
    c.slot120 = -1;
    c.target = 0;  // +0x121 is never written (cleared task memory): the lock-on table puts it in slots 0 / 1
    c.behaviour = 0;
    c.sub = 0;
    c.variant = 0;
    c.anim.kind = 0x4F;
    c.anim.motion[0] = 0x33;
    w.EnemyInitPose(c);  // 0x00410440 (+ actor slot)
    c.anim.rotOrder = 5;
    c.nodeFn = CreatureNode;
    c.postRootFn = [](World::Impl&, EnemyTask&) {};
    c.anim.drawFlags |= 4;
    c.anim.counter[0] = 0;
    c.radius = RF(w, 0x004C4D28 + 0x4F * 4);
    c.anim1320 = 0;
    c.lit = 0;
    c.lock = &w.lockTargets[w.nextLockTarget++];
    c.fn = 0x0042C8D0;
    t.fn = TaskCreature;
    TaskCreature(t);
}

// ---- creature patterns 0 and 2 (reached in the two-player capture 2p_auto_b: notes/agents/twoplayer2.md) -----------

namespace {

// shared start of 0x0042C0B0 / 0x0042CD30: kind, motion, pose, node callback, draw flags, radius
void CreatureSetup(World::Impl& w, CreatureTask& c, int kind, int motion) {
    c.actorSlot = -1;  // +0x3C
    c.slot120 = -1;
    c.target = 0;      // (+0x121 never written: cleared task memory)
    c.behaviour = 0;
    c.sub = 0;
    c.variant = 0;
    c.anim.kind = static_cast<int16_t>(kind);
    c.anim.motion[0] = motion;
    w.EnemyInitPose(c);  // 0x00410440
    c.anim.rotOrder = 5;
    c.nodeFn = CreatureNode;
    c.postRootFn = [](World::Impl&, EnemyTask&) {};
    c.anim.drawFlags |= 4;
    c.radius = RF(w, 0x004C4D28 + static_cast<uint32_t>(kind) * 4);
    c.anim1320 = 0;
    c.lit = 0;
    c.lock = &w.lockTargets[w.nextLockTarget++];
}

// 0x0042D220: pattern-2 shots on node 0x18 (from tick 0x12; sub-states 2 / 3); true when the boss lost hp
bool CreatureHits2(World::Impl& w, CreatureTask& c, FinalBossTask* b) {
    bool dmg = false;
    if (1 < c.sub && c.sub < 4 && (c.flags34 & 8) && !(c.flags34 & 0x40000000)) {
        for (int p = 0; p < 2; ++p) {
            const int node = c.hitNode[p];
            if (node == 0x18 && 0x11 < c.anim.tick[0]) {
                if (b) {
                    b->hp = static_cast<int16_t>(b->hp - EmperorDamage(w, *b, c));
                    RankAdd(*b, 1);  // 0x00428D60
                }
                w.Sound(0x3E25A9);
                dmg = true;
            } else if (node != 0) {
                w.Sound(0x1116A9);
            }
            c.hitNode[p] = 0;
            const uint32_t bit = 2u << p;
            EnemyNode& n = c.anim.nodes[node & 31];
            n.flags = n.flags & ~bit & ~8u;
            c.flags34 &= ~bit;
        }
    }
    return dmg;
}

// the light / draw of 0x0042D930 (pattern 2) and 0x0042C5A0 (pattern 0, + the second skeleton on node 1)
void CreatureDraw(World::Impl& w, CreatureTask& c, bool attachments) {
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    w.LightCustom(1.0f, w.cam.pitch, w.cam.yaw, white);  // 0x0041DC10
    w.animLight = 0;
    if (attachments) c.anim.attachHidden = c.lit != 1;  // 0x0042D930: attachment entry 0 on with +0x1324
    w.AnimDraw(c);     // 0x00411090
    w.EnemyShadow(c);
    if (c.extra && c.lit != 0) {  // 0x0042C5A0: the second skeleton at node 1 (camera matrix x node model-view)
        EnemyTask& x = *c.extra;
        Mat4 q = w.cam.inverse;
        mx::MulLeft(q, c.mv1);
        mx::GetTranslation(q, x.pos);
        mx::MatrixToAngles(q, &x.rot[0], &x.rot[1], &x.rot[2]);  // 0x004019E0
        w.AnimDraw(x);
        w.EnemyShadow(x);
        if (c.anim1320 != 0) ++x.anim.counter[0];
    }
    w.animLight = 1;
    w.LightScene();  // 0x0041DCC0
}

// 0x0042C490(c, path, t): the path point (0x004042D0) in the frame at the players' point facing the boss, 10 ahead
void CreaturePathTarget(World::Impl& w, CreatureTask& c, int path, float tm) {
    int yaw = 0, pitch = 0;
    FaceFrom(w.playerPos, c.boss->pos, &yaw, &pitch);
    float p[3] = {};
    int ang[3];
    st5::EvalPath(w, path, tm, p, ang);
    Mat4 m = Mat4::Identity();
    mx::Translate(m, w.playerPos[0], w.playerPos[1], w.playerPos[2]);
    mx::RotY(m, yaw);
    mx::RotX(m, pitch);
    mx::Translate(m, 0.0f, 0.0f, -10.0f);
    mx::Transform(m, p, c.tgt);
}

void CreatureEnd(World::Impl& w, CreatureTask& c) {  // the last sub-state of each pattern
    if (c.boss) c.boss->flags34 &= ~0x10000u;
    w.targetTaken[0] = 0;  // 0x009A2BA0
    State(w).f2448 = 0;    // 0x009A2448
    w.EnemyDestroy(c);     // 0x00409CC0
}

// 0x0042C1A0: pattern 0 - flies to a path start beside the players (0x1E frames), follows path 0x192 + side
// (0x0055D234[side] frames ahead), bites at 0x00577380[side] - 4 (Player_TakeHit 7, rank - 3); one hit after 16 path
// frames ends it (boss hp - 0x0055CCD6[players], rank + 1)
void TaskCreature0(Task& t) {
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    if (c.lock) c.slot120 = static_cast<int8_t>(c.lock->slot);
    FinalBossTask* b = c.boss;
    bool inc = false;
    switch (c.sub) {
    case 0: {
        if (b->c136C != 3) break;
        c.anim1320 = 1;
        c.lit = 1;
        const int r = w.Rand(0x0042C1EB) & 1;
        c.side = r;
        c.f1334 = w.exe.S16(0x0055D234 + static_cast<uint32_t>(r) * 2);
        c.t = 0;
        CreaturePathTarget(w, c, r + 0x192, static_cast<float>(c.f1334));
        w.targetTaken[0] = 1;
        w.SyncLock(c);
        w.lockOn.AddDirect(c.lock);  // 0x00408E80
        inc = true;
        break;
    }
    case 1: {
        for (int k = 0; k < 3; ++k) c.pos[k] = Lerp(c.pos[k], c.tgt[k], 1, 0x1E - c.t);
        if (c.t++ < 0x1F) break;
        memcpy(c.pos, c.tgt, 12);
        c.t = c.f1334;
        c.f1334 = 0;
        w.Sound(0x004417A9);
        inc = true;
        break;
    }
    case 2: {
        CreaturePathTarget(w, c, c.side + 0x192, static_cast<float>(c.t));
        memcpy(c.pos, c.tgt, 12);
        ++c.t;
        ++c.f1334;
        if (c.t == static_cast<int32_t>(w.exe.U32(0x00577380 + static_cast<uint32_t>(c.side) * 4)) - 4) {
            HitPlayers(w, 7);
            if (b) RankAdd(*b, -3);
            inc = true;
        } else if (0xF < c.f1334 && (c.flags34 & 8)) {
            w.Sound(0x3425A9);
            if (b) {
                b->hp = static_cast<int16_t>(b->hp - EmperorDamage(w, *b, c));
                RankAdd(*b, 1);
            }
            inc = true;
        }
        break;
    }
    case 3:
        CreatureEnd(w, c);
        return;
    default:
        break;
    }
    if (inc) ++c.sub;
    CreatureDraw(w, c, false);  // 0x0042C5A0
    if (c.anim1320 != 0) ++c.anim.counter[0];
    const uint32_t f = c.flags34;
    c.flags34 = f & ~0xEu;
    if (!(f & 0x100)) ViewPosR(w, c, 2.0f);  // 0x00409B70(2.0)
}

// 0x0042CDD0: pattern 2 - waits 0x1F frames lit, flies at the players' point (0x0055D248[rank] frames) to within
// 0x0055D2A8, then the attack motion with a flame on node 24 up to tick 0x32 (Player_TakeHit 9, rank - 3); a hit on
// node 0x18 from tick 0x12 ends it (boss hp, rank + 1)
void TaskCreature2(Task& t) {
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    if (c.lock) c.slot120 = static_cast<int8_t>(c.lock->slot);
    FinalBossTask* b = c.boss;
    bool inc = false;
    switch (c.sub) {
    case 0:
        if (b->c136C != 3) break;
        c.t = 0;
        c.lit = 1;
        w.targetTaken[0] = 1;
        w.SyncLock(c);
        w.lockOn.AddDirect(c.lock);  // 0x00408E80
        inc = true;
        break;
    case 1:
        if (c.t++ < 0x1F) break;
        memcpy(c.tgt, w.playerPos, 12);  // 0x009C71E0
        c.t = 0;
        c.f1334 = w.exe.S16(0x0055D248 + static_cast<uint32_t>(b ? b->rank : 0) * 2);
        w.Sound(0x3F25A9);
        inc = true;
        break;
    case 2: {
        if (CreatureHits2(w, c, b)) {
            c.sub = 4;
            break;
        }
        for (int k = 0; k < 3; ++k) c.pos[k] = Lerp(c.pos[k], c.tgt[k], 1, c.f1334 - c.t);
        const float dz = c.pos[2] - c.tgt[2], dy = c.pos[1] - c.tgt[1], dx = c.pos[0] - c.tgt[0];
        if (RF(w, 0x0055D2A8) < std::sqrt(dy * dy + dx * dx + dz * dz)) break;
        c.anim1320 = 1;
        w.Sound(0x4125A9);
        inc = true;
        break;
    }
    case 3: {
        const int tick = c.anim.tick[0];
        if (tick < 0x32) {  // the flame at node 24: T(7, -4.5, 0) RotY 0x4000 RotX 0x2000 scale 1.5
            Mat4 m = c.mv24;
            mx::Translate(m, 7.0f, -4.5f, 0.0f);
            mx::RotY(m, 0x4000);
            mx::RotX(m, 0x2000);
            mx::Scale(m, 1.5f, 1.5f, 1.5f);
            w.Draw(static_cast<int>(w.seedCounter % 0x27) + 0x97A, m, 8, -1.0f, false, -1, true);
            w.Draw(static_cast<int>(w.seedCounter & 0x1F) + 0x199C, m, 8, -1.0f, false, -1, true);
            if (CreatureHits2(w, c, b)) c.sub = 4;
            break;
        }
        if (tick == 0x32) {
            HitPlayers(w, 9);
            if (b) RankAdd(*b, -3);
            inc = true;
        } else if (tick == w.MotionTicks(c.anim.motion[0]) - 1) {
            inc = true;
        }
        break;
    }
    case 4:
        CreatureEnd(w, c);
        return;
    default:
        break;
    }
    if (inc) ++c.sub;
    {  // +0x100: node 24's world position (camera matrix x its model-view of the last draw)
        Mat4 q = w.cam.inverse;
        mx::MulLeft(q, c.mv24);
        mx::GetTranslation(q, c.body);
    }
    CreatureDraw(w, c, true);  // 0x0042D930
    if (c.anim1320 != 0) ++c.anim.counter[0];
    const uint32_t f = c.flags34;
    c.flags34 = f & ~0xEu;
    if (!(f & 0x100)) ViewPosR(w, c, 0.0f);  // 0x00409B70(0)
}

}  // namespace

void CreatureFirst0(Task& t) {  // 0x0042C0B0
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    CreatureSetup(w, c, 0x4D, 0x40C);
    c.anim.counter[0] = 0;
    auto x = std::make_unique<EnemyTask>();  // the second skeleton: a Mem_Alloc block, not a task
    x->actorSlot = -1;
    x->slot120 = -1;
    x->anim.kind = 0x4E;
    x->anim.motion[0] = 0xF;
    w.EnemyInitPose(*x);
    if (x->actorSlot >= 0 && x->actorSlot < 14) w.actorSlots[x->actorSlot] = nullptr;  // (no slot of its own)
    x->actorSlot = -1;
    x->anim.rotOrder = 5;
    x->flags34 |= 0x88000;
    // 0x00410440's callbacks stay: the plain node draw 0x00411050 (no enemy head look on node 2) and no post-root step
    x->nodeCb = 0x00411050;
    c.extra = std::move(x);
    c.fn = 0x0042C1A0;
    t.fn = TaskCreature0;
    TaskCreature0(t);
}

void CreatureFirst2(Task& t) {  // 0x0042CD30
    World::Impl& w = *CurrentWorld();
    CreatureTask& c = t.As<CreatureTask>();
    CreatureSetup(w, c, 0x50, 0x3B);
    c.anim.counter[0] = 0xB;
    c.billboardNode = 0x16;
    c.orbTable = 0x0055D268;
    c.fn = 0x0042CDD0;
    t.fn = TaskCreature2;
    TaskCreature2(t);
}

// Actor type 0x2D variant 1 (0x00426A70): the name card, the eight orbs, state 0 at once
bool World::Impl::SpawnFinalBoss(Task& t, const SpawnRecord& r) {
    auto d = st5::FromRecord<FinalBossTask>(r, 0x00426C30);
    FinalBossTask& b = *d;
    b.behaviour = 0;
    b.sub = 0;
    b.actorSlot = -1;
    b.slot120 = -1;
    b.target = 0;  // +0x121 is never written (cleared task memory = 0): slots 0 / 1 of the lock-on table
    b.variant = r.ParamS8(1);
    b.anim.kind = 0x4C;
    b.anim.motion[0] = r.ParamS16(2);
    EnemyInitPose(b);  // 0x00410440 (+ actor slot)
    b.anim.rotOrder = 5;
    b.nodeFn = st5::Node2D;  // +0x12EC = 0x00429040
    b.postRootFn = [](World::Impl&, EnemyTask&) {};
    b.anim.drawFlags |= 0xC;
    b.anim.counter[0] = r.ParamS16(4);
    b.alpha1370 = 1.0f;
    b.light[0] = b.light[1] = b.light[2] = 1.0f;
    b.radius = F32(exe, 0x004C4D28 + 0x4C * 4);
    b.f1374 = 2.0f;
    b.c136C = 0;
    b.lock = &lockTargets[nextLockTarget++];
    SpawnNameCard(0x005898C8);  // 0x00437A70
    FinalBossTask* bp = d.get();
    const uint32_t serial = t.serial;
    for (int i = 0; i < 8; ++i) {
        OrbTask* o = NewTask<OrbTask>([](Task& tt) {  // 0x00429D00 on its first run
            World::Impl& w = *CurrentWorld();
            OrbTask& oo = tt.As<OrbTask>();
            oo.flags34 = 1;
            oo.radius = Bits(0x3FE66666);
            oo.behaviour = 0;
            oo.fn = 0x00429D30;
            tt.fn = TaskOrb;
            (void)w;
            TaskOrb(tt);
        }, 0x00429D00);
        o->idx = i;
        if (cfg.portTyping) o->typingFlags = 0x38000008u;
        o->boss = bp;
        o->bossSerial = serial;
        o->actorSlot = -1;
        o->slot120 = 0;
        o->killer = 0;
        o->c131D = 0;
        o->objMesh = 0;
    }
    if (runner->cfg.gameType == 3) {  // boss mode: placed on camera path 0x185 at 1810, motion 0xA3, state 2
        float p[3] = {}, tg[3] = {};
        int roll = 0;
        if (cameraBank.Evaluate(typingReferenceActive ? 0x41A7 : 0x185, Bits(0x44E24000), p, tg, &roll, false)) {  // 0x004042D0
            memcpy(b.pos, p, 12);
            for (int k = 0; k < 3; ++k) b.rot[k] = static_cast<int>(tg[k]);
        }
        AnimSetMotion(b, 0xA3);  // 0x00411930
        runner->flags[0x32] = 1;  // 0x009C7232
        b.t = 0;
        b.c136C = 1;
        b.behaviour = 2;
    }
    St6State& s = State(*this);
    for (auto& o : s.orbs) o = St6State::OrbSlot{};
    s.f2448 = 0;
    t.data = std::move(d);
    t.fn = TaskBoss;
    TaskBoss(t);
    return true;
}

bool TypingBoss6(World::Impl& w, ActorTask& actor, TypingTargetSpec& out, bool complete) {
    if (auto* answer = dynamic_cast<EmperorAnswerTask*>(&actor)) {
        out.boss = true;
        auto* b = answer->boss;
        if (!b || !Alive(w, b, answer->bossSerial) || b->hp < 1 || b->behaviour != 5 ||
            !b->typingDialogueActive || b->typingRevealFrames) return true;
        out.retain = true;
        out.token = 0x650000u + b->typingDialogueToken * 4u + answer->choice;
        out.word = &b->typingAnswers[answer->choice];
        out.node = 1;
        out.view[0] = 0;
        out.view[2] = -100.0f;
        out.view[1] = (float(answer->choice) * 80.0f - 80.0f) * out.view[2] / w.focal;
        if (complete) {
            w.typingEmperorAnswerWeight += TypingEmperorAnswerWeight(*out.word);
            b->hp = static_cast<int16_t>(b->hp - TypingEmperorAnswerDamage(w.players == 2));
            b->typingAnswerHit = true;
            b->typingDialogueActive = false;
            ++b->typingDialogueToken;
            w.typingQuestion.clear();
            w.typingQuestionSeconds = 0;
        }
        return true;
    }
    if (auto* b = dynamic_cast<FinalBossTask*>(&actor)) {
        out.boss = true;
        if (w.cfg.portTyping && b->behaviour == 5) return true;
        if (b->hp < 1 || b->behaviour < 3 || b->behaviour > 5 || (b->flags34 & 0x4000000u)) return true;
        out.retain = true;
        out.token = 0x600000u | (static_cast<uint32_t>(b->behaviour) << 12);
        Mat4 m = b->mv1;
        mx::Translate(m, Bits(0x4013F972), Bits(0x3DE0AA65), 0.0f);
        mx::GetTranslation(m, out.view);
        const bool threshold = (b->behaviour == 3 && b->hp <= b->record.ParamS16(0xC)) ||
                               (b->behaviour == 4 && b->hp <= b->record.ParamS16(0xE));
        if (threshold) return true;
        if (b->sub >= 1 && b->sub <= 5 && !(b->flags34 & 0x40008100u) &&
            out.view[2] < -0.1f && w.OnScreen(out.view)) {
            out.node = 1;
            if (complete) TypingDeliver(*b, out.node);
            return true;
        }
        // Emperor owns the persistent phrase while its temporary creatures come
        // and go. A ready phrase may hit the current creature's actual weak point.
        for (Task* t : w.ts.Live()) {
            if (!t->alive || !t->data) continue;
            auto* c = dynamic_cast<CreatureTask*>(t->data.get());
            if (!c || c->boss != b) continue;
            TypingTargetSpec creature;
            TypingBoss6(w, *c, creature, false);
            if (creature.node < 0 || creature.view[2] >= -0.1f || !w.OnScreen(creature.view)) continue;
            out.node = creature.node;
            memcpy(out.view, creature.view, 12);
            if (complete) {
                TypingBoss6(w, *c, creature, true);
                ++b->typingRevision;
            }
            return true;
        }
        return true;
    }
    if (auto* c = dynamic_cast<CreatureTask*>(&actor)) {
        // The PC reference posts a separate word for each summoned creature.
        // The fallback's private descriptor is consumed by Emperor's word.
        out.boss = true;
        if (!c->boss || c->boss->hp < 1 || (c->flags34 & 0x44008100u)) return true;
        if (c->boss->behaviour == 4 && c->boss->hp <= c->boss->record.ParamS16(0xE)) return true;
        if (w.typingReferenceActive) {
            if (c->typingRevision != 0) return true;
            out.retain = true;
            out.token = 0x610000u | (c->fn & 0xFFFFu);
            memcpy(out.view, c->viewPos, 12);
        }
        if (c->fn == 0x0042C8D0 && ((c->sub == 2 && c->f1334 >= 0x32) || c->sub == 3 || c->sub == 4)) {
            out.node = 1;
            Mat4 m = c->mv1;
            mx::Translate(m, 0.0f, 4.0f, 1.0f);
            mx::GetTranslation(m, out.view);
        } else if (c->fn == 0x0042C1A0 && c->sub == 2 && c->f1334 > 0xF) {
            out.node = 1;
            memcpy(out.view, c->anim.nodes[1].hit, 12);
        } else if (c->fn == 0x0042D530 && (c->sub == 2 || (c->sub == 3 && c->anim.tick[0] < 0x2C))) {
            out.node = 2;
            memcpy(out.view, c->anim.nodes[2].hit, 12);
        } else if (c->fn == 0x0042CDD0 && (c->sub == 2 || (c->sub == 3 && c->anim.tick[0] < 0x32)) && c->anim.tick[0] > 0x11) {
            out.node = 0x18;
            memcpy(out.view, c->anim.nodes[0x18].hit, 12);
        } else {
            return true;
        }
        if (complete) TypingDeliver(*c, out.node);
        return true;
    }
    if (auto* o = dynamic_cast<OrbTask*>(&actor)) {
        out.danger = true;
        out.projectile = false; // Original orb flags 0x38000008 lack the unranked 0x10 bit.
        const bool flying = (o->behaviour == 3 && o->sub == 2) ||
                            (o->behaviour == 4 && o->sub == 4 && (o->idx & 1) == 0);
        if (!flying || !Alive(w, o->boss, o->bossSerial)) return true;
        out.node = 1;
        out.token = 0x620000u | (static_cast<uint32_t>(o->behaviour) << 4) | static_cast<uint32_t>(o->sub);
        memcpy(out.view, o->viewPos, 12);
        if (complete) {
            TypingDeliver(*o, out.node);
            o->typingStrike = false;
        }
        return true;
    }
    return false;
}

}  // namespace game
