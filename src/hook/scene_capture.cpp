// Scene capture from the original game (opt-in: HOTD2_SCENECAP=1; HOTD2_SCENECAP_FRAMES=N, default 2400).
// Writes game\scene_capture.txt with independently captured ground truth:
//   F <frame> <ctx> <camId> <tick> <px> <py> <pz> <pitch> <yaw> <roll>     active camera context after the frame
//   C <camId> <time> <px> <py> <pz> <tx> <ty> <tz> <roll> <rollFlag>       each Cam_Evaluate (0x004041E0) result
//   M <modelId> <m0..m15>                                                  each Mdl_Draw/Mdl_DrawAlt (obj+0x34 matrix)
//   T <state> <m0..m15>                                                    D3D GetTransform (2 view, 3 projection) every 60 frames
// Camera context i: 0x009A6000 + i*0x1A4 (view matrix +0x00, camera matrix +0x40, pos +0xC0, pitch +0xCC,
// yaw +0xD0, roll +0xD4, tick +0x144); active index 0x009C6F00; camera id 0x009A2D78.

#include "scene_capture.h"

#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <map>
#include <string>
#include <vector>

#include "log.h"

namespace {

FILE* g_out = nullptr;
int g_frame = 0, g_maxFrames = 2400;
bool g_captureTransforms = false;  // HOTD2_SCENECAP_TRANSFORMS=1
std::unordered_map<uintptr_t, int> g_modelIds;

template <typename T>
T Get(uintptr_t a) { return *reinterpret_cast<T*>(a); }

// HOTD2_SCENECAP_FROM=<frame>: nothing is written before that frame (long gameplay runs captured from a late frame;
// frames are still counted from boot). HOTD2_SCENECAP_MODELS=0: no M lines (cheap task-list runs); =stage: M lines of the
// stage section task 0x00401260 only (light captures for world_test's stage-section check).
int g_from = 0;
bool g_logModels = true;
bool g_modelsStageOnly = false;
bool Counting() { return g_out && g_frame < g_maxFrames; }
bool Active() { return Counting() && g_frame >= g_from; }

// Pointer -> id of the latest submit (0x00418560 / 0x004185A0 / 0x004185E0 called with that id): every model draw
// passes through one of them (stage sections 0x00401260 included), so this is the id the game asked for. Slots that
// are not loaded any more can keep the pointer of a model loaded later at the same address (stage geometry slots keep
// their flags too), so a pointer scan alone logged stage 2 / 3 section models under stage 1 / 2 ids.
std::unordered_map<uintptr_t, int> g_submitted;
void NoteSubmit(int id) {
    if (id <= 0 || id >= 0x2085) return;
    const uintptr_t p = Get<uintptr_t>(0x009A66A4 + id * 16);
    if (p) g_submitted[p] = id;
}

// The id of the submit being executed (0x00418560 / 0x004185A0 / 0x004185E0 / 0x00418620 / 0x00418660): pass-0 draws
// run inside the submit (0x004A8390 -> 0x004A8440), so a Mdl_Draw during a submit draws exactly that id. Pass-1 draws
// are copies of the draw record appended to the list 0x007E78C4 (count 0x007E793C) and drawn later by Mdl_DrawPass1
// 0x004A88E0: those records are mapped to the submit's id when the submit returns. Several loaded slots can hold the
// same model pointer (common model ids linked to another slot by Res_InitModelSlots 0x00418690, and a model loaded at a
// freed model's address), so the pointer alone cannot tell which id was asked for.
int g_curSubmit = 0;
int g_pass1Before = 0;
std::unordered_map<uintptr_t, int> g_pass1Ids;  // pass-1 record address -> id
bool g_idDebug = false;                          // HOTD2_SCENECAP_IDDEBUG=1: log ids the pointer scan resolved differently

void BeginSubmit(int id) {
    NoteSubmit(id);
    g_curSubmit = (id > 0 && id < 0x2085) ? id : 0;
    g_pass1Before = Get<int>(0x007E793C);
}
void EndSubmit(int id) {
    const int n = Get<int>(0x007E793C);
    const uintptr_t list = Get<uintptr_t>(0x007E78C4);
    if (id > 0 && id < 0x2085 && list && n > g_pass1Before)
        for (int i = g_pass1Before; i < n; ++i) g_pass1Ids[Get<uintptr_t>(list + 4 * i)] = id;
    g_curSubmit = 0;
}

int ModelIdScan(uintptr_t model);

// obj: the draw record (Mdl_Draw's argument, or a pass-1 list entry); model = its +0xC model pointer.
int ModelId(uintptr_t model, uintptr_t obj = 0) {
    auto slotIs = [model](int id) { return id > 0 && Get<uintptr_t>(0x009A66A4 + id * 16) == model; };
    int id = -1;
    if (slotIs(g_curSubmit)) id = g_curSubmit;
    if (id < 0 && obj) {
        auto p = g_pass1Ids.find(obj);
        if (p != g_pass1Ids.end() && slotIs(p->second)) id = p->second;
    }
    const int result = id >= 0 ? id : ModelIdScan(model);
    if (g_idDebug) {  // each (path, running submit, result) once: which id the game asked for vs the slots sharing the pointer
        static std::map<std::pair<int, std::pair<int, int>>, int> seen;
        const int path = id < 0 ? 2 : (slotIs(g_curSubmit) ? 0 : 1);  // 0 submit, 1 pass-1 record, 2 pointer scan
        if (seen.size() < 4000 && seen[{ path, { g_curSubmit, result } }]++ == 0) {
            char slots[160] = {};
            int n = 0, len = 0;
            for (int i = 1; i < 0x2085 && n < 6; ++i)
                if (Get<uintptr_t>(0x009A66A4 + i * 16) == model) {
                    len += sprintf_s(slots + len, sizeof(slots) - len, " %d(%04X)", i, Get<uint16_t>(0x009A66AC + i * 16));
                    ++n;
                }
            Log("scene capture ids: frame %d path %d submit %d (slot ptr %p) model %p -> %d; slots with the pointer:%s", g_frame, path, g_curSubmit,
                g_curSubmit > 0 ? reinterpret_cast<void*>(Get<uintptr_t>(0x009A66A4 + g_curSubmit * 16)) : nullptr,
                reinterpret_cast<void*>(model), result, slots);
        }
    }
    return result;
}

int ModelIdScan(uintptr_t model) {
    auto s = g_submitted.find(model);
    if (s != g_submitted.end() && Get<uintptr_t>(0x009A66A4 + s->second * 16) == model) return s->second;
    // Cache pointer -> id, re-validated: a model loaded at a freed model's address must not keep the old id. A freed
    // slot keeps its pointer, so a slot only counts while it is loaded (flags 0x009A66AC & 0x8001, as Mdl_Draw
    // 0x00418560 tests); without that a stage's models were logged with the ids of the previous stage's (same address).
    auto loaded = [](int i) { return (Get<uint16_t>(0x009A66AC + i * 16) & 0x8001) == 0x8001; };
    auto it = g_modelIds.find(model);
    if (it != g_modelIds.end() && it->second >= 0 && Get<uintptr_t>(0x009A66A4 + it->second * 16) == model && loaded(it->second))
        return it->second;
    int id = -1;
    for (int i = 0; i < 0x2085; ++i)
        if (Get<uintptr_t>(0x009A66A4 + i * 16) == model && (id < 0 || loaded(i))) {
            id = i;
            if (loaded(i)) break;
        }
    g_modelIds[model] = id;
    return id;
}

void WriteMatrix(const float* m) {
    for (int i = 0; i < 16; ++i) fprintf(g_out, " %.7g", m[i]);
    fputc('\n', g_out);
}

void CaptureTransforms() {
    // IDirect3DDevice7 at 0x007DEB74; GetTransform is vtable slot 12 (offset 0x30).
    void** dev = Get<void**>(0x007DEB74);
    if (!dev) return;
    using GetTransformFn = HRESULT(__stdcall*)(void*, int, float*);
    auto getTransform = reinterpret_cast<GetTransformFn>((*reinterpret_cast<void***>(dev))[12]);
    for (int state : { 2, 3 }) {
        float m[16];
        if (SUCCEEDED(getTransform(dev, state, m))) {
            fprintf(g_out, "T %d", state);
            WriteMatrix(m);
        }
    }
}

// Screenshots of the original at chosen frames (HOTD2_SCENECAP_SHOTS="700,900,..."): after Game_Frame has
// presented (windowed mode Blts to the client area), copy the client area to game\shot_<frame>.bmp.
std::vector<int> g_shots;

void SaveClientBmp(int frame) {
    HWND hwnd = Get<HWND>(0x007DE62C);
    RECT rc;
    if (!hwnd || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    const int w = rc.right, h = rc.bottom;
    HDC src = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(src);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(src, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, src, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\')) sprintf_s(slash + 1, path + MAX_PATH - (slash + 1), "shot_%05d.bmp", frame);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") == 0 && f) {
        BITMAPFILEHEADER fh = {};
        bi.bmiHeader.biHeight = h;  // write bottom-up BMP
        const DWORD rowBytes = static_cast<DWORD>(w) * 4;
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
        fh.bfSize = fh.bfOffBits + rowBytes * h;
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        for (int y = h - 1; y >= 0; --y) fwrite(static_cast<uint8_t*>(bits) + y * rowBytes, rowBytes, 1, f);
        fclose(f);
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, src);
}

// Event VM trace (HOTD2_SCENECAP_EVENTS=1): Evt_Run 0x0045ECC0 loops `table[*script]()` over the opcode
// handler table at 0x005931D8 (96 entries, writable data); handlers advance the script pointer 0x009C7108
// themselves. We replace each table entry with a per-opcode thunk that logs
//   E <frame> <opcode> <script address> <next 6 words>
// and then calls the original handler. No code is patched.
constexpr uintptr_t kEventTable = 0x005931D8;
constexpr uintptr_t kEventScript = 0x009C7108;
using Handler = void(__cdecl*)();
Handler g_eventOrig[96];

template <int N>
void __cdecl EventThunk() {
    if (Active()) {
        const uint32_t* p = Get<const uint32_t*>(kEventScript);
        fprintf(g_out, "E %d %d %08X", g_frame, N, static_cast<unsigned>(reinterpret_cast<uintptr_t>(p)));
        for (int i = 1; i <= 6; ++i) fprintf(g_out, " %08X", p[i]);
        fputc('\n', g_out);
    }
    g_eventOrig[N]();
}

template <int... I>
void InstallEventThunks(std::integer_sequence<int, I...>) {
    Handler thunks[] = { &EventThunk<I>... };
    Handler* table = reinterpret_cast<Handler*>(kEventTable);
    DWORD old;
    VirtualProtect(table, sizeof(thunks), PAGE_READWRITE, &old);
    for (int i = 0; i < 96; ++i) {
        g_eventOrig[i] = table[i];
        table[i] = thunks[i];
    }
    VirtualProtect(table, sizeof(thunks), old, &old);
}

int (__cdecl* Orig_GameFrame)();
int (__cdecl* Orig_MdlDraw)(uintptr_t obj, int pass);
int (__cdecl* Orig_MdlDrawAlt)(uintptr_t obj, int pass);
void (__cdecl* Orig_CamEvaluate)(int camId, float time, float* pos, float* tgt, int* roll, int* other);

// Texture filter check (HOTD2_SCENECAP_FILTER=<first frame>): after every Mdl_ApplyPvrState 0x004A7780 call, the
// device's actual stage-0 MAG/MIN/MIP filters are counted per TSP filter field (bits 14-13). Mdl_ApplyPvrState
// only issues state changes against its cached TSP (reset to 0x20800000 by 0x004A7630), so the device state can
// differ from what the material alone implies. Summary lines (cumulative, every 600 frames and at the end):
//   X <tsp filter bits> <MAGFILTER> <MINFILTER> <MIPFILTER> <count>
void (__cdecl* Orig_ApplyPvrState)(const uint32_t* pvr);
int g_filterFrom = -1;
std::map<uint32_t, int> g_filterCounts;

void __cdecl ApplyPvrState(const uint32_t* pvr) {
    Orig_ApplyPvrState(pvr);
    if (!Active() || g_filterFrom < 0 || g_frame < g_filterFrom) return;
    void** dev = Get<void**>(0x007DEB74);
    if (!dev) return;
    void** vt = *reinterpret_cast<void***>(dev);
    using GetTSS = HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD*);  // GetTextureStageState, vtable 0x90
    DWORD mag = 0, mn = 0, mip = 0;
    reinterpret_cast<GetTSS>(vt[36])(dev, 0, 16, &mag);
    reinterpret_cast<GetTSS>(vt[36])(dev, 0, 17, &mn);
    reinterpret_cast<GetTSS>(vt[36])(dev, 0, 18, &mip);
    ++g_filterCounts[((pvr[2] >> 13) & 3) << 24 | (mag & 0xFF) << 16 | (mn & 0xFF) << 8 | (mip & 0xFF)];
}

void WriteFilterSummary() {
    for (const auto& [k, n] : g_filterCounts)
        fprintf(g_out, "X %u %u %u %u %d\n", k >> 24, (k >> 16) & 0xFF, (k >> 8) & 0xFF, k & 0xFF, n);
}

// Cull check (HOTD2_SCENECAP_CULL=<first frame>): the device's actual D3DRENDERSTATE_CULLMODE at every Mdl_DrawStrip
// 0x004A59C0 / Mdl_DrawList 0x004A5A40 call, counted per model (Mdl_Draw 0x004A7EF0 only sets it when the strip's
// ctrl & 3 differs from the cached 0x007E7914). Summary lines like X:
//   V <modelId> <CULLMODE> <draw calls>
char (__cdecl* Orig_MdlDrawStrip)(const void* verts, int count);  // al: DrawPrimitive succeeded
char (__cdecl* Orig_MdlDrawList)(const void* verts, int count);
int g_cullFrom = -1, g_curModel = -1;
std::map<std::pair<int, DWORD>, int> g_cullCounts;

void CountCull() {
    if (!Active() || g_cullFrom < 0 || g_frame < g_cullFrom) return;
    void** dev = Get<void**>(0x007DEB74);
    if (!dev) return;
    void** vt = *reinterpret_cast<void***>(dev);
    using GetRS = HRESULT(__stdcall*)(void*, int, DWORD*);
    using GetTransformFn = HRESULT(__stdcall*)(void*, int, float*);
    DWORD cull = 0;
    reinterpret_cast<GetRS>(vt[21])(dev, 22, &cull);
    // + 10 * (WORLD 3x3 determinant < 0) + 100 * (VIEW 3x3 determinant < 0): mirrored transforms flip screen winding
    auto mirrored = [&](int state) {
        float m[16] = {};
        reinterpret_cast<GetTransformFn>(vt[12])(dev, state, m);
        const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                          m[2] * (m[4] * m[9] - m[5] * m[8]);
        return det < 0 ? 1u : 0u;
    };
    cull += 10 * mirrored(1) + 100 * mirrored(2);
    ++g_cullCounts[{ g_curModel, cull }];
}

// Render-state probe (HOTD2_SCENECAP_RSTATE=<first>:<last>:<model>,<model>...): at the strip draws of the listed
// models in frames first..last, the device state (logged when it differs from the previous R line of that model):
//   R <frame> <model> <count> rs 7 14 19 20 27 28 29 34 35 36 37 48 60 137 139 140 141 145 15 22 23 24 25 | tss0 1 2 3 4 |
//   tex <bound> <w>x<h>/<bits> sum <sum of every 4th dword of every 4th row: which texels the slot really holds>
//   | material diffuse ambient specular emissive power | light0 enable | vertex 0 (8 dwords) | cache <Mdl_Draw's cull
//   cache 0x007E7914 = the last strip control word>. HOTD2_SCENECAP_RSTATE_ALL=1: every strip draw (no dedupe).
int g_rsFrom = -1, g_rsTo = -1;
bool g_rsAll = false;
std::vector<int> g_rsModels;
std::map<int, std::string> g_rsLast;

void LogRenderState(const void* verts, int count) {
    if (g_rsFrom < 0 || !Active() || g_frame < g_rsFrom || g_frame > g_rsTo) return;
    if (std::find(g_rsModels.begin(), g_rsModels.end(), g_curModel) == g_rsModels.end() &&
        std::find(g_rsModels.begin(), g_rsModels.end(), -1) == g_rsModels.end())  // -1: every model
        return;
    void** dev = Get<void**>(0x007DEB74);
    if (!dev) return;
    void** vt = *reinterpret_cast<void***>(dev);
    using GetRS = HRESULT(__stdcall*)(void*, int, DWORD*);
    using GetTSS = HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD*);
    using GetTex = HRESULT(__stdcall*)(void*, DWORD, void**);
    using GetMat = HRESULT(__stdcall*)(void*, float*);
    using GetLE = HRESULT(__stdcall*)(void*, DWORD, BOOL*);
    std::string s;
    char b[64];
    static const int kRs[] = { 7, 14, 19, 20, 27, 28, 29, 34, 35, 36, 37, 48, 60, 137, 139, 140, 141, 145, 15, 22, 23, 24, 25 };
    s += "rs";
    for (int r : kRs) {
        DWORD v = 0;
        reinterpret_cast<GetRS>(vt[21])(dev, r, &v);
        snprintf(b, sizeof(b), " %lX", v);
        s += b;
    }
    s += " | tss0";
    for (DWORD t = 1; t <= 4; ++t) {
        DWORD v = 0;
        reinterpret_cast<GetTSS>(vt[36])(dev, 0, t, &v);
        snprintf(b, sizeof(b), " %lu", v);
        s += b;
    }
    void* tex = nullptr;
    reinterpret_cast<GetTex>(vt[34])(dev, 0, &tex);
    s += tex ? " | tex 1" : " | tex 0";
    if (tex) {
        // IDirectDrawSurface7: GetSurfaceDesc (22), Lock (25), Unlock (32): size, pixel format bits and a sum of the
        // first texels (which texture the slot really holds)
        void** sv = *reinterpret_cast<void***>(tex);
        uint8_t desc[124] = {};
        *reinterpret_cast<uint32_t*>(desc) = 124;
        using GetDesc = HRESULT(__stdcall*)(void*, void*);
        using LockFn = HRESULT(__stdcall*)(void*, RECT*, void*, DWORD, HANDLE);
        using UnlockFn = HRESULT(__stdcall*)(void*, RECT*);
        if (SUCCEEDED(reinterpret_cast<GetDesc>(sv[22])(tex, desc))) {
            const uint32_t h = *reinterpret_cast<uint32_t*>(desc + 8), w = *reinterpret_cast<uint32_t*>(desc + 12);
            const uint32_t bits = *reinterpret_cast<uint32_t*>(desc + 72 + 12);  // ddpfPixelFormat.dwRGBBitCount
            snprintf(b, sizeof(b), " %ux%u/%u", w, h, bits);
            s += b;
            uint8_t ld[124] = {};
            *reinterpret_cast<uint32_t*>(ld) = 124;
            if (SUCCEEDED(reinterpret_cast<LockFn>(sv[25])(tex, nullptr, ld, 0x1 | 0x800, nullptr))) {  // WAIT | READONLY
                const int32_t pitch = *reinterpret_cast<int32_t*>(ld + 16);
                const uint8_t* px = *reinterpret_cast<uint8_t**>(ld + 36);
                uint32_t sum = 0;
                if (px)
                    for (uint32_t y = 0; y < h; y += 4)
                        for (uint32_t x = 0; x < w * (bits / 8); x += 4) {
                            const uint32_t v = *reinterpret_cast<const uint32_t*>(px + y * pitch + x);
                            sum += v;
                        }
                reinterpret_cast<UnlockFn>(sv[32])(tex, nullptr);
                snprintf(b, sizeof(b), " sum %X", sum);
                s += b;
            } else {
                s += " nolock";
            }
        }
        reinterpret_cast<ULONG(__stdcall*)(void*)>(sv[2])(tex);  // Release
    }
    float mat[17] = {};
    reinterpret_cast<GetMat>(vt[17])(dev, mat);
    s += " | mat";
    for (float f : mat) {
        snprintf(b, sizeof(b), " %.4g", f);
        s += b;
    }
    BOOL le = FALSE;
    reinterpret_cast<GetLE>(vt[45])(dev, 0, &le);
    s += le ? " | light0 1" : " | light0 0";
    if (verts) {
        s += " | v0";
        const uint32_t* w = static_cast<const uint32_t*>(verts);
        for (int i = 0; i < 8; ++i) {
            snprintf(b, sizeof(b), " %X", w[i]);
            s += b;
        }
    }
    snprintf(b, sizeof(b), " | cache %X", Get<uint32_t>(0x007E7914));
    s += b;
    std::string& last = g_rsLast[g_curModel];
    if (s == last && !g_rsAll) return;
    last = s;
    fprintf(g_out, "R %d %d %d %s\n", g_frame, g_curModel, count, s.c_str());
}

