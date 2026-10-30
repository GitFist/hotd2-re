// game::World - the stage-2 boss: actor type 0x14 (0x00475E90 -> task 0x00476150, state table 0x00596218), its
// helpers 0x00476270 (shots) / 0x004763E0 (the chest opening) / 0x00476AD0 (skeleton, draw, model cycles) /
// 0x00477CD0 (arena walls) / 0x00477E60 (phases) / 0x00477FF0 (aggression), the water level actor type 0x16
// (0x00442290 / query 0x00442390) and komono kind 18 (task 0x00468E50, the lightning light).
// Verification: notes/agents/stage2c.md (CONFIRMED = frame-exact against scene_capture_auto_st2.txt).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

uint8_t U8(const ExeImage& exe, uint32_t va) {
    uint8_t v = 0;
    exe.Read(va, &v, 1);
    return v;
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
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

// 0x00477BF0 Boss2_LegAngle(a, b, c, d, e): the angle whose cosine is ((b + c) - a) / (d * e + d * e)
// (0 for >= 1, 0x8000 for <= -1, 0x4000 for 0; else atan(sqrt(1 - f^2) / f), + 0x8000 when f < 0)
int LegAngle(float a, float b, float c, float d, float e) {
    const float de = d * e;
    const float f = ((b + c) - a) / (de + de);
    if (!(f < 1.0f)) return 0;
    if (f <= -1.0f) return 0x8000;
    if (f == 0.0f) return 0x4000;
    const float s = std::sqrt(1.0f - f * f) / f;
    const float v = static_cast<float>(std::atan2(static_cast<double>(s), 1.0) * mx::kToAngle);
    const int r = static_cast<int16_t>(static_cast<int>(v));
    return f > 0.0f ? r : r + 0x8000;
}

// Control block (0xBC bytes, obj +0x1310 = 0x007DCF20)
struct Boss2Ctl {
    uint32_t flags = 1;      // +0x00 (1: no wall push / phase hold, 2: in the water, 4: no velocity integration)
    uint8_t state = 0;       // +0x04 (0x00596218 index)
    uint8_t sub = 0;         // +0x05
    uint8_t prevState = 0, prevSub = 0;  // +0x06 / +0x07 (state 0x10 / 0x11 return)
    uint8_t phase = 0;       // +0x08 (hp thresholds 0x00596670)
    float radius = 6.0f;     // +0x0C (0x00409B70 dy)
    float saved[3] = {};     // +0x10 position when the fight starts
    float dir[3] = {};       // +0x1C (P floats +4..+0xC: the arena's forward direction)
    float corner[4][3] = {}; // +0x28 / +0x34 / +0x40 / +0x4C (x, 0, z; P floats +0x10..+0x2C)
    int f58 = 0, f5C = 0;    // +0x58 / +0x5C bounce phase / step (death states)
    int16_t jumps = 0;       // +0x60
    int16_t row = 0xB;       // +0x62 motion row (0x00596408)
    int leg[6] = {};         // +0x64..+0x78 leg IK angles (draw)
    int16_t m7C = 0x2CB, m7E = 0x2CB, m80 = 0x2ED, m82 = 0;  // +0x7C..+0x82 cycling model (drawn with node 1)
    float m84 = 1.0f;        // +0x84
    int16_t m88 = 0x2EE, m8A = 0x2EE, m8C = 0x315, m8E = 0;  // +0x88..+0x8E chest model (opening frames)
    float m90 = 2.0f;        // +0x90
    int16_t row94 = 0;       // +0x94 (0x005965C0 rows)
    int8_t level = 0;        // +0x96 aggression 0..15 (starts at the rank)
    int8_t hits = 0;         // +0x97 hits since the last level change
    int8_t lives[2] = {};    // +0x98 / +0x99 players' lives last seen
    int c9C = 0, cA0 = 0, cA4 = 0, cA8 = 0, cAC = 0;  // +0x9C..+0xAC counters
};

struct Boss2Task : EnemyTask {
    Boss2Ctl c;
    int16_t endCam = 0, endTick = 0;  // P s16 0x30 / 0x32: destroyed at this camera tick
    Mat4 drawView = Mat4::Identity();  // the view of the last skeleton evaluation (the nodes keep model-view matrices)
    // 0x00476AD0's foot points (locals: node 15 / node 12 * camera * T(0, -1.25, 0)); the foot globals 0x009A3500 are
    // World::Impl::st2Feet (actor type 0x15's rafts)
    float footA[3] = {}, footB[3] = {};
    int typingSelector = 700, typingInterlude = 0, typingWalkChoice = 0;
    float typingInterludeStep[3] = {};
};

// Reference5B3140: three open/closed delays followed by the two model-frame
// rates, indexed by the live typing rank rather than the gun aggression row.
struct TypingChestRow { int open[3], closed[3]; float opening, closing; };
constexpr TypingChestRow kTypingChest[16] = {
    {{600,-1,-1},{30,0,0},1,-1}, {{600,-1,-1},{30,0,0},1,-1},
    {{600,-1,-1},{30,0,0},1,-1}, {{600,-1,-1},{30,0,0},1,-1},
    {{120,140,-1},{35,30,0},2,-2}, {{120,130,-1},{40,40,0},2,-2},
    {{120,120,-1},{45,45,0},2,-2}, {{120,110,-1},{50,50,0},3,-3},
    {{120,100,-1},{55,55,0},3,-3}, {{120,90,-1},{60,60,0},3,-3},
    {{115,85,-1},{65,65,0},4,-4}, {{110,80,-1},{70,70,0},4,-4},
    {{105,75,-1},{75,75,0},4,-4}, {{100,70,-1},{80,80,0},5,-5},
    {{95,65,-1},{85,85,0},5,-5}, {{90,60,-1},{90,90,0},5,-5}
};

void BossWords(World::Impl& w, Boss2Task& b, int selector = -1) {
    if (!w.typingReferenceActive) return;
    if (selector >= 0) b.typingSelector = selector;
    w.TypingWords(0, b.typingSelector); // Source4B0560 reposts the current selection.
}

void NoNode(World::Impl&, EnemyTask&, int, const Mat4&) {}  // node callback 0x0041EBB0
void NoPostRoot(World::Impl&, EnemyTask&) {}               // 0x00420810

int MotionOfRow(const World::Impl& w, int row) {  // *(s16 *)0x00596408[row]
    return w.exe.S16(w.exe.U32(0x00596408 + static_cast<uint32_t>(row) * 4));
}

int Ticks(const World::Impl& w, const EnemyTask& e) { return w.MotionTicks(e.anim.motion[0]); }

float Thr(const World::Impl& w, int phase) {
    if (w.typingReferenceActive && (phase == 0 || phase == 1)) return .45f; // Source5B32D0.
    if (w.typingReferenceActive && phase == 3) return .5f;
    return F32(w.exe, 0x00596670 + static_cast<uint32_t>(phase) * 4);
}

// (float)hp <= (float)hpMax * thr[phase]
bool BelowPhase(const World::Impl& w, const Boss2Task& b) {
    return static_cast<float>(b.hp) <= static_cast<float>(b.hpMax) * Thr(w, b.c.phase);
}

// the water level 0x00442390(pos): WaterLevelAt (world_st2_water.cpp: block +8 plus the wave objects of scene 39)

float Floor(World::Impl& w, const float p[3]) {  // Coll_FloorHeight(x, y + 100, z)
    int mat = 0;
    return w.FloorHeight(p[0], p[1] + F32(w.exe, 0x004C43B0), p[2], &mat);
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

// 0x00426170: target player +0x121 (one player: 0x009C7000; two: the one hit last, else by lives with rand)
void PickTarget(World::Impl& w, EnemyTask& e) {
    e.target = 0;
    if (w.players == 1) {
        e.target = static_cast<int8_t>(w.playersInGame);
    } else if (w.players == 2) {
        if (w.player[1].hitTimer != 0) {
            e.target = 0;
        } else if (w.player[0].hitTimer != 0) {
            e.target = 1;
        } else {
            const int b = w.player[0].lives <= w.player[1].lives ? 1 : 0;
            const int r = w.Rand(0x004261E0);
            const int n = (w.player[b].lives - w.player[b ^ 1].lives) + 2;
            e.target = static_cast<int8_t>(r % n == 0 ? (b ^ 1) : b);
        }
    }
}

// 0x00477CD0(pos, a, b, dist): keep pos on the inner side of the wall a -> b (x / z); 1 = no correction
int Wall(float* p, const float* a, const float* b, float d) {
    const float px = p[0] - a[0], pz = p[2] - a[2];
    const float ex = b[0] - a[0], ez = b[2] - a[2];
    float t = (ex * px + ez * pz) / (ex * ex + ez * ez);
    const float qx = t * ex - px, qz = t * ez - pz;
    const float l = std::sqrt(qx * qx + qz * qz);
    if (l == 0.0f) {
        if (d == 0.0f) return 1;
        if (ez != 0.0f) {
            const float s = ez < 0.0f ? ez * -1.0f : ez;
            p[0] = (ez / s) * d + p[0];
        }
        if (ex != 0.0f) {
            const float s = ex < 0.0f ? ex * -1.0f : ex;
            p[2] = p[2] - (ex / s) * d;
        }
        return 0;
    }
    if (0.0f <= ez * px - ex * pz) {
        if (d <= l) return 1;
        d = l - d;
    } else {
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

// node n's model-view matrix (the original keeps model-view; the native node matrix is world)
Mat4 NodeMV(const World::Impl& w, const Boss2Task& e, int n) {
    (void)w;
    return e.anim.nodes[n].mv;  // AnimDraw's model-view chain (from the view = drawView)
}

// 0x004A9230(camera matrix); 0x004A92A0(node model-view): node matrix x camera matrix in the x87 order
Mat4 NodeTimesCamera(const World::Impl& w, const Boss2Task& e, int n) {
    Mat4 q = w.cam.inverse;
    mx::MulLeft(q, e.anim.nodes[n].mv);
    return q;
}

// view-space hit point of node n (+0x68) through the camera matrix
void NodeHitWorld(const World::Impl& w, const EnemyTask& e, int n, float out[3]) {
    mx::Transform(w.cam.inverse, e.anim.nodes[n & 31].hit, out);
}

void Spark(World::Impl& w, const EnemyTask& e, int node, int p) {  // 0x00476A30
    float pt[3];
    NodeHitWorld(w, e, node, pt);
    w.SpawnEffectAt(pt, 0, 0, 2, 1, p);  // 0x00407340(pt, 2, 1, p)
}

// shot ray of player p in world space (block 0x009A2CB8: origin +0, direction +0xC)
void WorldRay(const World::Impl& w, int p, float origin[3], float dir[3]) {
    Mat4 rot = w.cam.inverse;
    rot.m[12] = rot.m[13] = rot.m[14] = 0.0f;
    const float zero[3] = { 0, 0, 0 };
    mx::Transform(w.cam.inverse, zero, origin);
    mx::Transform(rot, w.player[p].ray.dir, dir);
}

// 0x004763E0(obj, node 1, player): a hit on the chest counts only through the opening of the chest model frame
// (+0x88 - +0x8A -> 0x00596480 rows {max |yaw|, rotX, min pitch, max pitch}) on a sphere of 5.5 at node 1 + (0, 3, 0)
void ChestHit(World::Impl& w, Boss2Task& b, int node, int p) {
    Boss2Ctl& c = b.c;
    {
        Mat4 m = NodeMV(w, b, 1);
        mx::Translate(m, 0.0f, 4.0f, 1.0f);
        float v[3];
        mx::GetTranslation(m, v);
        if (!b.typingStrike && !ShotHitsSphere(w.player[p].ray, v, 3.5f)) {  // 0x004062A0
            Spark(w, b, node, p);
            return;
        }
    }
    const uint32_t row = 0x00596480 + static_cast<uint32_t>(c.m88 - c.m8A) * 8;
    const int maxYaw = w.exe.S16(row), rotX = w.exe.S16(row + 2), minP = w.exe.S16(row + 4), maxP = w.exe.S16(row + 6);
    if (maxYaw == 0 && !b.typingStrike) {
        Spark(w, b, node, p);
        return;
    }
    if (!b.typingStrike) {
        Mat4 m = NodeTimesCamera(w, b, 1);  // 0x004A9230(camera) * node 1
        mx::Translate(m, 0.0f, 3.0f, 0.0f);
        float ctr[3];
        mx::GetTranslation(m, ctr);
        float o[3], d[3];
        WorldRay(w, p, o, d);
        const float s[3] = { d[0] + o[0], d[1] + o[1], d[2] + o[2] };
        float A[3] = {}, B[3] = {};
        if (LineSphere(5.5f, ctr, o, s, A, B) == 0) w.Note("stage-2 boss chest: ray misses the 5.5 sphere (stack values used)");
        const float a0 = o[2] - A[2], a1 = o[1] - A[1], a2 = o[0] - A[0];
        const float b0 = o[2] - B[2], b1 = o[1] - B[1], b2 = o[0] - B[0];
        mx::RotX(m, rotX);
        const Mat4 inv = mx::Inverse(m);  // 0x004A8D20(0)
        float l[3];
        if (std::sqrt((b0 * b0 + b1 * b1) + b2 * b2) < std::sqrt((a0 * a0 + a1 * a1) + a2 * a2)) mx::Transform(inv, B, l);
        else mx::Transform(inv, A, l);
        const int pitch = AngleOf(-l[1], l[2]);
        int yaw = AngleOf(l[0], l[2]);
        if (yaw < 0) yaw = -yaw;
        if (!(yaw <= maxYaw && minP <= pitch && pitch <= maxP)) {
            Spark(w, b, node, p);
            return;
        }
    }
    if (!(b.flags34 & 0x4000000)) {
        float dmg = static_cast<float>(static_cast<int8_t>(U8(w.exe, 0x00596697 + static_cast<uint32_t>(w.players) +
                                                                      static_cast<uint32_t>(c.level) * 2)));
        float factor = 1.0f, boost = 0.0f;
        if (w.runner->cfg.gameType == 1 && ItemDamage(w, p, &factor, &boost))  // player +0xC: -1 doubles, else x factor
            dmg = factor == -1.0f ? dmg + dmg : dmg * factor;
        if (!(dmg <= F32(w.exe, 0x0055E1B4))) dmg = F32(w.exe, 0x0055E1B4);
        if (b.typingStrike) {
            if (w.cfg.portTyping) {
                b.typingStrike = false;
                dmg = float(TypingHierophantWordDamage());
            } else dmg = TypingDamage(b, dmg);
        }
        b.hp = static_cast<int16_t>(static_cast<int>(static_cast<float>(b.hp) - dmg));
        ++c.hits;
        w.Sound(0x316A9);
        if (b.hp < 1) {
            w.bossBar = 0.0f;
            b.flags34 = (b.flags34 & ~0x2000u) | 0x4000000u;
            --w.liveEnemies;
            w.AddScore(p, 0x5DC);
            if (w.cfg.portTyping) w.TypingController(p).addBasePoints(550);
            BossRecords(w);  // 0x00425F40: boss-mode (game type 3) records (world_modes_boss.cpp)
        } else {
            w.bossBar = static_cast<float>(b.hp) / static_cast<float>(b.hpMax);
            if (BelowPhase(w, b)) b.flags34 |= 0x100;
        }
        w.AddScore(p, 10);
    }
    w.SpawnHitBlood(b, node, 2.0f);  // 0x00407310
    if (b.flags34 & 0x40000000) return;
    if (!(b.flags34 & 0x2000)) {
        const int st = c.state;
        bool fall = false;
        if (st == 12 || st == 14) fall = Floor(w, b.pos) < b.pos[1];
        else if (st == 9) fall = Floor(w, b.pos) + F32(w.exe, 0x0055D2B4) < b.pos[1];
        b.flags34 = (b.flags34 & ~0x4000u) | 0x40000000u;
        c.prevState = c.state;
        c.prevSub = c.sub;
        c.state = fall ? 0x11 : 0x10;
        c.sub = 0;
        if (!fall) c.radius = 6.0f;
        w.Sound(0x1117A9);
    }
    if (!w.typingReferenceActive) {
        c.row94 = static_cast<int16_t>(c.row94 + 2);
        if (8 <= c.row94) c.row94 = 7;
    }
}

// 0x00476270: this frame's hits (players in random order when both hit), node 1 -> 0x004763E0, else a spark
void Shots(World::Impl& w, Boss2Task& b) {
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
        order[0] = w.Rand(0x00476295) & 1;
        order[1] = order[0] ^ 1;
    }
    b.flags34 &= ~0xEu;
    for (int p : order) {
        if (p == -1) continue;
        const int node = b.hitNode[p];
        if (node <= 0) continue;
        if (!(b.flags34 & 0x100) && node == 1) {
            ChestHit(w, b, 1, p);
            if (b.flags34 & 0x4000000) b.flags34 |= 0x100;
        } else {
            float pt[3];
            NodeHitWorld(w, b, node, pt);
            w.SpawnEffectAt(pt, 0, 0, 2, 1, p);
        }
    }
}

// 0x00476AD0: skeleton (node callback 0x0041EBB0: no node draws), foot points (draw), leg IK / floor follow, the
// cycling model +0x7C and the chest model +0x88 (its frames decide the opening), the custom draw
void DrawBoss(World::Impl& w, Boss2Task& b) {
    Boss2Ctl& c = b.c;
    w.AnimDraw(b);  // 0x00411090 = 0x004110D0
    w.EnemyShadow(b);  // + 0x0040A590 (floor shadow, after the attachments)
    b.drawView = w.cam.view;
    if (!(b.flags34 & 0x4000)) ++b.anim.counter[0];
    const Mat4& camM = w.cam.inverse;  // the active context's camera matrix 0x009A6040 + ctx * 0x1A4
    auto nodeWorld = [&](int n) {     // 0x004A9230(camera); 0x004A92A0(node n): node model-view x camera
        Mat4 m = camM;
        mx::MulLeft(m, b.anim.nodes[n].mv);
        return m;
    };
    const float kThigh = Bits(0xC106E979), kShin = Bits(0xC1183958);  // -8.432 / -9.514: knee / ankle offsets
    if (!(c.flags & 2)) {
        // foot points: node 15 / node 12 at (0, -1.25, 0); the globals at +2.5 / -5 along z from there
        const int footNode[2] = { 15, 12 };
        float* footPt[2] = { b.footA, b.footB };
        for (int k = 0; k < 2; ++k) {
            Mat4 m = nodeWorld(footNode[k]);
            mx::Translate(m, 0.0f, Bits(0xBFA00000), 0.0f);
            mx::GetTranslation(m, footPt[k]);
            mx::Translate(m, 0.0f, 0.0f, Bits(0x40200000));
            mx::GetTranslation(m, &w.st2Feet[k][1]);  // 0x009A3504 / 0x009A3514
            mx::Translate(m, 0.0f, 0.0f, Bits(0xC0A00000));
            mx::GetTranslation(m, &w.st2Feet[k + 2][1]);  // 0x009A3524 / 0x009A3534
        }
        // radii by the motion row's tick list 0x00596408[row] + 2: {s16 tick, s16 mode}..., -1 ends
        uint32_t p = w.exe.U32(0x00596408 + static_cast<uint32_t>(c.row) * 4) + 2;
        for (int16_t t = w.exe.S16(p); t != -1 && t < b.anim.tick[0]; t = w.exe.S16(p)) p += 4;
        static const float kRadius[7][2] = { { 75, 75 }, { 150, 150 }, { 150, 0 }, { 0, 150 }, { 300, 0 }, { 0, 300 }, { 0, 0 } };
        const int mode = w.exe.S16(p + 2);
        if (0 <= mode && mode <= 6) {  // 0x009A3500 / 0x009A3520 (foot A), 0x009A3510 / 0x009A3530 (foot B)
            w.st2Feet[0][0] = w.st2Feet[2][0] = kRadius[mode][0];
            w.st2Feet[1][0] = w.st2Feet[3][0] = kRadius[mode][1];
        }
    }
    // knee angles of the animated legs: node 14 (11) in node 13's (10's) frame at the knee, angle of its y axis
    auto knee = [&](int upper, int lower) {
        Mat4 m = b.anim.nodes[upper].mv;
        mx::Translate(m, 0.0f, kThigh, 0.0f);
        Mat4 inv = mx::Inverse(m);
        mx::MulLeft(inv, b.anim.nodes[lower].mv);
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        float v[3];
        mx::Transform(inv, up, v);
        return AngleOf(v[2], v[1]);
    };
    const int kneeA = knee(13, 14), kneeB = knee(10, 11);
    // leg IK / floor follow (0x00476F70..0x004775D3): the body follows the floor under the planted foot (+-0.3 per
    // frame), each planted foot is turned / bent onto the floor (law of cosines 0x00477BF0 with thigh 8.432 / shin 9.514)
    int target[6] = {};  // +0x64..+0x78
    float lift = 0.0f;
    if (!(b.flags34 & 0x20000) && !(b.anim.flags[0] & 1)) {
        if (c.flags & 2) w.Note("stage-2 boss leg IK in the water (0x00476AD0: foot points not computed)");
        float floorA, floorB;
        {
            int mat = 0;
            floorA = w.FloorHeight(b.footA[0], b.footA[1] + F32(w.exe, 0x004C43B0), b.footA[2], &mat);
            floorB = w.FloorHeight(b.footB[0], b.footB[1] + F32(w.exe, 0x004C43B0), b.footB[2], &mat);
        }
        float d;
        if (w.st2Feet[0][0] != 0.0f) d = floorA - b.footA[1];
        else if (w.st2Feet[1][0] != 0.0f) d = floorB - b.footB[1];
        else d = Floor(w, b.pos) - b.pos[1];
        if (!(d < F32(w.exe, 0x004C4D10))) d = Bits(0x3E99999A);
        else if (d <= F32(w.exe, 0x0055D2C4)) d = Bits(0xBE99999A);
        b.pos[1] = d + b.pos[1];
        float fa[3] = { b.footA[0], b.footA[1], b.footA[2] }, fb[3] = { b.footB[0], b.footB[1], b.footB[2] };
        if (w.st2Feet[0][0] != 0.0f || d + fa[1] < floorA) fa[1] = floorA;
        if (w.st2Feet[1][0] != 0.0f || d + fb[1] < floorB) fb[1] = floorB;
        if (fa[1] == floorA || fb[1] == floorB) {
            lift = d;
            // one leg: foot point on the floor -> hip yaw / pitch and knee bend targets
            auto solve = [&](const float* foot, int footNode, int upperNode, int kneeAngle, int* yaw, int* pitch, int* bend) {
                Mat4 m = nodeWorld(footNode);
                m.m[12] = foot[0];  // 0x004A9FD0
                m.m[13] = foot[1];
                m.m[14] = foot[2];
                mx::Translate(m, 0.0f, Bits(0x3FA00000), 0.0f);
                float P[3];
                mx::GetTranslation(m, P);
                Mat4 h = Mat4::Identity();
                mx::Translate(h, 0.0f, d, 0.0f);
                mx::MulLeft(h, camM);
                mx::MulLeft(h, b.anim.nodes[upperNode].mv);
                float q[3];
                mx::Transform(mx::Inverse(h), P, q);
                auto absf = [](float v) { return v < 0.0f ? v * -1.0f : v; };
                int a = 0;
                if (!(absf(q[0]) < 1.0f) && !(absf(q[2]) < 1.0f)) a = AngleOf(-q[0], -q[2]);
                mx::RotY(h, a);
                float r[3];
                mx::Transform(mx::Inverse(h), P, r);
                const float len2 = r[1] * r[1] + r[2] * r[2];
                const float len = std::sqrt(len2);
                const int hip = LegAngle(Bits(0x42B5084B), len2, Bits(0x428E327F), len, Bits(0x4106E979));
                *pitch = AngleOf(-r[2], -r[1]) - hip;
                *bend = (0x8000 - kneeAngle) - LegAngle(len2, Bits(0x42B5084B), Bits(0x428E327F), Bits(0x41183958), Bits(0x4106E979));
                *yaw = a;
            };
            if (fa[1] == floorA) solve(fa, 15, 13, kneeA, &target[2], &target[0], &target[4]);
            if (fb[1] == floorB) solve(fb, 12, 10, kneeB, &target[3], &target[1], &target[5]);
        }
    }
    // 0x00477C90(&v, target, 0.5): v += ftol(wrapped (s16)(target - v) * 0.5)
    for (int i = 0; i < 6; ++i) {
        int dd = static_cast<uint16_t>(static_cast<uint16_t>(target[i]) - static_cast<uint16_t>(c.leg[i]));
        if (dd > 0x8000) dd -= 0x10000;
        c.leg[i] += static_cast<int>(static_cast<float>(dd) * 0.5f);
    }
    // cycling model +0x7C
    if (c.m82 != 0) {
        --c.m82;
    } else {
        c.m7C = static_cast<int16_t>(static_cast<int>(static_cast<float>(c.m7C) + c.m84));
        if (c.m84 > 0.0f && c.m80 <= c.m7C) {
            c.m7C = c.m80;
            c.m82 = static_cast<int16_t>(static_cast<int>(F32(w.exe, 0x0055CCD4) / c.m84));
            c.m84 = c.m84 * -1.0f;
        } else if (c.m84 < 0.0f && c.m7C <= c.m7E) {
            c.m7C = c.m7E;
            c.m84 = c.m84 * -1.0f;
            c.m82 = static_cast<int16_t>(static_cast<int>(F32(w.exe, 0x0055CCD4) / c.m84));
        }
    }
    // chest model +0x88 (opening frames 0x2EE..0x315)
    if (w.typingReferenceActive) {
        // Source467FE9..468139: the word stays visible while the chest is
        // closed (typing flag10000); the controller cannot accept its input.
        if (c.m8E != 0) --c.m8E;
        else {
            const auto& row = kTypingChest[std::clamp(w.rank, 0, 15)];
            const int column = std::clamp<int>(c.row94, 0, 2);
            c.m88 = static_cast<int16_t>(static_cast<int>(float(c.m88) + c.m90));
            if (c.m88 >= c.m8C) {
                b.typingFlags &= ~0x10000u;
                c.m88 = c.m8C;
                c.m8E = static_cast<int16_t>(row.open[column]);
                c.m90 = row.closing;
            } else if (c.m88 <= c.m8A) {
                b.typingFlags |= 0x10000u;
                c.m88 = c.m8A;
                c.m8E = static_cast<int16_t>(row.closed[column]);
                c.m90 = row.opening;
                c.row94 = static_cast<int16_t>(std::min(2, column + 1));
            }
        }
    } else if (c.m8E != 0) {
        if (b.flags34 & 0x100) {
            if (c.m88 != c.m8A) c.m8E = 0;
        } else if (0 <= c.m8E && c.m8E <= 0x78) {
            --c.m8E;
        } else {
            const int st = c.state;
            if (st == 5 || (8 <= st && st <= 12)) c.m8E = 10;
        }
    } else if ((b.flags34 & 0x100) && c.m88 == c.m8A) {
        c.m8E = -1;
    } else {
        c.m88 = static_cast<int16_t>(static_cast<int>(static_cast<float>(c.m88) + c.m90));
        const uint32_t r = 0x005965C0 + static_cast<uint32_t>(c.row94) * 12;
        if (c.m8C <= c.m88) {
            c.m88 = c.m8C;
            c.m8E = w.exe.S16(r);
            c.m90 = F32(w.exe, r + 8);
        } else if (c.m88 <= c.m8A) {
            c.m88 = c.m8A;
            c.m8E = w.exe.S16(r + 2);
            c.m90 = F32(w.exe, r + 4);
        }
    }
    if (!w.typingReferenceActive && c.m90 < 1.0f) {
        if (!(c.m90 < 0.0f)) c.m90 = 1.0f;
        else if (c.m90 < -1.0f) c.m90 = -1.0f;
    } else if (!w.typingReferenceActive && c.m90 < -1.0f && c.m90 <= 0.0f) {
        c.m90 = -1.0f;
    }
    if (b.anim.drawFlags & 1) {
        World::Impl::NativeModelDrawScope modelScope(w,b.anim.kind);
        // model-view draws on top of T(0, lift, 0): nodes 1..9 (node 1 with +0x7C / +0x88), then each leg rebuilt from
        // its upper node: RotY(yaw) RotX(pitch) -> upper; T(0, -8.432, 0) RotX(bend + knee) -> lower; T(0, -9.514, 0)
        // gives the foot node's position (its rotation stays the animated one)
        w.LightChar();  // still inside 0x0041DC70 .. 0x0041DCC0 of 0x00476AD0
        struct Restore {
            World::Impl& w;
            ~Restore() { w.LightScene(); }
        } restore{ w };
        Mat4 base = Mat4::Identity();
        mx::Translate(base, 0.0f, lift, 0.0f);
        auto draw = [&](int model, const Mat4& m, int node) {
            if (model != 0) w.Draw(model, m, 8, -1.0f, false, node, true);
        };
        for (int i = 1; i < 10; ++i) {
            Mat4 m = base;
            mx::MulLeft(m, b.anim.nodes[i].mv);
            draw(b.anim.nodes[i].model, m, i);
            if (i == 1) {
                draw(c.m7C, m, 1);
                draw(c.m88, m, 1);
            }
        }
        auto leg = [&](int upper, int yaw, int pitch, int bend, int kneeAngle) {
            Mat4 m = base;
            mx::MulLeft(m, b.anim.nodes[upper].mv);
            mx::RotY(m, yaw);
            mx::RotX(m, pitch);
            draw(b.anim.nodes[upper].model, m, upper);
            mx::Translate(m, 0.0f, kThigh, 0.0f);
            mx::RotX(m, bend + kneeAngle);
            draw(b.anim.nodes[upper + 1].model, m, upper + 1);
            mx::Translate(m, 0.0f, kShin, 0.0f);
            Mat4 f = b.anim.nodes[upper + 2].mv;  // 0x004A9230(node) + 0x004A9FD0(position)
            f.m[12] = m.m[12];
            f.m[13] = m.m[13];
            f.m[14] = m.m[14];
            draw(b.anim.nodes[upper + 2].model, f, upper + 2);
        };
        leg(13, c.leg[2], c.leg[0], c.leg[4], kneeA);
        leg(10, c.leg[3], c.leg[1], c.leg[5], kneeB);
    }
}

// 0x00409B70(dy)
void ViewPos(World::Impl& w, EnemyTask& e, float dy) {
    mx::Transform(w.cam.view, e.body, e.viewPos);
    w.RegisterTarget(&e);
    e.body[1] = dy + e.body[1];
    w.SyncLock(e);
    w.lockOn.Register(e.lock, w.playerPos);  // 0x00408EC0
}

// 0x00477E60: hp thresholds of the phase: the next phase / state
void Phases(World::Impl& w, Boss2Task& b) {
    Boss2Ctl& c = b.c;
    if (!BelowPhase(w, b)) return;
    const bool walking = 4 < c.state && c.state < 8;
    switch (c.phase) {
    case 0:
        if (walking) {
            c.state = 10;
            c.sub = 0;
            c.phase = 1;
            BossWords(w, b, 350);
        }
        return;
    case 3: case 5: case 6:
        if (walking) {
            c.state = 6;
            c.sub = 0;
            c.phase = c.phase == 3 ? 4 : c.phase == 5 ? 6 : 7;
            if (c.phase == 4) BossWords(w, b, 350);
            if (w.typingReferenceActive && c.phase == 7) b.typingFlags &= ~0x40u;
            c.flags |= 1;
            c.c9C = 1;
        }
        return;
    case 8:
        if (walking) {
            c.state = 6;
            c.sub = 0;
            c.phase = 9;
            BossWords(w, b, 500);
            c.c9C = 0;
        }
        return;
    default:
        return;
    }
}

// 0x00477FF0: aggression +1 after hits, -3 (-2 in states 10 / 11 with two players) when a player loses a life
void Aggression(World::Impl& w, Boss2Task& b) {
    Boss2Ctl& c = b.c;
    if (0 < c.hits) {
        ++c.level;
        c.hits = 0;
    }
    auto check = [&](int p, int down) {
        const int16_t cur = w.player[p].lives;
        if (static_cast<int16_t>(c.lives[p]) != cur) {
            if (cur < static_cast<int16_t>(c.lives[p])) {
                c.level = static_cast<int8_t>(c.level - down);
                c.hits = 0;
            }
            c.lives[p] = static_cast<int8_t>(cur);
        }
    };
    if (w.players == 1) {
        check(w.playersInGame & 1, 3);
    } else {
        for (int p = 0; p < 2; ++p) check(p, (c.state == 10 || c.state == 11) ? 2 : 3);
    }
    if (c.level < 0) c.level = 0;
    else if (0xF < c.level) c.level = 0xF;
}

void Integrate(Boss2Task& b) {
    b.pos[0] = b.vel[0] + b.pos[0];
    b.pos[1] = b.vel[1] + b.pos[1];
    b.pos[2] = b.vel[2] + b.pos[2];
    b.vel[1] = b.acc[1] + b.vel[1];
}

void ZeroVel(Boss2Task& b) {
    b.acc[1] = 0;
    b.vel[2] = 0;
    b.vel[1] = 0;
    b.vel[0] = 0;
}

// the jump at the players (states 9 / 12 / 14): horizontal speed by the distance, up 4, gravity -0.068
void JumpAt(World::Impl& w, Boss2Task& b, float k) {
    const float dz = b.pos[2] - w.playerPos[2], dx = b.pos[0] - w.playerPos[0];
    const float v = std::sqrt(dz * dz + dx * dx) * k;
    b.vel[0] = SinA(b.rot[1]) * v;
    b.vel[1] = 4.0f;
    b.vel[2] = CosA(b.rot[1]) * v;
    b.acc[1] = Bits(0xBD8B60B6);  // -0.068055555
}

// the point beside the target player with two players (camera matrix * (x, 0, 0)), else the players' point
void TargetPoint(World::Impl& w, Boss2Task& b, float xl, float xr, float* dx, float* dz) {
    if (w.route == 2) {
        float off = 0;  // no target: the camera matrix's own translation (the camera position)
        if (b.target == 0) off = xl;
        else if (b.target == 1) off = xr;
        float pt[3];
        const float l[3] = { off, 0.0f, 0.0f };
        mx::Transform(w.cam.inverse, l, pt);
        *dz = b.pos[2] - pt[2];
        *dx = b.pos[0] - pt[0];
    } else {
        *dz = b.pos[2] - w.playerPos[2];
        *dx = b.pos[0] - w.playerPos[0];
    }
}

// the position relative to the fight start in the boss's frame (RotY(-ry), RotZ(-rz), RotX(rx))
void LocalSaved(const Boss2Task& b, float out[3]) {
    Mat4 m = Mat4::Identity();
    mx::RotY(m, -b.rot[1]);
    mx::RotZ(m, -b.rot[2]);
    mx::RotX(m, b.rot[0]);
    const float v[3] = { b.c.saved[0] - b.pos[0], b.c.saved[1] - b.pos[1], b.c.saved[2] - b.pos[2] };
    mx::Transform(m, v, out);
}

void TurnHome(World::Impl& w, Boss2Task& b, float k) {
    const Boss2Ctl& c = b.c;
    TurnTo(w, b, (c.saved[0] - c.dir[0] * k) - b.pos[0], (c.saved[2] - c.dir[2] * k) - b.pos[2], 0x200);
}

void TypingWalk(World::Impl& w, Boss2Task& b) {
    // Source469120: approach, back away to the saved point, then pick the
    // authored attack. The gun game's two-state walk omits this retreat.
    auto& c = b.c;
    static constexpr int approachChance[16] = {100,95,90,85,80,75,70,65,60,50,40,30,20,10,10,10};
    const int rank = std::clamp(w.rank, 0, 15);
    const bool basePhase = c.phase == 0 || c.phase == 3 || c.phase == 8;
    const auto motion = [&](int row) {
        c.row = static_cast<int16_t>(row);
        w.AnimBlendTo(b, MotionOfRow(w, row), 0, 10);
    };
    const auto turn = [&] { TurnTo(w,b,b.pos[0]-w.playerPos[0],b.pos[2]-w.playerPos[2],0x200); };
    const auto nearAttack = [&] {
        const float dx = b.pos[0]-w.playerPos[0], dz = b.pos[2]-w.playerPos[2];
        const float distance = std::sqrt(dx*dx+dz*dz);
        if (basePhase) return distance < 55.0f;
        if (c.phase != 2 && c.phase != 9 && c.phase != 5 && c.phase != 6 && c.phase != 7) return false;
        return distance < 135.0f && std::abs(LocalAngle(w,b,dx,dz)) < 0x2000;
    };
    if (c.sub == 0) {
        b.typingWalkChoice = w.Rand(0x00469143)%100;
        c.row94 = 0;
        if (c.m88 == c.m8C) c.m8E = static_cast<int16_t>(kTypingChest[rank].open[0]);
        if (basePhase) b.typingFlags &= ~0x400u;
        c.sub = 1;
    }
    if (c.sub == 1) {
        int row = 5;
        if (b.typingWalkChoice < approachChance[rank]) {
            if ((Thr(w,c.phase)+1.0f)*float(b.hpMax)*.5f < float(b.hp)) row = 2;
            else row = c.level == 15 ? 4 : c.level == 0 ? 0 : c.level < 3 ? 3 : 5;
        }
        motion(row);
        c.sub = 2;
    }
    if (c.sub == 2) {
        turn();
        if (Wall(b.pos,c.corner[0],c.corner[1],15.0f)) Wall(b.pos,c.corner[2],c.corner[3],15.0f);
        if (nearAttack()) { motion(basePhase ? 0x1A : 0x19); c.sub = 3; }
        return;
    }
    if (c.sub == 3) {
        float local[3];
        LocalSaved(b,local);
        if (local[2] >= 3.0f) { TurnHome(w,b,50.0f); return; }
        if (basePhase && c.m8E > -1) { b.anim.tick[0] = 0; c.sub = 1; return; }
        motion(b.typingWalkChoice < approachChance[rank] ? 2 : 5);
        c.sub = 4;
        return;
    }
    if (c.sub == 4) {
        turn();
        if (nearAttack()) {
            c.state = basePhase ? 8 : (c.phase == 2 || c.phase == 9) ? 9 : 12;
            c.sub = 0;
            PickTarget(w,b);
        }
    }
}

void TypingInterlude(World::Impl& w, Boss2Task& b) {
    // Source424CB0/424E90: 180 calls, with 30-frame lighting fades around
    // the four original instruction glyphs. Boss Mode starts at frame180.
    auto& c = b.c;
    if (c.sub == 0) {
        c.row = 8;
        w.AnimBlendTo(b,MotionOfRow(w,8),0,10);
        b.typingInterlude = w.cfg.gameType == 3 ? 180 : 0;
        for (int i=0;i<3;++i) b.typingInterludeStep[i] = (w.env.lightColor[i]-.4f)*(1.0f/30.0f);
        c.sub = 1;
        return;
    }
    const int frame = b.typingInterlude;
    if (w.cfg.gameType != 3) {
        if (frame < 30 || frame > 150)
            for (int i=0;i<3;++i) w.env.lightColor[i] += (frame < 30 ? -1.0f : 1.0f)*b.typingInterludeStep[i];
        else {
            if (frame == 30) w.TypingSound(0x436A9);
            for (int i=0;i<4;++i) w.TypingSprite(0xBE0+i,128.0f*float(i+1),334,1,.1f,0xA,0x00424D30);
        }
    }
    if (++b.typingInterlude >= 180) {
        c.row = 0xC;
        w.AnimBlendTo(b,MotionOfRow(w,0xC),0,10);
        c.state = 5; c.sub = 0; c.phase = 2;
        b.flags34 |= 0x10000000u;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// States (0x00596218)
// ---------------------------------------------------------------------------------------------------------------------

void StartFight(World::Impl& w, Boss2Task& b, bool flagA) {  // letterbox open: state 5, bar, boss active
    Boss2Ctl& c = b.c;
    c.state = 5;
    c.sub = 0;
    memcpy(c.saved, b.pos, 12);
    b.flags34 &= ~0x8000u;
    c.flags &= ~1u;
    if (flagA) w.runner->flags[0xA] = 1;  // 0x009C720A
    w.SpawnBossBar(320.0f, 35.0f);        // 0x00435E50
    if (flagA) w.bossActive = 1;          // 0x009CA0EA
}

void St0(World::Impl& w, Boss2Task& b) {  // 0x00478160: intro (rises)
    Boss2Ctl& c = b.c;
    switch (c.sub) {
    case 0:
        w.SpawnNameCard(0x005966B8);
        c.phase = 0;
        if (c.state == 3) {
            c.row = 0xE;
            w.AnimSetMotion(b, MotionOfRow(w, 0xE));
            w.runner->flags[9] = 1;
            b.flags34 &= ~0x20000u;
            w.Sound(0xC16A9);
            c.sub = 3;
        } else {
            c.row = 0x13;
            w.AnimSetMotion(b, MotionOfRow(w, 0x13));
            ++c.sub;
        }
        break;
    case 1:
        if (b.anim.tick[0] == 0x1E) {
            b.flags34 &= ~0x20000u;
            w.Sound(0xC16A9);
            ++c.sub;
        }
        break;
    case 2:
        if (b.anim.tick[0] == Ticks(w, b)) {
            c.row = 0xE;
            w.AnimBlendTo(b, MotionOfRow(w, 0xE), 0, 10);
            w.runner->flags[9] = 1;
            ++c.sub;
        }
        break;
    case 3:
        if (b.anim.tick[0] == 0x37) {
            w.Sound(0x417A9);
        } else if (b.anim.tick[0] == 0x5A) {
            c.row = 0xF;
            w.AnimBlendTo(b, MotionOfRow(w, 0xF), 0, 10);
            ++c.sub;
        }
        [[fallthrough]];
    case 4:
        if (b.anim.counter[0] == 0x4B) w.Sound(0x17A9);
        if (w.letterbox == 1) StartFight(w, b, true);
        break;
    default:
        break;
    }
    if (c.m7C == c.m7E && c.m82 == 0) w.Sound(0x3516A9);
}

void St1(World::Impl& w, Boss2Task& b, bool second) {  // 0x004783B0 / 0x00478640 (second: state 2)
    Boss2Ctl& c = b.c;
    auto hide = [&] {
        w.AnimSetMotion(b, 0x20);
        b.flags34 |= 0x8C000;
        b.anim.drawFlags &= ~1u;
        b.anim.attachHidden = true;  // attachment list +0x40 byte 1 = 0
    };
    switch (c.sub) {
    case 0:
        if (second) {
            hide();
            c.phase = 8;
            ++c.sub;
            goto sub1;
        }
        w.SpawnNameCard(0x005966F8);
        c.phase = 3;
        if (c.state == 4) {
            w.AnimSetMotion(b, 0x26);
            w.runner->flags[9] = 1;
            w.Sound(0x417A9);
            c.sub = 4;
        } else {
            hide();
            ++c.sub;
        }
        break;
    case 1:
    sub1:
        if (w.runner->flags[second ? 0x0B : 0x5F] != 0) {
            w.shakeTimer = 0x30;  // 0x009C8E8C
            c.c9C = 0x1E;
            ++c.sub;
        }
        break;
    case 2: {
        const int v = c.c9C--;
        if (v == 0) {
            b.flags34 &= ~0xC000u;
            b.anim.drawFlags |= 1;
            b.anim.attachHidden = false;
            ++c.sub;
        }
        break;
    }
    case 3:
        if (b.anim.tick[0] == Ticks(w, b)) {
            w.AnimBlendTo(b, 0x26, 0, 10);
            b.flags34 &= ~0x80000u;
            if (!second) w.runner->flags[9] = 1;
            w.Sound(0x417A9);
            ++c.sub;
        }
        break;
    case 4:
        if (b.anim.tick[0] == 0x37) {
            w.Sound(0x417A9);
        } else if (b.anim.tick[0] == 0x5A) {
            w.AnimBlendTo(b, 0x27, 0, 10);
            ++c.sub;
        }
        [[fallthrough]];
    case 5:
        if (b.anim.counter[0] == 0x4B) w.Sound(0x17A9);
        if (w.letterbox == 1) StartFight(w, b, !second);
        break;
    default:
        break;
    }
    if (2 < c.sub && c.m7C == c.m7E && c.m82 == 1) w.Sound(0x3516A9);
}

void St5(World::Impl& w, Boss2Task& b) {  // 0x00478870: walk at the players, pick an attack by the distance
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        const int ph = c.phase;
        int row;
        if (ph == 0 || ph == 3 || ph == 8) {
            if ((Thr(w, ph) + 1.0f) * static_cast<float>(b.hpMax) * 0.5f < static_cast<float>(b.hp)) row = 2;
            else if (c.level == 0xF) row = 4;
            else if (c.level == 0) row = 0;
            else if (c.level < 3) row = 3;
            else row = 5;
        } else {
            row = 5;
        }
        c.row = static_cast<int16_t>(row);
        w.AnimBlendTo(b, MotionOfRow(w, row), 0, 10);
        b.flags34 &= ~0x100u;
        ++c.sub;
    } else if (c.sub != 1) {
        goto tail;
    }
    {
        TurnTo(w, b, b.pos[0] - w.playerPos[0], b.pos[2] - w.playerPos[2], 0x200);
        const float dz = b.pos[2] - w.playerPos[2], dx = b.pos[0] - w.playerPos[0];
        const float dist = std::sqrt(dz * dz + dx * dx);
        bool pick = false;
        switch (c.phase) {
        case 0: case 3: case 8:
            if (F32(w.exe, 0x005691A0) <= dist) break;
            c.state = 8;
            c.sub = 0;
            PickTarget(w, b);
            goto tail;
        case 2: case 9: case 5: case 6: case 7: {
            if (F32(w.exe, 0x0056919C) <= dist) break;
            int a = LocalAngle(w, b, dx, dz);
            if (a < 0) a = -a;
            if (0x1FFF < a) break;
            c.state = (c.phase == 2 || c.phase == 9) ? 9 : 0xC;
            pick = true;
            break;
        }
        default:
            break;
        }
        if (pick) {
            c.sub = 0;
            PickTarget(w, b);
        }
    }
tail:
    if (b.anim.motion[0] == 0x19 || b.anim.motion[0] == 0x1A) {
        if (c.state == 5) {
            if (c.m88 == c.m8A) c.m8E = 0;
            else if (c.m88 == c.m8C) c.m8E = -1;
        } else if (c.m88 == c.m8C) {
            c.m8E = 10;
        }
    }
}

void St6(World::Impl& w, Boss2Task& b) {  // 0x00478C00: back to the start point (0x9C 0) / to the wall (0x9C 1)
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        int row;
        switch (c.phase) {
        case 2: case 4: case 5: case 6: case 7: case 9: row = 0x19; break;
        default: row = 0x1A; break;
        }
        c.row = static_cast<int16_t>(row);
        w.AnimBlendTo(b, MotionOfRow(w, row), 0, 10);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    if (c.c9C == 0) {
        float l[3];
        LocalSaved(b, l);
        if (l[2] < F32(w.exe, 0x0055D2B4)) {
            c.state = 7;
            c.sub = 0;
            c.jumps = 2;
            return;
        }
        TurnHome(w, b, F32(w.exe, 0x0055D2AC));
    } else if (c.c9C == 1) {
        if (Wall(b.pos, c.corner[1], c.corner[2], 10.0f) != 0) {
            TurnHome(w, b, F32(w.exe, 0x0055D2AC));
            return;
        }
        c.state = 0xF;
        c.sub = 0;
    }
}

void St7(World::Impl& w, Boss2Task& b) {  // 0x00478E30: roar (row 8) c.jumps times
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        c.row = 8;
        w.AnimBlendTo(b, MotionOfRow(w, 8), 0, 10);
        --c.jumps;
        w.Sound(0xD17A9);
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    if (b.anim.tick[0] == Ticks(w, b) - 1 && 0 < w.players) {
        if (c.jumps == 0) {
            c.state = 5;
            c.sub = 0;
            return;
        }
        w.Sound(0xD17A9);
        --c.jumps;
    }
}

void St8(World::Impl& w, Boss2Task& b) {  // 0x00478EE0: swipe (row 0x15), Player_TakeHit at tick 0x3C
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        c.row = 0x15;
        w.AnimBlendTo(b, MotionOfRow(w, 0x15), 0, 10);
        b.flags34 &= ~0x100u;
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    const int t = b.anim.tick[0];
    if (w.typingReferenceActive && t > 0x37) {
        c.m8E = c.m88 == c.m8A ? 1 : 0;
        b.typingFlags |= 0x400u; // Source469D40: the swipe's word window has closed.
    }
    if (0x1D < t && t < 0x38) {
        if (w.route == 2) {
            if (b.target == 0) b.rot[1] -= 0x1C;
            else if (b.target == 1) b.rot[1] += 4;
        } else {
            b.rot[1] -= 0xC;
        }
    }
    if (t == 0x37) {
        b.flags34 |= 0x2000;
    } else if (t == 0x3C) {
        w.PlayerTakeHit(b.target, 1, 6);
        b.flags34 &= ~0x2000u;
        if (--c.row94 < 0) c.row94 = 0;
    } else if (t == Ticks(w, b) - 1) {
        c.state = 6;
        c.sub = 0;
        c.c9C = 0;
        b.flags34 &= 0xEFFFDFFFu;
    }
}

// states 9 (0x00479030) / 12 (0x0047A2E0): jump at the target, Player_TakeHit on landing, back off
void StJump(World::Impl& w, Boss2Task& b, bool twelve) {
    Boss2Ctl& c = b.c;
    switch (c.sub) {
    case 0:
        if (!twelve) {
            c.row = 0x17;
            w.AnimBlendTo(b, MotionOfRow(w, 0x17), 0, 10);
            b.flags34 = (b.flags34 & 0xFFFFDEFFu) | 0x10000000u;
        } else {
            w.AnimBlendTo(b, 0x33, 0, 10);
            b.flags34 |= 0x10000000;
            b.flags34 &= ~0x100u;
        }
        ZeroVel(b);
        BossWords(w,b); // Source469E38/46A708 starts a fresh phrase for the leap.
        ++c.sub;
        [[fallthrough]];
    case 1: {
        float dx, dz;
        const float off = twelve ? 3.0f : 2.0f;
        TargetPoint(w, b, -off, off, &dx, &dz);
        TurnTo(w, b, dx, dz, 0x200);
        if (b.anim.tick[0] == 0x23) {
            float k = F32(w.exe, 0x0055D2A4);
            if (!twelve && c.phase == 9) k = F32(w.exe, 0x005691A8);
            JumpAt(w, b, k);
            if (w.typingReferenceActive) {
                static constexpr float speed[16] = {.4f,.4f,.41f,.42f,.43f,.43f,.44f,.44f,.45f,.46f,.48f,.52f,.54f,.56f,.58f,.58f};
                const float scale = speed[std::clamp(w.rank,0,15)];
                for (float& velocity : b.vel) velocity *= scale;
                b.acc[1] *= scale;
                b.typingFlags &= ~0x400u;
            }
            w.Sound(0x2A16A9);
            ++c.sub;
        }
        break;
    }
    case 2:
        if (b.anim.tick[0] == 0x41) b.flags34 |= 0x4000;
        if (b.vel[1] < 0.0f) {
            b.flags34 &= ~0x4000u;
            const int r = w.Rand(twelve ? 0x0047A4E6 : 0x00479256) & 3;
            c.m8E = static_cast<int16_t>(r * 8 + 1);
            ++c.sub;
        }
        c.m8E = c.m88 == c.m8A ? 1 : 0;
        break;
    case 3:
        if (b.anim.tick[0] == 0x55) b.flags34 |= 0x4000;
        if (b.pos[1] < Floor(w, b.pos) + F32(w.exe, 0x0055CAD8)) {
            w.PlayerTakeHit(b.target, 1, 6);
            const float k = F32(w.exe, 0x004C4CC8);
            b.vel[0] = b.vel[0] * k;
            b.vel[1] = b.vel[1] * k;
            b.vel[2] = b.vel[2] * k;
            b.acc[1] = 0;
            c.c9C = 10;
            if (w.typingReferenceActive) { b.typingFlags |= 0x400u; BossWords(w,b); }
            if (--c.row94 < 0) c.row94 = 0;
            ++c.sub;
        }
        c.row94 = 7;
        break;
    case 4: {
        const int v = c.c9C--;
        if (v == 0) {
            b.flags34 &= ~0x4000u;
            const float k = F32(w.exe, 0x005691A4);
            b.vel[0] = b.vel[0] * k;
            b.vel[1] = 0.1f;
            b.vel[2] = b.vel[2] * k;
            b.acc[1] = Bits(0xBD8B60B6);
            ++c.sub;
        }
        break;
    }
    case 5:
        if (b.pos[1] < Floor(w, b.pos)) {
            ZeroVel(b);
            b.pos[1] = Floor(w, b.pos);
        }
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            c.state = 6;
            c.sub = 0;
            c.c9C = 0;
            b.flags34 &= 0xEFFFDFFFu;
        }
        break;
    default:
        break;
    }
    if (!(c.flags & 4)) Integrate(b);
}

// the fish calls of states 10 / 11: fish counts by the remaining calls and the aggression (0x00596640), intervals
// 0x00596620 / 0x00596630
void FishWave(World::Impl& w, Boss2Task& b, bool eleven) {
    Boss2Ctl& c = b.c;
    if (0 < w.players) {
        if (c.cA0 == 0) {
            if (w.liveEnemies == 1) {
                if (c.c9C < 1) {
                    c.sub = static_cast<uint8_t>(eleven ? 8 : 6);
                } else if (!eleven) {
                    c.row = 0x1B;
                    w.AnimBlendTo(b, MotionOfRow(w, 0x1B), 0, 10);
                    b.flags34 |= 0x2000;
                    c.sub = 2;
                } else {
                    w.AnimBlendTo(b, 0x30, 0, 10);
                    b.flags34 |= 0x2000;
                    w.Sound(0x1617A9);
                    c.sub = 3;
                }
            }
        } else if (c.cA4 == 0) {
            if (w.liveEnemies < 4 && w.shakeTimer == 0) {
                float x, y, z;
                if (!eleven) {
                    const int r = w.Rand(0x00479944);
                    float k;
                    if (c.cA8 < r % 3) {
                        c.cA8 = 1;
                        x = (c.dir[2] * F32(w.exe, 0x0055CCD4) + b.pos[0]) - c.dir[0] * F32(w.exe, 0x004ECB7C);
                        k = c.dir[0] * F32(w.exe, 0x0055CCD4);
                    } else {
                        c.cA8 = 0;
                        x = (b.pos[0] - c.dir[2] * F32(w.exe, 0x004ECB7C)) - c.dir[0] * F32(w.exe, 0x004ECB7C);
                        k = c.dir[0] * F32(w.exe, 0x005691AC);
                    }
                    z = (b.pos[2] - k) - c.dir[2] * F32(w.exe, 0x004ECB7C);
                    const float p[3] = { x, 0, z };
                    y = WaterLevelAt(w, p) - 1.0f;  // 0x00442390(p)
                    w.SpawnFishJump(x, y, z, 100, 1);  // 0x00438640
                } else {
                    const float f2 = static_cast<float>(w.Rand(0x00479EDA) % 0xB) + F32(w.exe, 0x004C4398);
                    const float f3 = static_cast<float>(w.Rand(0x00479EF9) & 0xF) + F32(w.exe, 0x004C4398);
                    const int r = w.Rand(0x00479F28);
                    if (c.cA8 < r % 3) {
                        c.cA8 = 1;
                        x = f3 * c.dir[0] + f2 * c.dir[2] + b.pos[0];
                        z = (f3 * c.dir[2] - f2 * c.dir[0]) + b.pos[2];
                    } else {
                        c.cA8 = 0;
                        x = f3 * c.dir[0] + (b.pos[0] - f2 * c.dir[2]);
                        z = f3 * c.dir[2] + (b.pos[2] - -(f2 * c.dir[0]));
                    }
                    const float p[3] = { x, 0, z };
                    y = WaterLevelAt(w, p) - F32(w.exe, 0x004C43A4);  // 0x00442390(p)
                    w.SpawnFishJump(x, y, z, 0x50, 2);
                }
                --c.cA0;
                c.cA4 = static_cast<int8_t>(U8(w.exe, (eleven ? 0x00596630 : 0x00596620) + static_cast<uint32_t>(c.level)));
            }
        } else {
            --c.cA4;
        }
    }
    if (BelowPhase(w, b)) c.sub = static_cast<uint8_t>(eleven ? 8 : 6);
    const bool lost = (w.players == 1 && w.player[w.playersInGame & 1].lives < static_cast<int16_t>(c.lives[w.playersInGame & 1])) ||
                      w.player[0].lives < c.lives[0] || w.player[1].lives < c.lives[1];
    if (lost) c.cAC = 0;
}

void NextCall(World::Impl& w, Boss2Task& b, bool eleven) {  // a call ended: the count of fish for the next one
    Boss2Ctl& c = b.c;
    if (c.cAC != 0) {
        ++c.level;
        if (0xF < c.level) c.level = 0xF;
    }
    if (!eleven) {
        c.row = 0xC;
        w.AnimBlendTo(b, MotionOfRow(w, 0xC), 0, 10);
    } else {
        w.AnimBlendTo(b, 0x23, 0, 10);
    }
    b.flags34 &= ~0x2000u;
    --c.c9C;
    c.cA0 = static_cast<int8_t>(U8(w.exe, 0x00596640 + static_cast<uint32_t>(c.c9C + c.level * 3)));
    c.cA4 = 0;
    c.cAC = 1;
}

void St10(World::Impl& w, Boss2Task& b) {  // 0x00479530: back to the start, three calls of jumping fish
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        for (int& v : w.fishSlots) v = 0;  // 0x009A2C20
        c.cA8 = w.Rand(0x00479577) & 1;
        c.cAC = 0;
    }
    if (c.sub == 0 || c.sub == 1) {
        float l[3];
        LocalSaved(b, l);
        if ((c.sub == 0 && l[2] < F32(w.exe, 0x004C4398)) || (c.sub == 1 && l[2] < F32(w.exe, 0x0055D2B4))) {
            c.row = 0x1B;
            w.AnimBlendTo(b, MotionOfRow(w, 0x1B), 0, 10);
            b.flags34 = (b.flags34 & ~0x100u) | 0x2000u;
            c.m8E = 0;
            c.c9C = 3;
            c.sub = 2;
        } else {
            TurnHome(w, b, F32(w.exe, 0x0055D2AC));
        }
    }
    switch (c.sub) {
    case 0:
        c.row = 0x1A;
        w.AnimBlendTo(b, MotionOfRow(w, 0x1A), 0, 10);
        ++c.sub;
        return;
    case 2:
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            NextCall(w, b, false);
            c.sub = 4;
            return;
        }
        if (b.anim.tick[0] == 0x2D) w.Sound(0x1617A9);
        return;
    case 3:
        if (!BelowPhase(w, b) && 0 < c.c9C) {
            c.row = 0x1B;
            w.AnimBlendTo(b, MotionOfRow(w, 0x1B), 0, 10);
            b.flags34 |= 0x2000;
            c.sub = 2;
            return;
        }
        c.row = 0xC;
        w.AnimBlendTo(b, MotionOfRow(w, 0xC), 0, 10);
        c.sub = 6;
        return;
    case 4:
        FishWave(w, b, false);
        return;
    case 5:
        c.row = 0xC;
        w.AnimBlendTo(b, MotionOfRow(w, 0xC), 0, 10);
        c.sub = 6;
        [[fallthrough]];
    case 6:
        if (c.cAC != 0) {
            if (0xF < ++c.level) c.level = 0xF;
            c.cAC = 0;
        }
        if (w.attackers == 1 && 0 < w.players) {
            c.state = 5;
            c.sub = 0;
            c.phase = 2;
            b.flags34 |= 0x10000000;
        }
        return;
    default:
        return;
    }
}

void St11(World::Impl& w, Boss2Task& b) {  // 0x00479BE0: in the water: calls, camera 0x6A / 0x6B, then state 13
    Boss2Ctl& c = b.c;
    if (w.typingReferenceActive && c.sub >= 3 && c.sub <= 10)
        for (int i=0;i<4;++i) w.TypingSprite(0xBE0+i,128.0f*float(i+1),334,1,.1f,0xA,0x0046A386);
    switch (c.sub) {
    case 0:
        if (w.typingReferenceActive) { b.typingFlags |= 0x400u; BossWords(w,b); }
        w.AnimBlendTo(b, 0x23, 0, 10);
        b.flags34 &= ~0x4000u;
        c.c9C = 3;
        for (int& v : w.fishSlots) v = 0;
        c.cAC = 0;
        ++c.sub;
        return;
    case 1:
        if (w.runner->camera.aimedOnce || w.runner->camera.settled) {
            w.runner->flags[0xB] = 1;
            ++c.sub;
        }
        return;
    case 2:
        if (w.runner->camera.camId == (w.typingReferenceActive ? 0x407A : 0x6A) && w.runner->camera.tick == 0x5F) {
            b.flags34 &= 0xFFFEFEFFu;
            if (!w.typingReferenceActive) { c.m8E = 0; c.cA8 = 0x1E; }
            c.sub = w.typingReferenceActive ? 7 : 4; // Source46A4A6 skips the gun-only fish wave.
        }
        return;
    case 3:
        if (b.anim.tick[0] == Ticks(w, b)) {
            NextCall(w, b, true);
            c.sub = 6;
        }
        return;
    case 4: {
        const int v = c.cA8--;
        if (v != 0) return;
        c.cA8 = w.Rand(0x00479DCB) & 1;
    }
        [[fallthrough]];
    case 5:
        if (!BelowPhase(w, b) && 0 < c.c9C) {
            w.AnimBlendTo(b, 0x30, 0, 10);
            b.flags34 |= 0x2000;
            w.Sound(0x1617A9);
            c.sub = 3;
            return;
        }
        w.AnimBlendTo(b, 0x23, 0, 10);
        c.sub = 8;
        return;
    case 6:
        FishWave(w, b, true);
        return;
    case 7:
        w.AnimBlendTo(b, 0x23, 0, 10);
        c.sub = 8;
        [[fallthrough]];
    case 8:
        if (c.cAC != 0) {
            if (0xF < ++c.level) c.level = 0xF;
            c.cAC = 0;
        }
        if (w.typingReferenceActive || (w.attackers == 1 && 0 < w.players)) {
            w.AnimBlendTo(b, 0x2A, 0, 10);
            c.sub = 9;
            c.c9C = w.Rand(0x0047A19A) & 1;
            b.flags34 |= 0x100;
        }
        return;
    case 9: {
        float pt[3] = { c.c9C == 0 ? 190.0f : 270.0f, b.pos[1], -2150.0f };
        if (!w.EnemyPointBehind(b, pt)) {  // 0x0045BC10
            TurnTo(w, b, b.pos[0] - pt[0], b.pos[2] - pt[2], 0x200);
            return;
        }
        b.flags34 |= 0x10000;
        ++c.sub;
        return;
    }
    case 10:
        if (w.runner->camera.aimedOnce || w.runner->camera.settled) {
            w.runner->flags[0xC] = 1;
            ++c.sub;
        }
        return;
    case 11:
        if (w.runner->camera.camId == (w.typingReferenceActive ? 0x407B : 0x6B) && w.runner->camera.tick == 100) {
            c.state = 0xD;
            c.sub = 0;
            c.phase = 5;
            if (w.typingReferenceActive) b.typingFlags &= ~0x400u;
        }
        return;
    default:
        return;
    }
}

void St13(World::Impl& w, Boss2Task& b) {  // 0x0047A7C0: comes out of the water beside the arena (motion 0x38 / 0x39)
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        if (w.typingReferenceActive) b.typingFlags |= 0x400u;
        const float mid = (c.corner[2][0] + c.corner[1][0]) * F32(w.exe, 0x004C43AC);
        if (c.c9C == 0) {
            w.AnimSetMotion(b, 0x38);
            b.pos[0] = mid - F32(w.exe, 0x00569130);
        } else {
            w.AnimSetMotion(b, 0x39);
            b.pos[0] = mid + F32(w.exe, 0x00569130);
        }
        b.rot[1] = AngleOf(-c.dir[0], -c.dir[2]);
        b.pos[2] = c.saved[2];
        b.pos[1] = Floor(w, b.pos);
        b.flags34 &= 0xFFF7BFFFu;
        ++c.sub;
    } else if (c.sub == 1) {
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            c.state = 5;
            c.sub = 0;
            b.flags34 &= 0xFFFFDEFFu;
            c.flags &= ~1u;
            c.m8E = 0;
            return;
        }
        if (b.anim.tick[0] == 10) {
            b.flags34 &= ~0x10000u;
            const int rot[3] = { 0, w.cam.yaw, 0 };
            w.SpawnExplosionAt(b.pos, rot, 0, 4.5f);  // 0x0043FCA0
        }
    }
}

