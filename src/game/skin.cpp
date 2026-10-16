#include "game/skin.h"

#include <cstring>
#include <unordered_map>

namespace game {

namespace {

// General 4x4 inverse (row-vector convention irrelevant: inverse is inverse).
bool Invert(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
    return true;
}

}  // namespace

bool LoadSkinDef(const ExeImage& exe, uint32_t defVa, SkinDef& out) {
    out = SkinDef{};
    if (!exe.InData(defVa)) return false;
    out.va = defVa;
    out.model = static_cast<int>(exe.U32(defVa));
    const uint32_t vt = exe.U32(defVa + 4);
    if (!exe.InData(vt)) return false;
    // Skin_BuildVertexRefs 0x0041A320
    const uint32_t off2 = exe.U32(vt + 0xC), size2 = exe.U32(vt + 0x10);
    const int a = exe.S16(vt + 0x14), b = exe.S16(vt + 0x16);
    const int stride = a * 2 + b * 4;
    if (stride <= 0 || size2 > 0x100000) return false;
    const int count = static_cast<int>(size2) / stride;
    for (int i = 0; i < count; ++i) {
        std::vector<uint32_t> offs;
        const uint32_t e = vt + off2 + i * stride + a * 2;
        for (int k = 0; k < b; ++k) {
            const uint32_t o = exe.U32(e + k * 4);
            if (o == 0xFFFFFFFF) break;
            offs.push_back(o);
        }
        out.vertexOffsets.push_back(offs);
    }
    for (int r = 0; r < 4; ++r) {
        const uint32_t w = defVa + 8 + r * 12;
        auto& rec = out.rec[r];
        rec.count = static_cast<int>(exe.U32(w));
        const uint32_t src = exe.U32(w + 4), grp = exe.U32(w + 8);
        if (rec.count <= 0 || !exe.InData(src) || !exe.InData(grp)) {
            rec.count = 0;
            continue;
        }
        rec.src.resize(rec.count * 6);
        exe.Read(src, rec.src.data(), rec.src.size() * 4);
        rec.groups.resize(count);
        exe.Read(grp, rec.groups.data(), count);
    }
    return true;
}

std::map<int, uint32_t> SkinDefsByModel(const ExeImage& exe) {
    std::map<int, uint32_t> out;
    for (int kind = 0; kind < 0x60; ++kind) {
        const uint32_t t = exe.U32(0x0052ED08 + kind * 4);
        if (!exe.InData(t)) continue;
        const uint32_t n = exe.U32(t), arr = exe.U32(t + 4);
        if (n == 0 || n > 32 || !exe.InData(arr)) continue;  // (skeleton 0x4C has 18)
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t d = exe.U32(arr + i * 4);
            if (!exe.InData(d)) continue;
            SkinDef def;
            if (LoadSkinDef(exe, d, def) && def.model > 0 && def.model < 0x4000 && !def.vertexOffsets.empty())
                out.emplace(def.model, d);
        }
    }
    return out;
}

uint32_t SkinOffsetBase(const SkinDef& def, const Model& model, int* matched, int* total) {
    std::unordered_map<uint32_t, int> present;
    for (const auto& mesh : model.meshes)
        for (const auto& s : mesh.strips)
            for (uint32_t o : s.offsets) present[o] = 1;
    uint32_t best = UINT32_MAX;
    int bestHits = -1, n = 0;
    for (uint32_t base : { model.blockStart, model.root }) {
        int hits = 0;
        n = 0;
        for (const auto& v : def.vertexOffsets)
            for (uint32_t o : v) {
                ++n;
                hits += present.count(base + o) ? 1 : 0;
            }
        if (hits > bestHits) {
            bestHits = hits;
            best = (hits == n) ? base : UINT32_MAX;
            if (hits == n) break;
        }
    }
    if (matched) *matched = bestHits;
    if (total) *total = n;
    return best;
}

// 0x004198B0 -> 0x00419980 x4 (records 0, 1, 2, 3)
void SkinDeform(const SkinDef& def, uint32_t offsetBase, const Mat4& base, const Mat4* nodes, int nodeCount, Model& model,
                bool negateNormals) {
    std::unordered_map<uint32_t, std::vector<ModelVertex*>> at;
    for (auto& mesh : model.meshes)
        for (auto& s : mesh.strips)
            for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i) at[s.offsets[i]].push_back(&s.vertices[i]);
    float binv[16];
    if (!Invert(base.m, binv)) return;
    for (int r = 0; r < 4; ++r) {
        const auto& rec = def.rec[r];
        if (rec.count == 0) continue;
        const int ni = nodeCount == 4 ? r : (r == 0 ? 0 : (r == 2 ? 1 : -1));
        if (ni < 0 || ni >= nodeCount) continue;
        // M = node * base^-1 (row vectors: v' = v * node * base^-1)
        float m[16];
        const float* n = nodes[ni].m;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += n[i * 4 + k] * binv[k * 4 + j];
                m[i * 4 + j] = s;
            }
        for (size_t v = 0; v < def.vertexOffsets.size() && v < rec.groups.size(); ++v) {
            const int g = rec.groups[v];
            if (g < 0 || g >= rec.count) continue;
            const float* p = &rec.src[g * 6];
            const float* q = p + 3;
            float pos[3], nrm[3];
            for (int c = 0; c < 3; ++c) {
                pos[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
                nrm[c] = q[0] * m[c] + q[1] * m[4 + c] + q[2] * m[8 + c];
                if (negateNormals) nrm[c] = -nrm[c];
            }
            for (uint32_t o : def.vertexOffsets[v]) {
                auto it = at.find(offsetBase + o);
                if (it == at.end()) continue;
                for (ModelVertex* mv : it->second) {
                    memcpy(mv->pos, pos, sizeof(pos));
                    memcpy(mv->normal, nrm, sizeof(nrm));
                }
            }
        }
    }
}

}  // namespace game