// With the render-state probe on for the model: "R! <frame> <model> <count>" when DrawPrimitive failed.
void LogDrawFailure(char ok, int count) {
    if (ok || g_rsFrom < 0 || !Active() || g_frame < g_rsFrom || g_frame > g_rsTo) return;
    if (std::find(g_rsModels.begin(), g_rsModels.end(), g_curModel) == g_rsModels.end() &&
        std::find(g_rsModels.begin(), g_rsModels.end(), -1) == g_rsModels.end())
        return;
    fprintf(g_out, "R! %d %d %d\n", g_frame, g_curModel, count);
}

// Render-state experiment (HOTD2_RS_OVERRIDE=<model>:<frame>:<state>=<value>[/<state>=<value>...];...): sets device
// render states around the strip draws of a model in one frame and restores them after (which state hides a draw).
struct RsOverride {
    int model, frame;
    std::vector<std::pair<DWORD, DWORD>> states;
};
std::vector<RsOverride> g_rsOverrides;

template <class F> void WithOverrides(F&& draw) {
    void** dev = g_rsOverrides.empty() ? nullptr : Get<void**>(0x007DEB74);
    const RsOverride* o = nullptr;
    if (dev)
        for (const RsOverride& r : g_rsOverrides)
            if (r.model == g_curModel && r.frame == g_frame) o = &r;
    if (!o) return draw();
    void** vt = *reinterpret_cast<void***>(dev);
    using GetRS = HRESULT(__stdcall*)(void*, DWORD, DWORD*);
    using SetRS = HRESULT(__stdcall*)(void*, DWORD, DWORD);
    std::vector<DWORD> saved(o->states.size());
    for (size_t i = 0; i < o->states.size(); ++i) {
        reinterpret_cast<GetRS>(vt[21])(dev, o->states[i].first, &saved[i]);
        reinterpret_cast<SetRS>(vt[20])(dev, o->states[i].first, o->states[i].second);
    }
    draw();
    for (size_t i = 0; i < o->states.size(); ++i) reinterpret_cast<SetRS>(vt[20])(dev, o->states[i].first, saved[i]);
}

