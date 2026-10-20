// game::World - actor type 0x40 variants 0 / 1 (0x0043BD30): a group of flying creatures (bats, skeleton 0x1D) that
// circle over a grid of target cells, take turns diving at the players (0x0043D4F0) and fly back up; shot: blood
// 0x00430BD0, a falling body (0x0043DA20) and a ripple / splash effect (0x0043E4C0 -> 0x0043E540 -> 0x0043E650).
// First reached in the auto-play capture at 6898 (stage 1 scene 3, 8 bats, group type 0). See notes/agents/stage1b.md.
// CONFIRMED = frame-exact against the auto-play capture (rand, tasks, players, lock-on). The creatures are leech-like
// worms (skeleton 0x1D, mol.bin models 0x136E..0x1381: seven body segments and two jaws), drawn by the second animation
// system (0x0040EAE0 init, 0x0040F4F0 -> Anim_DrawSkeleton 0x0040F520 draw, 0x0040F840 / 0x0040F860 motion switch)
// whose per-node draw 0x0040F960 moves the seam vertices of each segment onto its neighbour (BatSkelDraw below).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

namespace game {

void SpawnBatWater(World::Impl& w, const float pos[3]);  // 0x0043EF70, world_st2_bats.cpp

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

int8_t S8(const ExeImage& exe, uint32_t va) {
    int8_t v = 0;
    exe.Read(va, &v, 1);
    return v;
}

// fild a; fmul qword 2pi/65536 (rounded to float at 24-bit precision); fsin / fcos (not rounded)
double SinA(int a) { return std::sin(static_cast<double>(static_cast<float>(static_cast<double>(a) * mx::kToRad))); }
double CosA(int a) { return std::cos(static_cast<double>(static_cast<float>(static_cast<double>(a) * mx::kToRad))); }
// fpatan(x, z); fmul qword 65536/2pi (rounded); __ftol
int Atan2A(float x, float z) {
    return static_cast<int>(static_cast<float>(std::atan2(static_cast<double>(x), static_cast<double>(z)) * mx::kToAngle));
}

// The second animation system (Anim_DrawSkeleton 0x0040F520 with node walker 3 = 0x0040F7B0): anim block at ctl + 0x70,
// node buffer at ctl + 0xA4 (10 entries of 0x70 bytes; skeleton 0x1D has 10 nodes)
struct BatAnimNode {
    int model = 0;              // +0x00 0x004E1F88[slot + wing * 10] (0x0040EC50)
    int cur[3] = {};            // +0x04 angles in use (x, y, z)
    int src[3] = {};            // +0x10 blend source (0x0040F900)
    Mat4 m = Mat4::Identity();  // +0x1C node matrix (the original stores model-view; world here)
};
struct BatAnim {
    int time = 0;               // +0x00 frames (the bat task adds 1 after each draw)
    int tick = 0;               // +0x04 motion tick
    int motion = 0x218;         // +0x08
    int8_t blendLen = 0;        // +0x0C blend frames + 1
    uint8_t flags = 0;          // +0x0D 1 blending, 2 end reached, 4 blend finished
    float rootS1[3] = {};       // +0x10 root position at the blend start
    bool end = false;           // +0x1D
    int16_t skeleton = 0x1D;    // +0x1E
    float root[3] = {};         // +0x28 root position of the last evaluation
    BatAnimNode node[10];
};

// Per-bat control block (heap block of 0x504 bytes at obj + 0x1390)
struct BatCtl {
    float prev[3] = {};       // +0x00 last position
    float start[2] = {};      // +0x0C / +0x10 spawn x / z (grid origin)
    float f14 = 0;            // +0x14
    float dive[3] = {};       // +0x18 dive start
    int yawRate = 0;          // +0x24 (state 4)
    int8_t cell[2] = {};      // +0x2C / +0x2D target cell
    float target[2] = {};     // +0x30 / +0x34
    float diveOff[2] = {};    // +0x38 / +0x3C
    int yawTarget = 0;        // +0x40
    int turn = 0;             // +0x44 (state 2)
    float turnCount = 0;      // +0x48
    float turnFrames = 0;     // +0x4C
    float speed = 1.0f;       // +0x50 (1 circling, 2 after an attack)
    int seg = 0;              // +0x58 spline segment
    float t = 0;              // +0x5C
    float tStep = 0;          // +0x60
    int group = 0;            // +0x68 group type (scene: 3 -> 0, 8 -> 1, 0x19 -> 2, 0xE -> 3, 0x12 -> 4)
    int count = 0;            // +0x6C bats in the group
    BatAnim anim;             // +0x70 (+0x70 time, +0x78 motion, +0xA4 node buffer)
};

struct BatTask : ActorTask {
    float pos[3] = {};        // +0x40
    float vy = 0;             // +0x50
    float f54 = 0;            // +0x54
    int rot[3] = {};          // +0x64 / +0x68 / +0x6C
    int16_t hp = 1;           // +0x11C
    int8_t slot120 = -1;      // +0x120
    int8_t target = 0;        // +0x121 (Task_Clear: 0)
    int variant = 0;          // +0x130C
    int16_t state = 0;        // +0x1310
    int8_t idx = 0;           // +0x131B
    int counter = 0;          // +0x1330 (creator: group size)
    int delay = 0;            // +0x1334
    int f1338 = 0, f133C = 0; // +0x1338 / +0x133C (skeleton draw: alpha path / rotation order Z, Y, X)
    float effScale = 0;       // +0x1340 (death effect)
    float diveT = 0, diveDt = 0;  // +0x1344 / +0x1348
    float scale = 0.75f;      // +0x134C
    int wing = 0;             // +0x1350 wing model set
    int circleCount = 0;      // +0x1354
    int f1358 = 0;            // +0x1358 skeleton drawn once (0x0040F960 normal cache only on the first draw)
    int dying = 0;            // +0x135C (skeleton draw: height squashed by (0x3D - +0x1330) / 61)
    int bumped = 0;           // +0x1360
    int fx1320 = 0;           // +0x1320 (death effect fade)
    BatCtl c;                 // +0x1390
    LockOnTarget* lock = nullptr;  // what the lock-on table reads (+0x40, +0x100, +0x121, +0x34; +0x120 written)
};

// Blood 0x00430BD0 (0x00430C50): models 0x3A..0x52 at the view-space point, scale 0.3
struct BatBloodTask : WTask {
    float pos[3] = {};  // +0x38
    int model = 0x3A;   // +0x54
};

// Effect 0x0043E540 / 0x0043E650 (0x0043E4C0)
struct BatSplashTask : WTask {
    float pos[3] = {};  // +0x40
    int yaw = 0;        // +0x68
    int timer = 0x78;   // +0x1330
    int fade = 0;       // +0x1320
    float scale = 1.0f; // +0x1340
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Skeleton draw of the bats / leeches (skeleton 0x1D): the second animation system 0x0040EAE0 / 0x0040F4F0
// ---------------------------------------------------------------------------------------------------------------------

static Mat4 BatRotZYX(int x, int y, int z) {  // identity; RotZ(z); RotY(y); RotX(x)
    Mat4 m = Mat4::Identity();
    mx::RotZ(m, z);
    mx::RotY(m, y);
    mx::RotX(m, x);
    return m;
}

// 0x0040EAE0(anim, &obj+0x40, nodeBuffer) -> 0x0040EB40 / 0x0040EC50: time / tick / blend cleared, walker 3; node
// models 0x004E1F88[slot + wing * 10]. It reads frame 0 of the motion 12 bytes further on than the draw does (frame
// + 0xC as the root position, frame + 0x18 + slot * 6 as the angles of a slot = the draw's slot + 2): those are the
// angles in use before the first draw, which an even first tick interpolates from (0x0040ED30). (The node matrices it
// stores are recomputed by every draw; the node callback 0x0040F490 it installs is not used by walker 3.)
static void BatAnimInit(World::Impl& w, BatAnim& a, int wing) {
    a.time = a.tick = 0;
    a.blendLen = 0;
    a.flags = 0;
    const Skeleton& s = w.EnemySkeleton(a.skeleton);
    const Motion* m = w.EnemyMotion(a.motion, w.exe.S16(0x004E0724 + a.skeleton * 2));
    const bool ok = m && m->data.size() >= 4 + 0x18 + 6 * 10;
    if (ok) memcpy(a.root, &m->data[4 + 0xC], 12);
    for (size_t n = 0; n < s.nodes.size(); ++n) {
        const int slot = s.nodes[n].index;
        if (slot < 0 || slot >= 10) continue;
        BatAnimNode& d = a.node[slot];
        int16_t r[3] = {};
        if (ok) memcpy(r, &m->data[4 + 0x18 + static_cast<size_t>(slot) * 6], 6);
        for (int k = 0; k < 3; ++k) d.cur[k] = r[k];
        if (n != 0) d.model = w.exe.S16(0x004E1F88 + static_cast<uint32_t>(slot + wing * 10) * 2);
    }
}

// 0x0040F840(anim, motion): switch without a blend
static void BatAnimSet(BatAnim& a, int motion) {
    a.motion = motion;
    a.tick = 0;
    a.time = 0;
    a.end = false;
}

// 0x0040F860(anim, motion, tick, frames): blend from the current pose (root position, node angles: 0x0040F900)
static void BatAnimBlend(BatAnim& a, int motion, int tick, int frames) {
    for (BatAnimNode& d : a.node) memcpy(d.src, d.cur, sizeof(d.src));
    a.time = 1;
    a.end = false;
    memcpy(a.rootS1, a.root, sizeof(a.rootS1));
    a.tick = tick;
    a.motion = motion;
    a.blendLen = static_cast<int8_t>(frames + 1);
    a.flags |= 1;
}

// 0x0040FFD0 (= 0x00412750) on the matrices of two angle sets, then 0x004019E0
static void BatBlendAngles(const int from[3], const int16_t to[3], float f, int out[3]) {
    Mat4 top = Mat4::Identity();
    mx::BlendRotation(top, BatRotZYX(from[0], from[1], from[2]), BatRotZYX(to[0], to[1], to[2]), f);
    mx::MatrixToAngles(top, &out[0], &out[1], &out[2]);
}

// Anim_NodeTransform 0x0040ED30 + walker 0x0040F7B0: node matrix = R(angles) T(offset) parent, stored per slot
static void BatAnimNodeWalk(World::Impl& w, BatTask& b, const Skeleton& s, const Motion& mo, int idx, const Mat4& parent) {
    BatAnim& a = b.c.anim;
    const SkeletonNode& sn = s.nodes[static_cast<size_t>(idx)];
    const int slot = sn.index;
    BatAnimNode& d = a.node[slot >= 0 && slot < 10 ? slot : 0];
    Mat4 m = parent;
    mx::Translate(m, sn.offset[0], sn.offset[1], sn.offset[2]);
    int16_t fr[3];
    mo.Rotation(a.tick / 2, slot, fr);
    int ang[3];
    if (a.flags & 1) {
        const float f = static_cast<float>(a.time) / static_cast<float>(a.blendLen);
        BatBlendAngles(d.src, fr, f, ang);
    } else {
        if (a.tick == 0 || (a.tick & 1)) {
            for (int k = 0; k < 3; ++k) ang[k] = fr[k];
        } else {  // even tick: halfway from the angles in use to the frame's
            for (int k = 0; k < 3; ++k) {
                int dd = static_cast<int>(static_cast<uint16_t>(static_cast<int16_t>(fr[k] - static_cast<int16_t>(d.cur[k]))));
                if (0x8000 < dd) dd -= 0x10000;
                ang[k] = dd / 2 + d.cur[k];
            }
        }
        // the jaws (slots 8 / 9) open while the creature dives (+0x34 & 0x10000000), game mode 6 only
        if (w.gameMode == 6 && a.skeleton == 0x1D && (b.flags34 & 0x10000000)) {
            if (slot == 8) ang[0] = 0xFFFF - static_cast<int>(b.diveT * F32(w.exe, 0x004E1FB8));
            if (slot == 9)
                ang[0] = (static_cast<int>(b.diveT * F32(w.exe, 0x004E1FB4)) - static_cast<int>(b.diveT * F32(w.exe, 0x004E1FB0))) - 0xFFFF;
        }
    }
    mx::RotZ(m, ang[2]);
    mx::RotY(m, ang[1]);
    mx::RotX(m, ang[0]);
    for (int k = 0; k < 3; ++k) d.cur[k] = ang[k];
    d.m = m;
    for (int c : sn.children) BatAnimNodeWalk(w, b, s, mo, c, m);
}

// 0x0040F960(slot) before the draw: the in-memory edits of the node's model (skipped while +0x34 & 0x1000000, the
// reflection draw, which reuses the edits just made). Slots 1..7: the seam vertices (table 0x004E1D48 + slot * 0x40:
// +0 points (12 bytes each, in the reference node's space), s8 +4 reference node, +6 one byte per inline vertex (1 =
// moved), +0x29 s8 point list (-1 ends; the n-th moved vertex takes point list[n]); slot 7 also moves points of its
// second list (+0x31) relative to node 0x004E1F4C) are moved to the points, transformed by (reference node) x (this
// node)^-1. Every material's diffuse R, G, B (words 12..14) = 1.0 when the node's world z <= -515 (0x004E1FD8) or in
// scene 0x15, else 0.6. Not ported: on a bat's first draw (+0x1358 == 0) the code also copies normals of slots 1..5
// into a cache (0x007C17B0 / 0x007C18D0) and writes one cached word into the normal x of moved vertices of slots 2..6
// (lighting only).
static void BatEditNodeModel(World::Impl& w, BatTask& b, int slot) {
    const ExeImage& exe = w.exe;
    BatAnim& a = b.c.anim;
    const BatAnimNode& d = a.node[slot];
    auto e = std::make_shared<ModelEdit>();
    if (slot < 8) {
        const uint32_t ent = 0x004E1D48 + static_cast<uint32_t>(slot) * 0x40;
        float pts[128][3] = {};
        auto transform = [&](uint32_t points, int ref, uint32_t list) {
            if (ref < 0 || ref >= 10) return;
            Mat4 t = mx::Inverse(d.m);         // Mtx_Load(node); Mtx_Inverse
            mx::MulLeft(t, a.node[ref].m);     // Mtx_Multiply(reference node)
            for (int n = 0; n < 64; ++n) {
                const int8_t k = S8(exe, list + static_cast<uint32_t>(n));
                if (k == -1) break;
                if (k < 0) continue;
                float p[3];
                exe.Read(points + static_cast<uint32_t>(k) * 12, p, 12);
                mx::Transform(t, p, pts[k]);
            }
        };
        transform(exe.U32(ent), S8(exe, ent + 4), ent + 0x29);
        if (slot == 7) transform(exe.U32(0x004E1F08), S8(exe, 0x004E1F4C), 0x004E1F39);
        int moved = 0;
        const bool first = b.f1358 == 0;
        int recorded = 0;  // local_6d0 (A entries written by this call)
        int search = 0;    // local_6e0
        for (int v = 0; v < 64; ++v) {  // inline vertices of the node models: 10..30
            if (S8(exe, ent + 6 + static_cast<uint32_t>(v)) != 1) {
                // first draw, slots < 6: the vertices listed at 0x004E1DB1 + slot * 0x40 record their normal in A
                if (first && slot < 6) {
                    for (uint32_t n = 0; n < 64; ++n) {
                        const int8_t c = S8(exe, 0x004E1DB1 + static_cast<uint32_t>(slot) * 0x40 + n);
                        if (c == -1) break;
                        if (c != v) continue;
                        if (recorded < 35) {
                            w.batCacheIdx[0][recorded] = v;
                            w.batCacheSrc[0][recorded] = { d.model, v };
                        }
                        ++recorded;
                        break;
                    }
                }
                continue;
            }
            const int8_t k = S8(exe, ent + 0x29 + static_cast<uint32_t>(moved));
            ++moved;
            // first draw, slots 2..6: normal x of the moved vertex from the cached normal in B with the same vertex
            // index (search from entry search + 1; a miss ends the searches of this call)
            if (first && 1 < slot && slot < 7) {
                const int from = search + 1;
                const bool active = search < 0x23;
                search = from;
                if (active) {
                    int j = from;
                    for (; j <= 35; ++j)
                        if (j < 35 && w.batCacheIdx[1][j] == v) break;
                    if (j <= 35) {
                        search = 0;
                        w.batNormalX[d.model][v] = w.batCacheSrc[1][j];
                    } else {
                        search = 36;
                    }
                }
            }
            if (k < 0) continue;
            ModelEdit::Vertex mv;
            mv.index = v;
            memcpy(mv.pos, pts[k], 12);
            uint32_t xb;
            memcpy(&xb, &mv.pos[0], 4);
            xb |= 1;
            memcpy(&mv.pos[0], &xb, 4);
            e->vertices.push_back(mv);
        }
        if (first) {  // end of 0x0040F960 on a first draw: B = A (35 entries)
            memcpy(w.batCacheIdx[1], w.batCacheIdx[0], sizeof(int) * 35);
            memcpy(w.batCacheSrc[1], w.batCacheSrc[0], sizeof(w.batCacheSrc[0][0]) * 35);
        }
    }
    auto nx = w.batNormalX.find(d.model);  // normal-x writes of first draws stay in the model data
    if (nx != w.batNormalX.end())
        for (const auto& [v, src] : nx->second) e->normalX.push_back({ v, src.model, src.index });
    const bool lit = w.runner->scene == 0x15 || d.m.m[14] <= F32(exe, 0x004E1FD8);
    e->diffuse = lit ? 1.0f : 0.6f;  // 0x3F19999A
    w.modelEdits[d.model] = std::move(e);
}

// 0x0040F4F0(anim, &obj+0x40, nodeBuffer) -> Anim_DrawSkeleton 0x0040F520: advance (0x0040F610), root (0x0040EFB0 +
// the object transform 0x0040F220), node matrices (walker 0x0040F7B0), then 0x0040F960 per slot 0..9 (slots 8 / 9
// not in state 6): model edits, then the node model drawn with its matrix (state 6: slot 7 draws 0x1371 / 0x1370 by
// +0x1350; +0x1338: the alpha path with (80 - +0x1330) * 0.0125). Scene 0x19 without +0x34 & 0x8000000: a slot is
// drawn only when its node's world x is below the window edge (-544 .. -540.8 by slot).
static void BatSkelDraw(World::Impl& w, BatTask& b) {
    const ExeImage& exe = w.exe;
    BatAnim& a = b.c.anim;
    // 0x0040F610
    const int ticks = w.MotionTicks(a.motion);
    a.end = false;
    a.flags &= 0xF9;
    if (!(a.flags & 1)) {
        if (ticks > 0) {
            a.tick = a.time % ticks;
            if (ticks <= a.tick) {
                a.end = true;
                a.flags |= 2;
            }
        }
    } else if (a.blendLen <= a.time) {
        a.time = a.tick;
        a.flags = static_cast<uint8_t>((a.flags & 0xF8) | 4);
    }
    const Skeleton& s = w.EnemySkeleton(a.skeleton);
    const Motion* mo = w.EnemyMotion(a.motion, exe.S16(0x004E0724 + a.skeleton * 2));
    if (!mo || mo->data.empty() || s.nodes.empty() || a.tick / 2 + 1 >= mo->StoredFrames()) {
        w.Note("bat skeleton draw: motion " + std::to_string(a.motion) + " missing");
        return;
    }
    // 0x0040EFB0: root position (odd ticks halfway to the next frame) and angles (slot 0)
    float root[3];
    int ang[3];
    int16_t r0[3];
    mo->RootPosition(a.tick / 2, root);
    mo->Rotation(a.tick / 2, 0, r0);
    if (!(a.flags & 1)) {
        if (a.tick & 1) {
            float nx[3];
            mo->RootPosition(a.tick / 2 + 1, nx);
            const float h = F32(exe, 0x004C43AC);  // 0.5
            for (int k = 0; k < 3; ++k) root[k] = (nx[k] - root[k]) * h + root[k];
        }
        for (int k = 0; k < 3; ++k) ang[k] = r0[k];
    } else {
        const float t = static_cast<float>(a.time), n = static_cast<float>(a.blendLen);
        for (int k = 0; k < 3; ++k) root[k] = ((root[k] - a.rootS1[k]) * t) / n + a.rootS1[k];
        BatBlendAngles(a.node[0].src, r0, t / n, ang);
    }
    memcpy(a.root, root, 12);
    // 0x0040F220: the object transform (walker 3: no +0x20 & 0x10 path)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, b.pos[0], b.pos[1], b.pos[2]);
    if (b.f133C == 0) {
        mx::RotY(m, b.rot[1]);
        mx::RotX(m, b.rot[0]);
        mx::RotZ(m, b.rot[2]);
    } else {
        mx::RotZ(m, b.rot[2]);
        mx::RotY(m, b.rot[1]);
        mx::RotX(m, b.rot[0]);
    }
    mx::Scale(m, b.scale, b.scale, b.scale);
    if (b.dying != 0) mx::Scale(m, 1.0f, static_cast<float>(0x3D - b.counter) * F32(exe, 0x004E1FBC), 1.0f);
    mx::Translate(m, root[0], root[1], root[2]);
    for (int k = 0; k < 3; ++k) a.node[0].cur[k] = ang[k];
    mx::RotZ(m, ang[2]);
    mx::RotY(m, ang[1]);
    mx::RotX(m, ang[0]);
    a.node[0].m = m;
    for (int c : s.nodes[0].children) BatAnimNodeWalk(w, b, s, *mo, c, m);
    // 0x0040F960 per slot
    for (int i = 0; i < 10; ++i) {
        if (b.state == 6 && 7 < i) break;
        if (i == 0) continue;
        const BatAnimNode& d = a.node[i];
        if (!(b.flags34 & 0x1000000)) BatEditNodeModel(w, b, i);
        const float x = d.m.m[12];  // world x of the node (0x009A6040 x node matrix)
        bool show = (b.flags34 & 0x8000000) || w.runner->scene != 0x19;
        if (!show) {
            const float lim = i >= 7 ? F32(exe, 0x004E1FD4) : i <= 2 ? F32(exe, 0x004E1FD0) : i <= 4 ? F32(exe, 0x004E1FCC)
                                                                                                    : F32(exe, 0x004E1FC8);
            show = x <= lim;
        }
        if (show) {
            WorldDraw* body = nullptr;
            if (b.f1338 != 0) {
                body = w.Draw(d.model, d.m, 8, (F32(exe, 0x004E1FC4) - static_cast<float>(b.counter)) * F32(exe, 0x004E1FC0), false, i);
            } else if (b.state == 6 && i == 7) {
                body = w.Draw(b.wing == 0 ? 0x1371 : 0x1370, d.m, 8, -1.0f, false, i);  // (0x0041EBB0 debug no-op before)
            } else {
                body = w.Draw(d.model, d.m, 8, -1.0f, false, i);
            }
            // Deliberate correction of the PC's scaled-normal highlight plateau.
            // This also covers the mirrored body; separate floor/water shadows
            // and hit effects retain their existing lighting and material state.
            if (body) body->unitSpecular = true;
        }
        if (i == 7) b.f1358 = 1;
    }
}

// 0x0042DFD0(seg, t): uniform quadratic B-spline weights ((1-t)^2/2, (1-t)t + 1/2, t^2/2)
static void SplineWeights(const ExeImage& exe, float t, float w[3]) {
    const float h = F32(exe, 0x004C43AC);  // 0.5
    const float u = F32(exe, 0x004C4380) - t;
    w[0] = u * u * h;
    w[1] = u * t + h;
    w[2] = t * t * h;
}

static uint32_t SplinePoints(const BatTask& b) {  // 0x0055E200 + (seg + (idx + group * 10) * 8) * 2: s8 (x, z) points
    return 0x0055E200 + static_cast<uint32_t>((b.c.seg + (b.idx + b.c.group * 10) * 8) * 2);
}

// Target point in the grid cell (b.c.cell): x = ((base + start) - size * 0.5) + rand() % (size + 1) + cell * size
static void BatPickTarget(World::Impl& w, BatTask& b, uint32_t siteX, uint32_t siteZ) {
    const ExeImage& exe = w.exe;
    const uint32_t g = static_cast<uint32_t>(b.c.group) * 2;
    const float half = F32(exe, 0x004C4CA0);
    {
        const int size = S8(exe, 0x0055E574 + g);
        const int r = w.Rand(siteX) % (size + 1);
        const float base = static_cast<float>(S8(exe, 0x0055E568 + g)) + b.c.start[0];
        b.c.target[0] = (static_cast<float>(r) + (base - static_cast<float>(size) * half)) + static_cast<float>(b.c.cell[0] * size);
    }
    {
        const int size = S8(exe, 0x0055E575 + g);
        const int r = w.Rand(siteZ) % (size + 1);
        const float base = static_cast<float>(S8(exe, 0x0055E569 + g)) + b.c.start[1];
        b.c.target[1] = (static_cast<float>(r) + (base - static_cast<float>(size) * half)) + static_cast<float>(b.c.cell[1] * size);
    }
}

// next attacker: (idx + 1) % count, skipping empty entries of 0x007DCC20
static void BatNextAttacker(World::Impl& w, int idx, int count) {
    if (count <= 0) return;
    w.batAttacker = (idx + 1) % count;
    for (int g = 0; g < 64 && !w.batList[w.batAttacker]; ++g)
        w.batAttacker = static_cast<int>(static_cast<uint32_t>(w.batAttacker + 1) % static_cast<uint32_t>(count));
}

static void BatSyncLock(BatTask& b) {
    if (!b.lock) return;
    memcpy(b.lock->pos, b.pos, 12);
    b.lock->targetPlayer = b.target;
    b.lock->flags = b.flags34;
}

// 0x0043D4F0: start a dive at the players (shooting camera, players in game; state 5 only when on screen)
static int BatStartDive(World::Impl& w, BatTask& b) {
    if (w.runner->camera.modeCur != 2 || w.players == 0) return 0;
    if (b.state == 5) {
        const int sx = static_cast<int>(w.focal * b.viewPos[0] / b.viewPos[2]);  // 0x0043EF30
        const int sy = static_cast<int>(w.focal * b.viewPos[1] / b.viewPos[2]);  // 0x0043EF50
        if ((sx < 0 ? -sx : sx) > 0xF0 || (sy < 0 ? -sy : sy) > 0xB4) {
            BatNextAttacker(w, b.idx, b.c.count);
            return 0;
        }
    }
    b.c.diveOff[0] = b.c.diveOff[1] = 0;
    if (w.horde) HordeBatDive(w, b.c.diveOff);  // port only: the dive ends at a barrel of the row (world_horde.cpp)
    if (w.players == 2) b.target = static_cast<int8_t>(w.Rand(0x0043D5E9) & 1);
    else if (w.playersInGame == 0) b.target = 0;
    else if (w.playersInGame == 1) b.target = 1;
    b.rot[1] &= 0xFFFF;
    const int a = Atan2A(w.cam.pos[0] - b.pos[0], w.cam.pos[2] - b.pos[2]) & 0xFFFF;
    b.c.turnCount = 0;
    const int yaw = b.rot[1];
    const int d = a - yaw;
    const int ad = d < 0 ? yaw - a : d;
    const float k = F32(w.exe, 0x005643F4);
    if (ad < 0x8001) {
        b.c.turnFrames = static_cast<float>(ad) * k;
        b.c.turn = yaw < a ? 0x222 : -0x222;
    } else if (a <= yaw) {
        b.c.turn = 0x222;
        b.c.turnFrames = static_cast<float>(d + 0x10000) * k;
    } else {
        b.c.turnFrames = static_cast<float>((yaw - a) + 0x10000) * k;
        b.c.turn = -0x222;
    }
    b.state = 2;
    w.batLastAttack = w.seedCounter;
    BatSyncLock(b);
    return 1;
}

// 0x00430BD0
static void TaskBatBlood(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatBloodTask& k = t.As<BatBloodTask>();
    const float s = 0.3f;  // 0x3E99999A (0.15 when 0x009C911E == 1)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::Scale(m, s, s, s);
    w.Draw(k.model++, m, 8, -1.0f, false, -1, true);
    if (0x52 < k.model) w.ts.KillCurrent();
}

// 0x0043E540 -> 0x0043E650
static void TaskBatSplash(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatSplashTask& k = t.As<BatSplashTask>();
    const ExeImage& exe = w.exe;
    if (k.fn == 0x0043E540) {
        if (--k.timer == 0) k.fn = 0x0043E650;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.yaw);
        const int a = static_cast<int>(static_cast<float>(k.timer) * F32(exe, 0x004C4CA4));
        // (the timer was read again after the decrement: the same value)
        const float s1 = static_cast<float>(static_cast<float>(CosA(a) * k.scale) * F32(exe, 0x004C49C0));
        Mat4 q = m;
        mx::Scale(q, s1, 1.0f, s1);
        w.Draw(0x1A38, q);
        const float s2 = static_cast<float>(SinA(a) * k.scale);
        mx::Scale(m, s2, s2, s2);
        w.Draw(static_cast<int>(w.seedCounter % 30u) + 0x15E4, m);
        return;
    }
    if (0x1D < ++k.timer && 0x1D < ++k.fade) w.ts.KillCurrent();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotY(m, k.yaw);
    const float s = static_cast<float>(k.scale * F64(exe, 0x0055CB00));
    mx::Scale(m, s, 1.0f, s);
    w.Draw(0x1A38, m, 8, static_cast<float>(0x28 - k.fade) * F32(exe, 0x005643F8));
}

// 0x0043DA20: shot bat falling to the floor, removed after 60 frames
static void TaskBatDead(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatTask& b = t.As<BatTask>();
    const ExeImage& exe = w.exe;
    if (0x3B < b.counter++) {
        w.batList[b.idx] = nullptr;
        b.flags34 = (b.flags34 & ~1u) | 0x80018000u;  // Actor_Destroy 0x00409CC0
        w.ts.KillCurrent();
    }
    if (b.c.group != 2 || b.idx < 3 || b.pos[0] < F32(exe, 0x005643F0)) {
        const float floor = w.ground + F32(exe, 0x004C4380);
        if (b.pos[1] <= floor) {
            b.pos[1] = floor;
        } else {
            const float vy = b.vy - F32(exe, 0x0055D7C0);
            b.vy = vy;
            b.pos[1] = vy + b.pos[1];
            if (b.pos[1] < w.ground) b.pos[1] = w.ground;
        }
    }
    if (b.c.group != 2 || b.pos[0] <= b.c.start[0] - F32(exe, 0x004C43A0) || (b.flags34 & 0x8000000)) {
        b.rot[1] += 0x8000;
        BatSkelDraw(w, b);  // 0x0040F4F0
        b.rot[1] -= 0x8000;
    }
    if (w.batAlive == 1) {
        if (b.lock) {
            memcpy(b.lock->point, b.pos, 12);  // +0x100
            BatSyncLock(b);
            w.lockOn.Register(b.lock, w.playerPos);  // 0x00408EC0
        }
    }
    ++b.c.anim.time;
    w.batList[b.idx] = &b;
}

// 0x0043C440
static void TaskBat(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatTask& b = t.As<BatTask>();
    if (b.fn == 0x0043DA20) {
        TaskBatDead(t);
        return;
    }
    const ExeImage& exe = w.exe;
    BatCtl& c = b.c;
    if (w.runner->camera.camId == 0x47) return;
    if (b.lock) b.slot120 = static_cast<int8_t>(b.lock->slot);  // +0x120 is written by the table task 0x00408DD0
    const float k5643f0 = F32(exe, 0x005643F0);
    // ---- shot
    if ((b.flags34 & 8) && b.state != 0 &&
        (b.state != 1 || S8(exe, 0x0055E534 + static_cast<uint32_t>(c.group * 10 + b.idx)) <= b.counter)) {
        {  // 0x00430C50: blood at the view-space point (+0x70)
            BatBloodTask* k = w.NewTask<BatBloodTask>(TaskBatBlood, 0x00430BD0);
            memcpy(k->pos, b.viewPos, 12);
            k->model = 0x3A;
        }
        BatAnimBlend(c.anim, 0x219, 0, 4);  // 0x0040F860
        --w.liveEnemies;
        --w.attackers;
        if (w.cfg.eventMode == 0) w.Sound((w.Rand(0x0043C4E6) & 1) ? 0x1D18A9u : 0x1E18A9u);
        else w.Sound((w.Rand(0x0043C507) & 1) ? 0x2019A9u : 0x2119A9u);
        int p;
        if ((b.flags34 & 2) && (b.flags34 & 4)) p = w.Rand(0x0043C53C) & 1;
        else p = (b.flags34 & 2) ? 0 : 1;
        w.AddScore(p, 0x50);
        ++w.player[p].hits;
        {  // 0x0043E4C0: splash effect
            float y, s = 1.0f;
            const float k4cc0 = F32(exe, 0x004C4CC0);
            if (c.group == 2 && b.idx >= 3 && !(b.pos[0] < k5643f0)) {
                y = static_cast<float>(b.idx) * k4cc0 + F32(exe, 0x005643EC);
                s = 0.75f;
            } else if (c.group == 2) {
                y = (static_cast<float>(b.idx) * k4cc0 + w.ground) + F32(exe, 0x005643E8);
            } else {
                y = static_cast<float>(b.idx) * k4cc0 + w.ground;
            }
            BatSplashTask* k = w.NewTask<BatSplashTask>(TaskBatSplash, 0x0043E540);
            k->pos[0] = b.pos[0];
            k->pos[1] = y;
            k->pos[2] = b.pos[2];
            k->yaw = b.rot[1];
            k->timer = 0x78;
            k->scale = s;
            w.Sound(w.cfg.eventMode == 0 ? 0x118A9u : 0x119A9u);
        }
        b.counter = 0;
        b.dying = 1;
        b.vy = 0;
        if ((b.state == 1 || b.state == 2 || b.state == 3) && 1u < static_cast<uint32_t>(w.batAlive)) {
            BatNextAttacker(w, b.idx, c.count);
            w.batLastAttack -= 0x5A;
        }
        b.state = 6;
        if (1u < static_cast<uint32_t>(w.batAlive)) {
            --w.batAlive;
            b.flags34 |= 0x10000;
            if (b.slot120 != -1) w.lockOn.Release(b.slot120);
        }
        b.fn = 0x0043DA20;
    }
    switch (b.state) {
    case 0:
        if (--b.delay < 0 && (c.group != 2 || w.runner->flags[0x5E] != 0)) {
            b.state = 1;
            w.dcc48 = 1;
            b.counter = 0;
            b.flags34 &= ~0x8000u;
        }
        if (c.group == 2 && w.runner->flags[0x5E] == 1) {
            if (b.lock) {
                BatSyncLock(b);
                w.lockOn.AddDirect(b.lock);
                b.slot120 = static_cast<int8_t>(b.lock->slot);
            }
            ++w.attackers;
            ++w.liveEnemies;
        }
        break;
    case 1: {
        ++b.counter;
        float wt[3];
        SplineWeights(exe, c.t, wt);
        const uint32_t pts = SplinePoints(b);
        const float b0 = S8(exe, pts), b1 = S8(exe, pts + 1), b2 = S8(exe, pts + 2), b3 = S8(exe, pts + 3),
                    b4 = S8(exe, pts + 4), b5 = S8(exe, pts + 5);
        b.pos[0] = ((b2 * wt[1] + b4 * wt[2]) + b0 * wt[0]) + c.start[0];
        b.pos[2] = ((b1 * wt[0] + b3 * wt[1]) + b5 * wt[2]) + c.start[1];
        const int ty = static_cast<int16_t>(Atan2A(b.pos[0] - c.prev[0], b.pos[2] - c.prev[2]));
        c.yawTarget = ty;
        if (c.tStep != 0.0f || c.t != 0.0f) {
            const int d = ty - b.rot[1];
            b.rot[1] = d / 4 + b.rot[1];
        }
        if (c.group == 2 && b.idx >= 3) {
            if (!(b.pos[1] < F32(exe, 0x005643E4)) && b.pos[0] < k5643f0) {
                const uint32_t u = 0xBCDEFC7Au;
                memcpy(&b.vy, &u, 4);
            }
            if (b.vy < 0.0f) {
                const float vy = b.vy - F32(exe, 0x0055CB10);
                b.rot[0] -= 0x180;
                b.vy = vy;
                b.pos[1] = vy + b.pos[1];
                const float floor = w.ground + F32(exe, 0x004C4380);
                if (b.pos[1] < floor) {
                    b.pos[1] = floor;
                    b.vy = 0;
                    b.rot[0] = 0;
                }
            }
        }
        c.t = c.t + c.tStep;
        if (!(c.t < F32(exe, 0x005643E0))) {
            c.t = 0;
            if (5 < ++c.seg) {
                if (c.group == 2 && b.idx < 3) {
                    BatStartDive(w, b);
                } else {
                    b.state = 5;
                    b.counter = 0;
                }
            }
        }
        if (c.group != 2 && b.idx == w.batAttacker && 3 < c.seg) BatStartDive(w, b);
        break;
    }
    case 2: {
        b.rot[1] += c.turn;
        c.turnCount = c.turnCount + F32(exe, 0x004C4380);
        const double k = F64(exe, 0x005643D8);
        const float x = static_cast<float>(static_cast<float>(SinA(b.rot[1]) * k) + b.pos[0]);
        b.pos[0] = x;
        b.pos[2] = static_cast<float>(static_cast<float>(CosA(b.rot[1]) * k) + b.pos[2]);
        if (c.turnCount > c.turnFrames) {
            c.dive[0] = x;
            c.dive[1] = b.pos[1];
            c.dive[2] = b.pos[2];
            c.speed = 1.0f;
            b.flags34 |= 0x10000000;
            b.state = 3;
            b.diveT = 0;
            const uint32_t u = 0x3CDD2F1Bu;
            memcpy(&b.diveDt, &u, 4);
            BatAnimBlend(c.anim, 0x217, 0, 4);  // 0x0040F860
            b.radius = b.scale * F32(exe, 0x0055E174);
        }
        break;
    }
    case 3: {
        if (b.diveT < F32(exe, 0x005643D0)) {

            const float k1a8 = F32(exe, 0x0055E1A8);
            b.pos[0] = ((w.cam.pos[0] + c.diveOff[0]) - c.dive[0]) * b.diveT * k1a8 + c.dive[0];
            const int a = static_cast<int>(b.diveT * F32(exe, 0x004E1FE0)) & 0xFFFF;
            const float s1 = static_cast<float>(SinA(a) * static_cast<double>(w.cam.pos[1] - c.dive[1]));
            const float s2 = static_cast<float>(SinA(w.cam.pitch));
            b.pos[1] = static_cast<float>(static_cast<float>((s2 + s2) + s1) * F64(exe, 0x005643C8)) + c.dive[1];
            b.pos[2] = ((w.cam.pos[2] + c.diveOff[1]) - c.dive[2]) * b.diveT * k1a8 + c.dive[2];
            b.rot[0] = static_cast<int>((F32(exe, 0x004C4380) - b.diveT) * F32(exe, 0x005643C0));

        }
        const float dt = b.diveDt - F32(exe, 0x005643BC);
        b.diveDt = dt;
        b.diveT = dt + b.diveT;
        if (dt <= F32(exe, 0x004C4CC0)) {
            const int p = b.target;
            if (w.horde) HordeBatStrike(w);  // port only: the barrel takes the bite (world_horde.cpp)
            else if (p >= 0 && p < 2 && w.player[p].state == 5) w.PlayerTakeHit(p, 1, 10);  // 0x00415300
            if (!(b.flags34 & 0x8000000) && b.idx < 3) b.flags34 |= 0x8000000;
            b.state = 4;
            c.yawRate = -0x240;
            b.vy = 0;
            b.f54 = 0;
            BatNextAttacker(w, b.idx, c.count);
        }
        break;
    }
    case 4: {
        const float vy = b.vy - F32(exe, 0x004C4D04);
        b.vy = vy;
        b.pos[1] = vy + b.pos[1];
        b.rot[0] += c.yawRate;
        c.yawRate = static_cast<int>(static_cast<float>(c.yawRate) * F32(exe, 0x005643B8));
        if (b.rot[0] < 0x2000) b.rot[0] = 0x2000;
        b.diveT = b.diveT * F32(exe, 0x004C43A8);
        if (b.pos[1] <= c.dive[1]) {
            b.diveT = 0;
            b.flags34 &= ~0x10000000u;
            int cx = 0, cz = 0;
            switch (c.group) {
            case 0: cx = 5; cz = b.idx % 8; break;
            case 1: cx = (b.idx * 2) % 8; cz = 5; break;
            case 2: cx = 4; cz = (b.idx * 2 + 1) % 8; break;
            default: cx = 0; cz = b.idx % 8; break;
            }
            c.cell[0] = static_cast<int8_t>(cx + 1);
            c.cell[1] = static_cast<int8_t>(cz + 1);
            BatPickTarget(w, b, 0x0043CD1B, 0x0043CD70);
            b.state = 5;
            b.rot[1] += 0x8000;
            b.pos[1] = c.dive[1];
            b.rot[0] = 0;
            b.circleCount = 0;
            BatAnimSet(c.anim, 0x218);  // 0x0040F840
            c.speed = 2.0f;
            b.radius = b.scale * F32(exe, 0x004C4CA0);
        }
        break;
    }
    case 5: {
        const int ty = Atan2A(c.target[0] - b.pos[0], c.target[1] - b.pos[2]) & 0xFFFF;
        c.yawTarget = ty;
        const int yaw = b.rot[1] & 0xFFFF;
        b.rot[1] = yaw;
        int d = ty - yaw;
        const int ad = d < 0 ? yaw - ty : d;
        if (0x8000 < ad) d = yaw - ty;
        b.rot[1] = d / 32 + yaw;
        const int cc = b.circleCount++;
        const float sp = c.speed * F32(exe, 0x004C4CC8);
        const float dx = static_cast<float>(SinA(b.rot[1]) * sp);
        const float dz = static_cast<float>(CosA(b.rot[1]) * sp);
        bool collided = false;
        int newDir = 0;
        bool loop = false;
        if (cc > 0x1E && c.count > 0) {
            loop = true;
            const float k8c = F32(exe, 0x004C4C8C), kr = F32(exe, 0x0055E1C8);
            for (int i = 0; i < c.count; ++i) {
                if (i == b.idx || !w.batList[i]) continue;
                BatTask& o = *static_cast<BatTask*>(w.batList[i]);
                const float A = dx * k8c + b.pos[0];
                const float B = dz * k8c + b.pos[2];
                const float ox = dx + o.pos[0];
                const float oz = dz + o.pos[2];
                const float ddz = B - oz;
                const float ddx = A - ox;
                const float dist = std::sqrt(ddx * ddx + ddz * ddz);
                if (dist < kr) {
                    collided = true;
                    b.circleCount = 0;
                    if (b.bumped == 0) {
                        o.bumped = 1;
                        const int oy = o.rot[1];
                        const int e = oy - b.rot[1];
                        const int ae = e < 0 ? b.rot[1] - oy : e;
                        if (ae > 0x6000) newDir = ((w.Rand(0x0043CF6D) & 1) << 15) + b.rot[1] - 0x4000;
                        else newDir = (oy - 0x8000) & 0xFFFF;
                    }
                }
            }
        }
        (void)loop;
        if (collided || b.bumped != 0) {
            if (c.speed == 2.0f) {
                b.pos[0] = dx + b.pos[0];
                b.pos[2] = dz + b.pos[2];
            } else if (b.bumped == 0) {
                const float kr = F32(exe, 0x0055E1C8);
                c.target[0] = static_cast<float>(SinA(newDir) * kr) + b.pos[0];
                c.target[1] = static_cast<float>(CosA(newDir) * kr) + b.pos[2];
            } else {
                b.bumped = 0;
                b.pos[0] = (dx + dx) + b.pos[0];
                b.pos[2] = (dz + dz) + b.pos[2];
            }
        } else {
            b.pos[0] = dx + b.pos[0];
            b.pos[2] = dz + b.pos[2];
        }
        const int c2 = b.counter++;
        const float ez = c.target[1] - b.pos[2], ex = c.target[0] - b.pos[0];
        if (c2 > 600 || std::sqrt(ex * ex + ez * ez) < F32(exe, 0x0055D2B4)) {
            const int r1 = w.Rand(0x0043D07A) & 7;
            const int r2 = w.Rand(0x0043D08E) & 7;
            c.cell[0] = static_cast<int8_t>(r1 + 1);
            c.cell[1] = static_cast<int8_t>(r2 + 1);
            BatPickTarget(w, b, 0x0043D0B9, 0x0043D10E);
            b.counter = 0;
            c.speed = 1.0f;
        }
        if (static_cast<int>(c.speed) == 1 && 0x5Au < w.seedCounter - w.batLastAttack && w.batAttacker == b.idx) {
            const int r = BatStartDive(w, b);
            if (r == 0 || c.speed == 2.0f) {
                w.batLastAttack = w.seedCounter;
                BatNextAttacker(w, b.idx, c.count);
            }
        }
        break;
    }
    default:
        break;
    }
    // ---- draw: the skeleton (yaw + 0x8000), in stage 1 scene 3 its reflection in the water, and the shadow
    const bool drawn = ((c.group != 2 || b.pos[0] <= c.start[0] - F32(exe, 0x004C43A0) || (b.flags34 & 0x8000000) ||
                         b.idx >= 3) &&
                        b.state != 0 && b.state != 6) ||
                       (c.group == 2 && b.idx >= 3 && w.runner->flags[2] == 1);
    if (drawn) {
        b.rot[1] += 0x8000;
        w.LightChar();  // 0x0041DC70 (renderer state)
        BatSkelDraw(w, b);  // 0x0040F4F0
        w.LightScene();  // 0x0041DCC0
        if (w.cfg.eventMode == 0 && w.runner->scene == 3) {
            // reflection: the same skeleton mirrored in height about the dive height c.dive[1] (+0x1C), 1.75 lower
            // (state 3: 4 more) and turned over (pitch + 0x8000) in states 2 / 3; +0x34 & 0x1000000 keeps the model
            // edits of the draw above (0x0040F960)
            const float y = b.pos[1];
            b.flags34 |= 0x1000000;
            b.pos[1] = (c.dive[1] - (y - c.dive[1])) - F32(exe, 0x0055D228);
            if (b.state == 3) b.pos[1] = b.pos[1] - F32(exe, 0x004C4CA0);
            if (b.state == 2 || b.state == 3) b.rot[0] += 0x8000;
            w.LightChar();  // 0x0041DC70: the reflection uses the body's actor lighting too
            BatSkelDraw(w, b);
            w.LightScene();  // 0x0041DCC0
            if (b.state == 2 || b.state == 3) b.rot[0] -= 0x8000;
            b.pos[1] = y;
            b.flags34 &= ~0x1000000u;
        }
        b.rot[1] -= 0x8000;
        ++c.anim.time;
        if (c.group != 2 || b.idx < 3 || !(k5643f0 < b.pos[0])) {
            Mat4 m = Mat4::Identity();
            const float h = c.group == 2 ? F32(exe, 0x005643E8) : F32(exe, 0x004C4CC8);
            mx::Translate(m, b.pos[0], w.ground + h, b.pos[2]);
            mx::RotY(m, b.rot[1]);
            mx::Translate(m, 0.0f, 0.0f, -1.0f);
            mx::Scale(m, 4.0f, 1.0f, 8.0f);
            w.Draw(0x10D0, m);
        }
    }
    mx::Transform(w.cam.view, b.pos, b.viewPos);
    w.RegisterTarget(&b);
    if (c.group != 2 || w.runner->flags[0x5E] != 0) {
        if (b.lock) {
            memcpy(b.lock->point, b.pos, 12);  // +0x100
            BatSyncLock(b);
            w.lockOn.Register(b.lock, w.playerPos);  // 0x00408EC0
        }
    }
    if (0x5Bu < w.seedCounter - w.batLastAttack && c.count > 0 && !w.batList[w.batAttacker])
        w.batAttacker = static_cast<int>(static_cast<uint32_t>(w.batAttacker + 1) % static_cast<uint32_t>(c.count));
    memcpy(c.prev, b.pos, 12);
    w.batList[b.idx] = &b;
}

// 0x0043BEF0: first run of a bat
static void TaskBatInit(Task& t) {
    World::Impl& w = *CurrentWorld();
    BatTask& b = t.As<BatTask>();
    const ExeImage& exe = w.exe;
    BatCtl& c = b.c;
    c.count = b.counter;
    b.flags34 = 1;
    b.hp = 1;
    b.scale = 0.75f;
    c.prev[0] = b.pos[0];
    c.prev[2] = b.pos[2];
    c.start[0] = b.pos[0];
    c.start[1] = b.pos[2];
    if (b.variant == 1) {
        const int st = w.cfg.eventMode, sc = w.runner->scene;
        if (st == 0 || st == 1) {
            if (st == 0) {
                if (sc == 3) {
                    b.wing = 0;
                    c.group = 0;
                } else if (sc == 8) {
                    c.group = 1;
                    b.wing = 1;
                }
            }
            if (sc == 0xE) {
                b.wing = 0;
                c.group = 3;
            } else if (sc == 0x12) {
                b.wing = 0;
                c.group = 4;
            } else if (sc == 0x19) {
                b.wing = 1;
                c.group = 2;
                b.scale = 0.55f;  // 0x3F0CCCCD
                if (b.idx == 0) SpawnBatWater(w, b.pos);  // 0x0043EF70 (world_st2_bats.cpp)
            }
        }
        float wt[3], wt2[3];
        SplineWeights(exe, 0.0f, wt);
        const uint32_t pts = 0x0055E200 + static_cast<uint32_t>((b.idx + c.group * 10) * 16);
        const float b0 = S8(exe, pts), b1 = S8(exe, pts + 1), b2 = S8(exe, pts + 2), b3 = S8(exe, pts + 3),
                    b4 = S8(exe, pts + 4), b5 = S8(exe, pts + 5);
        b.pos[0] = ((b2 * wt[1] + b4 * wt[2]) + b0 * wt[0]) + c.start[0];
        b.pos[2] = ((b3 * wt[1] + b5 * wt[2]) + b1 * wt[0]) + c.start[1];
        float t01;
        const uint32_t u = 0x3C23D70Au;
        memcpy(&t01, &u, 4);
        SplineWeights(exe, t01, wt2);
        const float x2 = ((b2 * wt2[1] + b4 * wt2[2]) + b0 * wt2[0]) + c.start[0];
        const float z2 = ((b3 * wt2[1] + b5 * wt2[2]) + b1 * wt2[0]) + c.start[1];
        b.rot[1] = static_cast<int16_t>(Atan2A(x2 - b.pos[0], z2 - b.pos[2]));
        b.state = 0;
        b.f1338 = b.f133C = 0;
        b.counter = 0;
        b.delay = b.idx * 20;
        b.f1358 = b.dying = b.bumped = 0;
        c.f14 = static_cast<float>(b.idx + 1) * F32(exe, 0x004C4D10);
        b.pos[1] = b.pos[1] + F32(exe, 0x004C4380);
        b.flags34 |= 0x8000;
        if (c.group == 2 && b.idx >= 3) {
            b.flags34 |= 0x8000000;
            const uint32_t y = 0x4225999Au;
            memcpy(&b.pos[1], &y, 4);
        }
    }
    b.rot[0] = 0;
    b.rot[2] = 0;
    switch (c.group) {
    case 0: c.cell[0] = 4; c.cell[1] = static_cast<int8_t>(b.idx % 8); break;
    case 3: c.cell[0] = 6; c.cell[1] = static_cast<int8_t>(b.idx % 8); break;
    case 4: c.cell[0] = 0; c.cell[1] = static_cast<int8_t>(b.idx % 8); break;
    default: c.cell[0] = static_cast<int8_t>((b.idx * 2) % 8); c.cell[1] = 4; break;
    }
    BatPickTarget(w, b, 0x0043C266, 0x0043C2BE);
    c.speed = 1.0f;
    c.dive[1] = b.pos[1];
    c.seg = 0;
    c.t = 0;
    c.tStep = F32(exe, 0x0055E520 + static_cast<uint32_t>(c.group) * 4);
    if (c.group == 2) {
        if (b.idx == 1) c.tStep = 0.05f;   // 0x3D4CCCCD
        if (b.idx >= 2) c.tStep = 0.025f;  // 0x3CCCCCCD
    }
    if (c.group == 3 && (b.idx % 2) == 1) c.tStep = 0.06f;  // 0x3D75C28F
    b.slot120 = -1;
    c.anim.motion = 0x218;
    BatAnimInit(w, c.anim, b.wing);  // 0x0040EAE0
    c.anim.time = w.Rand(0x0043C395) % 0x33;
    b.radius = b.scale * F32(exe, 0x004C4CA0);
    if (c.group != 2) {
        if (b.lock) {
            BatSyncLock(b);
            w.lockOn.AddDirect(b.lock);  // 0x00408E80
            b.slot120 = static_cast<int8_t>(b.lock->slot);
        }
        ++w.attackers;
        ++w.liveEnemies;
    }
    if (b.lock) memcpy(b.lock->point, b.pos, 12);  // +0x100
    mx::Transform(w.cam.view, b.pos, b.viewPos);
    w.RegisterTarget(&b);
    b.fn = 0x0043C440;
    t.fn = TaskBat;
}

// 0x0043BD30 (actor type 0x40 variants 0 / 1): the group (variant 0: 1 bat; 1: 8, 10 with two players; scenes 0xE /
// 0x12: 6 / 8; scene 0x19: 4)
void World::Impl::SpawnBatGroup(const SpawnRecord& r, int variant, int count) {
    int n = 1;
    if (variant == 1) {
        n = players == 2 ? 10 : 8;
        const int sc = runner->scene;
        if (sc == 0xE || sc == 0x12) n = players == 2 ? 8 : 6;
        if (sc == 0x19) n = 4;
    }
    if (count > 0) n = std::min(count, 10);  // port only (the Horde mode's groups)
    batAlive = 0;
    for (auto& p : batList) p = nullptr;
    for (int i = 0; i < n; ++i) {
        BatTask* b = NewTask<BatTask>(TaskBatInit, 0x0043BEF0);
        memcpy(b->pos, r.pos, 12);
        b->rot[1] = r.rot[1];
        b->idx = static_cast<int8_t>(i);
        b->variant = variant;
        b->counter = n;
        b->lock = &lockTargets[nextLockTarget++];
        b->lock->targetPlayer = 0;
        if (i < 10) batList[i] = b;
        ++batAlive;
    }
    batAttacker = 0;
    batLastAttack = seedCounter;
}

// 0x00430C50: blood (task 0x00430BD0) at a view-space point (other actors: actor 0x42 fish, world_routes_fish.cpp)
void World::Impl::SpawnBlood430BD0(const float view[3]) {
    BatBloodTask* k = NewTask<BatBloodTask>(TaskBatBlood, 0x00430BD0);
    memcpy(k->pos, view, 12);
    k->model = 0x3A;
}

// 0x0043E4C0(obj, x, y, z, scale): the ripple / splash (task 0x0043E540) with the object's yaw +0x68
void World::Impl::SpawnSplash43E4C0(int yaw, float x, float y, float z, float scale) {
    BatSplashTask* k = NewTask<BatSplashTask>(TaskBatSplash, 0x0043E540);
    k->pos[0] = x;
    k->pos[1] = y;
    k->pos[2] = z;
    k->yaw = yaw;
    k->timer = 0x78;
    k->scale = scale;
    Sound(cfg.eventMode == 0 ? 0x118A9u : 0x119A9u);
}

}  // namespace game