void St14(World::Impl& w, Boss2Task& b) {  // 0x0047A990: leaps out of the water at the target
    Boss2Ctl& c = b.c;
    switch (c.sub) {
    case 0: {
        const float mid = (c.corner[2][0] + c.corner[1][0]) * F32(w.exe, 0x004C43AC);
        b.pos[0] = c.c9C == 0 ? mid - F32(w.exe, 0x00564420) : mid + F32(w.exe, 0x00564420);
        w.AnimBlendTo(b, 0x33, 0x23, 0);
        b.pos[2] = c.saved[2];
        b.pos[1] = Floor(w, b.pos);
        if (w.route == 2) {
            float off = 0;
            if (b.target == 0) off = c.c9C == 0 ? -3.0f : -5.0f;
            else if (b.target == 1) off = c.c9C == 0 ? 5.0f : 3.0f;
            float pt[3];
            const float l[3] = { off, 0.0f, 0.0f };
            mx::Transform(w.cam.inverse, l, pt);
            b.rot[1] = AngleOf(b.pos[0] - pt[0], b.pos[2] - pt[2]);
        } else {
            b.rot[1] = AngleOf(b.pos[0] - w.playerPos[0], b.pos[2] - w.playerPos[2]);
        }
        JumpAt(w, b, F32(w.exe, 0x0055D2A4));
        b.flags34 = (b.flags34 & 0xFFF7BFFFu) | 0x10000000u;
        ++c.sub;
        break;
    }
    case 1: {
        const int t = b.anim.tick[0];
        if (t == 0x41) {
            b.flags34 |= 0x4000;
        } else if (t == 0x28) {
            const int rot[3] = { 0, w.cam.yaw, 0 };
            w.SpawnExplosionAt(b.pos, rot, 0, 4.5f);
        } else if (t == 0x2D) {
            w.Voice(c.c9C == 0 ? 0x56 : 0x55);  // 0x00435B80
            b.flags34 &= ~0x10000u;
        }
        if (b.vel[1] < 0.0f) {
            b.flags34 &= 0xFFFF9EFFu;
            if (w.typingReferenceActive) b.typingFlags &= ~0x400u;
            const int r = w.Rand(0x0047AC8F) & 3;
            c.m8E = static_cast<int16_t>(r * 8 + 1);
            ++c.sub;
        }
        break;
    }
    case 2:
        if (c.m8E == 0 && c.m88 == c.m8A) {
            c.flags &= ~1u;
            ++c.sub;
        }
        [[fallthrough]];
    case 3:
        if (b.anim.tick[0] == 0x55) b.flags34 |= 0x4000;
        if (b.pos[1] < Floor(w, b.pos) + F32(w.exe, 0x0055CAD8)) {
            w.PlayerTakeHit(b.target, 1, 6);
            const float k = F32(w.exe, 0x004C4CC8);
            b.vel[0] = b.vel[0] * k;
            b.vel[1] = b.vel[1] * k;
            b.vel[2] = b.vel[2] * k;
            b.acc[1] = 0;
            c.c9C = 10;
            if (w.typingReferenceActive) {
                b.typingFlags = (b.typingFlags & ~0x40u) | 0x400u;
                BossWords(w,b);
            }
            if (--c.row94 < 0) c.row94 = 0;
            c.sub = 4;
        }
        c.row94 = 7;
        break;
    case 4: {
        const int v = c.c9C--;
        if (v == 0) {
            b.flags34 &= ~0x4000u;
            const float k = F32(w.exe, 0x005691A4);
            b.vel[0] = b.vel[0] * k;
            b.vel[1] = 0.1f;
            b.vel[2] = b.vel[2] * k;
            b.acc[1] = Bits(0xBD8B60B6);
            ++c.sub;
        }
        break;
    }
    case 5:
        if (b.pos[1] < Floor(w, b.pos)) {
            ZeroVel(b);
            b.pos[1] = Floor(w, b.pos);
        }
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            c.state = 6;
            c.sub = 0;
            c.c9C = 0;
            b.flags34 &= ~0x10000000u;
        }
        break;
    default:
        break;
    }
    if (!(c.flags & 4)) Integrate(b);
}

