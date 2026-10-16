// game::World - the stage-4 boss: actor type 0x19 (ctor 0x004917E0 -> task 0x004919D0, state table 0x00597298), its
// helpers 0x00491B40 (shots) / 0x004928D0 (phase relocation) / 0x00492350 / 0x004922C0 (arena wall checks) /
// 0x00492460 (footsteps) / 0x00492620 (draw) / 0x00492790 (phases) / 0x00493090 (camera sweeps) / 0x00493330 (walls) /
// 0x004934D0 (aggression) / 0x004935E0 (engine sound), the wound marks 0x004920C0 -> task 0x00492210 and the carried
// objects (state 18: 0x00494F70 -> carried object type 2, world_st4_held.cpp).
// Verification: notes/agents/stage4b.md (world_test --stage-start 44107 on build\scene_capture_auto_st4.txt).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "game/mstack.h"
#include "game/world_held.h"
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

uint8_t U8(const ExeImage& exe, uint32_t va) {
    uint8_t v = 0;
    exe.Read(va, &v, 1);
    return v;
}

// fild i; fmul qword 2pi/65536; fsin / fcos (24-bit precision control)
float SinA(int i) {
    const float a = static_cast<float>(static_cast<double>(i) * mx::kToRad);
    return static_cast<float>(std::sin(static_cast<double>(a)));
}
float CosA(int i) {
    const float a = static_cast<float>(static_cast<double>(i) * mx::kToRad);
    return static_cast<float>(std::cos(static_cast<double>(a)));
}

// fpatan(y, x); fmul 65536/2pi; __ftol; s16
int AngleOf(float y, float x) {
    const float v = static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)) * mx::kToAngle);
    return static_cast<int16_t>(static_cast<int>(v));
}

// Control block (0xA4 bytes, obj +0x1310 = 0x007DD0AC)
struct Boss4Ctl {
    // +0x00: 1 on the vehicle (drawn in its frame), 2 arena walls, 4 node-5 model cycle (0x444..0x447), 8 relocation
    // pending (0x004928D0), 0x10 footsteps, 0x20 wall check (clears +0x34 0x10000), 0x40 wall check (threshold),
    // 0x80 camera sweep requested (0x004932A0), 0x100 / 0x200 node 15 / 12 lifted, 0x400 engine sound on
    uint32_t flags = 0;
    uint8_t state = 0, sub = 0;          // +0x04 / +0x05 (0x00597298 index)
    uint8_t prevState = 0, prevSub = 0;  // +0x06 / +0x07 (flinch return)
    uint8_t phase = 0xFF;                // +0x08 (hp thresholds 0x00570490)
    uint8_t objects = 2;                 // +0x09 carried objects left
    uint8_t taken = 0;                   // +0x0A objects taken (bit per 0x005704F8 entry)
    int8_t level = 0;                    // +0x0B aggression 0..15 (starts at the rank)
    int8_t hits = 0;                     // +0x0C node-2 hits since the last level change
    int8_t lives[2] = {};                // +0x0D / +0x0E players' lives last seen
    EnemyTask* vehicle = nullptr;        // +0x10 (0x009A2C88 when the record's state < 2)
    float camT = 0, camEnd = 0, camStep = 0;  // +0x14 / +0x18 / +0x1C camera sweep (0x00493090)
    int16_t camPath = 0;                 // +0x20
    int16_t camNext = -1;                // +0x22 sweep to start (0x00570538 row)
    float thr = 0;                       // +0x24 hp of the next phase
    float pt[6][3] = {};                 // +0x28.. (0x004932C0: 0 walk target, 1 approach point, 2..5 arena walls)
    float radius = 6.0f;                 // +0x70 view position dy (0x00409B70)
    int c74 = 0, c78 = 0;                // +0x74 / +0x78 next state / counters / camera ticks
    float f84 = 0, f88 = 0;              // +0x84 / +0x88 distance / point
    HeldTask* carried = nullptr;         // +0x94 the object in the hand (state 18)
};

struct Boss4Task : EnemyTask {
    Boss4Ctl c;
    Mat4 drawView = Mat4::Identity();  // the view of the last skeleton evaluation (the original's nodes keep model-view)
    int16_t endCam = 0, endTick = 0;   // params s16 0x40 / 0x42: removed at this camera tick
    bool lightNoted = false;
    int dbgState = -1, dbgPhase = -1;  // HOTD2_BOSS4_DEBUG
};

void NoPostRoot(World::Impl&, EnemyTask&) {}  // 0x00420810

int Ticks(const World::Impl& w, const EnemyTask& e) { return w.MotionTicks(e.anim.motion[0]); }

float Thr(const World::Impl& w, int phase) { return F32(w.exe, 0x00570490 + static_cast<uint32_t>(phase) * 4); }

// ftol((float)hpMax * thr[phase]) stored as a float (+0x24)
void SetThr(World::Impl& w, Boss4Task& b) {
    b.c.thr = static_cast<float>(b.hpMax) * Thr(w, b.c.phase);
}

// (float)hp <= (float)hpMax * thr[phase]
bool AtPhaseHp(const World::Impl& w, const Boss4Task& b, int phase) {
    return static_cast<float>(b.hp) <= static_cast<float>(b.hpMax) * Thr(w, phase);
}

// distance to the players' point in xz
float PlayerDist(const World::Impl& w, const EnemyTask& e) {
    const float dz = e.pos[2] - w.playerPos[2], dx = e.pos[0] - w.playerPos[0];
    return std::sqrt(dz * dz + dx * dx);
}

// 0x00426120(obj, dx, dz, step): turn +0x68 towards (dx, 0, dz) in the actor's frame (0x00426090) by at most step
void TurnTo(World::Impl& w, EnemyTask& e, float dx, float dz, int step) {
    const float v[3] = { dx, 0.0f, dz };
    const int a = w.CivLocalAngle(e, v);
    if (step < a) e.rot[1] += step;
    else if (a < -step) e.rot[1] -= step;
    else e.rot[1] += a;
}
int LocalAngle(World::Impl& w, EnemyTask& e, float dx, float dz) {  // 0x00426090
    const float v[3] = { dx, 0.0f, dz };
    return w.CivLocalAngle(e, v);
}
void TurnToPlayers(World::Impl& w, EnemyTask& e, int step) {
    TurnTo(w, e, e.pos[0] - w.playerPos[0], e.pos[2] - w.playerPos[2], step);
}

// 0x00426170: target player +0x121 (one player: 0x009C7000; two: the one hit last, else by lives with rand)
void PickTarget(World::Impl& w, int8_t& target) {
    target = 0;
    if (w.players == 1) {
        target = static_cast<int8_t>(w.playersInGame);
    } else if (w.players == 2) {
        if (w.player[1].hitTimer != 0) {
            target = 0;
        } else if (w.player[0].hitTimer != 0) {
            target = 1;
        } else {
            const int b = w.player[0].lives <= w.player[1].lives ? 1 : 0;
            const int r = w.Rand(0x004261E0);
            const int n = (w.player[b].lives - w.player[b ^ 1].lives) + 2;
            target = static_cast<int8_t>(r % n == 0 ? (b ^ 1) : b);
        }
    }
}

// one player whose target slot (0x009A2BA0) is taken and who has one life: the boss waits
bool PlayersAttackable(const World::Impl& w) {
    if (w.players == 0) return false;
    if (w.players != 1) return true;
    const int p = w.playersInGame & 1;
    return w.targetTaken[p] == 0 || w.player[p].lives != 1;
}

// 0x00493330(pos, a, b, dist, push): pos on the inner side of the wall a -> b (x / z); 1 = no correction needed
int Wall(float* p, const float* a, const float* b, float d, int push) {
    const float px = p[0] - a[0], pz = p[2] - a[2];
    const float ex = b[0] - a[0], ez = b[2] - a[2];
    float t = (ex * px + ez * pz) / (ex * ex + ez * ez);
    const float qx = t * ex - px, qz = t * ez - pz;
    const float l = std::sqrt(qx * qx + qz * qz);
    if (l == 0.0f) {
        if (d == 0.0f) return 1;
        if (push != 0) {
            if (ez != 0.0f) {
                const float s = ez < 0.0f ? ez * -1.0f : ez;
                p[0] = (ez / s) * d + p[0];
            }
            if (ex != 0.0f) {
                const float s = ex < 0.0f ? ex * -1.0f : ex;
                p[2] = p[2] - (ex / s) * d;
            }
        }
        return 0;
    }
    if (0.0f <= ez * px - ex * pz) {
        if (d <= l) return 1;
        if (push == 0) return 0;
        d = l - d;
    } else {
        if (push == 0) return 0;
        d = l + d;
    }
    p[0] = (d * qx) / l + p[0];
    p[2] = (d * qz) / l + p[2];
    return 0;
}