char __cdecl MdlDrawStrip(const void* verts, int count) {
    CountCull();
    LogRenderState(verts, count);
    char r = 0;
    WithOverrides([&] { r = Orig_MdlDrawStrip(verts, count); });
    LogDrawFailure(r, count);
    return r;
}

char __cdecl MdlDrawList(const void* verts, int count) {
    CountCull();
    LogRenderState(verts, count);
    char r = 0;
    WithOverrides([&] { r = Orig_MdlDrawList(verts, count); });
    LogDrawFailure(r, count);
    return r;
}

void WriteCullSummary() {
    for (const auto& [k, n] : g_cullCounts) fprintf(g_out, "V %d %lu %d\n", k.first, k.second, n);
}

// 2D sprites (HOTD2_SCENECAP_SPRITES=1):
//   S <14 words of the sprite definition passed to Spr_Draw 0x0041C630 (hex)> @<caller> [@<caller of Spr_DrawSimple>]
//   R <15 words of the draw record at Spr_DrawRecord 0x004A7AB0 (hex)> <texture w> <h> (runtime table 0x00810A00/40)
//   Q <count> then count TL vertices (x y z rhw diffuse specular u v) drawn by Draw_TLStrip 0x004A5A00
// R/Q pairs are adjacent; translucent records are drawn in pass 1, so R lines follow their S line late.
bool g_captureSprites = false;
void (__cdecl* Orig_SprDraw)(const uint32_t* def);
void (__cdecl* Orig_SprDrawRecord)(const uint32_t* rec);
int (__cdecl* Orig_DrawTLStrip)(const float* verts, int count);

