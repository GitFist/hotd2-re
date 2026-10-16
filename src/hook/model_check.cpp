// Runtime validation of the native model parser against the original renderer (opt-in: HOTD2_MODELCHECK=1).
//
// Mdl_Draw (0x004A7EF0) and Mdl_DrawAlt (0x004A8440) take (obj, pass) with the model at obj+0xC. Each strip
// they submit goes through Mdl_DrawStrip 0x004A59C0 / Mdl_DrawList 0x004A5A40 (verts, count), which call
// IDirect3DDevice7::DrawPrimitive with FVF 0x112 (XYZ|NORMAL|TEX1). While a model is being drawn, every
// submitted strip (count + 32-byte vertices) is looked up in game::ParseModel's output for that model,
// parsed from game memory. Strips whose UVs the engine rewrites (texture-address mode 2 path, copies into
// scratch 0x007DF8B8) can differ legitimately and are counted separately.

#include "model_check.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "game/model.h"
#include "log.h"

namespace {

uint64_t Fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t StripKey(const void* verts, uint32_t count) {
    return Fnv(verts, static_cast<size_t>(count) * 32, Fnv(&count, 4));
}

// Same key without UVs (positions + normals only), to classify UV-only differences.
uint64_t GeometryKey(const void* verts, uint32_t count) {
    uint64_t h = Fnv(&count, 4);
    const uint8_t* v = static_cast<const uint8_t*>(verts);
    for (uint32_t i = 0; i < count; ++i) h = Fnv(v + i * 32, 24, h);
    return h;
}

struct ParsedModel {
    uint64_t signature = 0;  // FNV of the model's first 64 bytes, to notice reused memory
    bool ok = false;
    std::unordered_set<uint64_t> strips;
    std::unordered_set<uint64_t> geometry;
};

std::unordered_map<uintptr_t, ParsedModel> g_cache;
const uint8_t* g_current = nullptr;
uint32_t g_matched = 0, g_unmatched = 0, g_unmatchedScratch = 0, g_outside = 0, g_parseFail = 0, g_models = 0;
uint32_t g_matchedAfterReparse = 0, g_uvOnly = 0;
DWORD g_lastReport = 0;

bool ParseFromMemoryUnguarded(const uint8_t* model, ParsedModel* pm) {
    game::PolEntry e{ 0, false, 0, 0x400000 };
    game::Model m;
    if (!game::ParseModel(model, 0x400000, e, m, nullptr)) {
        return false;
    }
    for (const auto& mesh : m.meshes) {
        for (const auto& s : mesh.strips) {
            const uint32_t n = static_cast<uint32_t>(s.vertices.size());
            pm->strips.insert(StripKey(s.vertices.data(), n));
            pm->geometry.insert(GeometryKey(s.vertices.data(), n));
        }
    }
    return true;
}

// The model's block size is unknown in memory; guard against reading past mapped memory.
bool ParseFromMemory(const uint8_t* model, ParsedModel* pm) {
    __try {
        return ParseFromMemoryUnguarded(model, pm);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Logs the in-memory texture fields of a newly seen model with its global model id (slot table 0x009A66A0,
// 16 bytes per id, model pointer at +4), so they can be compared with the values stored in the pol file.
void LogTextureFields(const uint8_t* model) {
    int id = -1;
    for (int i = 0; i < 0x2085; ++i) {
        if (*reinterpret_cast<const uint8_t* const*>(0x009A66A4 + i * 16) == model) {
            id = i;
            break;
        }
    }
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "model tex: id %d fields", id);
    const uint32_t* w = reinterpret_cast<const uint32_t*>(model + 0x18);
    for (int count = 0; count < 6 && *w; ) {
        if (*w & 0x80000000) {
            n += snprintf(buf + n, sizeof(buf) - n, " %08X", w[8]);
            ++count;
            w += 0x14 + (w[0x13] >> 2);
        } else {
            break;  // only material chunks are walked here (strips follow inside each material's length)
        }
    }
    Log("%s", buf);
}

void BeginModel(uintptr_t obj) {
    g_current = *reinterpret_cast<const uint8_t* const*>(obj + 0xC);
    if (!g_current) return;
    const uint64_t sig = Fnv(g_current, 64);
    ParsedModel& pm = g_cache[reinterpret_cast<uintptr_t>(g_current)];
    if (pm.signature != sig) {
        pm = ParsedModel{};
        pm.signature = sig;
        pm.ok = ParseFromMemory(g_current, &pm);
        ++g_models;
        if (!pm.ok && ++g_parseFail <= 5) Log("model check: parse failed for model at %p", g_current);
        if (pm.ok) LogTextureFields(g_current);
    }
}

void CheckStrip(const void* verts, uint32_t count) {
    if (!g_current) {
        ++g_outside;
        return;
    }
    auto it = g_cache.find(reinterpret_cast<uintptr_t>(g_current));
    if (it == g_cache.end() || !it->second.ok) return;
    const uint64_t key = StripKey(verts, count);
    if (it->second.strips.count(key)) {
        ++g_matched;
    } else if (reinterpret_cast<uintptr_t>(verts) == 0x007DF8B8) {
        ++g_unmatchedScratch;  // engine-modified copy (UV address mode path)
    } else {
        // The game may have changed this model's vertex data since our cached parse: parse current memory.
        ParsedModel fresh;
        fresh.signature = it->second.signature;
        fresh.ok = ParseFromMemory(g_current, &fresh);
        if (fresh.ok && fresh.strips.count(key)) {
            ++g_matchedAfterReparse;
            it->second = std::move(fresh);
        } else if (it->second.geometry.count(GeometryKey(verts, count))) {
            ++g_uvOnly;
        } else if (++g_unmatched <= 10) {
            Log("model check: UNMATCHED strip model %p verts %p count %u", g_current, verts, count);
        }
    }
    const DWORD now = GetTickCount();
    if (now - g_lastReport > 5000) {
        g_lastReport = now;
        Log("model check: %u models parsed (%u failed), strips matched %u, matched after re-parse (runtime-modified data) %u, "
            "UV-only diff %u, unmatched %u, scratch-modified %u, outside Mdl_Draw %u",
            g_models, g_parseFail, g_matched, g_matchedAfterReparse, g_uvOnly, g_unmatched, g_unmatchedScratch, g_outside);
    }
}

int (__cdecl* Orig_MdlDraw)(uintptr_t obj, int pass);
int (__cdecl* Orig_MdlDrawAlt)(uintptr_t obj, int pass);
void (__cdecl* Orig_DrawStrip)(const void* verts, uint32_t count);
void (__cdecl* Orig_DrawList)(const void* verts, uint32_t count);

int __cdecl MdlDraw(uintptr_t obj, int pass) {
    BeginModel(obj);
    const int r = Orig_MdlDraw(obj, pass);
    g_current = nullptr;
    return r;
}

int __cdecl MdlDrawAlt(uintptr_t obj, int pass) {
    BeginModel(obj);
    const int r = Orig_MdlDrawAlt(obj, pass);
    g_current = nullptr;
    return r;
}

void __cdecl DrawStrip(const void* verts, uint32_t count) {
    CheckStrip(verts, count);
    Orig_DrawStrip(verts, count);
}

void __cdecl DrawList(const void* verts, uint32_t count) {
    CheckStrip(verts, count);
    Orig_DrawList(verts, count);
}

}  // namespace

bool ModelCheckEnabled() {
    char buf[8] = {};
    return GetEnvironmentVariableA("HOTD2_MODELCHECK", buf, sizeof(buf)) > 0 && buf[0] == '1';
}

void ModelCheckHooks(ModelCheckHook out[4]) {
    out[0] = { 0x004A7EF0, reinterpret_cast<void*>(MdlDraw), reinterpret_cast<void**>(&Orig_MdlDraw) };
    out[1] = { 0x004A8440, reinterpret_cast<void*>(MdlDrawAlt), reinterpret_cast<void**>(&Orig_MdlDrawAlt) };
    out[2] = { 0x004A59C0, reinterpret_cast<void*>(DrawStrip), reinterpret_cast<void**>(&Orig_DrawStrip) };
    out[3] = { 0x004A5A40, reinterpret_cast<void*>(DrawList), reinterpret_cast<void**>(&Orig_DrawList) };
}

void ModelCheckReport() {
    Log("model check final: %u models parsed (%u failed), strips matched %u, unmatched %u, scratch-modified %u, outside Mdl_Draw %u",
        g_models, g_parseFail, g_matched, g_unmatched, g_unmatchedScratch, g_outside);
}