// 0x00445B10(center, p0, p1, &closest): closest point of the line p0 p1 to center; returns the distance
float ClosestOnLine(const float* c, const float* p0, const float* p1, float* out) {
    const float ex = p1[0] - p0[0], ey = p1[1] - p0[1], ez = p1[2] - p0[2];
    const float dx = p0[0] - c[0], dy = p0[1] - c[1], dz = p0[2] - c[2];
    const float t = -(((dx * ex + dy * ey) + dz * ez) / ((ex * ex + ey * ey) + ez * ez));
    const float ox = t * ex + dx, oy = t * ey + dy, oz = t * ez + dz;
    out[0] = ox + c[0];
    out[1] = oy + c[1];
    out[2] = oz + c[2];
    return std::sqrt((oz * oz + oy * oy) + ox * ox);
}

// 0x00445C00(radius, center, p0, p1, &near, &far): the line's intersections with the sphere (2), touching (1), none (0)
int LineSphere(float r, const float* c, const float* p0, const float* p1, float* a, float* b) {
    float q[3];
    const float d = ClosestOnLine(c, p0, p1, q);
    if (d < r) {
        const float* pf = (q[0] == p0[0] && q[1] == p0[1] && q[2] == p0[2]) ? p1 : p0;
        const float dx = c[0] - pf[0], dy = c[1] - pf[1], dz = c[2] - pf[2];
        float f2, f3, l;
        if (d == 0.0f) {
            l = std::sqrt((dx * dx + dy * dy) + dz * dz);
            f2 = r / l;
            f3 = l + r;
        } else {
            l = std::sqrt(((dx * dx + dy * dy) + dz * dz) - d * d);
            const float s = std::sqrt(r * r - d * d);
            f2 = s / l;
            f3 = s + l;
        }
        f3 = f3 / l;
        for (int i = 0; i < 3; ++i) a[i] = (pf[i] - q[i]) * f2 + q[i];
        for (int i = 0; i < 3; ++i) b[i] = f3 * q[i] - f2 * pf[i];
        return 2;
    }
    if (d == r) {
        memcpy(a, q, 12);
        memcpy(b, q, 12);
        return 1;
    }
    return 0;
}

// node n's world matrix as the original sees it at the start of the frame: the model-view of the last draw times the
// current camera matrix
Mat4 NodeWorldNow(const World::Impl& w, const Boss4Task& b, int n) {
    return b.anim.nodes[n & 31].world * b.drawView * w.cam.inverse;
}

// world y of node n (0x004A9230(camera) * node model-view, translation)
float NodeY(const World::Impl& w, const Boss4Task& b, int n) {
    const Mat4 m = NodeWorldNow(w, b, n);
    return m.m[13];
}

// 0x004932A0(row): request the camera sweep 0x00570538[row]
void CamRequest(Boss4Task& b, int row) {
    b.c.camNext = static_cast<int16_t>(row);
    b.c.flags |= 0x80;
}

// 0x004932C0: the six points of the phase (0x00570640[phase * 6 + i] = {x, z}, y = pos y); flags |= 0x60
void SetupPoints(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    for (int i = 0; i < 6; ++i) {
        const uint32_t row = 0x00570640 + static_cast<uint32_t>(c.phase * 6 + i) * 8;
        c.pt[i][0] = F32(w.exe, row);
        c.pt[i][2] = F32(w.exe, row + 4);
        c.pt[i][1] = b.pos[1];
    }
    c.flags |= 0x60;
}

// 0x00493870 / 0x00493890: the engine sound loop on / off
void SoundOn(World::Impl& w, Boss4Task& b) {
    if (!(b.c.flags & 0x400)) {
        b.c.flags |= 0x410;
        w.Sound(0x4D17A9);
    }
}
void SoundOff(World::Impl& w, Boss4Task& b) {
    if (b.c.flags & 0x400) {
        b.c.flags &= ~0x410u;
        w.Sound(0x4E17A9);
    }
}

// shot ray of player p in view space: origin (0, 0, 0) (0x009A2CE8, never written) and the unit direction (0x009A2CF4)
// ---------------------------------------------------------------------------------------------------------------------
// The wound mark 0x004920C0 -> task 0x00492210: model 0x3CD stuck at the hit point in node `node`'s frame for 0x78 frames
// (shrinking over the last 0x3C); it stays (and stops counting) once the boss is dead
struct WoundTask : ActorTask {
    float pos[3] = {};
    int rot[3] = {};
    Boss4Task* boss = nullptr;
    uint32_t bossSerial = 0;
    int node = 0;
    int count = 0x78;
    Mat4 last = Mat4::Identity();  // the node's last world matrix (the original reads freed boss memory)
    bool dead = false;
};

void TaskWound(Task& t) {
    World::Impl& w = *CurrentWorld();
    WoundTask& k = t.As<WoundTask>();
    bool alive = false;
    if (k.boss)
        for (Task* q : w.ts.Live())
            if (q->serial == k.bossSerial && q->data.get() == k.boss) alive = true;
    if (alive) {
        k.last = k.boss->anim.nodes[k.node].world;
        k.dead = (k.boss->flags34 & 0x4000000) != 0;
    }
    // (Once the boss task is freed the original reads its block's +0x34 wherever the heap reuses it: in
    // scene_capture_orig4_w4 a later task's +0x34 clears the bit and the mark ends at 51838; not modelled - no heap)
    Mat4 m = k.last;
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    if (k.count < 0x3C) {
        const float s = static_cast<float>(k.count) * F32(w.exe, 0x0055CB80);
        mx::Scale(m, s, s, s);
    }
    w.Draw(0x3CD, m);
    if (!k.dead && --k.count == 0) w.ts.KillCurrent();
}