void __cdecl SprDraw(const uint32_t* def) {
    if (Active()) {
        fputc('S', g_out);
        for (int i = 0; i < 14; ++i) fprintf(g_out, " %08X", def[i]);
        const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
        fprintf(g_out, " @%08X", static_cast<unsigned>(ret));
        // Spr_DrawSimple 0x0041C6D0 builds the definition at its esp after `sub esp, 0x40`: its own return
        // address follows at def + 0x40.
        if (ret == 0x0041C74D) fprintf(g_out, " @%08X", def[16]);
        fputc('\n', g_out);
    }
    Orig_SprDraw(def);
}

void __cdecl SprDrawRecord(const uint32_t* rec) {
    if (Active()) {
        fputc('R', g_out);
        for (int i = 0; i < 15; ++i) fprintf(g_out, " %08X", rec[i]);
        const uint32_t tex = rec[3];
        const uintptr_t table = Get<uintptr_t>((tex & 0x80000000) ? 0x00810A40 : 0x00810A00);
        const uintptr_t e = table + (tex & 0x7FFFFFFF) * 8;
        fprintf(g_out, " %u %u\n", table ? Get<uint32_t>(e) : 0, table ? Get<uint32_t>(e + 4) : 0);
    }
    Orig_SprDrawRecord(rec);
}

int __cdecl DrawTLStrip(const float* verts, int count) {
    if (Active()) {
        fprintf(g_out, "Q %d", count);
        for (int v = 0; v < count; ++v) {
            const float* p = verts + v * 8;
            fprintf(g_out, " %.7g %.7g %.7g %.7g %08X %08X %.7g %.7g", p[0], p[1], p[2], p[3],
                    *reinterpret_cast<const uint32_t*>(p + 4), *reinterpret_cast<const uint32_t*>(p + 5), p[6], p[7]);
        }
        fputc('\n', g_out);
    }
    return Orig_DrawTLStrip(verts, count);
}

// Task capture (HOTD2_SCENECAP_TASKS=<fn>,<fn>,... [HOTD2_SCENECAP_TASKS_FROM=<frame>]): after every frame the task
// tree (root 0x007DF878; task words [0] fn, [2] serial, [3] alive, [6]/[7] siblings, [8] parent, [10]/[11] first/last
// child) is walked:
//   K <serial>:<fn>:<parent serial> ...           every live task
//   T <serial> <fn> <header> <words...>           full dump (hex words from +0) of tasks whose first seen or current
//                                                  fn is listed; length from the allocation header at -0x10, capped
//   Z <rand state>                                 CRT rand state (per-thread data +0x14, _getptd 0x004B02A1)
//   N <caller>:<calls> ...                         rand (0x004ADED2) calls this frame by return address
// With tasks enabled, M lines get a trailing "^<serial>" of the task running Mdl_Draw (0x007DF8B0).
std::vector<uintptr_t> g_taskFns;
int g_tasksFrom = 0;
std::unordered_map<uint32_t, uintptr_t> g_taskFirstFn;

void DumpTask(uintptr_t t, int depth) {
    if (!t || depth > 16) return;
    for (uintptr_t c = Get<uintptr_t>(t + 0x28); c; c = Get<uintptr_t>(c + 0x1C)) {
        const uint32_t serial = Get<uint32_t>(c + 8);
        const uintptr_t fn = Get<uintptr_t>(c);
        auto it = g_taskFirstFn.emplace(serial, fn).first;
        fprintf(g_out, " %u:%X:%u", serial, static_cast<unsigned>(fn), Get<uint32_t>(t + 8));
        DumpTask(c, depth + 1);
        (void)it;
    }
}

void DumpSelected(uintptr_t t, int depth) {
    if (!t || depth > 16) return;
    for (uintptr_t c = Get<uintptr_t>(t + 0x28); c; c = Get<uintptr_t>(c + 0x1C)) {
        const uint32_t serial = Get<uint32_t>(c + 8);
        const uintptr_t first = g_taskFirstFn[serial], now = Get<uintptr_t>(c);
        bool sel = false;
        for (uintptr_t f : g_taskFns) sel = sel || f == first || f == now;
        if (sel) {
            const int header = Get<int>(c - 0x10);
            int words = (-header) / 4;
            if (words <= 0 || words > 0x600) words = 0x100;  // enemies (0x13F4 bytes) need ~0x505
            fprintf(g_out, "T %u %X %d", serial, static_cast<unsigned>(Get<uintptr_t>(c)), header);
            for (int i = 0; i < words; ++i) fprintf(g_out, " %X", Get<uint32_t>(c + 4 * i));
            fputc('\n', g_out);
        }
        DumpSelected(c, depth + 1);
    }
}

int (__cdecl* Orig_Rand)();
std::map<uintptr_t, int> g_randCalls;

// HOTD2_SCENECAP_RANDLOG=1 (with _TASKS): every rand call in order, "Y <caller> <task serial> <result>".
bool g_randLog = false;

int __cdecl Rand() {
    if (!g_taskFns.empty()) ++g_randCalls[reinterpret_cast<uintptr_t>(_ReturnAddress())];
    const int r = Orig_Rand();
    if (g_randLog && Active() && g_frame >= g_tasksFrom) {
        const uintptr_t cur = Get<uintptr_t>(0x007DF8B0);
        fprintf(g_out, "Y %X %u %d\n", static_cast<unsigned>(reinterpret_cast<uintptr_t>(_ReturnAddress())), cur ? Get<uint32_t>(cur + 8) : 0, r);
    }
    return r;
}

