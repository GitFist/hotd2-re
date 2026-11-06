#pragma once

#include "game/model.h"
#include <cmath>
#include <cstring>

namespace port {

// A source-proven rigid surface can be regrouped in the PC files and use a
// different UV range. Resolve only its exact native UV/material appearance.
// Seam normals and connectivity are intentionally retained: coincident source
// vertices may disagree on those even when every candidate has identical UV.
inline bool TransferNaomiSourceUv(const game::ModelMesh& pc,
                                  const game::ModelMesh& source,
                                  game::ModelMesh* output) {
    if (!output) return false;
    auto result=pc;
    for (auto& strip:result.strips) for (auto& vertex:strip.vertices) {
        const game::ModelVertex* match=nullptr;
        for (const auto& nativeStrip:source.strips) for (const auto& candidate:nativeStrip.vertices) {
            bool same=true;
            for (int k=0;k<3;++k) if (!std::isfinite(vertex.pos[k]) || !std::isfinite(candidate.pos[k]) ||
                std::abs(vertex.pos[k]-candidate.pos[k])>.0025f) same=false;
            if (!same) continue;
            if (!std::isfinite(candidate.uv[0]) || !std::isfinite(candidate.uv[1])) return false;
            if (match && std::memcmp(match->uv,candidate.uv,sizeof(candidate.uv))!=0) return false;
            match=&candidate;
        }
        if (!match) return false;
        std::memcpy(vertex.uv,match->uv,sizeof(vertex.uv));
    }
    result.material=source.material;
    result.textureBank=source.textureBank;
    *output=std::move(result);
    return true;
}

// Use an independently verified native mesh without invalidating the PC runtime's
// skin/edit tables, which address its original 32-byte vertex records. A strip
// correspondence must still hold; a genuinely different topology is not guessed.
// sourceProvenUv is reserved for bank evidence16: original skin records and
// group selectors prove the ordered correspondence despite authored UV changes.
inline bool PrepareNaomiDeformationMesh(const game::ModelMesh& pc,
                                       const game::ModelMesh& native,
                                       game::ModelMesh* output,
                                       bool sourceProvenUv = false) {
    if (!output || pc.strips.size()!=native.strips.size()) return false;
    for (size_t s=0;s<pc.strips.size();++s) {
        const auto& p=pc.strips[s];const auto& n=native.strips[s];
        if (p.triangleList!=n.triangleList || p.vertices.size()!=n.vertices.size()) return false;
        for (size_t v=0;v<p.vertices.size();++v) {
            for (int k=0;k<3;++k)
                if (!std::isfinite(p.vertices[v].pos[k]) || !std::isfinite(n.vertices[v].pos[k]) ||
                    std::abs(p.vertices[v].pos[k]-n.vertices[v].pos[k])>.001f) return false;
            for (int k=0;k<2;++k)
                if (!std::isfinite(p.vertices[v].uv[k]) || !std::isfinite(n.vertices[v].uv[k]) ||
                    (!sourceProvenUv && std::abs(p.vertices[v].uv[k]-n.vertices[v].uv[k])>.001f)) return false;
        }
    }
    auto result=native;
    for (size_t s=0;s<result.strips.size();++s) {
        result.strips[s].offsets=pc.strips[s].offsets;
        for (size_t v=0;v<result.strips[s].vertices.size();++v) {
            // The PC edit walk also checks the low bit of position X, not just
            // the strip word, to recognize an inline record.
            uint32_t p=0,n=0;
            std::memcpy(&p,&pc.strips[s].vertices[v].pos[0],sizeof(p));
            std::memcpy(&n,&result.strips[s].vertices[v].pos[0],sizeof(n));
            n=(n&~1u)|(p&1u);
            std::memcpy(&result.strips[s].vertices[v].pos[0],&n,sizeof(n));
        }
        // Inline/reference and environment-map flags describe the PC record
        // walk. Native winding/materials remain authoritative for rendering.
        constexpr uint32_t recordFlags=0x120;
        result.strips[s].ctrl=(result.strips[s].ctrl&~recordFlags)|(pc.strips[s].ctrl&recordFlags);
    }
    *output=std::move(result);
    return true;
}

} // namespace port
