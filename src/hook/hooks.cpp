#include "hooks.h"

#include <windows.h>

#include <detours.h>

#include <intrin.h>

#include <cstdlib>
#include <cstring>

#include "game/prs.h"
#include "display_hook.h"
#include "input_hook.h"
#include "anim_check.h"
#include "model_check.h"
#include "scene_capture.h"
#include "log.h"

namespace {

// Shadow mode (HOTD2_SHADOW=1): each replaced function also runs the original and logs mismatches.
bool g_shadow = false;

// 0x0040ACD0 Lz_DecompressPrs
int (__cdecl* Orig_Lz_DecompressPrs)(const uint8_t* src, uint8_t* dst);
int g_prsCalls = 0, g_prsMismatches = 0;

// HOTD2_DEMOKEY_GUARD=1: the attract demo's key file loader 0x00426980 (etc\key_*.bin, not in the installed files)
// gets INVALID_HANDLE_VALUE, GetFileSize 0xFFFFFFFF -> a 0-byte block from 0x004A7400, ReadFile fails, and then
// decompresses whatever heap memory follows that block into the 0x10000-byte buffer 0x009A2228 (+0x100): 122880 bytes in
// the logged runs, an overflow into the following heap blocks that crashes some runs (the custom heap's links). The key
// data is never read (0x009A2228 has no other reference). With the guard that one decompress is skipped when the file
// is missing; the allocations stay as they are.
bool g_demoKeyGuard = false;

bool DemoKeyMissing() {
    const int idx = *reinterpret_cast<const int*>(0x009A3400);
    const char* name = *reinterpret_cast<const char* const*>(0x00589838 + idx * 0x14);
    char path[96];
    wsprintfA(path, "etc\\%s", name);
    return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
}

int __cdecl Lz_DecompressPrs(const uint8_t* src, uint8_t* dst) {
    if (g_demoKeyGuard && reinterpret_cast<uintptr_t>(_ReturnAddress()) == 0x00426A35 && DemoKeyMissing()) {
        Log("Lz_DecompressPrs from 0x00426980 skipped: demo key file missing (HOTD2_DEMOKEY_GUARD)");
        return 0;
    }
    const int n = game::DecompressPrs(src, dst);
    if (g_shadow) {
        int& calls = g_prsCalls;
        int& mismatches = g_prsMismatches;
        ++calls;
        // The original has no output bound; give it generous slack past our result.
        const size_t cap = static_cast<size_t>(n) + (1u << 20);
        uint8_t* check = static_cast<uint8_t*>(VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (check) {
            const int m = Orig_Lz_DecompressPrs(src, check);
            if (m != n || memcmp(check, dst, n) != 0) {
                ++mismatches;
                Log("Lz_DecompressPrs MISMATCH call %d: ours=%d original=%d src=%p", calls, n, m, src);
            }
            VirtualFree(check, 0, MEM_RELEASE);
        }
        if (calls <= 10) {
            Log("Lz_DecompressPrs call %d: %d bytes, %s", calls, n, mismatches ? "MISMATCHES SO FAR" : "matches original");
        } else if (calls % 50 == 0) {
            Log("Lz_DecompressPrs shadow: %d calls, %d mismatches", calls, mismatches);
        }
    }
    return n;
}

// 0x004A05C0 Save_WriteFiles (Game_Save 0x004011F0 -> 0x004A0BD0: the scrambled block -> pol\bg_adv19.bin,
// tex\scr_tod_itm_itamidome2.bin, pol\komono_0.bin, pol\tv2.bin). HOTD2_NOSAVE=1: nothing is written (capture runs leave
// the user's save files alone); the in-memory block is the same either way (0x004A0BD0 unscrambles it again).
int g_saveSkips = 0;
int (__cdecl* Orig_Save_WriteFiles)();
int __cdecl Save_WriteFiles() {
    Log("Game_Save: save files NOT written (HOTD2_NOSAVE, call %d)", ++g_saveSkips);
    return 1;
}

}  // namespace

Hook g_hooks[] = {
    { "Lz_DecompressPrs", 0x0040ACD0, reinterpret_cast<void*>(Lz_DecompressPrs),
      reinterpret_cast<void**>(&Orig_Lz_DecompressPrs) },
    // Optional: filled in by InstallHooks when enabled by environment variables.
    { "Input_Poll", 0x0041E530, nullptr, nullptr },                 // HOTD2_RAWINPUT=1
    { "HGL_SetDisplayModeByIndex", 0x004A6200, nullptr, nullptr },  // HOTD2_WINDOWED=1
    { "Mdl_Draw", 0x004A7EF0, nullptr, nullptr },                   // HOTD2_MODELCHECK=1
    { "Mdl_DrawAlt", 0x004A8440, nullptr, nullptr },
    { "Mdl_DrawStrip", 0x004A59C0, nullptr, nullptr },
    { "Mdl_DrawList", 0x004A5A40, nullptr, nullptr },
    { "Anim_NodeTransform", 0x0040ED30, nullptr, nullptr },          // HOTD2_ANIMCHECK=1
    { "Anim_DrawSkeleton", 0x0040F520, nullptr, nullptr },
    { "Enemy_DrawSkeleton", 0x004110D0, nullptr, nullptr },
    { "Game_Frame", 0x0049E220, nullptr, nullptr },                  // HOTD2_SCENECAP=1 (+ Mdl_Draw/Alt)
    { "Cam_Evaluate", 0x004041E0, nullptr, nullptr },
    { "Mdl_ApplyPvrState", 0x004A7780, nullptr, nullptr },        // HOTD2_SCENECAP_FILTER
    { "Spr_Draw", 0x0041C630, nullptr, nullptr },                 // HOTD2_SCENECAP_SPRITES
    { "Spr_DrawRecord", 0x004A7AB0, nullptr, nullptr },
    { "Draw_TLStrip", 0x004A5A00, nullptr, nullptr },
    { "crt_rand", 0x004ADED2, nullptr, nullptr },                 // HOTD2_SCENECAP_TASKS
    { "Mdl_DrawPass1", 0x004A88E0, nullptr, nullptr },            // HOTD2_SCENECAP_PASS1
    { "Snd_PlayGameSound", 0x0041CFD0, nullptr, nullptr },        // HOTD2_SCENECAP_SOUNDS
    { "Task_Create", 0x004A6FA0, nullptr, nullptr },              // HOTD2_SCENECAP_ALLOCS
    { "Mdl_Submit", 0x00418560, nullptr, nullptr },               // HOTD2_SCENECAP (model ids of the M lines)
    { "Mdl_SubmitAlpha", 0x004185A0, nullptr, nullptr },
    { "Mdl_SelectAlt", 0x004185E0, nullptr, nullptr },
    { "Mdl_SubmitAltParam", 0x00418620, nullptr, nullptr },
    { "Mdl_DrawEnvMapped", 0x00418660, nullptr, nullptr },
    { "Res_StepRequest", 0x0041D5A0, nullptr, nullptr },        // HOTD2_SCENECAP_RES=1
    { "Save_WriteFiles", 0x004A05C0, nullptr, nullptr },          // HOTD2_NOSAVE=1
    { nullptr, 0, nullptr, nullptr },
};
const int g_hookCount = sizeof(g_hooks) / sizeof(g_hooks[0]) - 1;

static bool Apply(bool attach) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    for (int i = 0; i < g_hookCount; ++i) {
        Hook& h = g_hooks[i];
        if (!h.replacement) {
            continue;
        }
        if (attach) {
            *h.original = reinterpret_cast<void*>(h.address);
        }
        LONG err = attach ? DetourAttach(h.original, h.replacement) : DetourDetach(h.original, h.replacement);
        if (err != NO_ERROR) {
            Log("%s %s @ 0x%08X failed: %ld", attach ? "attach" : "detach", h.name, h.address, err);
            DetourTransactionAbort();
            return false;
        }
    }
    LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        Log("commit failed: %ld", err);
        return false;
    }
    return true;
}

