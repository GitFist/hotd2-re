// game::World - the stage-5 boss: actor type 0x32 (ctor 0x0047F5F0 -> task 0x0047C960, node callback 0x0047F780,
// state table 0x00596738: 0 intro 0x00480050, 1 entrance 0x0047D120, 2 death 0x00480140, 3 / 4 fall 0x00480290 /
// 0x00480470, 5 stagger 0x0047CA50, 6 / 7 move + volley 0x0047D220 / 0x0047D410, 8 / 9 move + charge 0x0047D5E0 /
// 0x0047D890, 10 / 11 final phase 0x0047DD50), its projectiles (0x0047EE30 -> task 0x0047EFA0, states 0x00596AB0) and
// effect tasks (trail 0x0047DC30, smoke 0x0047F4A0, glow 0x0047E130, beam 0x0047E2B0, death sparks 0x00480700,
// explosion 0x00480810). Damage only on node slots 4 / 6 / 11 / 13 (0x0047CE10). Verification: notes/agents/stage5.md.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/world_st5.h"
#include "game/typing_boss_rules.h"

namespace game {

using namespace st5;

namespace {

struct Boss5Task : EnemyTask {
    int c1328 = 0;           // +0x1328 (final phase: volleys fired)
    int c132C = 0;           // +0x132C
    int count = 0;           // +0x1330
    int flash = 0;           // +0x1334 hit flash frames (node 0x53F counts it down)
    int c1338 = 0;           // +0x1338
    int c133C = 0;           // +0x133C trail counter
    int shots = 0;           // +0x1350 projectiles alive
    int c1354 = 0;           // +0x1354 position index (0..3)
    int c1358 = 0;           // +0x1358 position step (+-1)
    int c135C = 0;           // +0x135C
    int c1360 = 0;           // +0x1360 volley kind
    int phase = 0;           // +0x1364 hp threshold row (0x00596770)
    int hitsLeft = 0;        // +0x1368 (behaviour 8: hits before the stagger)
    float f138C = 0;         // +0x138C (aggression timer)
    float goal[3] = {};      // +0x13C0
    uint32_t serial = 0;
    int typingWordFamily = -1;
    int typingMistypePlayer = -1, typingOrbsCleared = 0, typingInterlude = 0;
    bool typingHopsStarted = false;
    float typingInterludeStep[3] = {};
};

struct BossRef {
    Boss5Task* p = nullptr;
    uint32_t serial = 0;
};

Boss5Task* Alive(World::Impl& w, const BossRef& r) {
    if (!r.p) return nullptr;
    for (Task* t : w.ts.Live())
        if (t->serial == r.serial) return t->data.get() == r.p ? r.p : nullptr;
    return nullptr;
}

int Ticks(const World::Impl& w, const EnemyTask& e) { return w.MotionTicks(e.anim.motion[0]); }

void Zero(EnemyTask& e) {  // 0x0047FFC0
    for (int i = 0; i < 3; ++i) e.acc[i] = e.vel[i] = 0;
}

void VelTo(Boss5Task& b, int n) {  // vel = (target - pos) / n
    const float f = static_cast<float>(n);
    for (int i = 0; i < 3; ++i) b.vel[i] = (b.goal[i] - b.pos[i]) / f;
}

void Aggr(Boss5Task& b, int d) {  // 0x00480010
    int v = static_cast<int8_t>(static_cast<int8_t>(b.c131E) + d);
    if (v < 0) v = 0;
    if (0xF < v) v = 0xF;
    b.c131E = static_cast<int8_t>(v);
}

void ReleaseTarget(World::Impl& w, EnemyTask& e) {  // 0x0047FFE0
    if (e.target != -1) {
        w.targetTaken[e.target & 1] = 0;
        e.target = -1;
    }
}

// 0x00409DC0: the target is valid only in the shooting camera (mode 2) and (demo or player state 5)
bool TargetValid(World::Impl& w, int t) {
    return w.runner->camera.modeCur == 2 && (w.cfg.gameMode == 5 || (t >= 0 && w.player[t & 1].state == 5));
}

// world point of node n's view-space hit point (+0x68) through the current camera matrix
void NodePoint(const World::Impl& w, const EnemyTask& e, int n, float out[3]) {
    mx::Transform(w.cam.inverse, e.anim.nodes[n & 31].hit, out);
}

int AggRow(const World::Impl& w, uint32_t base, const Boss5Task& b) {  // int table [aggression * 0x10]
    return static_cast<int32_t>(w.exe.U32(base + static_cast<uint32_t>(static_cast<int8_t>(b.c131E)) * 0x10));
}

float PhaseThr(const World::Impl& w, const Boss5Task& b) {  // (float)(hpMax / 10) * 0x00596774[phase * 8]
    if (w.typingReferenceActive) return TypingMagicianPhaseThreshold(b.hpMax,b.phase);
    return static_cast<float>(b.hpMax / 10) * F32(w.exe, 0x00596774 + static_cast<uint32_t>(b.phase) * 8);
}

int PhaseBehaviour(const World::Impl& w, const Boss5Task& b) {
    return w.typingReferenceActive ? TypingMagicianPhaseBehaviour(b.phase) :
        w.exe.S16(0x00596770 + static_cast<uint32_t>(b.phase) * 8);
}

// 0x0047FE90: pick the attacked player (+0x121); 1 when one was taken
bool PickTarget(World::Impl& w, Boss5Task& b) {
    int t = -1;
    bool check = true;
    if (w.attackSlot != 0) {
        check = false;
    } else if (w.route == 1) {
        bool skip = false;
        if (w.playersInGame == 0) {
            if (w.targetTaken[0] != 0) skip = true;
            else t = 0;
        }
        if (!skip && w.playersInGame == 1 && w.targetTaken[1] == 0) t = 1;
    } else if (w.route == 2) {
        if (w.players == 1) {
            const int r = w.Rand(0x0047FECD) & 1;
            if (w.targetTaken[r] == 0) t = r;
        } else if (w.attackers == 1) {
            int r = w.Rand(0x0047FF02) & 1;
            if (w.targetTaken[r] != 0) r = ~r;
            t = static_cast<int8_t>(r);
        } else {
            const int side = !((w.focal * b.viewPos[0]) / b.viewPos[2] >= F32(w.exe, 0x004C436C)) ? 1 : -1;  // 0x00409C90 (NaN: 1)
            if (side != -1) {
                if (side == 1 && w.targetTaken[1] == 0) t = 1;
            } else if (w.targetTaken[0] == 0) {
                t = 0;
            }
        }
    }
    if (check && !TargetValid(w, t)) t = -1;
    b.target = static_cast<int8_t>(t);
    if (t != -1) {
        w.targetTaken[t & 1] = 1;
        return true;
    }
    return false;
}

// 0x00409B70(0): view position of the body point, hit target, lock-on candidate
void ViewPos(World::Impl& w, EnemyTask& e) {
    mx::Transform(w.cam.view, e.body, e.viewPos);
    w.RegisterTarget(&e);
    e.body[1] = 0.0f + e.body[1];
    w.SyncLock(e);
    w.lockOn.Register(e.lock, w.playerPos);  // 0x00408EC0
}

// ---------------------------------------------------------------------------------------------------------------------
// Effect tasks
// ---------------------------------------------------------------------------------------------------------------------

struct FxTask : ActorTask {
    BossRef boss;
    float pos[3] = {}, vel[3] = {}, acc[3] = {};  // +0x40 / +0x4C / +0x58
    int rot[3] = {};      // +0x64
    float scale = 0;      // +0x118
    int c1320 = 0;        // +0x1320
    int8_t c131A = 0;     // +0x131A
    int16_t sub = 0;      // +0x1312
    int count = 0;        // +0x1330
    int a1350 = 0, a1354 = 0, a1358 = 0;  // +0x1350 / +0x1354 / +0x1358
    float f138C = 0;      // +0x138C
    int model = 0;        // +0x13F0
};

Mat4 FxFrame(const FxTask& k) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    return m;
}

// 0x0047DC30: afterimage of the boss (model 0x5E5), fading 6 per frame from 255
void TaskTrail(Task& t) {
    World::Impl& w = *CurrentWorld();
    FxTask& k = t.As<FxTask>();
    k.c1320 -= 6;
    if (0x5A < k.c1320) {
        const float f = F32(w.exe, 0x004C43A8) / static_cast<float>(0xFF / k.c1320);
        k.f138C = f;
        // 0x0047DD28: 0x004AA0A0 by the boss's attack +0x131A (3 when its +0x131E >= 15): 0 -> (0, f / 8, f),
        // 1 -> (f, f / 8, 0), else (f, f, f); kept for the later draws
        if (Boss5Task* b = Alive(w, k.boss)) {
            const int a = b->c131E >= 0xF ? 3 : b->attack131A;
            const float e = f * F32(w.exe, 0x0055CB28);
            if (a == 0) w.LightColor(0.0f, e, f);
            else if (a == 1) w.LightColor(f, e, 0.0f);
            else w.LightColor(f, f, f);
        }
        w.Draw(k.model, FxFrame(k));
        return;
    }
    k.c1320 = 0x5A;
    DestroyActor(w, k);  // 0x00409CC0
}

void SpawnTrail(World::Impl& w, Boss5Task& b) {  // 0x0047DB50
    FxTask* k = w.NewTask<FxTask>(TaskTrail, 0x0047DC30);
    k->flags34 = 1;
    k->boss = { &b, b.serial };
    const float v[3] = { b.viewPos[0], b.viewPos[1], b.viewPos[2] - F32(w.exe, 0x0055D2B4) };
    mx::Transform(w.cam.inverse, v, k->pos);
    k->rot[0] = b.rot[0];
    k->rot[1] = b.rot[1] + 0x8000;
    k->rot[2] = b.rot[2];
    k->c1320 = 0xFF;
    k->model = 0x5E5;
}

// 0x0047F4A0: smoke of a projectile: rises, shrinks, fades 5 per frame
void TaskSmoke(Task& t) {
    World::Impl& w = *CurrentWorld();
    FxTask& k = t.As<FxTask>();
    const float vy = k.acc[1] + k.vel[1];
    k.vel[1] = vy;
    k.pos[1] = vy + k.pos[1];
    const float s = k.scale - F32(w.exe, 0x005644F8);
    k.scale = s;
    if (s < F32(w.exe, 0x004C4CC8)) k.scale = Bits(0x3DCCCCCD);
    k.c1320 -= 5;
    if (k.c1320 < 0x5B) {
        k.c1320 = 0x5A;
        DestroyActor(w, k);
        return;
    }
    Mat4 m = FxFrame(k);
    mx::Scale(m, k.scale, k.scale, k.scale);
    // 0x004AA0A0: f = 1 / (255 / +0x1320); kind 3 (the shot's +0x131A): (f, f * 0.4, f * 0.1), else (f + 0.5, f, f);
    // the colour stays set for the later draws
    const float f = 1.0f / static_cast<float>(0xFF / k.c1320);
    k.f138C = f;
    if (k.c131A == 3) w.LightColor(f, f * F32(w.exe, 0x005308D0), f * F32(w.exe, 0x0055D230));
    else w.LightColor(f + F32(w.exe, 0x004C43AC), f, f);
    w.Draw(k.model, m);
}

// 0x0047E130: the glow at the boss in the final phase (models 0xB3..0xC7 looping from 0xB4), while +0x136C & 0x20
void TaskGlow(Task& t) {
    World::Impl& w = *CurrentWorld();
    FxTask& k = t.As<FxTask>();
    Boss5Task* b = Alive(w, k.boss);
    if (!b || !(b->flags136C & 0x20)) {
        w.ts.KillCurrent();
        return;
    }
    if (k.a1358 < ++k.a1350) k.a1350 = k.a1354;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, b->pos[0], b->pos[1] + F32(w.exe, 0x004C4398), b->pos[2]);
    mx::RotZ(m, b->rot[2]);
    mx::RotY(m, b->rot[1]);
    mx::RotX(m, b->rot[0]);
    const float f = k.f138C + F32(w.exe, 0x004C4C88);
    k.f138C = f;
    if (F32(w.exe, 0x004C4380) < f) k.f138C = 1.0f;
    w.LightColor(k.f138C, k.f138C * F32(w.exe, 0x004C4C58), 0.0f);  // 0x004AA0A0: stays set for the later draws
    w.Draw(k.a1350, m, 9, k.f138C * F32(w.exe, 0x004C43AC));  // 0x004185A0 in pass-1 mode 9 (0x004A79F0)
}