void St15(World::Impl& w, Boss2Task& b) {  // 0x0047AF60: dives into the water, camera paths 0x6C / 0x6D
    Boss2Ctl& c = b.c;
    switch (c.sub) {
    case 0:
        w.AnimBlendTo(b, 0x24, 0, 10);
        w.Sound(0x2A16A9);
        ++c.sub;
        return;
    case 1:
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            c.cA4 = 10;
            b.flags34 |= 0x14000;
            ++c.sub;
            return;
        }
        if (b.anim.tick[0] == 10) b.flags34 |= 0x80000;
        return;
    case 2: {
        const int v = c.cA4--;
        if (v != 0) return;
        b.pos[1] = WaterLevelAt(w, b.pos);
        b.pos[0] = b.pos[0] - c.dir[0] * F32(w.exe, 0x004ECB7C);
        b.pos[2] = b.pos[2] - c.dir[2] * F32(w.exe, 0x004ECB7C);
        const int yaw = w.cam.yaw;
        b.rot[1] = static_cast<int16_t>(-0x8000 - static_cast<int16_t>(yaw));
        const int rot[3] = { 0, yaw, 0 };
        w.SpawnExplosionAt(b.pos, rot, 0, 8.0f);
        c.cA4 = 0x14;
        ++c.sub;
        return;
    }
    case 3: {
        const int v = c.cA4--;
        if (v != 0) return;
        if (c.phase == 4) {
            c.state = 0xB;
            c.sub = 0;
            return;
        }
        ++c.sub;
        return;
    }
    case 4:
        if (w.runner->camera.aimedOnce || w.runner->camera.settled) {
            w.runner->camera.forceState6 = true;  // 0x009CA094
            if (c.phase == 6) w.runner->flags[0xD] = 1;
            else if (c.phase == 7) w.runner->flags[0xF] = 1;
            c.c9C = (w.Rand(0x0047B15E) & 1) == 0 ? 0x6D : 0x6C;
            if (w.typingReferenceActive) c.c9C += 0x4010;
            c.cA0 = 0;
            ++c.sub;
        }
        return;
    case 5:
        if (c.cA0 == 0xA1) {
            w.runner->camera.forceState6 = false;
            if (c.phase == 6) w.runner->flags[0xE] = 1;
            else if (c.phase == 7) w.runner->flags[0x10] = 1;
            c.c9C = w.Rand(0x0047B1E3) & 1;
            c.state = 0xE;
            c.sub = 0;
            if (w.typingReferenceActive) {
                // Source46B60F: remove the old word before forcing the next
                // leap's abbreviation record. The global selection is kept.
                ++b.typingRevision;
                b.typingFlags |= 0x40u;
            }
            PickTarget(w, b);
            return;
        }
        {
            CameraContext& c0 = w.runner->camera.ctx[0];
            int roll = 0;
            if (!w.cameraBank.Evaluate(c.c9C, static_cast<float>(c.cA0), c0.pos, c0.target, &roll, false))
                w.Note("stage-2 boss camera path missing");
        }
        ++c.cA0;
        return;
    default:
        return;
    }
}