void SpawnWound(World::Impl& w, Boss4Task& b, int p, int node) {
    WoundTask* k = w.NewTask<WoundTask>(TaskWound, 0x00492210);
    k->boss = &b;
    k->bossSerial = w.CurrentSerial();
    k->node = node & 31;
    const Mat4 inv = mx::Inverse(NodeWorldNow(w, b, node));  // 0x004A8D20(0)
    mx::Transform(inv, w.impactPos[p], k->pos);
    float d[3];
    mx::TransformDir(inv, w.impactN[p], d);
    mx::DirToAngles(d[0], d[1], d[2], &k->rot[0], &k->rot[1]);  // 0x004016B0
    k->count = 0x78;
    k->last = b.anim.nodes[k->node].world;
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x00491B40: this frame's hits. Node 2 (the head, while +0x34 & 0x100 is clear) and material 0x3D (a wound) damage the
// boss; material 0x35 is the weapon (no effect here); the rest spark (effect 0x33).
void Shots(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (!(b.flags34 & 8)) return;
    int order[2];
    const uint32_t f = b.flags34 & 6;
    if (f == 2) {
        order[0] = 0;
        order[1] = -1;
    } else if (f == 4) {
        order[0] = 1;
        order[1] = -1;
    } else {
        order[0] = w.Rand(0x00491B8B) & 1;
        order[1] = order[0] ^ 1;
    }
    w.shotOrder[0] = order[0];  // 0x009C8908 / 0x009C890C
    w.shotOrder[1] = order[1];
    b.flags34 &= ~0xEu;
    int material = 0;  // (set by mesh hits only; not reset per player)
    for (int p : order) {
        if (p == -1) continue;
        const int node = b.hitNode[p];
        if (node <= 0) continue;
        const EnemyNode& n = b.anim.nodes[node & 31];
        float pt[3];
        if (n.mesh == -1) {
            const float o[3] = { 0, 0, 0 };
            float A[3] = {}, B[3] = {};
            const int r = LineSphere(n.radius, n.hit, o, w.player[p].ray.dir, A, B);
            float v[3];
            if (r == 1 || (r == 2 && A[2] < 0.0f && B[2] < 0.0f && B[2] <= A[2])) memcpy(v, A, 12);
            else if (r == 2 && A[2] < 0.0f && B[2] < 0.0f) memcpy(v, B, 12);
            else memcpy(v, n.hit, 12);
            mx::Transform(w.cam.inverse, v, pt);
        } else {
            memcpy(pt, w.impactPos[p], 12);
            material = static_cast<int>(w.impactMat[p]);
        }
        if (material == 0x3D) {
            SpawnWound(w, b, p, node);    // 0x004920C0
            w.SpawnHitBlood(b, node, 0.8f);  // 0x00407310(obj, node, 0.8)
            w.Sound(0x216A9);
        } else if (material != 0x35) {
            if (node == 2 && !(b.flags34 & 0x100)) {
                w.SpawnHitBlood(b, 2, 2.0f);
                w.Sound(0x316A9);
            } else {
                w.SpawnEffectAt(pt, 0, 0, 0x33, 1, p);  // 0x00407340
            }
        }
        if ((b.flags34 & 0x100) || (node != 2 && material != 0x3D)) continue;
        if (!(b.flags34 & 0x4000000)) {
            float dmg = 1.0f;  // 0x004C4380
            if (node == 2) {
                dmg = static_cast<float>(static_cast<int8_t>(
                    U8(w.exe, 0x005704D7 + static_cast<uint32_t>(w.players) + static_cast<uint32_t>(c.level) * 2)));
                ++c.hits;
                w.AddScore(p, 10);
            }
            if (w.runner->cfg.gameType == 1) {
                // 0x00491E31: the shooter's item factor 0x009A224C + p * 0x14 (bits 0xBF800000: x 2), the cap, then
                // _ftol(hp - dmg) on the x87 stack (53-bit precision: double)
                double d = dmg;
                float factor = 1.0f, boost = 0.0f;
                if (ItemDamage(w, p, &factor, &boost)) d = factor == -1.0f ? d + d : d * static_cast<double>(factor);
                if (!(d <= static_cast<double>(F32(w.exe, 0x0055E1B4)))) d = F32(w.exe, 0x0055E1B4);
                b.hp = static_cast<int16_t>(static_cast<int>(static_cast<double>(b.hp) - d));
            } else {
                if (!(dmg <= F32(w.exe, 0x0055E1B4))) dmg = F32(w.exe, 0x0055E1B4);
                b.hp = static_cast<int16_t>(static_cast<int>(static_cast<float>(b.hp) - dmg));
            }
            if (b.hp < 1) {
                w.bossBar = 0.0f;
                b.flags34 = (b.flags34 & ~0x2000u) | 0x4000100u;
                --w.liveEnemies;
                w.AddScore(p, 0x5DC);
                BossRecords(w);  // 0x00425F40: boss-mode (game type 3) records (world_modes_boss.cpp)
            } else {
                w.bossBar = static_cast<float>(b.hp) / static_cast<float>(b.hpMax);
                if (static_cast<float>(b.hp) <= c.thr) {
                    b.flags34 |= 0x100;
                    if (c.state != 0x12 && c.state != 0x13) b.flags34 &= ~0x2000u;
                }
            }
            if (w.bossBar < 1.0f) {  // head model by the damage (0x005709A0 - i)
                const int i = static_cast<int>(w.bossBar * F32(w.exe, 0x00570A44));
                b.anim.nodes[2].model = w.exe.S16(0x005709A0 - static_cast<uint32_t>(i) * 2);
            }
        }
        if (!(b.flags34 & 0x4000000)) {
            if (!(b.flags34 & 0x40002000) && node == 2) {  // flinch: 0x14 standing, 0x15 thrown back (a foot lifted)
                b.flags34 = (b.flags34 & 0xEFFFBFFFu) | 0x40000000u;
                c.prevState = c.state;
                c.prevSub = c.sub;
                const float g = w.ground + 10.0f;  // 0x009C8E58 + 0x004C43A4
                c.state = (NodeY(w, b, 15) < g || NodeY(w, b, 12) < g) ? 0x14 : 0x15;
                c.sub = 0;
            }
        } else {
            c.state = 0x16;
            c.sub = 0;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x004928D0: a phase change pending (+0 & 8): at the phase's camera tick the boss is placed at the next arena position
void RelocEnd(World::Impl& w, Boss4Task& b, uint32_t clear34) {
    b.c.flags &= ~0xAu;
    b.flags34 &= 0xBFFFDFFFu;
    SetupPoints(w, b);
    b.flags34 &= ~clear34;
}

void Relocate(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (!(c.flags & 8)) return;
    const int tick = w.runner->camera.tick;
    const bool waiting = c.state == 7 || c.state == 4;
    auto approach = [&](uint32_t x, uint32_t z) {  // LAB_00492FC0: walking at the next point: state 5 once it is passed
        if (!waiting) return;
        const float p[3] = { Bits(x), b.pos[1], Bits(z) };
        if (!w.EnemyPointBehind(b, p)) return;
        c.state = 5;
        c.sub = 0;
    };
    auto place = [&](uint32_t x, uint32_t z, int yaw) {
        b.pos[0] = Bits(x);
        b.pos[2] = Bits(z);
        b.rot[1] = yaw;
    };
    auto setState = [&](int s) {
        c.state = static_cast<uint8_t>(s);
        c.sub = 0;
    };
    switch (c.phase) {
    case 1:
    case 10: {
        if (tick < (c.phase == 1 ? 0x104 : 0x8C)) return;
        const float p[3] = { Bits(c.phase == 1 ? 0x434D0000 : 0xC3C30000), b.pos[1], Bits(c.phase == 1 ? 0xC4CA8000 : 0xC4C8A000) };
        if (!w.EnemyPointBehind(b, p)) {
            b.pos[0] = p[0];
            b.pos[2] = p[2];
            b.rot[1] = c.phase == 1 ? 0xC000 : 0x4000;
            w.AnimSetMotion(b, 0x6B);
        } else if (b.anim.motion[0] != 0x6B) {
            w.AnimBlendTo(b, 0x6B, 0, 10);
        }
        setState(7);
        RelocEnd(w, b, 0);
        return;
    }
    case 2:
        if (tick < 0x1CC) return;
        place(0x437F0000, 0xC4D48000, 0xC000);
        setState(8);
        RelocEnd(w, b, 0x10000);
        return;
    case 3:
        if (tick < 600) {
            approach(0x43D20000, 0xC4D2A000);
            return;
        }
        place(0x43D20000, 0xC4D2A000, 0xC000);
        if (b.anim.motion[0] != 0x6B) w.AnimBlendTo(b, 0x6B, 0, 2);
        setState(7);
        RelocEnd(w, b, 0);
        return;
    case 4:
        if (tick < 0x2A9) return;
        place(0x440AC000, 0xC4E88000, 0x400);
        w.AnimSetMotion(b, 0x6B);
        setState(7);
        RelocEnd(w, b, 0);
        return;
    case 5:
        if (tick < 0x316) {
            approach(0x43EB0000, 0xC4F0A000);
            return;
        }
        place(0x43D48000, 0xC4F64000, 0x2000);
        setState(0x13);
        c.c74 = 0x334;
        c.c78 = 0x32A;
        RelocEnd(w, b, 0);
        return;
    case 6:
        if (tick < 0x3CA) {
            approach(0x43B90000, 0xC5011000);
            return;
        }
        place(0x43AF0000, 0xC4F8C000, 0x2800);
        setState(8);
        RelocEnd(w, b, 0x10000);
        return;
    case 7:
        if (tick < 0x42E) {
            approach(0x43700000, 0xC4FE6000);
            return;
        }
        place(0x432F0000, 0xC4EFC000, 0x4000);
        setState(0x13);
        c.c74 = 0x46A;
        c.c78 = 0x456;
        RelocEnd(w, b, 0);
        return;
    case 8:
        if (tick < 0x4E2) {
            approach(0x42700000, 0xC4F00000);
            return;
        }
        place(0x42820000, 0xC4F14000, 0x5000);
        if (b.anim.motion[0] != 0x6D) w.AnimBlendTo(b, 0x6D, 0, 5);
        setState(7);
        RelocEnd(w, b, 0);
        return;
    case 11:
        if (tick < 0xFA) {
            approach(0xC3E60000, 0xC4DC0000);
            return;
        }
        place(0xC3DC0000, 0xC4D84000, 0xBB00);
        setState(0x13);
        c.c74 = 0x154;
        c.c78 = 0x136;
        RelocEnd(w, b, 0);
        return;
    case 12:
        if (tick < 0x212) return;
        place(0xC3870000, 0xC4DF2000, 0xDC00);
        setState(8);
        RelocEnd(w, b, 0x10000);
        return;
    case 13:
        if (tick < 0x26C) {
            approach(0xC3430000, 0xC4EF6000);
            return;
        }
        if (b.anim.motion[0] != 0x6B) w.AnimBlendTo(b, 0x6B, 0, 5);
        setState(9);
        c.c74 = 10;
        c.f84 = b.pos[0];
        c.f88 = Bits(0xC4F14000);
        c.flags &= ~0xAu;
        b.flags34 &= 0xBFFFDFFFu;
        SetupPoints(w, b);
        return;
    case 14:
        if (tick < 0x30C) return;
        place(0xC3870000, 0xC5020000, 0x4000);
        w.AnimBlendTo(b, 0x6B, 10, 0);
        setState(7);
        RelocEnd(w, b, 0x14000);
        return;
    case 15:
        if (tick < 0x3D4) {
            approach(0xC3E10000, 0xC502F000);
            return;
        }
        place(0xC3B40000, 0xC5020000, 0x3800);
        setState(8);
        RelocEnd(w, b, 0x10000);
        return;
    case 16:
        if (tick < 0x442) {
            approach(0xC3E10000, 0xC502F000);
            return;
        }
        place(0xC3F28000, 0xC4F38000, 0x5800);
        setState(0x13);
        c.c74 = 0x474;
        c.c78 = 0x460;
        RelocEnd(w, b, 0);
        return;
    case 17:
        if (0xE < c.state) return;
        w.AnimBlendTo(b, 0x6D, 0, 10);
        setState(7);
        c.flags &= ~0xAu;
        b.flags34 &= 0xBFFFDFFFu;
        SetupPoints(w, b);
        return;
    default:
        return;
    }
}

// 0x00492350: +0 & 0x40: once inside the three walls 2..5 (5 units) the arena walls are on (+0 |= 2), +0x34 & 0x100 off
void WallThreshold(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (!(c.flags & 0x40)) return;
    if (!(c.flags & 2)) {
        if (Wall(b.pos, c.pt[2], c.pt[3], 5.0f, 0) == 0) return;
        if (Wall(b.pos, c.pt[3], c.pt[4], 5.0f, 0) == 0) return;
        if (Wall(b.pos, c.pt[4], c.pt[5], 5.0f, 0) == 0) return;
        c.flags |= 2;
    }
    b.flags34 &= ~0x100u;
    SetThr(w, b);
    c.flags &= ~0x40u;
}

// 0x004922C0: +0 & 0x20: once inside the walls 2 -> 3 and 4 -> 5 the +0x34 bit 0x10000 (not a target) is cleared
void WallClear(World::Impl& w, Boss4Task& b) {
    (void)w;
    Boss4Ctl& c = b.c;
    if (!(c.flags & 0x20)) return;
    if (!(b.flags34 & 0x10000)) {
        c.flags &= ~0x20u;
        return;
    }
    if ((c.flags & 8) || c.state == 0x13) return;
    if (Wall(b.pos, c.pt[2], c.pt[3], 0.0f, 0) == 0) return;
    if (Wall(b.pos, c.pt[4], c.pt[5], 0.0f, 0) == 0) return;
    b.flags34 &= ~0x10000u;
    c.flags &= ~0x20u;
}

// 0x00492460: +0 & 0x10: a foot (nodes 15 / 12) comes down (was above 47, now below 0x005308DC): shake by the distance,
// sound 0x151BA9
void Footsteps(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (!(c.flags & 0x10)) return;
    int shake = 0;
    const float hi = F32(w.exe, 0x00570A4C), lo = F32(w.exe, 0x005308DC);
    const float y15 = NodeY(w, b, 15), y12 = NodeY(w, b, 12);
    if (!(c.flags & 0x100)) {
        if (hi < y15) c.flags |= 0x100;
    } else if (y15 < lo) {
        shake = 5;
        c.flags &= ~0x100u;
    }
    if (!(c.flags & 0x200)) {
        if (hi < y12) c.flags |= 0x200;
    } else if (y12 < lo) {
        shake = 5;
        c.flags &= ~0x200u;
    }
    if (shake == 0) return;
    const float d = PlayerDist(w, b);
    if (d <= F32(w.exe, 0x0055D2AC)) {
        shake += 10;
    } else if (d < F32(w.exe, 0x00570A48)) {
        shake = static_cast<int>((F32(w.exe, 0x00570A48) - d) * F32(w.exe, 0x0055D230) + static_cast<float>(shake));
    }
    if (w.shakeTimer < shake) w.shakeTimer = shake;  // 0x009C8E8C
    w.Sound(0x151BA9);
}

// 0x00492620: skeleton (in the vehicle's frame while on it), counter, node-5 model cycle, the carried objects on the
// body (0x005704F8: two entries {x, y, z, rotX, rotY, rotZ, s16 node, s16 motion, s16 grab tick, s16 throw tick})
void DrawBoss(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    // (light 0x0041DC70 / 0x0041DCC0: AnimDraw, world_light.cpp)
    if (c.flags & 1) {
        b.framed = c.vehicle != nullptr;
        b.frame = Mat4::Identity();
        if (c.vehicle) {
            mx::Translate(b.frame, c.vehicle->pos[0], c.vehicle->pos[1], c.vehicle->pos[2]);
            mx::RotX(b.frame, c.vehicle->rot[0]);
            mx::RotZ(b.frame, c.vehicle->rot[2]);
            mx::RotY(b.frame, c.vehicle->rot[1]);
            b.frameMv = w.cam.view;  // (the same ops on the view: the original's stack top)
            mx::Translate(b.frameMv, c.vehicle->pos[0], c.vehicle->pos[1], c.vehicle->pos[2]);
            mx::RotX(b.frameMv, c.vehicle->rot[0]);
            mx::RotZ(b.frameMv, c.vehicle->rot[2]);
            mx::RotY(b.frameMv, c.vehicle->rot[1]);
        }
    } else {
        b.framed = false;
    }
    w.AnimDraw(b);     // 0x00411090 = 0x004110D0
    w.EnemyShadow(b);  // + 0x0040A590 (floor shadow)
    b.drawView = w.cam.view;
    if (!(b.flags34 & 0x4000)) ++b.anim.counter[0];
    if (c.flags & 4) {
        int& m5 = b.anim.nodes[5].model;
        if (0x447 < ++m5) m5 = 0x444;
    }
    for (int i = 0; i < 2; ++i) {
        if (c.taken & (1 << i)) continue;
        const uint32_t e = 0x005704F8 + static_cast<uint32_t>(i) * 0x20;
        Mat4 m = b.anim.nodes[w.exe.S16(e + 0x18) & 31].mv;  // the node's model-view (+0x28)
        mx::Translate(m, F32(w.exe, e), F32(w.exe, e + 4), F32(w.exe, e + 8));
        mx::RotZ(m, static_cast<int>(w.exe.U32(e + 0x14)));
        mx::RotY(m, static_cast<int>(w.exe.U32(e + 0x10)));
        mx::RotX(m, static_cast<int>(w.exe.U32(e + 0xC)));
        w.Draw(0x396, m, 8, -1.0f, false, -1, true);
    }
}

// 0x00492790: hp below the phase's threshold (no shake, no camera sweep running or requested): next phase, sweep
void PhaseCheck(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (w.shakeTimer != 0 || c.camStep != 0.0f || (c.flags & 0x80)) return;
    const int ph = c.phase;
    switch (ph) {
    case 3: case 8: case 0xD: case 0x11:
        return;
    case 0x10:
        if (static_cast<float>(b.hpMax) * Thr(w, ph) < static_cast<float>(b.hp)) return;
        if (static_cast<int16_t>(static_cast<int>(c.camT)) != w.exe.S16(0x00570636)) return;
        c.phase = static_cast<uint8_t>(ph + 1);
        break;
    default:
        if (ph >= 20) return;  // (0xFF before the fight: never reached, state 0 sets the phase first)
        if (static_cast<float>(b.hpMax) * Thr(w, ph) < static_cast<float>(b.hp)) return;
        b.flags34 |= 0x10000;
        c.phase = static_cast<uint8_t>(ph + 1);
        break;
    }
    CamRequest(b, c.phase);
    c.flags |= 8;
}

// 0x00409B70(dy)
void ViewPos(World::Impl& w, EnemyTask& e, float dy) {
    mx::Transform(w.cam.view, e.body, e.viewPos);
    w.RegisterTarget(&e);
    e.body[1] = dy + e.body[1];
    w.SyncLock(e);
    w.lockOn.Register(e.lock, w.playerPos);  // 0x00408EC0
}

// 0x00493090: a requested sweep starts (row 0x00570538: s16 start, s16 end, f32 step, s16 camera path) and holds the
// gameplay camera (0x009CA094); each frame context 0 follows the path and turns towards the lock-on look-at
void CameraSweep(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    CameraDirector& cd = w.runner->camera;
    if (c.camStep == 0.0f && 0 <= c.camNext) {
        const int row = c.camNext;
        if (row != 5 && row != 7 && row != 0xB && row != 0x10) c.flags &= ~0x80u;
        c.camNext = -1;
        const uint32_t r = 0x00570538 + static_cast<uint32_t>(row) * 0xC;
        c.camT = static_cast<float>(w.exe.S16(r));
        c.camEnd = static_cast<float>(w.exe.S16(r + 2));
        c.camStep = F32(w.exe, r + 4);
        c.camPath = w.exe.S16(r + 8);
        cd.railTick = w.exe.S16(r);      // 0x009C70AC
        cd.railEnd = w.exe.S16(r + 2);   // 0x009C70B0
        cd.forceState6 = true;           // 0x009CA094
    }
    if (c.camStep == 0.0f) return;
    CameraContext& c0 = cd.ctx[0];
    cd.railTickF = c.camT;  // 0x009C70BC
    float tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(c.camPath, c.camT, c0.pos, tg, &roll, false))  // Cam_Evaluate 0x004041E0
        w.Note("stage-4 boss camera path " + std::to_string(c.camPath) + " missing");
    cd.railTick = static_cast<int>(c.camT);
    if (c.camT < c.camEnd) {
        c.camT = c.camStep + c.camT;
    } else {
        c.camStep = 0.0f;
        cd.forceState6 = false;
    }
    LockOnSlot slots[4];
    w.lockOn.Slots(slots);
    cd.targetSeen = SelectLookAt(slots, cd.railTarget, cd.lookAt);  // 0x00403050
    float out[3];
    TurnToward(c0.pos, cd.lookAt, c0.target, out, 1, 0x10);  // 0x00403C00
    memcpy(c0.target, out, 12);
    ContextLookAngles(c0, c0.target, 0);  // 0x00403AC0
}

// 0x004934D0: aggression +1 after 2 (one player) / 3 head hits, -3 when a player loses a life
void Aggression(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    auto check = [&](int p) {
        const int16_t cur = w.player[p].lives;
        if (static_cast<int16_t>(c.lives[p]) != cur) {
            if (cur < static_cast<int16_t>(c.lives[p])) {
                c.level = static_cast<int8_t>(c.level - 3);
                c.hits = 0;
            }
            c.lives[p] = static_cast<int8_t>(cur);
        }
    };
    if (w.players == 1) {
        if (1 < c.hits) {
            ++c.level;
            c.hits = 0;
        }
        check(w.playersInGame & 1);
    } else {
        if (2 < c.hits) {
            ++c.level;
            c.hits = 0;
        }
        for (int p = 0; p < 2; ++p) check(p);
    }
    if (c.level < 0) c.level = 0;
    else if (0xF < c.level) c.level = 0xF;
}

// 0x004935E0: the engine sound loop by the camera path / tick
void EngineSound(World::Impl& w, Boss4Task& b) {
    const int cam = w.runner->camera.camId, t = w.runner->camera.tick;
    int what = 0;  // 1 on, 2 off
    if (cam == 0xB9) {
        if (0x3C0 < t) {
            if (t == 0x433 || t == 0x4D3) what = 2;
            else if (t == 0x456 || t == 0x500) what = 1;
        } else if (t == 0x3C0) {
            what = 1;
        } else if (t < 0x312) {
            if (t == 0x311 || t == 0x18B) what = 2;
            else if (t == 0x1CC) what = 1;
        } else if (t == 0x32A) {
            what = 1;
        } else if (t == 0x3A2) {
            what = 2;
        }
    } else if (cam == 0xC1) {
        if (t < 0x2F9) {
            if (t == 0x2F8 || t == 0x136 || t == 0x208) what = 1;
            else if (t == 500 || t == 0xF0 || t == 700) what = 2;
        } else if (t == 900 || t == 0x42E) {
            what = 2;
        } else if (t == 0x3C0 || t == 0x460) {
            what = 1;
        }
    }
    if (what == 1) SoundOn(w, b);
    else if (what == 2) SoundOff(w, b);
}

// ---------------------------------------------------------------------------------------------------------------------
// States (0x00597298)
// ---------------------------------------------------------------------------------------------------------------------

// 0x00493F30: the next attack by the aggression (0x005709B4[level * 9 + rand % 9]: 0 state 0xF, 1 0x10, 2 0x11) and the
// distance at which it starts (+0x84); phases 3 / 13: state 0x12 (throw) at 180
void PickAttack(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.phase == 3 || c.phase == 0xD) {
        c.c74 = 0x12;
        c.f84 = Bits(0x43340000);
        return;
    }
    if (b.flags34 & 0x100) {
        c.c74 = 0xF;
        c.f84 = Bits(0x42480000);
        return;
    }
    const int r = w.Rand(0x00493F67);
    int8_t a = static_cast<int8_t>(U8(w.exe, 0x005709B4 + static_cast<uint32_t>(c.level * 9 + r % 9)));
    if (c.state == 7 && a == 0) a = 1;
    if (a == 0) {
        c.c74 = 0xF;
        c.f84 = Bits(0x42480000);
    } else if (a == 1) {
        c.c74 = 0x10;
        c.f84 = Bits(0x428C0000);
    } else if (a == 2) {
        c.c74 = 0x11;
        c.f84 = Bits(0x428C0000);
    }
}

// 0x004952A0: back to walking (state 7) with the phase's sweep for phases 5 / 7 / 11 / 16; nobody playing: state 13
void BackToWalk(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (w.players < 1) {
        c.state = 0xD;
        c.sub = 0;
        return;
    }
    switch (c.phase) {
    case 5: CamRequest(b, 0x12); break;
    case 7: CamRequest(b, 0x13); break;
    case 0xB: CamRequest(b, 0x14); break;
    case 0x10: CamRequest(b, 0x15); break;
    default: break;
    }
    w.AnimBlendTo(b, 0x6B, 0, 10);
    c.state = 7;
    c.sub = 0;
}

// 0x004938B0 (states 0 / 2) / 0x00493B40 (states 1 / 3): the intro on the vehicle (name card 0x005972F8 / 0x00597338);
// flag 0x1E: jumps off (to world), the fight starts when the letterbox opens
void StIntro(World::Impl& w, Boss4Task& b, bool second) {
    Boss4Ctl& c = b.c;
    switch (c.sub) {
    case 0:
        w.SpawnNameCard(second ? 0x00597338 : 0x005972F8);  // 0x00437A70
        c.phase = second ? 9 : 0;
        SetThr(w, b);
        if (c.state == (second ? 3 : 2)) {
            w.AnimSetMotion(b, 0x75);
            SoundOn(w, b);
            c.flags &= ~0x10u;
            c.sub = 2;
            return;
        }
        w.AnimSetMotion(b, 0x7C);
        ++c.sub;
        return;
    case 1: {
        if (w.runner->flags[0x1E] == 0) return;  // 0x009C721E
        SoundOn(w, b);
        c.flags &= ~1u;
        if (c.vehicle) {  // local position / angles in the vehicle -> world (0x00401AE0)
            Mat4 m = Mat4::Identity();
            mx::Translate(m, c.vehicle->pos[0], c.vehicle->pos[1], c.vehicle->pos[2]);
            mx::RotX(m, c.vehicle->rot[0]);
            mx::RotZ(m, c.vehicle->rot[2]);
            mx::RotY(m, c.vehicle->rot[1]);
            mx::Translate(m, b.pos[0], b.pos[1], b.pos[2]);
            mx::RotX(m, b.rot[0]);
            mx::RotZ(m, b.rot[2]);
            mx::RotY(m, b.rot[1]);
            mx::GetTranslation(m, b.pos);
            MatrixToAnglesYAxis(m, &b.rot[0], &b.rot[1], &b.rot[2]);
        } else {
            w.Note("stage-4 boss: no vehicle (0x009A2C88) to jump off");
        }
        b.framed = false;
        w.AnimBlendTo(b, 0x75, 0, 10);
        w.Sound(0x231BA9);
        ++c.sub;
        return;
    }
    case 2:
        if (b.anim.motion[0] == 0x75 && b.anim.tick[0] == 0x5A) w.AnimBlendTo(b, 0x74, 0, 10);
        if (w.letterbox == 1) {  // 0x009CA0F4
            w.runner->flags[0x1F] = 1;  // 0x009C721F
            CamRequest(b, c.phase);
            b.flags34 &= ~0x8000u;
            SetupPoints(w, b);
            w.SpawnBossBar(320.0f, 35.0f);  // 0x00435E50
            c.flags |= 0x12;
            c.state = 7;
            c.sub = 0;
            w.AnimBlendTo(b, 0x6B, 0, 0x16);
            w.bossActive = 1;  // 0x009CA0EA
        }
        return;
    default:
        return;
    }
}

// 0x00493DC0 (state 4): pick the attack (0x00493F30), walk / stand by phase, turn to the players; within +0x84: state +0x74
void St4(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        PickAttack(w, b);
        auto blend = [&](int m, int n) {
            if (b.anim.motion[0] != m) w.AnimBlendTo(b, m, 0, n);
        };
        switch (c.phase) {
        case 2: blend(0x78, 10); break;
        case 3: case 0xD: blend(0x6B, 10); break;
        case 8: case 0x11: blend(0x6D, 3); break;
        default:
            if ((w.Rand(0x00493E2B) & 1) == 0) blend(0x6B, 10);
            else blend(0x78, 10);
            break;
        }
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    if (!(c.flags & 8)) TurnToPlayers(w, b, 0x200);
    if (PlayerDist(w, b) < c.f84) {
        c.state = static_cast<uint8_t>(c.c74);
        c.sub = 0;
        PickTarget(w, b.target);
    }
}

// 0x00494010 (state 5): walk to point 0; phases 3 / 13: state 9 / 12 / 14 when the hp is down (or no objects left),
// else state 4 further than 180 / 155; other phases: state 6 further than 90 / 120
void St5(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (c.phase == 8 || c.phase == 0x11) {
            if (b.anim.motion[0] != 0x6E) w.AnimBlendTo(b, 0x6E, 0, 3);
        } else if (b.anim.motion[0] != 0x6C) {
            w.AnimBlendTo(b, 0x6C, 0, 10);
        }
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    TurnTo(w, b, c.pt[0][0] - b.pos[0], c.pt[0][2] - b.pos[2], 0x200);
    const float d = PlayerDist(w, b);
    switch (c.phase) {
    case 3:
        if (AtPhaseHp(w, b, 3) || c.objects == 0) {
            if (F32(w.exe, 0x005691C4) < d) {
                if (PlayersAttackable(w)) {
                    c.state = 9;
                    c.sub = 0;
                    c.c74 = 0xB;
                    c.f84 = Bits(0x44048000);
                    c.f88 = Bits(0xC4D70000);
                    ++c.phase;
                    c.flags &= ~2u;
                    b.flags34 |= 0x100;
                    return;
                }
                c.state = 0xE;
                c.sub = 0;
            }
        } else if (F32(w.exe, 0x00570A5C) < d) {
            c.state = 4;
            c.sub = 0;
        }
        return;
    case 8:
    case 0x11:
        if (F32(w.exe, 0x00570A58) < d) {
            c.state = 6;
            c.sub = 0;
            c.c74 = 1;
        }
        return;
    case 0xD:
        if (AtPhaseHp(w, b, 0xD) || c.objects == 0) {
            c.state = PlayersAttackable(w) ? 0xC : 0xE;
            c.sub = 0;
            return;
        }
        if (F32(w.exe, 0x00570A54) < d) {
            c.state = 4;
            c.sub = 0;
        }
        return;
    default:
        if (!(c.flags & 8) && F32(w.exe, 0x00570A50) < d) {
            c.state = 6;
            c.sub = 0;
            c.c74 = 1;
        }
        return;
    }
}

// 0x004943B0 (state 6): roar (0x7C, phases 8 / 0x11: 0x7D) +0x74 times, then state 4
void St6(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (c.phase == 8 || c.phase == 0x11) {
            if (b.anim.motion[0] != 0x7D) w.AnimBlendTo(b, 0x7D, 0, 3);
            if (!(c.flags & 8)) w.Sound(0x271BA9);
        } else {
            if (b.anim.motion[0] != 0x7C) w.AnimBlendTo(b, 0x7C, 0, 10);
            if (!(c.flags & 8)) w.Sound(0x231BA9);
        }
        --c.c74;
        ++c.sub;
        return;
    }
    if (c.sub != 1) return;
    if (b.anim.tick[0] != Ticks(w, b) - 1) return;
    if (c.c74 != 0) {
        --c.c74;
        return;
    }
    if (w.players < 1 || (c.flags & 8)) return;
    c.state = 4;
    c.sub = 0;
}

// 0x004944A0 (state 7): walk at the players; facing them (< 0x800) and past point 1: attack pick, state 4 sub 1
void St7(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (!(c.flags & 8) && !(b.anim.flags[0] & 1)) TurnToPlayers(w, b, 0x100);
    if (b.flags34 & 0x10000) return;
    int a = LocalAngle(w, b, b.pos[0] - w.playerPos[0], b.pos[2] - w.playerPos[2]);
    if (a < 0) a = -a;
    if (a < 0x800 && w.EnemyPointBehind(b, c.pt[1])) {
        PickAttack(w, b);
        c.state = 4;
        c.sub = 1;
    }
}

// 0x004945A0 (state 8): motion 0x72 to its end, then 0x78, state 7
void St8(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        w.AnimSetMotion(b, 0x72);
        ++c.sub;
    } else if (c.sub == 1 && b.anim.tick[0] == Ticks(w, b)) {
        w.AnimBlendTo(b, 0x78, 0, 10);
        c.state = 7;
        c.sub = 0;
    }
}

