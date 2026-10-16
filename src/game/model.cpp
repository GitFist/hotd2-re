#include "model.h"

#include <cstring>

namespace game {

namespace {

uint32_t U32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

bool Fail(std::string* error, const char* what, size_t at) {
    if (error) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s at 0x%zx", what, at);
        *error = buf;
    }
    return false;
}

void ReadVertex(const uint8_t* p, ModelVertex& v) {
    memcpy(v.pos, p, 12);
    memcpy(v.normal, p + 12, 12);
    memcpy(v.uv, p + 24, 8);
}

}  // namespace

float ModelMaterial::AsFloat(int i) const {
    float f;
    memcpy(&f, &words[i], 4);
    return f;
}

int PolModelCount(const uint8_t* data, size_t size) {
    uint32_t prev = 0;
    for (size_t k = 0; (k + 1) * 8 <= size; ++k) {
        const uint32_t s = U32(data + k * 8) & 0x7FFFFFFF;
        if (s == size) return static_cast<int>(k);
        if ((k > 0 && s < prev) || s > size) return -1;
        prev = s;
    }
    return -1;
}

std::vector<PolEntry> ReadPolHeader(const uint8_t* data, size_t size) {
    std::vector<PolEntry> out;
    const int n = PolModelCount(data, size);
    for (int k = 0; k < n; ++k) {
        const uint32_t raw = U32(data + k * 8);
        PolEntry e;
        e.start = raw & 0x7FFFFFFF;
        e.flag = (raw >> 31) != 0;
        e.root = U32(data + k * 8 + 4);
        e.end = U32(data + (k + 1) * 8) & 0x7FFFFFFF;
        out.push_back(e);
    }
    return out;
}

// Mirrors the chunk walk of Mdl_Draw (0x004A7EF0).
bool ParseModel(const uint8_t* data, size_t size, const PolEntry& e, Model& out, std::string* error) {
    out = Model{};
    const size_t root = e.root, end = e.end;
    if (end > size || root + 0x18 > end) return Fail(error, "root past model block", root);
    out.word0 = U32(data + root);
    out.flags = U32(data + root + 4);
    memcpy(out.sphere, data + root + 8, 16);
    out.blockStart = e.start;
    out.root = e.root;

    size_t pos = root + 0x18;
    size_t stripEnd = 0;
    bool haveMaterial = false;
    for (;;) {
        if (pos + 4 > end) return Fail(error, "chunk stream runs past block", pos);
        const uint32_t ctrl = U32(data + pos);
        if (ctrl == 0) break;

        if (ctrl & 0x80000000) {  // material
            if (haveMaterial && pos != stripEnd) return Fail(error, "material length mismatch", pos);
            if (pos + 0x50 > end) return Fail(error, "material past block", pos);
            ModelMesh mesh;
            memcpy(mesh.material.words, data + pos, 0x50);
            stripEnd = pos + 0x50 + mesh.material.StripBytes();
            out.meshes.push_back(std::move(mesh));
            haveMaterial = true;
            pos += 0x50;
            continue;
        }
        if (!haveMaterial) return Fail(error, "strip before any material", pos);
        if (pos + 8 > end) return Fail(error, "strip header past block", pos);
        if (out.flags & 0x10) return Fail(error, "alternate vertex format (flag 0x10) not supported", pos);

        ModelStrip strip;
        strip.ctrl = ctrl;
        strip.triangleList = (ctrl & 0x08) != 0;
        uint32_t count = U32(data + pos + 4);
        if (strip.triangleList) count *= 3;
        pos += 8;
        strip.vertices.resize(count);
        strip.offsets.resize(count);

        for (uint32_t i = 0; i < count; ++i) {
            if (ctrl & 0x20) {
                if (pos + 8 > end) return Fail(error, "vertex reference past block", pos);
                if (U32(data + pos) & 1) {  // inline vertex
                    if (pos + 32 > end) return Fail(error, "vertex past block", pos);
                    ReadVertex(data + pos, strip.vertices[i]);
                    strip.offsets[i] = static_cast<uint32_t>(pos);
                    pos += 32;
                } else {                    // {flag, signed byte offset from the entry's end}
                    int32_t off;
                    memcpy(&off, data + pos + 4, 4);
                    const int64_t target = static_cast<int64_t>(pos) + 8 + off;
                    if (target < static_cast<int64_t>(root) || target + 32 > static_cast<int64_t>(end))
                        return Fail(error, "vertex reference out of block", pos);
                    ReadVertex(data + target, strip.vertices[i]);
                    strip.offsets[i] = static_cast<uint32_t>(target);
                    pos += 8;
                }
            } else {
                if (pos + 32 > end) return Fail(error, "vertex past block", pos);
                ReadVertex(data + pos, strip.vertices[i]);
                strip.offsets[i] = static_cast<uint32_t>(pos);
                pos += 32;
            }
        }
        out.meshes.back().strips.push_back(std::move(strip));
    }
    if (haveMaterial && pos != stripEnd) return Fail(error, "last material length mismatch", pos);
    return true;
}

void Triangulate(const ModelStrip& s, std::vector<ModelVertex>& out) {
    const auto& v = s.vertices;
    if (s.triangleList) {
        for (size_t i = 0; i + 2 < v.size(); i += 3) {
            out.push_back(v[i]);
            out.push_back(v[i + 1]);
            out.push_back(v[i + 2]);
        }
        return;
    }
    for (size_t i = 0; i + 2 < v.size(); ++i) {
        if (i % 2 == 0) {
            out.push_back(v[i]);
            out.push_back(v[i + 1]);
        } else {
            out.push_back(v[i + 1]);
            out.push_back(v[i]);
        }
        out.push_back(v[i + 2]);
    }
}

}  // namespace game