// HOTD2_SCENECAP_SOUNDS=1: every Snd_PlayGameSound 0x0041CFD0 request in order, "U <id> <caller> <task serial>"
// (from _TASKS_FROM).
bool g_soundLog = false;
int (__cdecl* Orig_PlaySound)(uint32_t);

int __cdecl PlaySound(uint32_t id) {
    if (g_soundLog && Active() && g_frame >= g_tasksFrom) {
        const uintptr_t cur = Get<uintptr_t>(0x007DF8B0);
        fprintf(g_out, "U %X %X %u\n", id, static_cast<unsigned>(reinterpret_cast<uintptr_t>(_ReturnAddress())), cur ? Get<uint32_t>(cur + 8) : 0);
    }
    return Orig_PlaySound(id);
}

// HOTD2_SCENECAP_RES=1: every resource request step 0x0041D5A0 (ring 0x007DA220), "Q <type> <arg> <step> <result>
// <caller>" (from _TASKS_FROM; caller 0x41D57B = Res_Step from Game_Frame, others = a flush / forced load), and every
// request queued "q <type> <arg> <caller>" (0x0041D5D0..0x0041D7D0 write the tail entry).
bool g_resLog = false;
int (__cdecl* Orig_ResStep)(int);

int __cdecl ResStep(int index) {
    const uintptr_t e = 0x007DA220 + static_cast<uintptr_t>(index & 0x3F) * 16;
    const uint32_t type = Get<uint32_t>(e), arg = Get<uint32_t>(e + 8), step = Get<uint32_t>(e + 12);
    const int r = Orig_ResStep(index);
    if (g_resLog && Active() && g_frame >= g_tasksFrom)
        fprintf(g_out, "Q %u %X %u %d %X\n", type, arg, step, r, static_cast<unsigned>(reinterpret_cast<uintptr_t>(_ReturnAddress())));
    return r;
}

// HOTD2_SCENECAP_ALLOCS=1: every task creation Task_Create 0x004A6FA0 (from _TASKS_FROM), "A <serial> <fn> <caller> <running
// task serial>" (tasks that are created and killed within one frame never show in the K lines).
bool g_allocLog = false;
void* (__cdecl* Orig_TaskAlloc)(uint32_t, uint32_t);

void* __cdecl TaskAlloc(uint32_t fn, uint32_t size) {
    const uint32_t serial = Get<uint32_t>(0x00598AA8);
    void* r = Orig_TaskAlloc(fn, size);
    if (g_allocLog && Active() && g_frame >= g_tasksFrom) {
        const uintptr_t cur = Get<uintptr_t>(0x007DF8B0);
        fprintf(g_out, "A %u %X %X %u\n", serial, fn, static_cast<unsigned>(reinterpret_cast<uintptr_t>(_ReturnAddress())), cur ? Get<uint32_t>(cur + 8) : 0);
    }
    return r;
}

// HOTD2_SCENECAP_WATCH=<addr>:<bytes>,... : "W <addr>:<hex bytes> ..." after every frame (from _TASKS_FROM).
std::vector<std::pair<uintptr_t, int>> g_watch;

void LogWatch() {
    if (g_watch.empty() || g_frame < g_tasksFrom) return;
    fputc('W', g_out);
    for (const auto& [a, n] : g_watch) {
        fprintf(g_out, " %X:", static_cast<unsigned>(a));
        for (int i = 0; i < n; ++i) fprintf(g_out, "%02X", Get<uint8_t>(a + i));
    }
    fputc('\n', g_out);
}

void LogTasks() {
    if (g_taskFns.empty() || g_frame < g_tasksFrom) {
        g_randCalls.clear();
        return;
    }
    const uint8_t* ptd = reinterpret_cast<uint8_t* (__cdecl*)()>(0x004B02A1)();
    fprintf(g_out, "Z %08X\nN", ptd ? *reinterpret_cast<const uint32_t*>(ptd + 0x14) : 0);
    for (const auto& [a, n] : g_randCalls) fprintf(g_out, " %X:%d", static_cast<unsigned>(a), n);
    fputc('\n', g_out);
    g_randCalls.clear();
    fputc('K', g_out);
    DumpTask(0x007DF878, 0);
    fputc('\n', g_out);
    DumpSelected(0x007DF878, 0);
}

// Pass-1 list (HOTD2_SCENECAP_PASS1=1): at Mdl_DrawPass1 0x004A88E0 entry (list 0x007E78C4, count 0x007E793C,
// before the qsort) and after it returns (same array, sorted):
//   P <n> <entry>...   unsorted; entry = S:<texture id>:<key>:<depth float bits> (sprite) or M:<model id>:<key>:<depth bits>
//   p <n> <i>...       sorted order as indexes into the P list
bool g_capturePass1 = false;
void (__cdecl* Orig_DrawPass1)();

void __cdecl DrawPass1() {
    if (!Active() || !g_capturePass1) {
        Orig_DrawPass1();
        return;
    }
    const uintptr_t list = Get<uintptr_t>(0x007E78C4);
    const int n = Get<int>(0x007E793C);
    std::vector<uintptr_t> before(n > 0 ? n : 0);
    fprintf(g_out, "P %d", n);
    for (int i = 0; i < n; ++i) {
        const uintptr_t e = Get<uintptr_t>(list + 4 * i);
        before[i] = e;
        const uint32_t key = Get<uint32_t>(e);
        const uint32_t depth = Get<uint32_t>(e + 4);  // float bits (exact)
        if (key & 0x40000000) fprintf(g_out, " M:%d:%08X:%08X", ModelId(Get<uintptr_t>(e + 0xC), e), key, depth);
        else fprintf(g_out, " S:%X:%08X:%08X", Get<uint32_t>(e + 0xC), key, depth);
    }
    fputc('\n', g_out);
    Orig_DrawPass1();
    fprintf(g_out, "p %d", n);
    for (int i = 0; i < n; ++i) {
        const uintptr_t e = Get<uintptr_t>(list + 4 * i);
        int idx = -1;
        for (int j = 0; j < n; ++j)
            if (before[j] == e) idx = j;
        fprintf(g_out, " %d", idx);
    }
    fputc('\n', g_out);
}

// HOTD2_SCENECAP_SHOT_EVERY=N: a screenshot every N frames (in addition to HOTD2_SCENECAP_SHOTS).
int g_shotEvery = 0;
int g_shotPhase = 0;  // HOTD2_SCENECAP_SHOT_PHASE

// HOTD2_SCENECAP_ENV=1: the environment as the frame left it, one line whenever it changes (also before
// HOTD2_SCENECAP_FROM; cheap enough for reference-shot runs):
//   V <frame> <mode 0x009C8E98> | block 0 | block 1 | <light globals: colour r g b, intensity, dir x y z> |
//     <0x009A2BB4> <0x009C8D4C> <0x009C71D4 r g b> | <slot>:<D3DLIGHT7 type r g b px py pz dx dy dz range att0 att1 att2
//     theta phi> for each enabled light slot of the table 0x009A1A20 (stride 0x74, +0x68 enabled, +0x70 index)
//   block = <+0x18 pitch> <+0x1C yaw> <+0x24 +0x28 +0x2C fog rgb> <+0x30 near> <+0x34 far> <+0x38 s16>
//           <+0x240 +0x244 +0x248 colour> <+0x24C intensity>
bool g_envLog = false;
std::string g_lastEnv;