// 0x00494610 (state 9): walk to (+0x84, +0x88); facing away from it (> 0x7000) and past it: state +0x74
void St9(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (b.anim.motion[0] != 0x78) w.AnimBlendTo(b, 0x78, 0, 10);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    const float p[3] = { c.f84, b.pos[1], c.f88 };
    TurnTo(w, b, b.pos[0] - p[0], b.pos[2] - p[2], 0x200);
    int a = LocalAngle(w, b, b.pos[0] - p[0], b.pos[2] - p[2]);
    if (a < 0) a = -a;
    if (0x7000 < a && w.EnemyPointBehind(b, p)) {
        c.state = static_cast<uint8_t>(c.c74);
        c.sub = 0;
    }
}

// 0x00494730 (state 10): turn round (motion 0x76); at its end the turn is moved from the root into +0x68 (not ported)
void St10(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (b.anim.motion[0] != 0x76) w.AnimBlendTo(b, 0x76, 0, 10);
        ++c.sub;
    } else if (c.sub == 1 && b.anim.tick[0] == Ticks(w, b) - 1) {
        w.Note("stage-4 boss state 10 turn end (0x00494730: root / node 1 / node 9 angles re-derived) not ported");
        b.anim.motion[0] = 0x6B;
        b.anim.tick[0] = 0;
        w.AnimBlendTo(b, 0x6B, 0, 10);
        PickAttack(w, b);
        c.state = 4;
        c.sub = 1;
    }
}

