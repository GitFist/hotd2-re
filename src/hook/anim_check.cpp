// Runtime validation of game::EvaluatePose against the original animation code (opt-in: HOTD2_ANIMCHECK=1).
//
// Anim_DrawSkeleton 0x0040F520 (obj, ?, nodeBuffer) walks the skeleton of obj (+0x1E type, +0x08 motion id,
// +0x04 tick, +0x0D flags). Each Anim_NodeTransform 0x0040ED30 (node, rotations) leaves the node's full matrix
// (parent chain x object world x view) on the engine's matrix stack, whose top is *(float**)0x007E7990.
// We capture it per rotation slot, then compare each node's matrix relative to the torso (slot of node 1) with
// the same relative matrix from EvaluatePose: rel = M_node * inverse(M_torso) depends only on the chain below
// the torso, so object placement and camera cancel out.

#include "anim_check.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "game/anim.h"
#include "game/exe_image.h"
#include "log.h"

namespace {

constexpr uintptr_t kCurrentMatrix = 0x007E7990;  // float* to the top of the matrix stack
constexpr uintptr_t kMotionSlots = 0x009A37E0;    // per motion id: void* data, s16 state (+4; 2 = loaded)

game::ExeImage g_exe;
bool g_exeOk = false;
std::vector<game::Skeleton> g_skel;
float g_captured[512][16];
bool g_have[512];
uint32_t g_enemyDraws = 0, g_enemyCompared = 0, g_enemyBad = 0, g_enemyBlended = 0, g_enemyBlendedBad = 0, g_enemySkipped = 0;
float g_enemyWorst = 0;
uint32_t g_frames = 0, g_comparedEven = 0, g_badEven = 0, g_comparedOdd = 0, g_badOdd = 0, g_skippedBlend = 0,
         g_skippedOther = 0;
float g_worstEven = 0, g_worstOdd = 0;
DWORD g_lastReport = 0;

bool Invert(const float* m, float* out) {
    // General 4x4 inverse (row-major), Gauss-Jordan with partial pivoting.
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) a[r][c] = m[r * 4 + c];
        for (int c = 0; c < 4; ++c) a[r][4 + c] = r == c;
    }
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return false;
        for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[p][k]);
        const double d = a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = static_cast<float>(a[r][4 + c]);
    return true;
}

void Mul(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            out[i * 4 + j] = s;
        }
}

// Largest element difference relative to the matrix scale (the view matrix may scale).
float Compare(const float* a, const float* b) {
    float scale = 1, err = 0;
    for (int i = 0; i < 16; ++i) scale = std::max(scale, std::fabs(b[i]));
    for (int i = 0; i < 16; ++i) err = std::max(err, std::fabs(a[i] - b[i]));
    return err / scale;
}

void ReportEnemies(const char* prefix) {
    Log("%s (enemies): skeleton draws %u; node matrices compared %u (mismatched %u, worst %.2g); blended %u (differing %u); skipped %u",
        prefix, g_enemyDraws, g_enemyCompared, g_enemyBad, g_enemyWorst, g_enemyBlended, g_enemyBlendedBad, g_enemySkipped);
}

void Report(const char* prefix) {
    Log("%s: skeleton draws %u; node matrices compared even ticks %u (mismatched %u, worst %.2g), odd ticks %u "
        "(mismatched %u, worst %.2g); skipped blended %u, other %u",
        prefix, g_frames, g_comparedEven, g_badEven, g_worstEven, g_comparedOdd, g_badOdd, g_worstOdd, g_skippedBlend,
        g_skippedOther);
}