void St16(World::Impl& w, Boss2Task& b) {  // 0x0047B280: staggers after a chest hit, then back to the old state
    Boss2Ctl& c = b.c;
    if (c.sub == 0) {
        if (w.typingReferenceActive) b.typingFlags |= 0x400u;
        if (c.prevState == 0xB) {
            w.AnimBlendTo(b, 0x29, 0, 10);
        } else {
            c.row = 10;
            w.AnimBlendTo(b, MotionOfRow(w, 10), 0, 10);
            if (b.flags34 & 0x20000) b.pos[1] = Floor(w, b.pos);
        }
        c.m90 = -1.0f;
        c.m8E = 0;
        ++c.sub;
    } else if (c.sub != 1) {
        return;
    }
    TurnTo(w, b, (c.saved[0] - c.dir[0] * F32(w.exe, 0x004C43B0)) - b.pos[0],
           (c.saved[2] - c.dir[2] * F32(w.exe, 0x004C43B0)) - b.pos[2], 0x200);
    if (b.anim.tick[0] == Ticks(w, b) - 1) {
        const uint8_t ps = c.prevState;
        switch (ps) {
        case 7: c.state = 5; c.jumps = 0; c.sub = 0; break;
        case 8: case 9: case 0xC: case 0xE: c.state = 6; c.sub = 0; c.c9C = 0; break;
        case 10: case 0xB: c.state = ps; c.sub = static_cast<uint8_t>(c.prevSub - 1); break;
        default: c.state = ps; c.sub = 0; break;
        }
        b.flags34 &= ~0x40000000u;
        const int r = w.Rand(0x0047B40F);
        c.m8E = static_cast<int16_t>(((r % 7) + 4) * 5);
        BossWords(w,b);
        return;
    }
    if (b.anim.tick[0] == 0x14 && (b.flags34 & 0x4000000)) {
        const int ph = c.phase;
        if (ph == 2) {
            c.state = 0x12;
            w.runner->flags[0x11] = 1;
        } else if (ph == 7) {
            c.state = 0x13;
            w.runner->flags[0x11] = 1;
        } else if (ph == 9) {
            c.state = 0x14;
            w.runner->flags[0x1F] = 1;
        }
        c.sub = 0;
    }
    if (c.m88 == c.m8A) c.m8E = -1;
}