// 0x0047E2B0: a beam between nodes 8 and 5 (models 0x1279 + n up to 0x1299), random roll
void TaskBeam(Task& t) {
    World::Impl& w = *CurrentWorld();
    FxTask& k = t.As<FxTask>();
    if (k.a1358 < ++k.a1350) {
        w.ts.KillCurrent();
        return;
    }
    Boss5Task* b = Alive(w, k.boss);
    if (b) {
        float a[3], c[3];
        NodePoint(w, *b, 8, a);
        NodePoint(w, *b, 5, c);
        const float h = F32(w.exe, 0x004C43AC);
        for (int i = 0; i < 3; ++i) k.pos[i] = (a[i] - c[i]) * h + c[i];
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, 0);
    mx::RotX(m, 0);
    w.LightColor(1.0f, 9.5f, Bits(0x3F0A3D71));  // 0x0047E41D: 0x004AA0A0(1, 9.5, 0.54), kept for the later draws
    w.Draw(k.a1350, m);
}

// 0x00480700: death sparks (models 0xC53..0xC68); the boss's +0x34 bit 8 blinks with them
void TaskSpark5(Task& t) {
    World::Impl& w = *CurrentWorld();
    FxTask& k = t.As<FxTask>();
    if (0xC68 < ++k.model) {
        w.ts.KillCurrent();
        return;
    }
    if (Boss5Task* b = Alive(w, k.boss)) {
        if ((k.model & 1) == 0) b->flags34 |= 8;
        else b->flags34 &= ~8u;
    }
    Mat4 m = FxFrame(k);
    mx::Scale(m, k.scale, k.scale, k.scale);
    w.Draw(k.model, m);
}

void SpawnSpark5(World::Impl& w, Boss5Task& b) {  // 0x004805C0
    FxTask* k = w.NewTask<FxTask>(TaskSpark5, 0x00480700);
    k->model = 0xC53;
    k->boss = { &b, b.serial };
    k->scale = 1.0f;
    const int r = w.Rand(0x00480600);
    NodePoint(w, b, r % 0xF + 1, k->pos);
    k->rot[0] = w.Rand(0x0048068F) % 0xFFFF;
    k->rot[1] = w.Rand(0x0048069F) % 0xFFFF;
    k->rot[2] = w.Rand(0x004806AF) % 0xFFFF;
    k->scale = static_cast<float>((w.Rand(0x004806BF) & 3) + 1);
    if (b.flash < 1) b.flash = 8;
    w.Sound(0xB16A9);
}