void LogEnv() {
    char buf[4096];
    int n = 0;
    auto put = [&](const char* fmt, auto... a) {
        if (n < static_cast<int>(sizeof(buf)) - 64) n += sprintf_s(buf + n, sizeof(buf) - n, fmt, a...);
    };
    put("%d", Get<int>(0x009C8E98));
    for (uintptr_t b : { uintptr_t(0x009A3540), uintptr_t(0x009A59E0) }) {
        put(" | %d %d %d %d %d %.7g %.7g %d %.7g %.7g %.7g %.7g", Get<int>(b + 0x18), Get<int>(b + 0x1C), Get<int>(b + 0x24),
            Get<int>(b + 0x28), Get<int>(b + 0x2C), Get<float>(b + 0x30), Get<float>(b + 0x34), Get<int16_t>(b + 0x38),
            Get<float>(b + 0x240), Get<float>(b + 0x244), Get<float>(b + 0x248), Get<float>(b + 0x24C));
    }
    put(" | %.7g %.7g %.7g %.7g %.7g %.7g %.7g", Get<float>(0x007E7998), Get<float>(0x007E799C), Get<float>(0x007E79A0),
        Get<float>(0x007E79A4), Get<float>(0x007E79A8), Get<float>(0x007E79AC), Get<float>(0x007E79B0));
    put(" | %d %d %.7g %.7g %.7g |", Get<int>(0x009A2BB4), Get<int>(0x009C8D4C), Get<float>(0x009C71D4), Get<float>(0x009C71D8),
        Get<float>(0x009C71DC));
    for (int i = 0; i < 16; ++i) {
        const uintptr_t s = 0x009A1A20 + i * 0x74;
        if (Get<int>(s + 0x68) != 1) continue;
        const float* l = reinterpret_cast<const float*>(s);
        put(" %d:%d", i, Get<int>(s + 0x70));
        put(":%d", Get<int>(s));
        for (int k : { 1, 2, 3, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25 }) put(" %.6g", l[k]);
    }
    const std::string cur(buf, n);
    if (cur == g_lastEnv) return;
    g_lastEnv = cur;
    fprintf(g_out, "V %d %s\n", g_frame, cur.c_str());
}

int __cdecl GameFrame() {
    const int r = Orig_GameFrame();
    if (g_envLog && Counting()) LogEnv();
    if (g_shotEvery > 0 && Counting() && g_frame > 0 && g_frame % g_shotEvery == g_shotPhase) SaveClientBmp(g_frame);
    if (Active()) {
        LogTasks();
        LogWatch();
    }
    if (Active()) {
        const int ctx = Get<int>(0x009C6F00);
        const uintptr_t c = 0x009A6000 + ctx * 0x1A4;
        fprintf(g_out, "F %d %d %d %d %.7g %.7g %.7g %d %d %d\n", g_frame, ctx, Get<int>(0x009A2D78), Get<int>(c + 0x144),
                Get<float>(c + 0xC0), Get<float>(c + 0xC4), Get<float>(c + 0xC8), Get<int>(c + 0xCC), Get<int>(c + 0xD0),
                Get<int>(c + 0xD4));
        for (int s : g_shots)
            if (s == g_frame) SaveClientBmp(g_frame);
        if (g_frame % 60 == 0) {
            if (g_captureTransforms) CaptureTransforms();
            fflush(g_out);  // the process is usually killed, so never rely on fclose
            if (g_frame % 600 == 0) {
                Log("scene capture: frame %d", g_frame);
                if (g_filterFrom >= 0) WriteFilterSummary();
                if (g_cullFrom >= 0) WriteCullSummary();
            }
        }
    }
    if (Counting() && !Active()) {
        for (int s : g_shots)
            if (s == g_frame) SaveClientBmp(g_frame);
        if (g_frame % 600 == 0) Log("scene capture: frame %d (not logging before %d)", g_frame, g_from);
        g_randCalls.clear();
        ++g_frame;
    } else if (Active()) {
        if (++g_frame == g_maxFrames) {
            if (g_filterFrom >= 0) WriteFilterSummary();
            if (g_cullFrom >= 0) WriteCullSummary();
            fflush(g_out);
            Log("scene capture: %d frames written", g_frame);
        }
    }
    return r;
}

// Light state at Mdl_Draw entry (HOTD2_SCENECAP_LIGHTS=1): the pass loops set lights (0x004AA120 / 0x004AA8B0)
// before Mdl_Draw. Logged only when it changes:
//   L <LIGHTING> <AMBIENT argb hex> <light0 enabled> <D3DLIGHT7: type diffuse[4] specular[4] ambient[4] pos[3] dir[3] ...>
bool g_captureLights = false;
uint8_t g_lastLight[2048];
bool g_captureLightsAll = false;  // HOTD2_SCENECAP_LIGHTS=2
size_t g_lastLightSize = 0;

void LogLights() {
    void** dev = Get<void**>(0x007DEB74);
    if (!dev) return;
    void** vt = *reinterpret_cast<void***>(dev);
    using GetRS = HRESULT(__stdcall*)(void*, int, DWORD*);
    using GetLight = HRESULT(__stdcall*)(void*, DWORD, void*);
    using GetLightEnable = HRESULT(__stdcall*)(void*, DWORD, BOOL*);
    DWORD lighting = 0, ambient = 0;
    reinterpret_cast<GetRS>(vt[21])(dev, 137, &lighting);  // D3DRENDERSTATE_LIGHTING
    reinterpret_cast<GetRS>(vt[21])(dev, 139, &ambient);   // D3DRENDERSTATE_AMBIENT
    BOOL enabled = FALSE;
    reinterpret_cast<GetLightEnable>(vt[45])(dev, 0, &enabled);  // GetLightEnable follows LightEnable (0xB0)
    float light[26] = {};
    reinterpret_cast<GetLight>(vt[19])(dev, 0, light);
    uint8_t buf[2048];
    size_t n = 0;
    memcpy(buf + n, &lighting, 4); n += 4;
    memcpy(buf + n, &ambient, 4); n += 4;
    memcpy(buf + n, &enabled, 4); n += 4;
    memcpy(buf + n, light, sizeof(light)); n += sizeof(light);
    // HOTD2_SCENECAP_LIGHTS=2: also every other enabled light (the light table 0x004AA8B0), as the runtime holds it
    float more[15][26] = {};
    int moreIdx[15], moreCount = 0;
    if (g_captureLightsAll) {
        for (DWORD i = 1; i < 16; ++i) {
            BOOL on = FALSE;
            reinterpret_cast<GetLightEnable>(vt[45])(dev, i, &on);
            if (!on) continue;
            reinterpret_cast<GetLight>(vt[19])(dev, i, more[moreCount]);
            moreIdx[moreCount++] = static_cast<int>(i);
        }
        if (n + sizeof(more) + sizeof(moreIdx) <= sizeof(buf)) {
            memcpy(buf + n, more, sizeof(more[0]) * moreCount); n += sizeof(more[0]) * moreCount;
            memcpy(buf + n, moreIdx, sizeof(int) * moreCount); n += sizeof(int) * moreCount;
        }
    }
    if (n == g_lastLightSize && memcmp(buf, g_lastLight, n) == 0) return;
    memcpy(g_lastLight, buf, n);
    g_lastLightSize = n;
    fprintf(g_out, "L %lu %08lX %d %d", lighting, ambient, enabled ? 1 : 0, *reinterpret_cast<int*>(&light[0]));
    for (int i = 1; i < 26; ++i) fprintf(g_out, " %.6g", light[i]);
    for (int k = 0; k < moreCount; ++k) {
        fprintf(g_out, " | %d:%d", moreIdx[k], *reinterpret_cast<int*>(&more[k][0]));
        for (int i = 1; i < 26; ++i) fprintf(g_out, " %.6g", more[k][i]);
    }
    fputc('\n', g_out);
}