// 0x004958F0 (state 11): walk to (535, -1900): sweep + relocation within 0x004C4C8C; footsteps on near the players
void St11(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    const float tx = F32(w.exe, 0x00570A60), tz = F32(w.exe, 0x00570A64);
    if (c.sub == 0) {
        b.flags34 |= 0x10000;
        if (b.anim.motion[0] != 0x6B) w.AnimBlendTo(b, 0x6B, 0, 10);
        c.flags &= ~0x10u;
        SoundOff(w, b);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    TurnTo(w, b, b.pos[0] - tx, b.pos[2] - tz, 0x200);
    const float dz = b.pos[2] - tz, dx = b.pos[0] - tx;
    if (std::sqrt(dz * dz + dx * dx) < F32(w.exe, 0x004C4C8C)) {
        CamRequest(b, c.phase);
        c.flags |= 8;
    }
    if (!(c.flags & 0x10) && PlayerDist(w, b) < F32(w.exe, 0x00570A48)) {
        c.flags |= 0x10;
        SoundOn(w, b);
    }
}

// 0x00495A20 (state 12): close (< 95): walk to point 0 then on; far (> 105): approach; then out of the arena to
// (-260, -1990) and the next phase
void St12(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    auto leave = [&] {
        c.flags &= ~2u;
        b.flags34 |= 0x100;
        ++c.phase;
        CamRequest(b, c.phase);
        c.flags |= 8;
    };
    switch (c.sub) {
    case 0: {
        const float d = PlayerDist(w, b);
        if (d < F32(w.exe, 0x00570A74)) {
            if (b.anim.motion[0] != 0x6C) w.AnimBlendTo(b, 0x6C, 0, 10);
            c.sub = 2;
            return;
        }
        if (F32(w.exe, 0x00570A70) < d) {
            if (b.anim.motion[0] != 0x6B) w.AnimBlendTo(b, 0x6B, 0, 10);
            c.sub = 1;
            return;
        }
        if (b.anim.motion[0] != 0x6B) w.AnimBlendTo(b, 0x6B, 0, 10);
        c.sub = 3;
        leave();
        return;
    }
    case 1:
        TurnToPlayers(w, b, 0x200);
        if (F32(w.exe, 0x004C43B0) <= PlayerDist(w, b)) return;
        c.sub = 3;
        leave();
        return;
    case 2:
        TurnTo(w, b, c.pt[0][0] - b.pos[0], c.pt[0][2] - b.pos[2], 0x200);
        if (F32(w.exe, 0x004C43B0) < PlayerDist(w, b)) {
            if (b.anim.motion[0] != 0x6B) w.AnimBlendTo(b, 0x6B, 0, 10);
            c.sub = 3;
            leave();
        }
        return;
    case 3: {
        if (!(b.flags34 & 0x10000) && Wall(b.pos, c.pt[4], c.pt[5], 0.0f, 0) == 0) b.flags34 |= 0x10000;
        const float p[3] = { Bits(0xC3820000), b.pos[1], Bits(0xC4F8C000) };
        TurnTo(w, b, b.pos[0] - F32(w.exe, 0x00570A68), b.pos[2] - F32(w.exe, 0x00570A6C), 0x200);
        if (w.EnemyPointBehind(b, p)) {
            b.flags34 |= 0x4000;
            ++c.sub;
        }
        return;
    }
    default:
        return;
    }
}

// 0x00495D30 (state 13): roar, then back to walking once somebody plays
void St13(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (b.anim.motion[0] != 0x7C) w.AnimBlendTo(b, 0x7C, 0, 10);
        w.Sound(0x231BA9);
        ++c.sub;
    } else if (c.sub == 1 && 0 < w.players) {
        BackToWalk(w, b);
    }
}