void St17(World::Impl& w, Boss2Task& b) {  // 0x0047B4C0: knocked back in the air (hit while jumping)
    Boss2Ctl& c = b.c;
    auto blendRow = [&](int row) {
        c.row = static_cast<int16_t>(row);
        const int n = c.c9C--;
        w.AnimBlendTo(b, MotionOfRow(w, row), c.cA0 - c.c9C, n);
    };
    auto land = [&] {
        c.cA0 = 10;
        c.c9C = 10;
        blendRow(0x10);
        w.Sound(c.phase == 2 ? 0xC16A9 : 0x1D16A9);
        c.sub = 3;
    };
    switch (c.sub) {
    case 0: {
        if (w.typingReferenceActive) { c.flags &= ~1u; b.typingFlags |= 0x400u; }
        c.cA0 = 10;
        c.c9C = 10;
        blendRow(0x14);
        const float dz = b.pos[2] - w.playerPos[2];
        const float dx = b.pos[0] - w.playerPos[0];
        const int pitch = AngleOf(b.pos[1] - w.playerPos[1], std::sqrt(dx * dx + dz * dz));
        const int yaw = AngleOf(dx, dz);
        if (c.phase == 2) {
            const float cp = CosA(pitch);
            b.vel[0] = cp * c.dir[0] * F32(w.exe, 0x005691A4);
            b.vel[2] = cp * c.dir[2] * F32(w.exe, 0x005691A4);
        } else {
            const float cp = CosA(pitch);
            b.vel[0] = SinA(yaw) * cp * F32(w.exe, 0x004C4CBC);
            b.vel[2] = CosA(yaw) * cp * F32(w.exe, 0x004C4CBC);
        }
        b.vel[1] = SinA(pitch) * F32(w.exe, 0x004C4CBC);
        b.acc[1] = Bits(0xBDA740DA);  // -0.08166666
        c.m90 = -1.0f;
        c.m8E = 0;
        ++c.sub;
        break;
    }
    case 1:
        if (c.c9C != 0) blendRow(0x14);
        if (c.m88 == c.m8A) c.m8E = -1;
        if (b.pos[1] < Floor(w, b.pos)) {
            const float k = F32(w.exe, 0x004C4CC8);
            b.vel[0] = b.vel[0] * k;
            b.vel[2] = b.vel[2] * k;
            b.acc[1] = 0;
            b.vel[1] = 0;
            b.pos[1] = Floor(w, b.pos);
            if (b.flags34 & 0x4000) {
                land();
                b.flags34 &= ~0x4000u;
                break;
            }
            c.sub = 2;
        }
        [[fallthrough]];
    case 2:
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            if (c.sub == 1) b.flags34 |= 0x4000;
            else land();
        }
        if (c.m88 == c.m8A) c.m8E = -1;
        break;
    case 3:
        if (c.c9C != 0) blendRow(0x1D);
        if (b.anim.tick[0] == Ticks(w, b)) {
            b.vel[2] = 0;
            b.vel[0] = 0;
            c.row = 0xD;
            w.AnimBlendTo(b, MotionOfRow(w, 0xD), 0, 10);
            c.sub = 4;
        }
        if (c.m88 == c.m8A) c.m8E = -1;
        break;
    case 4:
        if (b.anim.tick[0] == Ticks(w, b) - 1) {
            c.state = 6;
            c.c9C = 0;
            c.sub = 0;
            b.flags34 &= ~0x40000000u;
            const int r = w.Rand(0x0047BA15);
            c.m8E = static_cast<int16_t>(((r % 7) + 4) * 5);
            BossWords(w,b);
            break;
        }
        if (b.anim.tick[0] == 0x3C && (b.flags34 & 0x4000000)) {
            const int ph = c.phase;
            if (ph == 2) {
                c.state = 0x12;
                w.runner->flags[0x11] = 1;
            } else if (ph == 7) {
                c.state = 0x13;
                w.runner->flags[0x11] = 1;
            } else if (ph == 9) {
                c.state = 0x14;
                w.runner->flags[0x1F] = 1;
            }
            c.sub = 0;
        }
        break;
    default:
        break;
    }
    Integrate(b);
}