// 0x00480810: the final explosion at the body point: shrinking model 0x7F4, then the boss hidden and models 0x7EF..
void TaskBlast5(Task& t) {
    World::Impl& w = *CurrentWorld();
    FxTask& k = t.As<FxTask>();
    int stage = k.sub;
    if (stage == 0) {
        const float s = k.scale - F32(w.exe, 0x005644F8);
        k.scale = s;
        if (!(F32(w.exe, 0x004C436C) < s)) {
            k.count = 0xF;
            k.scale = 0.0f;
            k.sub = 1;
            stage = 1;
        } else {
            stage = -1;
        }
    }
    if (stage == 1) {
        if (0 < --k.count) {
            stage = -1;
        } else {
            k.model = 0x7EF;
            k.scale = 1.0f;
            if (Boss5Task* b = Alive(w, k.boss)) {
                b->anim.drawFlags &= ~1u;
                b->anim.attachHidden = true;  // 0x00409D10(anim, 0)
            }
            w.Sound(0xE23A9);
            ++k.sub;
            k.count = 0;
            stage = 2;
        }
    }
    if (stage == 2 && 2 < ++k.count) {
        k.count = 0;
        if (0x815 < ++k.model) {
            w.ts.KillCurrent();
            return;
        }
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::Scale(m, k.scale, k.scale, k.scale);
    w.Draw(k.model, m);
}

// ---------------------------------------------------------------------------------------------------------------------
// Projectiles (0x0047EE30 -> task 0x0047EFA0; states 0x0047E440 / 0x0047E810 / 0x0047E980 / 0x0047ED10)
// ---------------------------------------------------------------------------------------------------------------------

struct Shot5Task : EnemyTask {
    BossRef boss;
    float scale = 0;         // +0x118
    int c1320 = 0xFF;        // +0x1320 alpha
    int count = 0;           // +0x1330 smoke counter
    int smokeEvery = 0;      // +0x1334
    int frames = 0;          // +0x1338
    int c133C = 0;           // +0x133C
    float f1340 = 0;         // +0x1340 fade step
    int fadeTo = 0;          // +0x1350
    int kind = 0;            // +0x1354 (0 / 1 volley, 2 charge, 4 final phase)
    int volley = 0;          // +0x1360 the boss's volley kind
    int growDelay = 0;       // +0x1364
    int index = 0;           // +0x1368 the boss's shot count at the spawn
    float growTo = 0;        // +0x1370
    float growStep = 0;      // +0x1374
    float f138C = 0;         // +0x138C
    float goal[3] = {};      // +0x13C0
    int model = 0xB02;       // +0x13F0
};

uint32_t BossParams(const Boss5Task& b) { return b.record.addr + 0x24; }
int8_t BossP8(const Boss5Task& b, int off) { return b.record.ParamS8(off); }
float BossPF(const Boss5Task& b, int off) {
    const int32_t v = b.record.ParamS32(off);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
int32_t BossP32(const Boss5Task& b, int off) { return b.record.ParamS32(off); }

void SpawnSmoke5(World::Impl& w, Shot5Task& s) {  // 0x0047F400
    FxTask* k = w.NewTask<FxTask>(TaskSmoke, 0x0047F4A0);
    k->flags34 = 1;
    memcpy(k->pos, s.pos, 12);
    memcpy(k->vel, s.vel, 12);
    memcpy(k->acc, s.acc, 12);
    memcpy(k->rot, s.rot, 12);
    k->vel[1] = 0.0f;
    k->acc[1] = static_cast<float>((w.Rand(0x0047F448) & 3) + 1) * F32(w.exe, 0x005646E8);
    k->c1320 = s.c1320;
    k->c131A = s.attack131A;
    k->model = s.model;
    k->scale = s.scale + F32(w.exe, 0x004C4D08);
}

// 0x0047F190(obj, out): the aim point near the players (view offsets by the volley / index), through the camera
void AimPoint(World::Impl& w, Shot5Task& s, float out[3]) {
    float x = 0.0f, y = 0.0f;
    bool row = true;
    const int v = s.volley;
    if (w.players == 2) {
        switch (s.index) {
        case 1: x = 105.0f; break;
        case 2: x = 213.0f; break;
        case 3: x = -213.0f; break;
        case 4: x = -105.0f; break;
        default: x = 0.0f; break;
        }
    } else if (v < 0 || 2 < v) {
        row = false;
    } else if (v == 2) {
        switch (s.index) {
        case 1: x = 105.0f; break;
        case 2: x = 213.0f; break;
        case 3: x = -213.0f; break;
        case 4: x = -105.0f; break;
        default: x = 0.0f; break;
        }
    } else if (s.index == 1) {
        x = -160.0f;
    } else if (s.index == 2) {
        x = 160.0f;
    }
    if (row) y = 40.0f;
    const uint32_t r1 = static_cast<uint32_t>(w.Rand(0x0047F237));
    x = static_cast<float>(static_cast<int>(7 - (r1 & 0xF))) + x;
    const uint32_t r2 = static_cast<uint32_t>(w.Rand(0x0047F256));
    const float k = F32(w.exe, 0x005691C0);
    const float px = (x * k) / w.focal;
    const float py = ((static_cast<float>(static_cast<int>(0x1F - (r2 & 0x3F))) + y) * k) / w.focal;
    const float p[3] = { px, py, -4.0f };
    mx::Transform(w.cam.inverse, p, out);
}

// 0x0047EB30: a random point on screen (kind 4; +0x131B 1: near the centre)
void RandomPoint(World::Impl& w, Shot5Task& s) {
    int x, y;
    if (s.c131B == 1) {
        const int a = w.Rand(0x0047EB47) & 1;
        x = (1 - 2 * a) * (w.Rand(0x0047EB61) % 0xF0);
        const int c = w.Rand(0x0047EB7D) & 1;
        y = (1 - 2 * c) * (w.Rand(0x0047EB97) % 0xB4);
    } else {
        const int sgn = 1 - 2 * (w.Rand(0x0047EBB1) & 1);
        x = (w.Rand(0x0047EBCB) % 0x280 + 0x280) * sgn;
        y = (w.Rand(0x0047EBE9) % 0x1E0 + 0x1E0) * sgn;
    }
    const float k1 = F32(w.exe, 0x005691C0), k2 = F32(w.exe, 0x005691BC);
    const float p[3] = { static_cast<float>(x) * k1 * k2, static_cast<float>(y) * k1 * k2, -4.0f };
    mx::Transform(w.cam.inverse, p, s.goal);
}

// 0x0047ECA0: the projectile is inside the screen window
bool OnScreen5(World::Impl& w, const Shot5Task& s) {
    const float k = F32(w.exe, 0x005308D4);
    const float fx = (s.viewPos[0] * k) / s.viewPos[2], fy = (s.viewPos[1] * k) / s.viewPos[2];
    return F32(w.exe, 0x005691D0) <= fx && fx <= F32(w.exe, 0x005691CC) && F32(w.exe, 0x005691C8) <= fy &&
           fy <= F32(w.exe, 0x005691C4);
}

// 0x0047F320: the projectile reaches the players: Player_TakeHit (by screen side); +0x136C |= 0x40 when nobody lost
// a life, else the boss's aggression - 3
void HitPlayers(World::Impl& w, Shot5Task& s) {
    const int l0 = w.player[0].lives, l1 = w.player[1].lives;  // 0x009A5C66 / 0x009A5D96
    if (w.route == 1) {
        if (TargetValid(w, 0)) w.PlayerTakeHit(0, 1, 7);
        if (TargetValid(w, 1)) w.PlayerTakeHit(1, 1, 7);
    } else if ((s.viewPos[0] * w.focal) / s.viewPos[2] < static_cast<float>(F64(w.exe, 0x00564770))) {
        if (TargetValid(w, 1)) w.PlayerTakeHit(1, 1, 7);
    } else {
        if (TargetValid(w, 0)) w.PlayerTakeHit(0, 1, 7);
    }
    if (l0 <= w.player[0].lives && l1 <= w.player[1].lives) {
        s.flags136C |= 0x40;
        return;
    }
    if (Boss5Task* b = Alive(w, s.boss)) Aggr(*b, -3);
}

void ShotState0(World::Impl& w, Shot5Task& s, Boss5Task* b) {  // 0x0047E440: follows the boss's hand until thrown
    s.flags34 |= 0x8000;
    bool launch = false, grow = true;
    if (s.sub == 0) {
        int node = 0, motion = 0, tick = 0;
        switch (s.kind) {
        case 0:
            node = 5;
            if (b && b->c132C == 2) motion = 0x82, tick = 0x1D;
            else motion = 0x8C, tick = 0x46;
            break;
        case 1: node = 8, motion = 0x8B, tick = 0x46; break;
        case 2: node = 8, motion = 0x84, tick = 0x2F; break;
        case 4: node = 8, motion = 0x94, tick = 0xA5; break;
        default: w.Note("stage-5 boss projectile kind " + std::to_string(s.kind) + " (0x0047E440 default)"); break;
        }
        if (b) {
            if (s.kind == 4) {
                float a[3], c[3];
                NodePoint(w, *b, 8, a);
                NodePoint(w, *b, 5, c);
                const float h = F32(w.exe, 0x004C43AC);
                for (int i = 0; i < 3; ++i) s.pos[i] = (a[i] - c[i]) * h + c[i];
            } else {
                NodePoint(w, *b, node, s.pos);
            }
        }
        if (s.kind != 2) {
            if (b && ((b->anim.motion[0] == motion && b->anim.tick[0] == tick) ||
                      (b->anim.motion[0] == 0x8A && b->anim.tick[0] == 0x5A))) {
                ++s.sub;
                launch = true;
            }
        } else if (!b || !(b->flags34 & 0x10000000) || (b->flags34 & 0x40000000)) {
            s.behaviour = 3;
            s.sub = 0;
        }
    } else if (s.sub == 1) {
        launch = true;
    }
    if (launch) {
        const float sc = s.scale * F32(w.exe, 0x004C4D10);
        s.scale = sc;
        const float r = b ? sc * BossPF(*b, 0x14) : sc;
        s.radius = r;
        if (r < F32(w.exe, 0x004C4CC8)) s.radius = Bits(0x3DCCCCCD);
        AimPoint(w, s, s.goal);  // 0x0047F190
        if (b && (b->flags136C & 1)) {
            w.Sound(0xD23A9);
            b->flags136C &= ~1u;
        }
        s.flags34 &= ~0x8000u;
        s.behaviour = s.kind == 4 ? 2 : 1;
        s.sub = 0;
    }
    if (grow && --s.growDelay < 1) {  // LAB_0047E78A
        if (0xB33 < ++s.model) s.model = 0xB33;
        const float sc = s.scale + s.growStep;
        s.scale = sc;
        if (s.growTo <= sc) s.growStep = 0.0f;
    }
}

void ShotHpCheck(World::Impl& w, Shot5Task& s, Boss5Task* b) {
    if ((s.flags34 & 8) && --s.hp < 1) {
        s.behaviour = 3;
        s.sub = 0;
        // Source46F372/46F514: the hit bit persists for five task updates.
        // Only a typed destruction advances the final-phase orb contract.
        if (w.typingReferenceActive && b) ++b->typingOrbsCleared;
    }
}

void ShotState1(World::Impl& w, Shot5Task& s, Boss5Task* b) {  // 0x0047E810: flies to the aim point, spinning
    if (s.sub == 0) {
        s.model = 0xB33;
        s.flags34 &= ~0x8000u;
        Zero(s);
        const int agg = b ? static_cast<int8_t>(b->c131E) : 0;
        const int n = w.typingReferenceActive ? TypingMagicianTimingForRank(agg).projectile :
            static_cast<int32_t>(w.exe.U32(0x00596958 + static_cast<uint32_t>(agg) * 4));
        if (s.volley == 2) s.frames = static_cast<int>(static_cast<float>(n) * F32(w.exe, 0x005691B8));
        else s.frames = n;
        const float f = static_cast<float>(s.frames);
        ++s.sub;
        for (int i = 0; i < 3; ++i) s.vel[i] = (s.goal[i] - s.pos[i]) / f;
    } else if (s.sub != 1) {
        ShotHpCheck(w,s,b);
        return;
    }
    for (int i = 0; i < 3; ++i) s.pos[i] = s.vel[i] + s.pos[i];
    s.rot[0] -= w.Rand(0x0047E8E9) & 0xFFF;
    const int r2 = w.Rand(0x0047E8FF) & 0xFFF;
    --s.frames;
    s.rot[2] -= r2;
    if (s.frames < 1) {
        Zero(s);
        HitPlayers(w, s);  // 0x0047F320
        s.behaviour = 3;
        s.sub = 0;
    }
    ShotHpCheck(w,s,b);
}

void ShotState2(World::Impl& w, Shot5Task& s, Boss5Task* b) {  // 0x0047E980: kind 4, to a random point on screen
    if (s.sub == 0) {
        s.flags34 &= ~0x8000u;
        s.model = 0xB33;
        RandomPoint(w, s);  // 0x0047EB30
        Zero(s);
        const uint32_t row = 0x005969A0 + static_cast<uint32_t>(b ? static_cast<int8_t>(b->c131E) : 0) * 0x10;
        int n = w.typingReferenceActive ? TypingMagicianTimingForRank(b ? b->c131E : 0).projectile :
            w.Rand(0x0047E9D6) % static_cast<int32_t>(w.exe.U32(row + 4)) + static_cast<int32_t>(w.exe.U32(row));
        s.frames = n;
        if (s.c131B == 0) {
            s.frames = ((n + ((n >> 31) & 3)) >> 2) + n;
            s.c1320 = s.c1320 / 2;
        }
        const float f = static_cast<float>(s.frames);
        for (int i = 0; i < 3; ++i) s.vel[i] = (s.goal[i] - s.pos[i]) / f;
        if (s.c131B == 1) w.Sound(0xD23A9);
        ++s.sub;
        s.c133C = s.frames / 6;
    } else if (s.sub != 1) {
        ShotHpCheck(w,s,b);
        return;
    }
    for (int i = 0; i < 3; ++i) s.pos[i] = s.vel[i] + s.pos[i];
    const int r = w.Rand(0x0047EAA4) & 0xFFF;
    --s.frames;
    s.rot[0] -= r;
    if (s.frames < 1) {
        Zero(s);
        if (OnScreen5(w, s)) HitPlayers(w, s);
        s.behaviour = 3;
        s.sub = 0;
    }
    ShotHpCheck(w,s,b);
}

void ShotState3(World::Impl& w, Shot5Task& s, Boss5Task* b) {  // 0x0047ED10: fades out, then Actor_Destroy
    if (s.sub == 0) {
        const float sc = s.scale * F32(w.exe, 0x004C4CB8);
        const uint32_t f = s.flags34;
        s.c1320 = 0xFF;
        s.flags34 = f | 0x4008000;
        s.scale = sc;
        s.fadeTo = b ? BossP32(*b, 0x20) : 0;
        const int n = b ? BossP32(*b, 0x1C) : 1;
        s.smokeEvery = n;
        if ((s.flags136C & 0x40) || (s.attack131A == 3 && (f & 8))) s.smokeEvery = (n + ((n >> 31) & 7)) >> 3;
        s.f1340 = static_cast<float>((0xFF - s.fadeTo) / (s.smokeEvery != 0 ? s.smokeEvery : 1));
        if (b && b->behaviour != 9) w.Sound(0x3C16A9);
        if (s.volley != 4 && b) --b->shots;
        ++s.sub;
    } else if (s.sub != 1) {
        return;
    }
    s.c1320 = static_cast<int>(static_cast<float>(s.c1320) - s.f1340);
    if (s.c1320 <= s.fadeTo) {
        if (s.volley == 4 && b) --b->shots;
        w.EnemyDestroy(s);  // 0x00409CC0
    }
}

// 0x0047EFA0
void TaskShot5(Task& t) {
    World::Impl& w = *CurrentWorld();
    Shot5Task& s = t.As<Shot5Task>();
    Boss5Task* b = Alive(w, s.boss);
    if (!(s.flags34 & 0x4000000) && (!b || (b->flags34 & 0x40000000) || b->hp < 1)) {
        s.behaviour = 3;
        s.sub = 0;
    }
    switch (s.behaviour) {
    case 0: ShotState0(w, s, b); break;
    case 1: ShotState1(w, s, b); break;
    case 2: ShotState2(w, s, b); break;
    case 3: ShotState3(w, s, b); break;
    default: break;
    }
    if (!(s.anim.drawFlags & 1)) return;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::RotZ(m, s.rot[2]);
    mx::RotY(m, s.rot[1]);
    mx::RotX(m, s.rot[0]);
    mx::Scale(m, s.scale, s.scale, s.scale);
    s.f138C = 1.0f / static_cast<float>(0xFF / (s.c1320 != 0 ? s.c1320 : 1));
    w.LightColor(1.0f, s.f138C, s.f138C);  // 0x0047F080: 0x004AA0A0(1, f, f), kept for the later draws
    w.Draw(s.model, m);
    mx::Transform(w.cam.view, s.pos, s.viewPos);  // 0x009A6000[ctx] x pos -> +0x70
    w.RegisterTarget(&s);                         // 0x00405160
    if (s.behaviour != 3) {
        int every = s.smokeEvery;
        if (s.kind == 4) {
            if (s.behaviour == 1 || s.behaviour == 2) {
                if (0 < --s.c133C) every = s.count + 2;
            } else {
                every = every << 3;
            }
        }
        if (every <= ++s.count) {
            s.count = 0;
            SpawnSmoke5(w, s);  // 0x0047F400
        }
    }
}

void SpawnShot5(World::Impl& w, Boss5Task& b, int kind) {  // 0x0047EE30
    Shot5Task* s = w.NewTask<Shot5Task>(TaskShot5, 0x0047EFA0);
    const bool large = w.typingReferenceActive && kind == 6;
    if (large) kind = 4;
    if (kind == 4) s->c131B = 1;
    else if (kind == 5) {
        s->c131B = 0;
        kind = 4;
    }
    ++b.shots;
    s->flags34 = 0x80008001u;
    s->anim.drawFlags = 1;  // +0x1F8
    s->boss = { &b, b.serial };
    s->kind = kind;
    s->volley = b.c1360;
    s->index = b.shots;
    s->growTo = kind == 4 ? Bits(0x3FCCCCCD) : Bits(0x3FE00000);
    if (large) s->growTo *= 5.0f;
    s->count = 0;
    s->radius = Bits(0x3DCCCCCD);
    s->hp = s->hpMax = BossP8(b, 0x10);
    if (w.cfg.portTyping && s->c131B == 1) {
        s->hp = s->hpMax = 5;
        s->typingFlags = 0x38000004u;
    }
    s->model = 0xB02;
    s->c1320 = 0xFF;
    s->attack131A = b.attack131A;
    s->behaviour = 0;
    s->sub = 0;
    if (kind == 2) {
        s->smokeEvery = BossP8(b, 0x1A);
        if (b.flags136C & 4) s->smokeEvery = BossP8(b, 0x1A) * 5;
        s->growDelay = 0;
    } else {
        s->smokeEvery = BossP8(b, 0x12);
        s->growDelay = b.c1360 == 2 ? 0x23 : 0;
    }
    s->growStep = 0.5f;
    s->actorSlot = -1;
    s->slot120 = -1;
    RegisterSlot(w, *s);  // 0x00409270
}

// ---------------------------------------------------------------------------------------------------------------------
// The boss
// ---------------------------------------------------------------------------------------------------------------------

// node callback 0x0047F780 (+ model draw 0x0047FC50: lights / hit flash are renderer state)
void Boss5Node(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    Boss5Task& b = static_cast<Boss5Task&>(e);
    const int model = e.anim.nodes[idx & 31].model;
    const bool alt = (e.flags38 & 8) && w.altModels != 0;
    auto draw = [&](int id) {
        // 0x0047FC50 (renderer state): in behaviour 9 (and 10 after sub-state 3) both env blocks' light angles point
        // from this node's view-space point (+0x274 + idx * 0x90 = node +0x68) to node 8's, through the camera matrix,
        // and the light colour is (1, 0.781, 0.565); a hit flash (+0x1334 > 0) makes it (1 or 0, 0, 0) by parity
        if (e.behaviour == 9 || (e.behaviour == 10 && e.sub > 3)) {
            float a[3], c[3];
            const Mat4& cm = w.cam.inverse;  // 0x009A6040 + ctx * 0x1A4
            auto toWorld = [&](const float* v, float* out) {
                for (int k = 0; k < 3; ++k) out[k] = v[0] * cm.m[k] + v[1] * cm.m[4 + k] + v[2] * cm.m[8 + k] + cm.m[12 + k];
            };
            toWorld(e.anim.nodes[idx & 31].hit, a);
            toWorld(e.anim.nodes[8].hit, c);
            int pitch = 0, yaw = 0;
            DirToAngles(c[0] - a[0], c[1] - a[1], c[2] - a[2], &pitch, &yaw);  // 0x004016B0
            w.env.lightPitch = w.env.block1.lightPitch = pitch;  // 0x009A3558 / 5C, 0x009A59F8 / FC
            w.env.lightYaw = w.env.block1.lightYaw = yaw;
            w.LightColor(1.0f, Bits(0x3F47EF9E), Bits(0x3F10A3D7));
        }
        if (b.flash > 0) w.LightColor((b.flash & 1) ? 1.0f : 0.0f, 0.0f, 0.0f);
        w.Draw(id, m, 8, -1.0f, alt, idx);
    };
    const int f = static_cast<int>(w.seedCounter % 0x28);  // 0x009A32A0 % 40
    switch (model) {
    case 0x5B8: draw(0x5B8); draw(f + 0x5B9); return;
    case 0x4EF: draw(f + 0x4EF); draw(f + 0x517); return;
    case 0x44A: draw(0x44A); draw(f + 0x44B); return;
    case 0x49B: draw(0x49B); draw(f + 0x49C); return;
    case 0x4C6: draw(0x4C6); draw(f + 0x4C7); return;
    case 0x53F:
        if (--b.flash < 0) b.flash = 0;
        draw(0x53F);
        draw(f + 0x540);
        return;
    case 0x568: draw(f + 0x568); draw(f + 0x590); return;
    case 0x5E6: draw(f + 0x5E6); draw(f + 0x60E); return;
    case 0x636: draw(0x636); draw(f + 0x637); return;
    case 0x65F: draw(0x65F); draw(f + 0x660); return;
    case 0x689: draw(0x689); draw(f + 0x68A); return;
    case 0x6B2: draw(0x6B2); draw(f + 0x6B3); return;
    case 0x6DB: draw(f + 0x6DB); draw(f + 0x703); return;
    default: draw(model); return;
    }
}

// 0x0047CE10: one player's hit: slots 4 / 6 / 11 / 13 take damage (P s8 4, x 0x005644F4 with two players), others spark
void HitNode(World::Impl& w, Boss5Task& b, int p) {
    const int slot = w.hitSlot[p];
    float pt[3];
    NodePoint(w, b, slot, pt);
    if (b.flags34 & 0x100) {
        w.SpawnEffectAt(pt, 0, 0, 0x35, 1, p);  // 0x00407340(pt, 0x35, 1, p)
        return;
    }
    if (b.flags34 & 0x4000000) return;
    switch (w.typingReferenceActive ? 4 : slot) {
    case 0:
        break;
    case 4: case 6: case 0xB: case 0xD: {
        w.SpawnEffectAt(pt, 0, 0, 0x50, 2, p);
        w.Sound(0x1E16A9);
        w.AddScore(p, 10);
        float dmg = static_cast<float>(b.record.ParamS8(4));
        if (w.players == 2) dmg = dmg * F32(w.exe, 0x005644F4);
        if (w.typingReferenceActive || (w.cfg.portTyping && b.typingStrike)) {
            b.typingStrike = false;
            b.hp = static_cast<int16_t>(b.hp - int(TypingMagicianWordDamage(b.behaviour)));
        } else if (w.runner->cfg.gameType == 1) {
            // 0x0047CF38: x the shooter's item factor (bits 0xBF800000: x 2), _ftol(hp - dmg) at 53-bit precision; the
            // two-player product stays on the x87 stack (not rounded to float)
            double d = static_cast<double>(b.record.ParamS8(4));
            if (w.players == 2) d = d * static_cast<double>(F32(w.exe, 0x005644F4));
            float factor = 1.0f, boost = 0.0f;
            if (ItemDamage(w, p, &factor, &boost)) d = factor == -1.0f ? d + d : d * static_cast<double>(factor);
            if (b.typingStrike) {
                const float floor = std::max(0.0f, std::ceil(PhaseThr(w, b)) - 1.0f);
                d = std::min(TypingDamage(b, static_cast<float>(d)), std::max(0.0f, static_cast<float>(b.hp) - floor));
            }
            b.hp = static_cast<int16_t>(static_cast<int>(static_cast<double>(b.hp) - d));
        } else {
            if (b.typingStrike) {
                const float floor = std::max(0.0f, std::ceil(PhaseThr(w, b)) - 1.0f);
                dmg = std::min(TypingDamage(b, dmg), std::max(0.0f, static_cast<float>(b.hp) - floor));
            }
            b.hp = static_cast<int16_t>(static_cast<int>(static_cast<float>(b.hp) - dmg));
        }
        break;
    }
    default:
        w.SpawnEffectAt(pt, 0, 0, 0x35, 1, p);
        break;
    }
    if (b.hp < 1) {
        b.flags34 |= 0x4000000;
        w.AddScore(p, 0x5DC);
        if (w.cfg.portTyping) w.TypingController(b.typingPlayer).addBasePoints(1300);
        b.killer = static_cast<int8_t>(p);
        w.bossActive = 0;
    }
}

// 0x0047CC20 (+ 0x0047CD40): this frame's hits; a damaging hit below the phase threshold (or the hit count of
// behaviour 8) staggers (behaviour 5), death -> behaviour 2
void Shots(World::Impl& w, Boss5Task& b) {
    const int16_t before = b.hp;
    if (!(b.flags34 & 8)) return;
    // 0x0047CD40: Shot_PlayerOrder 0x004093C0, then every player's hit
    w.shotOrder[0] = w.shotOrder[1] = -1;
    if (w.playersInGame == 0) w.shotOrder[0] = 0;
    else if (w.playersInGame == 1) w.shotOrder[0] = 1;
    else if (w.playersInGame == 2) {
        if (w.Rand(0x004093E2) & 1) w.shotOrder[0] = 0, w.shotOrder[1] = 1;
        else w.shotOrder[0] = 1, w.shotOrder[1] = 0;
    }
    for (int i = 0; i < 2; ++i) {
        const int p = w.shotOrder[i];
        if (p == -1) continue;
        w.hitSlot[p] = 0;
        const uint32_t bit = 1u << (p + 1);
        if (b.flags34 & bit) {
            w.hitSlot[p] = b.hitNode[p];
            HitNode(w, b, p);
            EnemyNode& n = b.anim.nodes[w.hitSlot[p] & 31];
            n.flags &= ~8u;
            n.flags &= ~bit;
        }
        b.flags34 &= ~8u;
        b.flags34 &= ~bit;
    }
    const uint32_t f = b.flags34;
    if (f & 0x100) return;
    if (f & 0x4000000) {
        b.flash = 8;
        b.flags136C |= 2;
        b.behaviour = 2;
        b.sub = 0;
        return;
    }
    if (b.hp < before) {
        b.flash = 8;
        if (b.attack131A == 1) {
            if (0 < --b.hitsLeft) return;
        } else if (PhaseThr(w, b) <= static_cast<float>(b.hp)) {
            return;
        }
        b.flags34 = f | 0x100;
        b.c131B = static_cast<int8_t>(b.behaviour);
        b.behaviour = 5;
        b.sub = 0;
    }
}

// ---- states ----

void St0(World::Impl& w, Boss5Task& b) {  // 0x00480050
    const int tick = w.runner->camera.tick;
    if (b.sub == 0) {
        b.flags34 |= 0x4000;
        ++b.sub;
    }
    if (b.sub == 1) {
        if (0x1A3 < tick) {
            float p[3];
            int a[3];
            if (EvalPath(w, w.typingReferenceActive ? 0x41A2 : 0x180, static_cast<float>(tick), p, a)) {
                memcpy(b.pos, p, 12);
                memcpy(b.rot, a, 12);
            }
        }
        if (0x256 < tick) ++b.sub;
        else return;
    }
    if (b.sub == 2) {
        if (w.runner->flags[0x16] == 0) return;
        ++b.sub;
    }
    if (b.sub == 3) {
        if (w.runner->flags[0x17] == 0) return;
        ++b.sub;
    }
    if (b.sub == 4) {
        b.behaviour = 1;
        b.sub = 0;
    }
}

void St1(World::Impl& w, Boss5Task& b) {  // 0x0047D120
    if (b.sub == 0) {
        b.flags34 |= 0x100;
        w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
        b.goal[0] = Bits(0x4410C000);
        b.goal[1] = Bits(0xC2820000);
        b.goal[2] = Bits(0xC60A0000);
        b.count = 0x91;
        Zero(b);
        ++b.sub;
        VelTo(b, b.count);
    } else if (b.sub != 1) {
        return;
    }
    if (--b.count < 1) {
        b.flags34 &= ~0x100u;
        Zero(b);
        b.c131E = static_cast<int8_t>(w.rank);  // 0x0040A8A0
        b.c132C = 0;
        b.f138C = 0;
        b.behaviour = PhaseBehaviour(w,b);
        b.typingHopsStarted = true; // Source46D916: global813080 = 1.
        b.sub = 0;
        w.bossActive = 1;
    }
}

void St2(World::Impl& w, Boss5Task& b) {  // 0x00480140: death
    switch (b.sub) {
    case 0:
        Zero(b);
        b.flags34 |= 0x100;
        w.Sound(0xC23A9);
        w.Sound(0x80000000);
        b.count = 0x3C;
        w.letterbox = 3;
        ++b.sub;
        [[fallthrough]];
    case 1:
        if (0 < --b.count) return;
        w.AnimBlendTo(b, 0x8F, 0, 4);
        ++b.sub;
        [[fallthrough]];
    case 2:
        if (b.anim.tick[0] <= 0x1E) return;
        w.Sound(0x1023A9);
        ++b.sub;
        b.count = BossP32(b, 0x2C);
        [[fallthrough]];
    case 3:
        if (Ticks(w, b) - 2 <= b.anim.tick[0]) w.AnimBlendTo(b, 0x8F, 0x1E, 1);
        if (b.count % BossP32(b, 0x34) == 0) SpawnSpark5(w, b);  // 0x004805C0
        if (--b.count < 1) {
            b.behaviour = 3;
            b.sub = 0;
        }
        return;
    default:
        return;
    }
}

void St3(World::Impl& w, Boss5Task& b) {  // 0x00480290
    if (b.sub != 0) {
        w.Note("stage-5 boss state 3 sub 1 (0x00480290) not ported");
        return;
    }
    b.attack131A = -1;
    b.flags34 = (b.flags34 & ~0x4000u) | 0x10100;
    w.AnimBlendTo(b, 0x8E, 0, 4);
    Zero(b);
    --w.liveEnemies;
    --w.attackers;
    if (b.slot120 != -1) w.lockOn.Release(b.slot120);  // 0x009A5EC0[slot] = 0
    ReleaseTarget(w, b);  // 0x0047FFE0
    w.Sound(0xC23A9);
    w.runner->camera.settled = true;     // 0x009C6F2D
    w.runner->camera.returning = false;  // 0x009C6F2C
    b.pos[0] = Bits(0x44110000);
    b.pos[1] = Bits(0xC20C0000);
    b.pos[2] = Bits(0xC60A036C);
    b.behaviour = 4;
    b.sub = 0;
}

void St4(World::Impl& w, Boss5Task& b) {  // 0x00480470
    if (b.anim.motion[0] != 0x8F || w.exe.S16(0x004E08EE) - 2 <= b.anim.tick[0]) w.AnimBlendTo(b, 0x8F, 0x1E, 1);
    switch (b.sub) {
    case 0:
        b.flags34 |= 0x10000;
        w.runner->flags[0x18] = 1;
        ++b.sub;
        b.rot[1] = 0x8000;
        return;
    case 1: {
        const int tick = w.runner->camera.tick;
        float p[3];
        int a[3];
        if (EvalPath(w, 0x181, static_cast<float>(tick), p, a)) memcpy(b.pos, p, 12);
        if (tick != 0xAA) return;
        ++b.sub;
    }
        [[fallthrough]];
    case 2: {
        b.flags136C &= ~0x20u;
        FxTask* k = w.NewTask<FxTask>(TaskBlast5, 0x00480810);  // 0x004807B0
        k->boss = { &b, b.serial };
        memcpy(k->pos, b.body, 12);
        k->model = 0x7F4;
        k->scale = 1.0f;
        ++b.sub;
        b.count = 0x78;
    }
        [[fallthrough]];
    case 3:
        if (0 < --b.count) return;
        ++b.sub;
        b.count = 0xB4;
        [[fallthrough]];
    case 4:
        if (0 < --b.count) return;
        w.runner->flags[0x1E] = 1;
        BossRecords(w);  // 0x00425F40: boss-mode (game type 3) records (world_modes_boss.cpp)
        w.EnemyDestroy(b);  // 0x00409CC0
        return;
    default:
        return;
    }
}

void St5(World::Impl& w, Boss5Task& b) {  // 0x0047CA50: stagger
    if (b.sub == 0) {
        const int8_t a = b.attack131A;
        if (-1 < a && (a < 2 || a == 3)) {
            w.Sound(0x1123A9);
            w.AnimBlendTo(b, PhaseThr(w, b) <= static_cast<float>(b.hp) ? 0x88 : 0x89, 0, 4);
        }
        b.flags34 |= 0x40000000;
        Zero(b);
        ReleaseTarget(w, b);
        ++b.sub;
    } else if (b.sub != 1) {
        return;
    }
    if (b.anim.tick[0] <= Ticks(w, b) - 2) return;
    w.AnimBlendTo(b, 0x8D, 0, 4);
    const int8_t a = b.attack131A;
    b.flags34 &= ~0x40000000u;
    if (a == 0) {
        b.behaviour = w.typingReferenceActive ? 8 : 6;
        b.sub = 1;
    } else if (a == 1) {
        b.behaviour = 8;
        b.sub = 1;
    } else if (a == 3) {
        b.behaviour = 10;
        b.sub = 3;
    }
    if (static_cast<float>(b.hp) < PhaseThr(w, b)) {
        ++b.phase;
        b.sub = 0;
        b.behaviour = PhaseBehaviour(w,b);
    }
    b.c131B = -1;
}

void St6(World::Impl& w, Boss5Task& b) {  // 0x0047D220: move to one of four points around the players
    const int every = BossP8(b, 0x19);
    switch (b.sub) {
    case 0:
        b.attack131A = 0;
        b.shots = 0;
        b.c1354 = w.Rand(0x0047D257) & 3;
        ++b.sub;
        b.c1360 = -1;
        b.f138C = 0;
        [[fallthrough]];
    case 1: {
        b.flags34 &= ~0x100u;
        w.AnimBlendTo(b, 0x8E, 0, 4);
        const int r = w.Rand(0x0047D2A5) & 1;
        b.c1354 = b.c1354 - 2 * r + 1;
        if (b.c1354 < 0) b.c1354 = 3;
        if (3 < b.c1354) b.c1354 = 0;
        const uint32_t row = 0x005967A0 + static_cast<uint32_t>(b.c1354) * 12;
        for (int i = 0; i < 3; ++i) b.goal[i] = w.playerPos[i] + F32(w.exe, row + 4 * i);
        Zero(b);
        b.count = static_cast<int32_t>(w.exe.U32(0x005967D0 + static_cast<uint32_t>(static_cast<int8_t>(b.c131E)) * 4));
        VelTo(b, b.count);
        w.Sound(0xF23A9);
        ++b.sub;
    }
        [[fallthrough]];
    case 2:
        if (++b.c133C % every == 0) SpawnTrail(w, b);  // 0x0047DB50
        if (0 < --b.count) return;
        Zero(b);
        ++b.sub;
        [[fallthrough]];
    case 3:
        if (PickTarget(w, b)) {
            b.behaviour = 7;
            b.sub = 0;
        }
        return;
    default:
        return;
    }
}

void St7(World::Impl& w, Boss5Task& b) {  // 0x0047D410: a volley
    if (b.sub == 0) {
        const int n = b.c1360 + 1;
        b.flags34 &= ~0x4100u;
        b.flags136C |= 1;
        b.c1360 = n;
        if (2 < n) b.c1360 = 0;
        if (b.c132C == 2) b.c1360 = 0;
        const bool two = w.players == 2;
        switch (b.c1360) {
        case 0:
            w.AnimBlendTo(b, b.c132C == 2 ? 0x82 : 0x8C, 0, 4);
            SpawnShot5(w, b, 0);
            SpawnShot5(w, b, 0);
            if (two) {
                SpawnShot5(w, b, 0);
                SpawnShot5(w, b, 0);
            }
            break;
        case 1:
            w.AnimBlendTo(b, 0x8B, 0, 4);
            SpawnShot5(w, b, 1);
            SpawnShot5(w, b, 1);
            if (two) {
                SpawnShot5(w, b, 1);
                SpawnShot5(w, b, 1);
            }
            break;
        case 2:
            w.AnimBlendTo(b, 0x8A, 0, 4);
            SpawnShot5(w, b, 0);
            SpawnShot5(w, b, 0);
            SpawnShot5(w, b, 1);
            SpawnShot5(w, b, 1);
            break;
        default:
            break;
        }
        ReleaseTarget(w, b);
        ++b.sub;
    } else if (b.sub != 1 && b.sub != 2) {
        return;
    }
    if (b.sub == 1) {
        if (b.anim.tick[0] < Ticks(w, b) - 2) return;
        w.AnimBlendTo(b, 0x8E, 0, 4);
        ++b.sub;
    }
    if (b.shots < 1) {
        b.behaviour = 6;
        b.sub = 1;
    }
}

void St8(World::Impl& w, Boss5Task& b) {  // 0x0047D5E0: hops between points, then the charge
    const int every = BossP8(b, 0x19);
    // (0x00596848[aggression] is read where it is used: case 1 changes the aggression first)
    auto row = [&] { return 0x00596848 + static_cast<uint32_t>(static_cast<int8_t>(b.c131E)) * 0x10; };
    auto timing = [&] { return TypingMagicianTimingForRank(b.c131E); };
    switch (b.sub) {
    case 0:
        b.attack131A = 1;
        b.c1354 = 0;
        if (w.typingReferenceActive) b.shots = 0;
        else b.c1360 = 0;
        b.c1338 = -1;
        b.f138C = 0;
        ++b.sub;
        [[fallthrough]];
    case 1:
        if (w.typingReferenceActive) {
            b.flags34 &= ~0x4100u;
            w.TypingWords(0,600); // Source46DD5E: every hop window, including after a stagger.
        } else b.flags34 |= 0x100;
        if (b.anim.motion[0] != 0x8E) w.AnimBlendTo(b, 0x8E, 0, 4);
        if (w.players == 0) return;
        ReleaseTarget(w, b);
        b.c135C = w.typingReferenceActive && b.typingHopsStarted ? -2 : 0;
        b.c1358 = 1 - 2 * (w.Rand(0x0047D682) & 1);
        b.hitsLeft = w.typingReferenceActive || w.players == 1 ? 1 : 2;
        Aggr(b, 1);
        ++b.sub;
        [[fallthrough]];
    case 2: {
        b.c1354 += b.c1358;
        if (b.c1354 < 0) b.c1354 = 3;
        if (3 < b.c1354) b.c1354 = 0;
        const uint32_t p = 0x00596818 + static_cast<uint32_t>(b.c1354) * 12;
        for (int i = 0; i < 3; ++i) b.goal[i] = w.playerPos[i] + F32(w.exe, p + 4 * i);
        b.count = w.typingReferenceActive ? timing().move : static_cast<int32_t>(w.exe.U32(row()));
        Zero(b);
        VelTo(b, b.count);
        w.Sound(0xF23A9);
        ++b.sub;
    }
        [[fallthrough]];
    case 3:
        if (++b.c133C % every == 0) SpawnTrail(w, b);
        if (0 < --b.count) return;
        Zero(b);
        ++b.sub;
        b.count = w.typingReferenceActive ? timing().hold : static_cast<int32_t>(w.exe.U32(row() + 4));
        [[fallthrough]];
    case 4:
        if (!w.typingReferenceActive || b.typingMistypePlayer == -1) {
            if (0 < --b.count) return;
            if (++b.c135C < (w.typingReferenceActive ? timing().repeats : static_cast<int32_t>(w.exe.U32(row() + 8)))) {
                b.sub = 2;
                return;
            }
        }
        ++b.sub;
        [[fallthrough]];
    case 5:
        if (PickTarget(w, b)) {
            b.behaviour = 9;
            b.sub = 0;
        }
        return;
    default:
        return;
    }
}

void St9(World::Impl& w, Boss5Task& b) {  // 0x0047D890: the charge at the players
    if (w.typingReferenceActive && b.typingMistypePlayer != -1 && !(w.frameCounter2BAC & 8))
        w.TypingSprite(0xB84,320,340,1,1,0xA,0x004B2960);
    if (b.sub == 0) {
        if (w.typingReferenceActive) b.flags34 |= 0x10000100u;
        else b.flags34 = (b.flags34 & ~0x100u) | 0x10000000;
        const float pp[3] = { w.playerPos[0], w.playerPos[1] + F32(w.exe, 0x004C4CB8), w.playerPos[2] };
        float v[3];
        mx::Transform(w.cam.view, pp, v);  // 0x009A6000[ctx]
        v[2] = v[2] - F32(w.exe, 0x005691B4);
        float o[3];
        mx::Transform(w.cam.inverse, v, o);  // 0x009A6040[ctx]
        memcpy(b.goal, o, 12);
        w.AnimBlendTo(b, 0x84, 0, 4);
        const int n = w.typingReferenceActive ? TypingMagicianTimingForRank(b.c131E).charge :
            static_cast<int32_t>(w.exe.U32(0x00596854 + static_cast<uint32_t>(static_cast<int8_t>(b.c131E)) * 0x10));
        Zero(b);
        b.count = n;
        VelTo(b, n);
        SpawnShot5(w, b, 2);
        ++b.sub;
    } else if (b.sub != 1 && b.sub != 2) {
        return;
    }
    if (b.sub == 1) {
        --b.count;
        if (b.count < 0x1E && (w.seedCounter & 1)) w.Sound(0x4817A9);
        if (b.anim.tick[0] == 0x19) w.AnimBlendTo(b, 0x84, 0x1A, b.count - 0x16);
        if (0 < b.count) return;
        Zero(b);
        w.Sound(0xD23A9);
        const bool v0 = TargetValid(w, 0) && (!w.typingReferenceActive || b.typingMistypePlayer == -1 || b.typingMistypePlayer == 0);
        if (v0) w.PlayerTakeHit(0, 1, 7);
        if (TargetValid(w, 1) && (!w.typingReferenceActive || b.typingMistypePlayer == -1 || b.typingMistypePlayer == 1)) {
            w.PlayerTakeHit(1, 1, 7);
            Aggr(b, -3);
        } else if (v0) {
            Aggr(b, -3);
        }
        b.typingMistypePlayer = -1;
        ++b.sub;
    }
    if (Ticks(w, b) - 2 <= b.anim.tick[0]) {
        b.behaviour = 8;
        b.sub = 1;
        b.flags34 &= ~0x10000000u;
    }
}

void TypingFinalBeam(World::Impl& w, Boss5Task& b) {
    if (b.anim.tick[0] >= 0x95 || w.seedCounter % 0x2D == 0) return;
    FxTask* k = w.NewTask<FxTask>(TaskBeam, 0x0047E2B0);
    k->boss = {&b,b.serial};
    k->rot[2] = w.Rand(0x0047E27A) & 0xFFFF;
    k->a1350 = 0x1279; k->a1354 = 0x127A; k->a1358 = 0x1299;
}

// Supplied English PC46E500: clear three small words to expose the single
// large word; only that second completion hits the boss. Failed volleys retry.
void TypingSt10(World::Impl& w, Boss5Task& b) {
    switch (b.sub) {
    case 0:
        b.typingFlags |= 0x400u;
        b.flags34 |= 0x100u;
        b.attack131A = 3;
        b.goal[0] = Bits(0x44110000); b.goal[1] = Bits(0xC0400000); b.goal[2] = Bits(0xC6079400);
        b.sub = 20;
        [[fallthrough]];
    case 20:
        b.typingInterlude = w.cfg.gameType == 3 ? 180 : 0;
        for (int i=0;i<3;++i) b.typingInterludeStep[i] = (w.env.lightColor[i]-.4f)*(1.0f/30.0f);
        b.sub = 21;
        [[fallthrough]];
    case 21: {
        // 424CB0/424E90, also used for Hierophant's phase instructions.
        const int frame = b.typingInterlude;
        if (w.cfg.gameType != 3) {
            if (frame < 30 || frame > 150)
                for (int i=0;i<3;++i) w.env.lightColor[i] += (frame < 30 ? -1.0f : 1.0f)*b.typingInterludeStep[i];
            else {
                if (frame == 30) w.TypingSound(0x436A9);
                for (int i=0;i<4;++i) w.TypingSprite(0xDB1+i,128.0f*float(i+1),334,1,.1f,0xA,0x00424D30);
            }
        }
        if (++b.typingInterlude < 180) return;
        b.sub = 1; b.count = 30;
        Zero(b); VelTo(b,b.count);
        w.AnimBlendTo(b,0x8D,0,4); w.Sound(0xF23A9);
        b.flags136C |= 0x20;
        FxTask* k = w.NewTask<FxTask>(TaskGlow,0x0047E130);
        k->boss = {&b,b.serial}; memcpy(k->pos,b.pos,12);
        k->a1350=0xB3; k->a1354=0xB4; k->a1358=199;
        return; // Source46E611 returns before the first movement decrement.
    }
    case 1:
        if (++b.c133C % BossP8(b,0x19) == 0) SpawnTrail(w,b);
        if (--b.count > 0) return;
        Zero(b); w.AnimBlendTo(b,0x8D,0,4);
        b.sub=2; b.count=150;
        [[fallthrough]];
    case 2:
        if (w.players == 0 || --b.count > 0) return;
        b.f138C=0; b.flags34 &= ~0x100u; b.sub=3;
        [[fallthrough]];
    case 3:
        if (w.players == 0) return;
        b.vel[1]=Bits(0x3E4CCCCD);
        w.AnimBlendTo(b,0x94,0,4); w.Sound(0xB23A9);
        b.sub=4; b.c1328=0;
        [[fallthrough]];
    case 4:
        if (b.anim.tick[0] != 165) {
            TypingFinalBeam(w,b);
            if (w.seedCounter % 15 == 1 && b.shots < 10)
                SpawnShot5(w,b,++b.c1328 <= 3 ? 4 : 5);
            return;
        }
        w.Sound(0xC23A9); Zero(b);
        w.TypingWords(0,w.players == 2 ? 300 : 200);
        b.typingOrbsCleared=0; b.sub=5;
        return;
    case 5:
        if (b.typingOrbsCleared == 3) {
            b.typingOrbsCleared=0; b.sub=8;
            return;
        }
        if (b.anim.tick[0] < Ticks(w,b)-2) return;
        if (b.anim.motion[0] != 0x8D) w.AnimBlendTo(b,0x8D,0,4);
        if (b.shots > 0) return;
        b.count=40; Zero(b); VelTo(b,b.count); b.sub=6;
        [[fallthrough]];
    case 6:
        if (--b.count > 0) return;
        Zero(b); b.flags34 &= ~0x4000u; b.sub=7;
        [[fallthrough]];
    case 7:
        Aggr(b,1); b.sub=3;
        return;
    case 8:
        if (w.players == 0) return;
        w.TypingWords(0,700);
        b.vel[1]=Bits(0x3E4CCCCD);
        w.AnimBlendTo(b,0x94,0,4); w.Sound(0xB23A9);
        b.sub=9; b.c1328=0;
        [[fallthrough]];
    case 9:
        if (b.anim.tick[0] != 165) {
            TypingFinalBeam(w,b);
            if (b.anim.tick[0] == 20) SpawnShot5(w,b,6);
            return;
        }
        w.Sound(0xC23A9); Zero(b); b.sub=10;
        return;
    case 10:
        if (b.typingOrbsCleared != 0) {
            b.typingOrbsCleared=0;
            b.flags34=(b.flags34 & ~0x100u)|0xEu;
            // The PC hit routine applies its fixed twenty damage for each
            // active player; its final attack deliberately supplies both bits.
            b.hitNode[0]=b.hitNode[1]=4;
        }
        if (b.anim.tick[0] < Ticks(w,b)-2) return;
        if (b.anim.motion[0] != 0x8D) w.AnimBlendTo(b,0x8D,0,4);
        if (b.shots > 0) return;
        b.count=40; Zero(b); VelTo(b,b.count); b.sub=11;
        [[fallthrough]];
    case 11:
        if (--b.count > 0) return;
        Zero(b); b.flags34 &= ~0x4000u; b.sub=12;
        [[fallthrough]];
    case 12:
        Aggr(b,1); b.sub=3;
        return;
    default: return;
    }
}

void St10(World::Impl& w, Boss5Task& b) {  // 0x0047DD50: the final phase
    if (w.typingReferenceActive) { TypingSt10(w,b); return; }
    const int every = BossP8(b, 0x19);
    switch (b.sub) {
    case 0:
        b.attack131A = 3;
        b.flags34 |= 0x100;
        b.goal[0] = Bits(0x44110000);
        b.goal[1] = Bits(0xC0400000);
        b.goal[2] = Bits(0xC6079400);
        b.count = 0x1E;
        Zero(b);
        VelTo(b, b.count);
        w.AnimBlendTo(b, 0x8D, 0, 4);
        w.Sound(0xF23A9);
        b.flags136C |= 0x20;
        {  // 0x0047E0D0: the glow task
            FxTask* k = w.NewTask<FxTask>(TaskGlow, 0x0047E130);
            k->boss = { &b, b.serial };
            memcpy(k->pos, b.pos, 12);
            k->a1350 = 0xB3;
            k->a1354 = 0xB4;
            k->a1358 = 199;
        }
        ++b.sub;
        [[fallthrough]];
    case 1:
        if (++b.c133C % every == 0) SpawnTrail(w, b);
        if (0 < --b.count) return;
        Zero(b);
        w.AnimBlendTo(b, 0x8D, 0, 4);
        ++b.sub;
        b.count = 0x96;
        [[fallthrough]];
    case 2:
        if (w.players == 0) return;
        if (0 < --b.count) return;
        b.f138C = 0;
        ++b.sub;
        b.flags34 &= ~0x100u;
        [[fallthrough]];
    case 3:
        if (w.players == 0) return;
        b.vel[1] = Bits(0x3E4CCCCD);
        w.AnimBlendTo(b, 0x94, 0, 4);
        w.Sound(0xB23A9);
        ++b.sub;
        b.c1328 = 0;
        [[fallthrough]];
    case 4:
        if (b.anim.tick[0] != 0xA5) {
            if (b.anim.tick[0] < 0x95 && w.seedCounter % 0x2D != 0) {  // 0x0047E250: a beam
                FxTask* k = w.NewTask<FxTask>(TaskBeam, 0x0047E2B0);
                k->boss = { &b, b.serial };
                k->rot[2] = w.Rand(0x0047E27A) & 0xFFFF;
                k->a1350 = 0x1279;
                k->a1354 = 0x127A;
                k->a1358 = 0x1299;
            }
            if (w.seedCounter % 0xF == 1) {
                const uint32_t row = 0x005969A8 + static_cast<uint32_t>(static_cast<int8_t>(b.c131E)) * 0x10;
                if (b.shots < static_cast<int32_t>(w.exe.U32(row))) {
                    int n = static_cast<int32_t>(w.exe.U32(row + 4));
                    if (w.players == 2) n += 3;
                    if (n < ++b.c1328) SpawnShot5(w, b, 5);
                    else SpawnShot5(w, b, 4);
                }
            }
            return;
        }
        w.Sound(0xC23A9);
        Zero(b);
        ++b.sub;
        [[fallthrough]];
    case 5:
        if (b.anim.tick[0] < Ticks(w, b) - 2) return;
        if (b.anim.motion[0] != 0x8D) w.AnimBlendTo(b, 0x8D, 0, 4);
        if (0 < b.shots) return;
        b.count = 0x28;
        Zero(b);
        ++b.sub;
        VelTo(b, b.count);
        [[fallthrough]];
    case 6:
        if (0 < --b.count) return;
        Zero(b);
        ++b.sub;
        b.flags34 &= ~0x4000u;
        [[fallthrough]];
    case 7:
        Aggr(b, 1);
        b.sub = 3;
        return;
    default:
        return;
    }
}

// 0x0047C960
void TaskBoss5(Task& t) {
    World::Impl& w = *CurrentWorld();
    Boss5Task& b = t.As<Boss5Task>();
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);
    w.bossBar = b.hp < 1 ? 0.0f : static_cast<float>(b.hp) / static_cast<float>(b.hpMax);  // 0x009C8E10
    const float f = b.f138C + F32(w.exe, 0x004C4380);
    b.f138C = f;
    if (!w.typingReferenceActive && F32(w.exe, 0x005691B0) < f) {
        b.c131E = 0x10;
        b.f138C = 0;
    }
    Shots(w, b);  // 0x0047CC20
    switch (b.behaviour) {
    case 0: St0(w, b); break;
    case 1: St1(w, b); break;
    case 2: St2(w, b); break;
    case 3: St3(w, b); break;
    case 4: St4(w, b); break;
    case 5: St5(w, b); break;
    case 6: St6(w, b); break;
    case 7: St7(w, b); break;
    case 8:
        St8(w,b);
        if (w.typingReferenceActive)
            b.typingFrameColor=std::max(0.0f,1.0f-float(b.c135C)/float(TypingMagicianTimingForRank(b.c131E).repeats-1));
        break;
    case 9: St9(w, b); break;
    case 10: case 11: St10(w, b); break;
    default: break;
    }
    if (w.cfg.portTyping && !w.typingReferenceActive && b.behaviour >= 6 && b.behaviour <= 11) {
        const int family = b.behaviour < 8 ? 0 : b.behaviour < 10 ? 1 : 2;
        if (family != b.typingWordFamily) {
            b.typingWordFamily = family;
            w.typing.setWordSelection(0, family == 0 ? 600 : family == 1 ? (w.players == 2 ? 300 : 200) : 700);
            w.typing2.setWordSelection(0, family == 0 ? 600 : family == 1 ? (w.players == 2 ? 300 : 200) : 700);
        }
    }
    for (int i = 0; i < 3; ++i) b.pos[i] = b.vel[i] + b.pos[i];
    int pitch = 0;
    DirToAngles(b.pos[0] - w.playerPos[0], b.pos[1] - w.playerPos[1], b.pos[2] - w.playerPos[2], &pitch, &b.rot[1]);
    // 0x0047FE40: skeleton (lights 0x0041DC70 / 0x0041DCC0: renderer), counters unless frozen (0x4000)
    w.AnimDraw(b);
    w.EnemyShadow(b);
    if (!(b.flags34 & 0x4000)) {
        ++b.anim.counter[0];
        ++b.anim.counter[1];
    }
    ViewPos(w, b);  // 0x00409B70(0)
}

}  // namespace

