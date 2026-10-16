#pragma once

// HOTD2 pol\*.bin models (see notes/formats.md). From Res_LoadModel 0x00418820, Res_LoadCommon 0x00418200,
// Mdl_Draw 0x004A7EF0 / 0x004A8440, Tex_RegisterModelTextures 0x004AC980.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace game {

struct ModelVertex {
    float pos[3];
    float normal[3];
    float uv[2];
};

struct ModelStrip {
    uint32_t ctrl = 0;      // bits: 0x08 triangle list, 0x20 vertex references, &3 cull mode, 0x40 shade mode
    bool triangleList = false;
    std::vector<ModelVertex> vertices;  // references resolved, in drawing order
    std::vector<uint32_t> offsets;      // byte offset (in the decoded file) of each vertex's 32-byte record
};

// 20-word material chunk (first word has bit 31 set).
struct ModelMaterial {
    uint32_t words[20] = {};

    uint32_t ParamControl() const { return words[0]; }  // PVR TA parameter control word
    uint32_t Isp() const { return words[1]; }
    uint32_t Tsp() const { return words[2]; }
    uint32_t TextureControl() const { return words[3]; }
    float Sphere(int i) const { return AsFloat(4 + i); }  // centre xyz, radius
    // common.bin: index into tex\common.bin descriptors; other files: global texture id. 0xFFFFFFFF = none.
    uint32_t TextureRef() const { return words[8]; }
    bool Textured() const { return words[8] != 0xFFFFFFFF; }
    int32_t SpecularExponent() const { return static_cast<int32_t>(words[9]); }  // power = 1 << n when > 0
    float AmbientScale() const { return AsFloat(10); }
    float DiffuseA() const { return AsFloat(11); }
    float DiffuseR() const { return AsFloat(12); }
    float DiffuseG() const { return AsFloat(13); }
    float DiffuseB() const { return AsFloat(14); }
    float Specular(int i) const { return AsFloat(16 + i); }
    uint32_t StripBytes() const { return words[0x13] & ~3u; }

    float AsFloat(int i) const;
};

struct ModelMesh {
    ModelMaterial material;
    std::vector<ModelStrip> strips;
};

struct Model {
    uint32_t word0 = 0;
    uint32_t flags = 0;  // 0x10: alternate vertex path (not decoded; none in the shipped files)
    float sphere[4] = {};
    std::vector<ModelMesh> meshes;
    uint32_t blockStart = 0, root = 0;  // header entry of the model (byte offsets in the decoded file)
};

// Number of models in a decoded pol file: the header ends with an entry whose start equals the size.
// Returns -1 if the data has no model header.
int PolModelCount(const uint8_t* data, size_t size);

struct PolEntry {
    uint32_t start;   // byte offset of the model's block (bit 31 of the raw word stripped)
    bool flag;        // raw bit 31
    uint32_t root;    // byte offset of the model root
    uint32_t end;     // start of the next block
};

// Header entries for models 0..count-1.
std::vector<PolEntry> ReadPolHeader(const uint8_t* data, size_t size);

// Parses one model with structural validation. Returns false (with a reason) on any inconsistency.
bool ParseModel(const uint8_t* data, size_t size, const PolEntry& e, Model& out, std::string* error = nullptr);

// Triangles in drawing order (strips alternate winding). Appends 3 vertices per triangle.
void Triangulate(const ModelStrip& s, std::vector<ModelVertex>& out);

}  // namespace game