// 0x0045CE70: the new motion's root (frame tick / 2) placed so node 1 stays where it is
void KeepNode1(World::Impl& w, Boss2Task& b) {
    const int nodes = w.exe.S16(0x004E0724 + b.anim.kind * 2);
    const Motion* m = w.EnemyMotion(b.anim.motion[0], nodes);
    float root[3] = {};
    int16_t r0[3] = {};
    if (m && !m->data.empty() && b.anim.tick[0] / 2 < m->StoredFrames()) {
        m->RootPosition(b.anim.tick[0] / 2, root);
        m->Rotation(b.anim.tick[0] / 2, 0, r0);
    } else {
        w.Note("stage-2 boss: motion frame data missing (0x00412F50)");
    }
    float cur[3];
    {
        const Mat4 q = NodeTimesCamera(w, b, 1);  // 0x004A9230(camera), 0x004A92A0(node 1 +0x28)
        mx::GetTranslation(q, cur);
    }
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

void DeathCommon(World::Impl& w, Boss2Task& b) {  // the end of states 18..20
    Boss2Ctl& c = b.c;
    if (c.c9C == 0) {
        b.flags34 |= 0x10000;
        --w.attackers;
        if (w.residency) w.resQueue.push_back({ 0x53, 0x2B });  // 0x0041D690(0x2B)
    }
}

void StDeath(World::Impl& w, Boss2Task& b, int which) {  // 0x0047BAD0 (18) / 0x0047BFE0 (19) / 0x0047C5F0 (20)
    Boss2Ctl& c = b.c;
    const uint8_t s = c.sub;
    auto bounceStart = [&] {
        c.f58 = 0;
        c.f5C = 0x200;
        b.vel[1] = b.vel[1] / SinA(c.f5C);
        b.flags34 |= 0x4000;
        ++c.sub;
    };
    auto toFloat = [&] {  // motion 0x2C placed at node 1 (0x0045CE70), drifting
        c.row = 0x12;
        b.anim.motion[0] = 0x2C;
        b.anim.tick[0] = 0;
        KeepNode1(w, b);
        w.AnimBlendTo(b, b.anim.motion[0], 0, 10);
        b.vel[0] = -0.03f;
        b.vel[2] = -0.005f;
        b.vel[1] = 0;
        b.acc[1] = Bits(0x3A326750);  // 0.0006805556
        b.rot[1] = 0x4000;
        ++c.sub;
    };
    if (s == 0) {
        if (which == 18) {
            c.row = 0x1C;
            w.AnimBlendTo(b, MotionOfRow(w, 0x1C), 0, 10);
            c.flags |= 1;
            b.pos[0] = Bits(0xC4A74666);  // -1338.2
            b.pos[1] = -23.0f;
            b.pos[2] = -1931.1f;
        } else {
            w.AnimBlendTo(b, 0x3A, 0, 0);
            if (which == 20) b.flags34 |= 0x10000;
            c.flags |= 1;
            if (which == 19) {
                b.pos[0] = Bits(0x43634CCD);  // 227.3
                b.pos[1] = 23.0f;
                b.pos[2] = -2121.9f;
            } else {
                b.pos[0] = Bits(0x4411CCCD);  // 583.2
                b.pos[1] = -71.3f;
                b.pos[2] = -4979.7f;
            }
        }
        b.rot[0] = 0;
        b.rot[1] = 0x8000;
        b.rot[2] = 0;
        if (which != 18) ZeroVel(b);
        if (which != 20) c.c9C = 0x78;
        if (w.residency) w.resQueue.push_back({ 0x52, 0x2B });  // 0x0041D650(0x2B)
        if (which != 18) w.Sound(0x1717A9);
        if (which != 20) w.bossActive = 0;
        ++c.sub;
    }
    if (c.sub == 1) {
        if (Wall(b.pos, c.corner[1], c.corner[2], 10.0f) == 0) {
            if (which == 18) {
                c.row = 6;
                w.AnimBlendTo(b, MotionOfRow(w, 6), 0, 10);
            } else {
                w.AnimBlendTo(b, 0x1B, 0, 5);
            }
            w.Sound(0x1417A9);
            ++c.sub;
        } else {
            TurnTo(w, b, (c.saved[0] - c.dir[0] * F32(w.exe, 0x0055D2AC)) - b.pos[0],
                   (c.saved[2] - c.dir[2] * F32(w.exe, 0x0055D2AC)) - b.pos[2], 0x200);
        }
        if (which == 20) return;
        goto tail;
    }
    if (which == 18) {
        switch (c.sub) {
        case 2: {
            if (b.anim.tick[0] == 0xF) b.flags34 |= 0x80000;
            const Mat4 m = NodeTimesCamera(w, b, 1);
            float pt[3];
            mx::GetTranslation(m, pt);
            if (pt[1] <= WaterLevelAt(w, pt)) {
                b.flags34 |= 0x20000;
                c.flags |= 2;
                for (auto& f : w.st2Feet) f[0] = 0.0f;  // 0x0047BCE5: foot radii 0x009A3500.. cleared
                int rot[3] = { 0, w.cam.yaw, 0 };
                w.SpawnExplosionAt(pt, rot, 0, 3.0f);
                rot[1] = 0;
                w.SpawnExplosionAt(pt, rot, 2, 3.0f);
                ++c.sub;
            }
            break;
        }
        case 3:
            if (b.anim.tick[0] == Ticks(w, b)) toFloat();
            break;
        case 4:
            b.pos[0] = b.vel[0] + b.pos[0];
            b.pos[2] = b.vel[2] + b.pos[2];
            b.vel[1] = b.acc[1] + b.vel[1];
            b.pos[1] = b.vel[1] + b.pos[1];
            if (WaterLevelAt(w, b.pos) <= b.pos[1]) bounceStart();
            break;
        case 5: case 6:
            goto bob;
        default:
            break;
        }
        goto tail;
    }
    if (which == 19) {
        switch (c.sub) {
        case 2:
            if (b.anim.tick[0] == 0x2D) {
                b.flags34 |= 0x4000;
                b.vel[0] = SinA(b.rot[1]) * F32(w.exe, 0x005644F4);
                b.vel[1] = -1.0f;
                b.vel[2] = CosA(b.rot[1]) * F32(w.exe, 0x005644F4);
                b.acc[1] = Bits(0xBDA740DA);
                ++c.sub;
            } else if (b.anim.tick[0] == 0xF) {
                b.flags34 |= 0x80000;
            }
            break;
        case 3: {
            Integrate(b);
            const float wl = WaterLevelAt(w, b.pos);
            const Mat4 m = NodeTimesCamera(w, b, 2);  // anim +0x1C0 = node 2
            float pt[3];
            mx::GetTranslation(m, pt);
            if (pt[1] <= wl) {
                int rot[3] = { 0, w.cam.yaw, 0 };
                w.SpawnExplosionAt(pt, rot, 0, 8.0f);
                rot[1] = 0;
                w.SpawnExplosionAt(pt, rot, 2, 3.0f);
                b.vel[0] = b.vel[0] * 0.5f;
                b.vel[1] = b.vel[1] * 0.5f;
                b.vel[2] = b.vel[2] * 0.5f;
                b.acc[1] = Bits(0x3C5F0123);  // 0.013611111
                b.flags34 &= ~0x4000u;
                ++c.sub;
            }
            break;
        }
        case 4:
            Integrate(b);
            if (b.anim.tick[0] == Ticks(w, b)) toFloat();
            break;
        case 5:
            b.pos[0] = b.vel[0] + b.pos[0];
            b.pos[2] = b.vel[2] + b.pos[2];
            b.vel[1] = b.acc[1] + b.vel[1];
            b.pos[1] = b.vel[1] + b.pos[1];
            if (WaterLevelAt(w, b.pos) <= b.pos[1]) bounceStart();
            break;
        case 6: case 7:
            goto bob;
        default:
            break;
        }
        goto tail;
    }
    // which == 20
    switch (c.sub) {
    case 2:
        if (b.anim.tick[0] == 0x2D) {
            b.flags34 |= 0x4000;
            b.vel[0] = SinA(b.rot[1]) * F32(w.exe, 0x005644F4);
            b.vel[1] = -1.0f;
            b.vel[2] = CosA(b.rot[1]) * F32(w.exe, 0x005644F4);
            b.acc[1] = Bits(0xBDA740DA);
            c.c9C = 0x1E;
            ++c.sub;
        } else if (b.anim.tick[0] == 0xF) {
            b.flags34 |= 0x80000;
        }
        return;
    case 3:
        Integrate(b);
        if (--c.c9C == 0) {
            const Mat4 m = NodeTimesCamera(w, b, 2);
            float pt[3];
            mx::GetTranslation(m, pt);
            const int rot[3] = { 0, w.cam.yaw, 0 };
            w.SpawnExplosionAt(pt, rot, 0, 8.0f);
            c.c9C = 0x5A;
            ++c.sub;
        }
        return;
    case 4:
        if (--c.c9C == 0) {
            --w.attackers;
            if (w.residency) w.resQueue.push_back({ 0x53, 0x2B });
        }
        return;
    default:
        return;
    }
bob : {
    const bool bouncing = (which == 18 && c.sub == 5) || (which == 19 && c.sub == 6);
    b.pos[0] = b.vel[0] + b.pos[0];
    b.pos[2] = b.vel[2] + b.pos[2];
    if (bouncing) {
        const int old = c.f58;
        c.f58 = c.f5C + old;
        if (((c.f58 ^ old) & 0x8000) != 0) {
            b.vel[1] = b.vel[1] * F32(w.exe, 0x004C43A8);
            if (0x80 < c.f5C) c.f5C -= 0x40;
            if (b.vel[1] < F32(w.exe, 0x004C4D10)) {
                c.f58 = 0;
                ++c.sub;
            }
        }
        b.pos[1] = SinA(c.f58) * b.vel[1] + WaterLevelAt(w, b.pos);
    } else {
        b.pos[1] = WaterLevelAt(w, b.pos);
    }
    --c.c9C;
}
tail:
    DeathCommon(w, b);
}

// 0x00476150
void TaskBoss2(Task& t) {
    World::Impl& w = *CurrentWorld();
    Boss2Task& b = t.As<Boss2Task>();
    Boss2Ctl& c = b.c;
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);
    Shots(w, b);  // 0x00476270
    switch (c.state) {
    case 0: case 3: St0(w, b); break;
    case 1: case 4: St1(w, b, false); break;
    case 2: St1(w, b, true); break;
    case 5: if (w.typingReferenceActive) TypingWalk(w,b); else St5(w, b); break;
    case 6: St6(w, b); break;
    case 7: St7(w, b); break;
    case 8: St8(w, b); break;
    case 9: StJump(w, b, false); break;
    case 10: if (w.typingReferenceActive) TypingInterlude(w,b); else St10(w, b); break;
    case 11: St11(w, b); break;
    case 12: StJump(w, b, true); break;
    case 13: St13(w, b); break;
    case 14: St14(w, b); break;
    case 15: St15(w, b); break;
    case 16: St16(w, b); break;
    case 17: St17(w, b); break;
    case 18: StDeath(w, b, 18); break;
    case 19: StDeath(w, b, 19); break;
    case 20: StDeath(w, b, 20); break;
    default: w.Note("stage-2 boss state " + std::to_string(c.state) + " out of range"); break;
    }
    DrawBoss(w, b);  // 0x00476AD0
    if (!(c.flags & 1)) {
        if (Wall(b.pos, c.corner[0], c.corner[1], 5.0f) != 0) Wall(b.pos, c.corner[2], c.corner[3], 5.0f);
        Wall(b.pos, c.corner[1], c.corner[2], 10.0f);
    }
    Phases(w, b);       // 0x00477E60
    ViewPos(w, b, c.radius);  // 0x00409B70(+0xC)
    Aggression(w, b);   // 0x00477FF0
    if (w.runner->camera.camId == b.endCam && w.runner->camera.tick == b.endTick) {
        if (b.actorSlot >= 0 && b.actorSlot < 14) w.actorSlots[b.actorSlot] = nullptr;
        w.EnemyDestroy(b);
        return;
    }
    w.SyncLock(b);
    static const bool dbg = getenv("HOTD2_BOSS2DBG") != nullptr;
    if (dbg)
        fprintf(stderr, "%d boss2 st %d pos %.9g %.9g %.9g rot %d %d %d\n", w.frame, c.state, b.pos[0], b.pos[1], b.pos[2], b.rot[0],
                b.rot[1], b.rot[2]);
}

