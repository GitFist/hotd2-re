#pragma once

// Skinned enemy attachments (meshes whose vertices follow up to four skeleton nodes), from Enemy_SetupAttachment
// 0x00419520, Skin_BuildVertexRefs 0x0041A320, Skin_Deform 0x00419980 (+ vertex writers 0x0041A480 / 0x0041A4D0),
// draw sequence 0x004198B0 (records 0, 1 then 2, 3) and Skin_DrawModel 0x00419B40.
//
// Definition (exe data, 0x0052ED08[enemy kind] = {count, defs*}; def = 14 words):
//   [0] model id, [1] vertex table, [2..4] record 0, [5..7] record 1, [8..10] record 2, [11..13] record 3;
//   record = {source count, source vectors (count x {pos[3], normal[3]} floats), groups (s8 per logical vertex)}.
// Vertex table: {?, off1, size1, off2, size2, s16 a, s16 b} at the table address; entries at +off2, stride a*2 + b*4:
//   a s16 prefix words, then b s32 byte offsets (relative to the loaded model) of every occurrence of that logical
//   vertex in the strip data, -1 terminated.
// Each draw (re)writes, for every present record r and logical vertex i with group g = groups[i] >= 0: position =
// src[g].pos * (node_r * base^-1), normal = src[g].normal * (rotation part), at every occurrence; then the model is
// drawn with the base node matrix. Records 1 and 3 are empty in the defs seen so far.

#include <cstdint>
#include <map>
#include <vector>

#include "anim.h"  // Mat4
#include "exe_image.h"
#include "model.h"

namespace game {

struct SkinDef {
    uint32_t va = 0;
    int model = 0;
    std::vector<std::vector<uint32_t>> vertexOffsets;  // per logical vertex: occurrence offsets relative to the model
    struct Record {
        int count = 0;
        std::vector<float> src;      // count * 6 floats (pos xyz, normal xyz)
        std::vector<int8_t> groups;  // one per logical vertex
    } rec[4];
};

bool LoadSkinDef(const ExeImage& exe, uint32_t defVa, SkinDef& out);

// Every attachment definition reachable from 0x0052ED08 (enemy kinds 0..0x5F), by model id.
std::map<int, uint32_t> SkinDefsByModel(const ExeImage& exe);

// The model base the occurrence offsets are relative to (the loaded model pointer 0x009A66A4[id]).
// Returns the base (blockStart or root of the parsed model) that makes every offset land on a vertex record, or
// UINT32_MAX if neither does.
uint32_t SkinOffsetBase(const SkinDef& def, const Model& model, int* matched = nullptr, int* total = nullptr);

// Applies the deformation to `model` (vertex positions / normals in model space). nodes: the skin node world matrices
// in WorldDraw order (4 nodes: records 0..3; 2 nodes: records 0 and 2); base: the base node world matrix.
// negateNormals: skeleton 0x4C attachments 0..5 (0x0041A520 / 0x0041A580) also write every deformed vertex into the twin
// model 0x82C0 bytes further in the pol file (0x004EDA52[i]) with the normal negated: deform the twin with the def
// of 0x004EDA50[i], its own block start as the offset base and negateNormals.
void SkinDeform(const SkinDef& def, uint32_t offsetBase, const Mat4& base, const Mat4* nodes, int nodeCount, Model& model,
                bool negateNormals = false);

}  // namespace game