void TypingBoss5Mistype(World::Impl& w, uint32_t serial, int player) {
    if (!w.typingReferenceActive || player < 0 || player > 1) return;
    for (Task* t : w.ts.Live()) {
        if (t->serial != serial) continue;
        auto* b=dynamic_cast<Boss5Task*>(t->data.get());
        if (b && b->hp > 0 && (b->typingFlags & 0x200000u)) {
            b->flags34 |= 0x100u; // Source4B2940: close the word window.
            b->typingMistypePlayer=player;
        }
        return;
    }
}

// Actor type 0x32 (0x0047F5F0): the spawn task becomes the boss (first run of 0x0047C960 next frame)
void World::Impl::SpawnBoss5(Task& t, const SpawnRecord& r) {
    auto d = FromRecord<Boss5Task>(r, 0x0047C960);
    Boss5Task& b = *d;
    b.anim.motion[0] = 0x8E;
    b.anim.kind = r.ParamS8(0);
    b.actorSlot = -1;
    b.slot120 = -1;
    b.lock = &lockTargets[nextLockTarget++];
    EnemyInitPose(b);   // 0x00410440
    RegisterSlot(*this, b);  // (0x00409270 on 0x009A26A0 = this object)
    b.anim.rotOrder = 1;
    b.nodeFn = Boss5Node;                             // +0x12EC = 0x0047F780
    b.postRootFn = [](World::Impl&, EnemyTask&) {};  // +0x12F0 = 0x00420810
    b.anim.drawFlags |= 0xC;
    b.radius = F32(exe, 0x004C4D28 + static_cast<uint32_t>(b.anim.kind) * 4);
    b.radius128 = 5.0f;
    if (!(b.flags34 & 0x40000)) {
        int pitch = 0, yaw = 0;
        const float dy = static_cast<float>(static_cast<double>(playerPos[1]) + F64(exe, 0x00565DD8));
        DirToAngles(playerPos[0] - b.pos[0], dy - b.pos[1], playerPos[2] - b.pos[2], &pitch, &yaw);
        b.headPitch = pitch & 0xFFFF;
        b.headYaw = yaw & 0xFFFF;
    }
    b.target = -1;
    b.behaviour = r.ParamS8(2);
    b.sub = 0;
    if (runner->cfg.gameType == 3) {  // boss mode: placed on camera path 0x180 at 598, sub-state 3, flag 0x16
        float p[3] = {}, tg[3] = {};
        int roll = 0;
        if (cameraBank.Evaluate(typingReferenceActive ? 0x41A2 : 0x180, 598.0f, p, tg, &roll, false)) {  // 0x004042D0
            memcpy(b.pos, p, 12);
            for (int k = 0; k < 3; ++k) b.rot[k] = static_cast<int>(tg[k]);
        }
        b.sub = 3;
        runner->flags[0x16] = 1;  // 0x009C7216
    }
    ++attackers;
    ++liveEnemies;
    b.serial = CurrentSerial();
    t.data = std::move(d);
    t.fn = TaskBoss5;
    SpawnNameCard(0x00596AC0);  // 0x00437A70
}