void CompareObject(uintptr_t obj) {
    const int type = *reinterpret_cast<int16_t*>(obj + 0x1E);
    const int motionId = *reinterpret_cast<int32_t*>(obj + 0x08);
    const int tick = *reinterpret_cast<int32_t*>(obj + 0x04);
    const uint8_t flags = *reinterpret_cast<uint8_t*>(obj + 0x0D);
    ++g_frames;
    if (flags & 1) {  // blending between motions (Anim_NodeTransform's first branch); not modelled
        ++g_skippedBlend;
        return;
    }
    if (type < 0 || type >= static_cast<int>(g_skel.size()) || g_skel[type].nodes.size() < 2 || motionId < 0 ||
        motionId > 4096) {
        ++g_skippedOther;
        return;
    }
    const game::Skeleton& s = g_skel[type];
    const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(kMotionSlots + motionId * 8);
    const int16_t state = *reinterpret_cast<const int16_t*>(kMotionSlots + motionId * 8 + 4);
    if (!data || state != 2) {
        ++g_skippedOther;
        return;
    }
    game::Motion m;
    m.id = motionId;
    m.ticks = game::MotionTicks(g_exe, motionId);
    m.nodes = s.nodeCount;
    if (m.ticks <= 0) {
        ++g_skippedOther;
        return;
    }
    m.data.assign(data, data + 4 + m.StoredFrames() * m.FrameStride());
    const auto ours = game::EvaluatePose(s, m, tick);

    const int torso = s.nodes[1].index;
    if (torso < 0 || torso >= 512 || !g_have[torso]) {
        ++g_skippedOther;
        return;
    }
    float engTorsoInv[16], ourTorsoInv[16];
    if (!Invert(g_captured[torso], engTorsoInv) || !Invert(ours[1].m, ourTorsoInv)) {
        ++g_skippedOther;
        return;
    }
    const bool odd = (tick & 1) != 0;
    for (size_t i = 2; i < s.nodes.size(); ++i) {
        const int slot = s.nodes[i].index;
        if (slot < 0 || slot >= 512 || !g_have[slot]) continue;
        // Only nodes below the torso: their relative matrix is independent of root/world/view.
        int p = s.nodes[i].parent;
        while (p > 1) p = s.nodes[p].parent;
        if (p != 1) continue;
        float engRel[16], ourRel[16];
        Mul(g_captured[slot], engTorsoInv, engRel);
        Mul(ours[i].m, ourTorsoInv, ourRel);
        const float err = Compare(ourRel, engRel);
        if (odd) {
            ++g_comparedOdd;
            g_worstOdd = std::max(g_worstOdd, err);
            if (err > 1e-3f) ++g_badOdd;
        } else {
            ++g_comparedEven;
            g_worstEven = std::max(g_worstEven, err);
            if (err > 1e-3f && ++g_badEven <= 10)
                Log("anim check: MISMATCH type %d motion %d tick %d node %zu (slot %d) err %.3g", type, motionId, tick,
                    i, slot, err);
        }
    }
}

// Enemy skeletons: Enemy_DrawSkeleton 0x004110D0 (obj, ?, nodeBuffer). obj: +0x60 type, +0x20 motion, +0x18 stored
// frame index, +0x37 flags (& 0x21: blending/interpolating). Enemy_NodeWalk 0x004114C0 stores each node's matrix at
// nodeBuffer + slot * 0x90 + 0x28 (after Enemy_NodeTransform 0x00411700, which may blend per node).
void CompareEnemy(uintptr_t obj, uintptr_t nodeBuffer) {
    ++g_enemyDraws;
    const int type = *reinterpret_cast<int16_t*>(obj + 0x60);
    const int motionId = *reinterpret_cast<int32_t*>(obj + 0x20);
    const int frame = *reinterpret_cast<int32_t*>(obj + 0x18);
    const bool blended = (*reinterpret_cast<uint8_t*>(obj + 0x37) & 0x21) != 0;
    if (!nodeBuffer || type < 0 || type >= static_cast<int>(g_skel.size()) || g_skel[type].nodes.size() < 2 ||
        motionId < 0 || motionId > 4096 || frame < 0) {
        ++g_enemySkipped;
        return;
    }
    const game::Skeleton& s = g_skel[type];
    const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(kMotionSlots + motionId * 8);
    if (!data || *reinterpret_cast<const int16_t*>(kMotionSlots + motionId * 8 + 4) != 2) {
        ++g_enemySkipped;
        return;
    }
    game::Motion m;
    m.id = motionId;
    m.ticks = game::MotionTicks(g_exe, motionId);
    m.nodes = s.nodeCount;
    if (m.ticks <= 0 || frame >= m.StoredFrames()) {
        ++g_enemySkipped;
        return;
    }
    m.data.assign(data, data + 4 + m.StoredFrames() * m.FrameStride());
    const auto ours = game::EvaluatePose(s, m, frame * 2);
    auto stored = [&](int slot) { return reinterpret_cast<const float*>(nodeBuffer + slot * 0x90 + 0x28); };
    float engTorsoInv[16], ourTorsoInv[16];
    if (!Invert(stored(s.nodes[1].index), engTorsoInv) || !Invert(ours[1].m, ourTorsoInv)) {
        ++g_enemySkipped;
        return;
    }
    for (size_t i = 2; i < s.nodes.size(); ++i) {
        int p = s.nodes[i].parent;
        while (p > 1) p = s.nodes[p].parent;
        if (p != 1) continue;
        float engRel[16], ourRel[16];
        Mul(stored(s.nodes[i].index), engTorsoInv, engRel);
        Mul(ours[i].m, ourTorsoInv, ourRel);
        const float err = Compare(ourRel, engRel);
        if (blended) {
            ++g_enemyBlended;
            if (err > 1e-3f) ++g_enemyBlendedBad;
            continue;
        }
        ++g_enemyCompared;
        g_enemyWorst = std::max(g_enemyWorst, err);
        if (err > 1e-3f && ++g_enemyBad <= 10)
            Log("anim check: enemy MISMATCH type %d motion %d frame %d node %zu err %.3g", type, motionId, frame, i, err);
    }
}