// 0x00495D90 (state 14): roar, state 5 once a player can be attacked
void St14(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (b.anim.motion[0] != 0x7C) w.AnimBlendTo(b, 0x7C, 0, 10);
        w.Sound(0x231BA9);
        ++c.sub;
    } else if (c.sub == 1 && PlayersAttackable(w)) {
        c.state = 5;
        c.sub = 0;
    }
}

// 0x00494A80 / 0x00494B80 / 0x00494C70 (states 15..17): attacks (motions 0x65 / 0x7A / 0x7B): turn during the swing,
// +0x34 0x2000 (not interruptible) from tick a, Player_TakeHit(target, 1, 8) at tick b, state 5 at the end
void StAttack(World::Impl& w, Boss4Task& b, int motion, int t0, int t1, int lock, int hit, bool route1) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        w.AnimBlendTo(b, motion, 0, 10);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    const int tick = b.anim.tick[0];
    if (t0 < tick && tick < t1) {
        if (w.route == 2) {
            if (route1) {
                if (b.target == 1) b.rot[1] += 0xC0;
            } else {
                b.rot[1] += b.target == 0 ? -0x60 : 0x60;
            }
        } else if (route1) {
            b.rot[1] += 0x60;
        }
    }
    if (tick == lock) {
        b.flags34 |= 0x2000;
    } else if (tick == hit) {
        if (!(c.flags & 8)) {
            w.PlayerTakeHit(b.target, 1, 8);  // 0x00415300
            b.flags34 &= ~0x2000u;
        }
    } else if (tick == Ticks(w, b) - 1) {
        c.state = 5;
        c.sub = 0;
        b.flags34 &= 0xEFFFDFFFu;
    }
}