// Global fog state (same switch), logged when it changes:
//   G <FOGCOLOR hex> <FOGTABLEMODE> <FOGVERTEXMODE> <RANGEFOGENABLE> <FOGSTART> <FOGEND> <FOGDENSITY>
//     <env +0x24 r g b> <env +0x30 near> <env +0x34 far> <env +0x38 s16 enable>
// FOGENABLE itself is per material (Mdl_ApplyPvrState, TSP bit 23) and not logged here.
DWORD g_lastFog[14];
bool g_haveFog = false;

void LogFog() {
    void** dev = Get<void**>(0x007DEB74);
    if (!dev) return;
    void** vt = *reinterpret_cast<void***>(dev);
    using GetRS = HRESULT(__stdcall*)(void*, int, DWORD*);
    static const int kStates[7] = {34, 35, 140, 48, 36, 37, 38};
    DWORD s[14] = {};
    for (int i = 0; i < 7; ++i) reinterpret_cast<GetRS>(vt[21])(dev, kStates[i], &s[i]);
    const uintptr_t env = 0x009A3540;
    s[7] = Get<DWORD>(env + 0x24);
    s[8] = Get<DWORD>(env + 0x28);
    s[9] = Get<DWORD>(env + 0x2C);
    s[10] = Get<DWORD>(env + 0x30);
    s[11] = Get<DWORD>(env + 0x34);
    s[12] = static_cast<DWORD>(Get<int16_t>(env + 0x38));
    if (g_haveFog && memcmp(s, g_lastFog, sizeof(s)) == 0) return;
    memcpy(g_lastFog, s, sizeof(s));
    g_haveFog = true;
    auto f = [](DWORD v) { float x; memcpy(&x, &v, 4); return static_cast<double>(x); };
    fprintf(g_out, "G %08lX %lu %lu %lu %.6g %.6g %.6g %lu %lu %lu %.6g %.6g %d\n", s[0], s[1], s[2], s[3], f(s[4]),
            f(s[5]), f(s[6]), s[7], s[8], s[9], f(s[10]), f(s[11]), static_cast<int>(static_cast<int16_t>(s[12])));
}

void LogModel(uintptr_t obj) {
    if (!Active() || !g_logModels) return;
    if (g_captureLights) {
        LogLights();
        LogFog();
    }
    const uintptr_t model = Get<uintptr_t>(obj + 0xC);
    if (!model) return;
    if (g_modelsStageOnly) {
        const uintptr_t cur = Get<uintptr_t>(0x007DF8B0);
        if (!cur || Get<uintptr_t>(cur) != 0x00401260) return;
    }
    g_curModel = ModelId(model, obj);
    fprintf(g_out, "M %d", g_curModel);
    if (!g_taskFns.empty() && g_frame >= g_tasksFrom) {
        const float* m = reinterpret_cast<const float*>(obj + 0x34);
        for (int i = 0; i < 16; ++i) fprintf(g_out, " %.7g", m[i]);
        const uintptr_t cur = Get<uintptr_t>(0x007DF8B0);
        fprintf(g_out, " ^%u\n", cur ? Get<uint32_t>(cur + 8) : 0);
    } else {
        WriteMatrix(reinterpret_cast<const float*>(obj + 0x34));
    }
}

void (__cdecl* Orig_MdlSubmit)(int id);
void (__cdecl* Orig_MdlSubmitAlpha)(int id, float alpha);
void (__cdecl* Orig_MdlSelectAlt)(int id);
void (__cdecl* Orig_MdlSubmitAltParam)(int id, float param);
void (__cdecl* Orig_MdlSubmitEnv)(int id);
void __cdecl MdlSubmit(int id) {  // 0x00418560
    BeginSubmit(id);
    Orig_MdlSubmit(id);
    EndSubmit(id);
}
void __cdecl MdlSubmitAlpha(int id, float alpha) {  // 0x004185A0
    BeginSubmit(id);
    Orig_MdlSubmitAlpha(id, alpha);
    EndSubmit(id);
}
void __cdecl MdlSelectAlt(int id) {  // 0x004185E0
    BeginSubmit(id);
    Orig_MdlSelectAlt(id);
    EndSubmit(id);
}
void __cdecl MdlSubmitAltParam(int id, float param) {  // 0x00418620 (alt light set + param, 0x004AA5A0)
    BeginSubmit(id);
    Orig_MdlSubmitAltParam(id, param);
    EndSubmit(id);
}
void __cdecl MdlSubmitEnv(int id) {  // 0x00418660 Mdl_DrawEnvMapped (0x004AA400)
    BeginSubmit(id);
    Orig_MdlSubmitEnv(id);
    EndSubmit(id);
}

int __cdecl MdlDraw(uintptr_t obj, int pass) {
    LogModel(obj);
    return Orig_MdlDraw(obj, pass);
}

int __cdecl MdlDrawAlt(uintptr_t obj, int pass) {
    LogModel(obj);
    return Orig_MdlDrawAlt(obj, pass);
}

void __cdecl CamEvaluate(int camId, float time, float* pos, float* tgt, int* roll, int* other) {
    Orig_CamEvaluate(camId, time, pos, tgt, roll, other);
    if (Active())
        fprintf(g_out, "C %d %.7g %.7g %.7g %.7g %.7g %.7g %.7g %d %d\n", camId, time, pos[0], pos[1], pos[2], tgt[0], tgt[1],
                tgt[2], *roll, Get<int>(0x009A21B0));
}

}  // namespace