// 0x00468E50 (komono kind 18): a light (0x0040DD90, renderer) while event flag 0x5F (event mode 1) / 0x0B (mode 4)
// is set; 0x6C frames later the task ends (and clears flag 0x5F outside scene 0x25)
struct Komono18Task : ActorTask {
    float pos[3] = {};
    int light = 0;    // +0x324 (1 in event mode 1, else 0x19)
    int range = 0;    // +0x328 (0x1D8)
    int counter = 0;  // +0x32C
    int prev = 0;     // +0x330
};

void TaskKomono18(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono18Task& k = t.As<Komono18Task>();
    if (w.MotionResident(k.range)) {  // 0x009A46A4 == 2: prop skeleton +0x324 (motion +0x328, frame +0x32C) at the record
        if (const Motion* mo = w.PropMotion(k.range)) {  // 0x0040DD90 -> 0x0040DDC0 (stage 5)
            Mat4 base = Mat4::Identity();
            mx::Translate(base, k.pos[0], k.pos[1], k.pos[2]);
            PropAnim pa{ k.light, k.range, k.counter, k.prev };
            for (const PropDraw& x : PropPose(w.PropSkel(k.light), *mo, pa, base, -1, false))
                if (x.model != 0) w.Draw(x.model, x.world);
            k.counter = pa.frame;
            k.prev = pa.prevFrame;
        }
    }
    const int mode = w.cfg.eventMode;
    if (mode == 1) {
        if (w.runner->flags[0x5F] != 1) return;
    } else {
        if (mode != 4 || w.runner->flags[0x0B] == 0) return;
    }
    if (0x6B < ++k.counter) {
        if (mode == 1 && w.runner->scene != 0x25) w.runner->flags[0x5F] = 0;
        w.ts.KillCurrent();
    }
}

}  // namespace