// 0x00494D60 (state 18): take one of the objects on the body (rand among the ones left), throw it at the target
void St18(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        if (w.players < 1 || (w.players == 1 && w.targetTaken[w.playersInGame & 1] != 0)) {
            c.state = 6;
            c.sub = 0;
            c.c74 = 1;
            return;
        }
        c.c74 = -1;
        int n = w.Rand(0x00494DC7) % static_cast<int>(c.objects) + 1;
        while (n != 0) {
            ++c.c74;
            if (!(c.taken & (1 << (c.c74 & 31)))) --n;
        }
        w.AnimBlendTo(b, w.exe.S16(0x00570512 + static_cast<uint32_t>(c.c74) * 0x20), 0, 10);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    const uint32_t e = static_cast<uint32_t>(c.c74) * 0x20;
    const int grab = w.exe.S16(0x00570514 + e), thr = w.exe.S16(0x00570516 + e);
    const int tick = b.anim.tick[0];
    if (tick == grab) {
        c.carried = SpawnCarriedType2(w, b, c.phase == 3);  // 0x00494F70
        b.flags34 |= 0x10002000;
        b.anim.nodes[8].model = 0x442;  // anim +0x4F8 = node 8 model: the hand holding
        c.taken |= static_cast<uint8_t>(1 << (c.c74 & 31));
        --c.objects;
    } else if (tick == thr - 5) {
        w.Sound(0x1BA9);
    } else if (tick == thr) {
        if (c.carried && HeldAlive(c.carried)) {
            c.carried->state = 4;  // throw (0x005926A8[4] = 0x00443B90)
            PickTarget(w, c.carried->slot121);
            if (c.carried->slot121 >= 0) w.targetTaken[c.carried->slot121] = 1;
        } else {
            w.Note("stage-4 boss: carried object gone at the throw tick (0x00494D60)");
        }
        b.flags34 &= 0xEFFFDFFFu;
        b.anim.nodes[8].model = 0x441;
    } else if (tick == Ticks(w, b) - 1) {
        c.state = 5;
        c.sub = 0;
    }
    if (grab < tick && tick < thr) TurnToPlayers(w, b, 0x200);
}

// 0x00495070 (state 19): crouch (0x70, not a target) until camera tick +0x74 (+0x78: becomes a target), then the leap
// (turning to a point beside the target), radius -15 at tick 0x23, 6 and Player_TakeHit at 0x47
void St19(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        w.AnimSetMotion(b, 0x70);
        b.flags34 |= 0x4000;
        ++c.sub;
    }
    if (c.sub == 1) {
        const int tick = w.runner->camera.tick;
        if (tick < c.c74) {
            if (tick == c.c78) b.flags34 &= ~0x10000u;
            return;
        }
        b.flags34 &= ~0x4000u;
        PickTarget(w, b.target);
        ++c.sub;
        return;
    }
    if (c.sub != 2) return;
    const int tick = b.anim.tick[0];
    if (tick == Ticks(w, b) - 1) {
        BackToWalk(w, b);
        return;
    }
    if (0x1D < tick && tick < 0x47) {
        float x = 5.0f;  // 0x40A00000
        if (w.route != 1) x = b.target == 0 ? 2.5f : 7.5f;
        const float l[3] = { x, 0.0f, 0.0f };
        float p[3];
        mx::Transform(w.cam.inverse, l, p);
        TurnTo(w, b, b.pos[0] - p[0], b.pos[2] - p[2], 0x200);
    }
    if (b.anim.tick[0] == 0x23) {
        c.radius = Bits(0xC1700000);
    } else if (b.anim.tick[0] == 0x47) {
        c.radius = 6.0f;
        w.PlayerTakeHit(b.target, 1, 8);
    }
}

// 0x00495340 (state 20): flinch (0x6F / 0x73 by the old state), then back to it (or by the old state to 4 / 5 / 7)
void St20(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        int m;
        if (c.prevState == 7) {
            c.c74 = b.anim.motion[0];
            m = c.phase == 3 ? 0x73 : 0x6F;
        } else {
            c.radius = 6.0f;
            m = 0x73;
        }
        w.AnimBlendTo(b, m, 0, 10);
        w.Sound(0x281BA9);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    if (c.prevState != 0x13) {
        if (b.anim.motion[0] == 0x6F) TurnTo(w, b, b.pos[0] - w.playerPos[0], b.pos[2] - w.playerPos[2], 0x200);
        else TurnTo(w, b, c.pt[0][0] - b.pos[0], c.pt[0][2] - b.pos[2], 0x200);
    }
    if (b.anim.tick[0] != Ticks(w, b) - 1) return;
    auto done = [&] { b.flags34 &= ~0x40000000u; };
    switch (c.prevState) {
    case 6:
    case 10:
        c.state = 4;
        c.sub = 0;
        done();
        return;
    case 7:
        w.AnimBlendTo(b, c.c74, 0, 10);
        c.state = 7;
        c.sub = 0;
        done();
        return;
    case 8:
        w.AnimBlendTo(b, 0x78, 0, 10);
        c.state = 7;
        break;
    case 9:
        if (c.phase == 0xD) {
            c.state = 4;
            c.sub = 0;
            done();
            return;
        }
        done();  // (sub stays: the old state's sub is not restored)
        return;
    case 0xF: case 0x10: case 0x11: case 0x12:
        c.state = 5;
        break;
    case 0x13:
        BackToWalk(w, b);
        done();
        return;
    default:
        c.state = c.prevState;
        break;
    }
    c.sub = 0;
    done();
}

// 0x00495570 (state 21): thrown back by a head hit with a foot up (0x71 placed at node 1, 0x0045CE70): flies back from
// the players, lands on the floor 0x009C8E58, back to walking at the motion's end
void St21(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        c.radius = 6.0f;
        b.anim.motion[0] = 0x71;
        b.anim.tick[0] = 0;
        {  // 0x0045CE70: the new motion's root (frame tick / 2) placed so node 1 stays where it is
            const int nodes = w.exe.S16(0x004E0724 + b.anim.kind * 2);
            const Motion* m = w.EnemyMotion(b.anim.motion[0], nodes);
            float root[3] = {};
            int16_t r0[3] = {};
            if (m && !m->data.empty() && b.anim.tick[0] / 2 < m->StoredFrames()) {
                m->RootPosition(b.anim.tick[0] / 2, root);
                m->Rotation(b.anim.tick[0] / 2, 0, r0);
            } else {
                w.Note("stage-4 boss: motion frame data missing (0x00412F50)");
            }
            const Mat4 n1 = NodeWorldNow(w, b, 1);
            float cur[3];
            mx::GetTranslation(n1, cur);
            const uint32_t sk = w.exe.U32(w.exe.U32(0x004E0430 + 4 * static_cast<uint32_t>(b.anim.kind)) + 0x18);
            Mat4 a = Mat4::Identity();
            mx::Translate(a, b.pos[0], b.pos[1], b.pos[2]);
            mx::RotX(a, b.rot[0]);
            mx::RotZ(a, b.rot[2]);
            mx::RotY(a, b.rot[1]);
            mx::Translate(a, root[0], root[1], root[2]);
            mx::RotZ(a, r0[2]);
            mx::RotY(a, r0[1]);
            mx::RotX(a, r0[0]);
            mx::Translate(a, F32(w.exe, sk + 4), F32(w.exe, sk + 8), F32(w.exe, sk + 0xC));
            float nw[3];
            mx::GetTranslation(a, nw);
            b.pos[0] = (cur[0] - nw[0]) + b.pos[0];
            b.pos[1] = (cur[1] - nw[1]) + b.pos[1];
            b.pos[2] = (cur[2] - nw[2]) + b.pos[2];
            memcpy(b.anim.root, root, 12);
        }
        w.AnimBlendTo(b, b.anim.motion[0], 0, 10);
        // velocity away from the players: pitch / yaw of (pos - players), speed 0x004C4D10, up 0x004E30F0
        const float dz = b.pos[2] - w.playerPos[2], dx = b.pos[0] - w.playerPos[0], dy = b.pos[1] - w.playerPos[1];
        const int pitch = AngleOf(dy, std::sqrt(dx * dx + dz * dz));
        const int yaw = AngleOf(dx, dz);
        const float k = F32(w.exe, 0x004C4D10);
        const float cp = CosA(pitch);
        b.vel[0] = SinA(yaw) * cp * k;
        b.vel[1] = SinA(pitch) * k - F32(w.exe, 0x004E30F0);
        b.vel[2] = CosA(yaw) * cp * k;
        b.acc[1] = -0.054444443f;
        w.Sound(0x281BA9);
        ++c.sub;
    } else if (c.sub == 2) {
        if (b.anim.tick[0] != Ticks(w, b) - 1) return;
        BackToWalk(w, b);
        b.flags34 &= ~0x40000000u;
        return;
    } else if (c.sub != 1) {
        return;
    }
    b.pos[0] = b.vel[0] + b.pos[0];
    b.pos[1] = b.vel[1] + b.pos[1];
    b.pos[2] = b.vel[2] + b.pos[2];
    b.vel[1] = b.acc[1] + b.vel[1];
    if (b.anim.tick[0] == 0x19) b.flags34 |= 0x4000;
    if (b.pos[1] < w.ground) {
        b.flags34 &= ~0x4000u;
        b.pos[1] = w.ground;
        ++c.sub;
    }
}