bool SceneCaptureEnabled() {
    char buf[16] = {};
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_FRAMES", buf, sizeof(buf)) > 0) g_maxFrames = atoi(buf);
    char shots[512] = {};
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_SHOTS", shots, sizeof(shots)) > 0) {
        for (char* p = shots; *p;) {
            g_shots.push_back(atoi(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    }
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_SHOT_EVERY", buf, sizeof(buf)) > 0) g_shotEvery = atoi(buf);
    buf[0] = 0;
    // HOTD2_SCENECAP_SHOT_PHASE=P: the SHOT_EVERY shots at frame % N == P (an offset sweep, e.g. 150 with N = 300)
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_SHOT_PHASE", buf, sizeof(buf)) > 0) g_shotPhase = atoi(buf);
    buf[0] = 0;
    g_envLog = GetEnvironmentVariableA("HOTD2_SCENECAP_ENV", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    g_captureLights = GetEnvironmentVariableA("HOTD2_SCENECAP_LIGHTS", buf, sizeof(buf)) > 0 && (buf[0] == '1' || buf[0] == '2');
    g_captureLightsAll = g_captureLights && buf[0] == '2';
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_FILTER", buf, sizeof(buf)) > 0) g_filterFrom = atoi(buf);
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_CULL", buf, sizeof(buf)) > 0) g_cullFrom = atoi(buf);
    char rs[512] = {};  // HOTD2_SCENECAP_RSTATE=<first>:<last>:<model>,<model>...
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_RSTATE", rs, sizeof(rs)) > 0) {
        char* p = rs;
        g_rsFrom = static_cast<int>(strtol(p, &p, 0));
        if (*p == ':') g_rsTo = static_cast<int>(strtol(p + 1, &p, 0));
        if (*p == ':') ++p;
        while (*p) {
            g_rsModels.push_back(static_cast<int>(strtol(p, &p, 0)));
            while (*p == ',' || *p == ' ') ++p;
        }
    }
    char ov[1024] = {};  // HOTD2_RS_OVERRIDE=<model>:<frame>:<state>=<value>[/<state>=<value>...];... (or + between)
    if (GetEnvironmentVariableA("HOTD2_RS_OVERRIDE", ov, sizeof(ov)) > 0) {
        for (char* p = ov; *p;) {
            RsOverride o;
            o.model = static_cast<int>(strtol(p, &p, 0));
            if (*p == ':') o.frame = static_cast<int>(strtol(p + 1, &p, 0));
            while (*p == ':' || *p == '/') {
                const DWORD s = strtoul(p + 1, &p, 0);
                if (*p != '=') break;
                o.states.push_back({ s, strtoul(p + 1, &p, 0) });
            }
            g_rsOverrides.push_back(o);
            while (*p && *p != ';' && *p != '+') ++p;
            if (*p == ';' || *p == '+') ++p;
        }
    }
    buf[0] = 0;
    g_rsAll = GetEnvironmentVariableA("HOTD2_SCENECAP_RSTATE_ALL", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    g_captureSprites = GetEnvironmentVariableA("HOTD2_SCENECAP_SPRITES", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    g_capturePass1 = GetEnvironmentVariableA("HOTD2_SCENECAP_PASS1", buf, sizeof(buf)) > 0 && buf[0] == '1';
    char tasks[2048] = {};  // long lists for the gameplay captures
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_TASKS", tasks, sizeof(tasks)) > 0) {
        for (char* p = tasks; *p;) {
            g_taskFns.push_back(static_cast<uintptr_t>(strtoul(p, &p, 0)));
            while (*p == ',' || *p == ' ') ++p;
        }
    }
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_TASKS_FROM", buf, sizeof(buf)) > 0) g_tasksFrom = atoi(buf);
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_FROM", buf, sizeof(buf)) > 0) g_from = atoi(buf);
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_MODELS", buf, sizeof(buf)) > 0) {
        g_logModels = buf[0] != '0';
        g_modelsStageOnly = strcmp(buf, "stage") == 0;
    }
    buf[0] = 0;
    g_idDebug = GetEnvironmentVariableA("HOTD2_SCENECAP_IDDEBUG", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    g_randLog = GetEnvironmentVariableA("HOTD2_SCENECAP_RANDLOG", buf, sizeof(buf)) > 0 && buf[0] == '1';
    g_soundLog = GetEnvironmentVariableA("HOTD2_SCENECAP_SOUNDS", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    g_allocLog = GetEnvironmentVariableA("HOTD2_SCENECAP_ALLOCS", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    g_resLog = GetEnvironmentVariableA("HOTD2_SCENECAP_RES", buf, sizeof(buf)) > 0 && buf[0] == '1';
    char watch[4096] = {};  // long lists for the later-stage captures
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_WATCH", watch, sizeof(watch)) > 0) {
        for (char* p = watch; *p;) {
            const uintptr_t a = static_cast<uintptr_t>(strtoul(p, &p, 0));
            int n = 4;
            if (*p == ':') n = static_cast<int>(strtol(p + 1, &p, 0));
            if (a) g_watch.push_back({ a, n });
            while (*p == ',' || *p == ' ') ++p;
        }
    }
    buf[0] = 0;
    g_captureTransforms = GetEnvironmentVariableA("HOTD2_SCENECAP_TRANSFORMS", buf, sizeof(buf)) > 0 && buf[0] == '1';
    buf[0] = 0;
    return GetEnvironmentVariableA("HOTD2_SCENECAP", buf, sizeof(buf)) > 0 && buf[0] == '1';
}

void SceneCaptureHooks(SceneCaptureHook out[20]) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\')) strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "scene_capture.txt");
    fopen_s(&g_out, path, "w");
    Log("scene capture: %s (%d frames)", g_out ? path : "FAILED to open", g_maxFrames);
    char ev[8] = {};
    if (GetEnvironmentVariableA("HOTD2_SCENECAP_EVENTS", ev, sizeof(ev)) > 0 && ev[0] == '1') {
        InstallEventThunks(std::make_integer_sequence<int, 96>{});
        Log("scene capture: event VM trace enabled (96 handler table entries wrapped)");
    }
    out[0] = { 0x0049E220, reinterpret_cast<void*>(GameFrame), reinterpret_cast<void**>(&Orig_GameFrame) };
    out[1] = { 0x004A7EF0, reinterpret_cast<void*>(MdlDraw), reinterpret_cast<void**>(&Orig_MdlDraw) };
    out[2] = { 0x004A8440, reinterpret_cast<void*>(MdlDrawAlt), reinterpret_cast<void**>(&Orig_MdlDrawAlt) };
    out[3] = { 0x004041E0, reinterpret_cast<void*>(CamEvaluate), reinterpret_cast<void**>(&Orig_CamEvaluate) };
    if (g_filterFrom >= 0)
        out[4] = { 0x004A7780, reinterpret_cast<void*>(ApplyPvrState), reinterpret_cast<void**>(&Orig_ApplyPvrState) };
    if (g_captureSprites) {
        out[5] = { 0x0041C630, reinterpret_cast<void*>(SprDraw), reinterpret_cast<void**>(&Orig_SprDraw) };
        out[6] = { 0x004A7AB0, reinterpret_cast<void*>(SprDrawRecord), reinterpret_cast<void**>(&Orig_SprDrawRecord) };
        out[7] = { 0x004A5A00, reinterpret_cast<void*>(DrawTLStrip), reinterpret_cast<void**>(&Orig_DrawTLStrip) };
    }
    if (!g_taskFns.empty()) out[8] = { 0x004ADED2, reinterpret_cast<void*>(Rand), reinterpret_cast<void**>(&Orig_Rand) };
    if (g_capturePass1) out[9] = { 0x004A88E0, reinterpret_cast<void*>(DrawPass1), reinterpret_cast<void**>(&Orig_DrawPass1) };
    if (g_soundLog) out[10] = { 0x0041CFD0, reinterpret_cast<void*>(PlaySound), reinterpret_cast<void**>(&Orig_PlaySound) };
    if (g_allocLog) out[11] = { 0x004A6FA0, reinterpret_cast<void*>(TaskAlloc), reinterpret_cast<void**>(&Orig_TaskAlloc) };
    if (g_cullFrom >= 0 || g_rsFrom >= 0 || !g_rsOverrides.empty()) {
        out[12] = { 0x004A59C0, reinterpret_cast<void*>(MdlDrawStrip), reinterpret_cast<void**>(&Orig_MdlDrawStrip) };
        out[13] = { 0x004A5A40, reinterpret_cast<void*>(MdlDrawList), reinterpret_cast<void**>(&Orig_MdlDrawList) };
    }
    out[14] = { 0x00418560, reinterpret_cast<void*>(MdlSubmit), reinterpret_cast<void**>(&Orig_MdlSubmit) };
    out[15] = { 0x004185A0, reinterpret_cast<void*>(MdlSubmitAlpha), reinterpret_cast<void**>(&Orig_MdlSubmitAlpha) };
    out[16] = { 0x004185E0, reinterpret_cast<void*>(MdlSelectAlt), reinterpret_cast<void**>(&Orig_MdlSelectAlt) };
    out[17] = { 0x00418620, reinterpret_cast<void*>(MdlSubmitAltParam), reinterpret_cast<void**>(&Orig_MdlSubmitAltParam) };
    out[18] = { 0x00418660, reinterpret_cast<void*>(MdlSubmitEnv), reinterpret_cast<void**>(&Orig_MdlSubmitEnv) };
    if (g_resLog) out[19] = { 0x0041D5A0, reinterpret_cast<void*>(ResStep), reinterpret_cast<void**>(&Orig_ResStep) };
}

void SceneCaptureClose() {
    if (g_out) {
        fclose(g_out);
        g_out = nullptr;
    }
}