bool InstallHooks() {
    char buf[8] = {};
    g_shadow = GetEnvironmentVariableA("HOTD2_SHADOW", buf, sizeof(buf)) > 0 && buf[0] == '1';
    g_demoKeyGuard = GetEnvironmentVariableA("HOTD2_DEMOKEY_GUARD", buf, sizeof(buf)) > 0 && buf[0] == '1';

    // Refuse to patch anything but the exact executable the addresses were mapped from.
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleA(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kExpectedTimestamp) {
        Log("unexpected exe timestamp 0x%08lX (want 0x%08lX); hooks not installed",
            nt->FileHeader.TimeDateStamp, kExpectedTimestamp);
        return false;
    }

    // Assigns a feature's replacement to the table entry for its address. Features that share an address
    // (model check and scene capture both use Mdl_Draw) must not be enabled together; the later one wins.
    auto assign = [](uintptr_t address, void* replacement, void** original) {
        for (int i = 0; i < g_hookCount; ++i) {
            if (g_hooks[i].address == address) {
                if (g_hooks[i].replacement) Log("warning: %s claimed by two features", g_hooks[i].name);
                g_hooks[i].replacement = replacement;
                g_hooks[i].original = original;
            }
        }
    };
    StartKeepActive();
    if (SceneCaptureEnabled()) {
        SceneCaptureHook sc[20] = {};
        SceneCaptureHooks(sc);
        for (const SceneCaptureHook& h : sc) assign(h.address, h.replacement, h.original);
    }

    AnimCheckHook ac[3] = {};
    const bool animCheck = AnimCheckEnabled();
    if (animCheck) {
        AnimCheckHooks(ac);
    }
    ModelCheckHook mc[4] = {};
    const bool modelCheck = ModelCheckEnabled();
    if (modelCheck) {
        ModelCheckHooks(mc);
    }
    for (int i = 0; i < g_hookCount; ++i) {
        for (const AnimCheckHook& h : ac) {
            if (animCheck && g_hooks[i].address == h.address) {
                g_hooks[i].replacement = h.replacement;
                g_hooks[i].original = h.original;
            }
        }
        for (const ModelCheckHook& h : mc) {
            if (modelCheck && g_hooks[i].address == h.address) {
                g_hooks[i].replacement = h.replacement;
                g_hooks[i].original = h.original;
            }
        }
        if (g_hooks[i].address == 0x0041E530 && InputHookEnabled()) {
            g_hooks[i].replacement = InputHookReplacement();
            g_hooks[i].original = InputHookOriginal();
        }
        if (g_hooks[i].address == 0x004A05C0 && GetEnvironmentVariableA("HOTD2_NOSAVE", buf, sizeof(buf)) > 0 && buf[0] == '1') {
            g_hooks[i].replacement = reinterpret_cast<void*>(Save_WriteFiles);
            g_hooks[i].original = reinterpret_cast<void**>(&Orig_Save_WriteFiles);
        }
        if (g_hooks[i].address == 0x004A6200 && DisplayHookEnabled()) {
            g_hooks[i].replacement = DisplayHookReplacement();
            g_hooks[i].original = DisplayHookOriginal();
        }
    }

    bool ok = Apply(true);
    if (ok) {
        int active = 0;
        for (int i = 0; i < g_hookCount; ++i) {
            if (g_hooks[i].replacement) {
                Log("hooked %-32s 0x%08X", g_hooks[i].name, g_hooks[i].address);
                ++active;
            }
        }
        Log("%d hook(s) installed%s", active, g_shadow ? " (shadow mode)" : "");
    }
    return ok;
}

void RemoveHooks() {
    if (ModelCheckEnabled()) {
        ModelCheckReport();
    }
    if (AnimCheckEnabled()) {
        AnimCheckReport();
    }
    SceneCaptureClose();
    if (g_shadow) {
        Log("Lz_DecompressPrs shadow total: %d calls, %d mismatches", g_prsCalls, g_prsMismatches);
    }
    Apply(false);
}