// Event op 0x2B (0x0045FE40): players in state 5 with more than 19 shots get the hit-rate bonus
// 0x00567990[(hits * 100 / shots) / 10] (s16: 0, 0, 0, 0, 500 .. 3000, 4000)
void World::Impl::PlayerBonus() {
    for (int p = 0; p < 2; ++p) {
        const Player& P = player[p];
        if (P.state == 5 && 0x13 < P.shots)
            AddScore(p, exe.S16(0x00567990 + static_cast<uint32_t>(((P.hits * 100) / P.shots) / 10) * 2));
    }
}

// Stage-2 spawn dispatch additions (called first by SpawnStage2)
bool World::Impl::SpawnStage2c(Task& t, SpawnTask& s) {
    const SpawnRecord r = s.rec;
    if (r.type == 0x16) {  // 0x00442290: the water level block (0x007DCC4C: count 0, mask 0, +8 = record y)
        st2Water = r.pos[1];
        WaterReset(*this);  // world_st2_water.cpp: count / mask 0
        ts.KillCurrent();
        return true;
    }
    if (r.type == 0x41 && r.ParamS8(1) == 18) {  // generic ctor 0x00461CF0 case 0x12 -> task 0x00468E50
        Komono18Task* k = NewTask<Komono18Task>(TaskKomono18, 0x00468E50);
        memcpy(k->pos, r.pos, 12);
        k->flags34 = 0x80000001u;
        k->light = cfg.eventMode == 1 ? 1 : 0x19;
        k->range = 0x1D8;
        ts.KillCurrent();
        return true;
    }
    if (r.type != 0x14) return false;
    // 0x00475E90: the spawn task becomes the boss
    auto d = std::make_unique<Boss2Task>();
    SetTypingSpawnOrigin(*d, r);
    Boss2Task& b = *d;
    b.record = r;
    b.flags34 = r.flags | 1;  // 0x00408A20
    memcpy(b.pos, r.pos, 12);
    memcpy(b.rot, r.rot, 12);
    b.hp = b.hpMax = static_cast<int16_t>(r.state);
    b.lo = static_cast<int16_t>(r.lo);
    b.actorSlot = -1;
    b.flags34 |= 0x8000;
    b.anim.kind = static_cast<int16_t>(r.params[0]);
    Boss2Ctl& c = b.c;
    c.row = 0xB;
    b.anim.motion[0] = MotionOfRow(*this, 0xB);
    b.anim.counter[0] = 0;
    b.anim.tick[0] = 0;
    EnemyInitPose(b);  // Anim_InitEnemy 0x00410440 (+ actor slot)
    b.nodeFn = NoNode;          // +0x1158 = 0x0041EBB0
    b.postRootFn = NoPostRoot;  // 0x00420810
    b.anim.rotOrder = 1;
    b.radius = 30.0f;
    ++attackers;
    ++liveEnemies;
    b.target = -1;
    b.slot120 = -1;
    b.lock = &lockTargets[nextLockTarget++];
    SyncLock(b);
    lockOn.AddDirect(b.lock);  // 0x00408E80
    b.slot120 = static_cast<int8_t>(b.lock->slot);
    c.state = static_cast<uint8_t>(r.params[1]);
    c.sub = 0;
    c.radius = 6.0f;
    auto pf = [&](int off) {
        const int32_t v = r.ParamS32(off);
        float x;
        memcpy(&x, &v, 4);
        return x;
    };
    for (int i = 0; i < 3; ++i) c.dir[i] = pf(4 + 4 * i);
    c.flags = 1;
    for (int k = 0; k < 4; ++k) {
        c.corner[k][0] = pf(0x10 + 8 * k);
        c.corner[k][2] = pf(0x14 + 8 * k);
    }
    c.m7C = c.m7E = 0x2CB;
    c.m80 = 0x2ED;
    c.m84 = 1.0f;
    c.m82 = 0;
    c.m88 = c.m8A = 0x2EE;
    c.m8C = 0x315;
    c.m8E = 0;
    c.row94 = 0;
    c.m90 = typingReferenceActive ? 1.0f : F32(exe, 0x005965C4);
    c.level = static_cast<int8_t>(rank);  // 0x0040A8A0
    c.hits = 0;
    c.lives[0] = static_cast<int8_t>(player[0].lives);  // 0x009A5C66
    c.lives[1] = static_cast<int8_t>(player[1].lives);  // 0x009A5D96
    b.endCam = r.ParamS16(0x30);
    b.endTick = r.ParamS16(0x32);
    b.fn = 0x00476150;
    t.data = std::move(d);
    t.fn = TaskBoss2;
    if (typingReferenceActive) BossWords(*this,b,cfg.eventMode == 1 ? 550 : 700);
    else if (cfg.portTyping) { typing.setWordSelection(0, 700); typing2.setWordSelection(0,700); }
    return true;
}

bool TypingBoss2(World::Impl& w, ActorTask& actor, TypingTargetSpec& out, bool complete) {
    auto* b = dynamic_cast<Boss2Task*>(&actor);
    if (!b) return false;
    out.boss = true;
    const auto& c = b->c;
    if (b->hp < 1 || (b->flags34 & 0x4000000u) || c.state < 5 || c.state >= 18) return true;
    if (w.typingReferenceActive && (b->flags34 & 0x8100u) && !(b->typingFlags & 0x200u)) return true;
    out.retain = true;
    out.token = 0x200000u | (static_cast<uint32_t>(c.phase) << 12);
    Mat4 m = NodeMV(w, *b, 1);
    mx::Translate(m, 0.0f, 4.0f, 1.0f);
    mx::GetTranslation(m, out.view);
    if ((b->flags34 & 0x40008100u) || (w.typingReferenceActive && (b->typingFlags & 0x10000u))) return true;
    out.node = 1;
    if (complete) TypingDeliver(*b, out.node);
    return true;
}

}  // namespace game