// 0x00495770 (state 22): death (0x69); at tick 0x46 placed by the phase, event flag 0x20; at 0x82 the fall (sound,
// shake 0x32); 0xF0 frames after the motion's end the boss stops counting as an attacker
void St22(World::Impl& w, Boss4Task& b) {
    Boss4Ctl& c = b.c;
    if (c.sub == 0) {
        w.AnimBlendTo(b, 0x69, 0, 10);
        c.flags &= ~2u;
        c.c74 = 0xF0;
        w.Sound(0x241BA9);
        w.bossActive = 0;  // 0x009CA0EA
        ++c.sub;
    } else if (c.sub != 1) {
        if (c.sub == 2) --c.c74;
        if (c.c74 == 0) --w.attackers;
        return;
    }
    const int tick = b.anim.tick[0];
    if (tick == Ticks(w, b) - 1) {
        b.flags34 |= 0x4000;
        ++c.sub;
    } else if (tick == 0x46) {
        if (c.phase == 8) {
            b.pos[0] = Bits(0x43870000);
            b.pos[1] = Bits(0x422A6666);
            b.pos[2] = Bits(0xC4DFACCD);
            b.rot[1] = 0;
        } else {
            b.pos[0] = Bits(0xC400C666);
            b.pos[1] = Bits(0x422A6666);
            b.pos[2] = Bits(0xC4D6CCCD);
            b.rot[1] = 0x8000;
        }
        b.flags34 |= 0x10000;
        c.flags &= ~0x10u;
        w.runner->flags[0x20] = 1;  // 0x009C7220
    } else if (tick == 0x82) {
        w.Sound(0xB16A9);
        SoundOff(w, b);
        w.shakeTimer = 0x32;  // 0x009C8E8C
    }
    if (c.c74 == 0) --w.attackers;
}

// 0x004919D0
void TaskBoss4(Task& t) {
    World::Impl& w = *CurrentWorld();
    Boss4Task& b = t.As<Boss4Task>();
    Boss4Ctl& c = b.c;
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);
    Shots(w, b);          // 0x00491B40
    Relocate(w, b);       // 0x004928D0
    WallThreshold(w, b);  // 0x00492350
    WallClear(w, b);      // 0x004922C0
    switch (c.state) {    // 0x00597298
    case 0: case 2: StIntro(w, b, false); break;
    case 1: case 3: StIntro(w, b, true); break;
    case 4: St4(w, b); break;
    case 5: St5(w, b); break;
    case 6: St6(w, b); break;
    case 7: St7(w, b); break;
    case 8: St8(w, b); break;
    case 9: St9(w, b); break;
    case 10: St10(w, b); break;
    case 11: St11(w, b); break;
    case 12: St12(w, b); break;
    case 13: St13(w, b); break;
    case 14: St14(w, b); break;
    case 15: StAttack(w, b, 0x65, 0x27, 0x33, 0x32, 0x37, true); break;
    case 16: StAttack(w, b, 0x7A, 0x1D, 0x29, 0x2B, 0x30, false); break;
    case 17: StAttack(w, b, 0x7B, 0x13, 0x1F, 0x21, 0x26, false); break;
    case 18: St18(w, b); break;
    case 19: St19(w, b); break;
    case 20: St20(w, b); break;
    case 21: St21(w, b); break;
    case 22: St22(w, b); break;
    case 23: w.Note("stage-4 boss state 23 (0x00495E20: pad-controlled debug move) not ported"); break;
    default: w.Note("stage-4 boss state " + std::to_string(c.state) + " out of range"); break;
    }
    static const bool dbg = getenv("HOTD2_BOSS4_DEBUG") != nullptr;  // state / phase trace on stderr
    if (dbg && (c.state != b.dbgState || c.phase != b.dbgPhase)) {
        fprintf(stderr, "boss4 %d: state %d sub %d phase %d hp %d motion 0x%X flags %X/%X\n", w.frame, c.state, c.sub,
                c.phase, b.hp, b.anim.motion[0], c.flags, b.flags34);
        b.dbgState = c.state;
        b.dbgPhase = c.phase;
    }
    Footsteps(w, b);   // 0x00492460
    DrawBoss(w, b);    // 0x00492620
    PhaseCheck(w, b);  // 0x00492790
    ViewPos(w, b, c.radius);  // 0x00409B70(+0x70)
    CameraSweep(w, b); // 0x00493090
    if (c.flags & 2) {
        if (Wall(b.pos, c.pt[2], c.pt[3], 5.0f, 1) != 0) Wall(b.pos, c.pt[4], c.pt[5], 5.0f, 1);
        if (Wall(b.pos, c.pt[3], c.pt[4], 5.0f, 1) != 0) Wall(b.pos, c.pt[5], c.pt[2], 0.0f, 1);
    }
    Aggression(w, b);   // 0x004934D0
    EngineSound(w, b);  // 0x004935E0
    if (w.runner->camera.camId == b.endCam && w.runner->camera.tick == b.endTick) {
        if (b.actorSlot >= 0 && b.actorSlot < 14) w.actorSlots[b.actorSlot] = nullptr;
        w.EnemyDestroy(b);
        return;
    }
    w.SyncLock(b);
}

}  // namespace

// 0x004917E0 (actor type 0x19): the spawn task becomes the boss (record params byte 0 skeleton, byte 1 first state,
// words 1..15 node collision meshes, s16 0x40 / 0x42 removal camera tick)
bool World::Impl::SpawnBoss4(Task& t, SpawnTask& s) {
    const SpawnRecord r = s.rec;
    auto d = std::make_unique<Boss4Task>();
    Boss4Task& b = *d;
    b.fn = 0x004919D0;
    b.record = r;
    b.flags34 = r.flags | 1;  // 0x00408A20
    memcpy(b.pos, r.pos, 12);
    memcpy(b.rot, r.rot, 12);
    b.hp = b.hpMax = static_cast<int16_t>(r.state);
    b.lo = static_cast<int16_t>(r.lo);
    b.flags34 |= 0x8000;
    b.anim.kind = static_cast<int16_t>(r.params[0]);
    b.anim.motion[0] = 0x7C;
    b.anim.counter[0] = 0;
    b.anim.tick[0] = 0;
    EnemyInitPose(b);  // Anim_InitEnemy 0x00410440 (+ actor slot)
    b.nodeCb = 0x00411050;      // default node draw
    b.postRootFn = NoPostRoot;  // 0x00420810
    b.anim.rotOrder = 1;
    b.anim.nodes[5].model = 0x444;
    b.radius = 30.0f;
    ++attackers;
    ++liveEnemies;
    b.target = -1;
    b.slot120 = -1;
    b.lock = &lockTargets[nextLockTarget++];
    SyncLock(b);
    lockOn.AddDirect(b.lock);  // 0x00408E80
    b.slot120 = static_cast<int8_t>(b.lock->slot);
    Boss4Ctl& c = b.c;
    c.state = static_cast<uint8_t>(r.params[1]);
    if (c.state < 2) {
        c.flags = 1;
        c.vehicle = static_cast<EnemyTask*>(obj9A2C88);
        if (!c.vehicle) Note("stage-4 boss: no vehicle 0x009A2C88 at the spawn");
    } else {
        c.flags = 0;
    }
    c.sub = 0;
    c.level = static_cast<int8_t>(rank);  // 0x0040A8A0
    c.hits = 0;
    c.lives[0] = static_cast<int8_t>(player[0].lives);  // 0x009A5C66
    c.lives[1] = static_cast<int8_t>(player[1].lives);  // 0x009A5D96
    for (int i = 1; i < 16; ++i) {
        EnemyNode& n = b.anim.nodes[i];
        n.mesh = r.ParamS32(4 * i);
        if (n.mesh != -1) {
            n.flags |= 0x51;
            n.radius = 0;
        }
    }
    c.phase = 0xFF;
    c.thr = 0;
    c.objects = 2;
    c.taken = 0;
    c.camStep = 0;
    c.camNext = -1;
    c.radius = 6.0f;
    b.endCam = r.ParamS16(0x40);
    b.endTick = r.ParamS16(0x42);
    t.data = std::move(d);
    t.fn = TaskBoss4;
    return true;
}

}  // namespace game