bool TypingBoss5(World::Impl& w, ActorTask& actor, TypingTargetSpec& out, bool complete) {
    if (auto* b = dynamic_cast<Boss5Task*>(&actor)) {
        out.boss = true;
        if (b->hp < 1 || b->behaviour < 5 || b->behaviour > 11 || (b->flags34 & 0x4000000u)) return true;
        out.retain = true;
        out.token = 0x500000u | (static_cast<uint32_t>(b->phase) << 12);
        memcpy(out.view, b->anim.nodes[4].hit, 12);
        if (b->behaviour < 6 || (b->flags34 & 0x40008100u)) return true;
        if (w.cfg.portTyping) {
            out.node = 4;
            if (complete) {
                b->flags34 &= ~0x4000u;
                TypingDeliver(*b, out.node);
            }
            return true;
        }
        for (int node : {4, 6, 11, 13}) {
            const EnemyNode& n = b->anim.nodes[node];
            if (n.model != 0 && (n.radius > 0.0f || n.mesh != -1) && (n.flags & 0x20)) {
                out.node = node;
                memcpy(out.view, n.hit, 12);
                break;
            }
        }
        if (complete && out.node >= 0) {
            // The entrance carries a hold bit into the first combat pose. A
            // completed phrase must let the native stagger animation advance;
            // otherwise St5 waits forever at its initial animation frame.
            b->flags34 &= ~0x4000u;
            TypingDeliver(*b, out.node);
        }
        return true;
    }
    if (auto* shot = dynamic_cast<Shot5Task*>(&actor)) {
        out.danger = true;
        out.projectile = false;
        if (w.cfg.portTyping && (shot->kind != 4 || shot->c131B != 1 || (shot->flags34 & 8))) return true;
        if (shot->behaviour < 1 || shot->behaviour > 2 || (shot->flags34 & 0x04008000u)) return true;
        out.node = 1;
        out.token = 0x510000u | (static_cast<uint32_t>(shot->behaviour) << 4) | static_cast<uint32_t>(shot->sub);
        memcpy(out.view, shot->viewPos, 12);
        if (complete) {
            TypingDeliver(*shot, out.node);
            shot->typingStrike = false;
        }
        return true;
    }
    return false;
}

}  // namespace game