void (__cdecl* Orig_EnemyDraw)(uintptr_t obj, uintptr_t a, uintptr_t nodeBuffer);

void __cdecl EnemyDraw(uintptr_t obj, uintptr_t a, uintptr_t nodeBuffer) {
    Orig_EnemyDraw(obj, a, nodeBuffer);
    if (g_exeOk) CompareEnemy(obj, nodeBuffer);
    const DWORD now = GetTickCount();
    if (now - g_lastReport > 5000) {
        g_lastReport = now;
        Report("anim check");
        ReportEnemies("anim check");
    }
}

void (__cdecl* Orig_NodeTransform)(uintptr_t node, const int16_t* rot);
void (__cdecl* Orig_DrawSkeleton)(uintptr_t obj, uintptr_t a, uintptr_t nodeBuffer);

void __cdecl NodeTransform(uintptr_t node, const int16_t* rot) {
    Orig_NodeTransform(node, rot);
    const int slot = *reinterpret_cast<int16_t*>(node + 0x14);
    if (slot >= 0 && slot < 512) {
        memcpy(g_captured[slot], *reinterpret_cast<const float* const*>(kCurrentMatrix), 64);
        g_have[slot] = true;
    }
}

void __cdecl DrawSkeleton(uintptr_t obj, uintptr_t a, uintptr_t nodeBuffer) {
    memset(g_have, 0, sizeof(g_have));
    Orig_DrawSkeleton(obj, a, nodeBuffer);
    if (g_exeOk) CompareObject(obj);
    const DWORD now = GetTickCount();
    if (now - g_lastReport > 5000) {
        g_lastReport = now;
        Report("anim check");
    }
}

}  // namespace

bool AnimCheckEnabled() {
    char buf[8] = {};
    return GetEnvironmentVariableA("HOTD2_ANIMCHECK", buf, sizeof(buf)) > 0 && buf[0] == '1';
}

void AnimCheckHooks(AnimCheckHook out[3]) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    g_exeOk = g_exe.Load(path);
    if (g_exeOk) {
        g_skel.resize(64);
        for (int t = 0; t < 64; ++t) game::LoadSkeleton(g_exe, t, g_skel[t]);
    }
    Log("anim check: exe tables %s", g_exeOk ? "loaded" : "NOT loaded");
    out[0] = { 0x0040ED30, reinterpret_cast<void*>(NodeTransform), reinterpret_cast<void**>(&Orig_NodeTransform) };
    out[1] = { 0x0040F520, reinterpret_cast<void*>(DrawSkeleton), reinterpret_cast<void**>(&Orig_DrawSkeleton) };
    out[2] = { 0x004110D0, reinterpret_cast<void*>(EnemyDraw), reinterpret_cast<void**>(&Orig_EnemyDraw) };
}

void AnimCheckReport() {
    Report("anim check final");
    ReportEnemies("anim check final");
}
