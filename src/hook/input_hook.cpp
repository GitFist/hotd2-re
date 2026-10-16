// Two-player raw-input guns/mice for the original game (opt-in: HOTD2_RAWINPUT=1).
//
// The original supports only one pointer (the system cursor, device mode 5) unless a
// SEGAJOY.VXD gun or DirectInput gun is present. This hook runs the original Input_Poll,
// then, for each player who has joined with their own raw-input device, writes exactly the
// state mode 5 would have written for that player's gun slot. Players who have not joined
// are left untouched, so keyboard/pad/mouse config from Hod2.ini keeps working.
//
// Aim is written in game pixels (640x480, from 0x007DE634/638), not client pixels: the game
// uses gun aim directly as 640x480 screen coordinates (0x004169C0, 0x0040CE70), while on
// modern Windows the fullscreen client area need not be 640x480.

#include "input_hook.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <vector>

#include "auto_play.h"
#include "gun_state.h"
#include "input/raw_input.h"
#include "log.h"

namespace {

template <typename T>
T& At(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }

// --- Original game state (see notes/symbols.csv, Input_*) ------------------------------
// Gun slots: player 0 -> slot 0, player 1 -> slot 6.
constexpr uintptr_t kMainHwnd     = 0x007DE62C;  // HWND, main game window
constexpr uintptr_t kGameWidth    = 0x007DE634;  // int, set by HGL_SetDisplayMode (640)
constexpr uintptr_t kGameHeight   = 0x007DE638;  // int (480)
constexpr uintptr_t kFullscreenFlag = 0x007DE63C;  // byte, set by HGL_SetDisplayMode
constexpr uintptr_t kGunPos       = 0x007DBD50;  // int[slot*2] x, y (game px; 0xFFFF = off-screen, set by Input_LatchShots)
constexpr uintptr_t kGunFired     = 0x007DC120;  // int[slot]
constexpr uintptr_t kGunOffscreen = 0x007DB7C8;  // int[slot]
constexpr uintptr_t kDevRecord    = 0x007DC1B8;  // 0x34-byte per-slot record
constexpr uintptr_t kDevDesc      = 0x007DAC70;  // 0x78-byte per-slot descriptor
constexpr uintptr_t kModeP1       = 0x00588E24;  // device mode, read by Input_GetDeviceModes 0x0041E260
constexpr uintptr_t kModeP2       = 0x007DC698;
constexpr uint32_t  kTypeGun      = 0x80;
constexpr uint32_t  kModeMouseGun = 5;

std::mutex g_lock;
input::RawInput g_raw;
std::atomic<bool> g_started{false};
HWND g_receiver = nullptr;
uint32_t g_prevHeld[input::kMaxPlayers] = {};
bool g_joinedLogged[input::kMaxPlayers] = {};
input::Aspect g_aspect = input::Aspect::Stretch;
bool g_rawEnabled = false;

LRESULT CALLBACK ReceiverProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT || msg == WM_INPUT_DEVICE_CHANGE) {
        std::lock_guard lock(g_lock);
        g_raw.HandleMessage(msg, wParam, lParam);
        if (msg == WM_INPUT_DEVICE_CHANGE) return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// Logs what this process sees, to settle whether Windows virtualizes the 640x480 fullscreen mode.
void LogGeometry(HWND game) {
    RECT client = {}, window = {};
    GetClientRect(game, &client);
    GetWindowRect(game, &window);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoA(MonitorFromWindow(game, MONITOR_DEFAULTTOPRIMARY), &mi);
    Log("geometry (in-process): client %ldx%ld, window %ld,%ld-%ld,%ld, screen %dx%d, monitor %ld,%ld-%ld,%ld, game %dx%d",
        client.right, client.bottom, window.left, window.top, window.right, window.bottom,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), mi.rcMonitor.left, mi.rcMonitor.top,
        mi.rcMonitor.right, mi.rcMonitor.bottom, At<int>(kGameWidth), At<int>(kGameHeight));
}

DWORD WINAPI InputThread(LPVOID) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = ReceiverProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "hotd2_rawinput";
    RegisterClassA(&wc);
    // A real (hidden) top-level window: RIDEV_INPUTSINK is unreliable on message-only windows.
    g_receiver = CreateWindowExA(WS_EX_TOOLWINDOW, wc.lpszClassName, "", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                 wc.hInstance, nullptr);
    const HWND game = At<HWND>(kMainHwnd);
    bool ok;
    {
        std::lock_guard lock(g_lock);
        ok = g_receiver && g_raw.Init(g_receiver, game);
    }
    Log("raw input %s (aspect %s)", ok ? "ready - pull each trigger to join P1/P2" : "INIT FAILED",
        g_aspect == input::Aspect::Pillarbox43 ? "4:3 pillarbox" : "stretch");
    LogGeometry(game);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        DispatchMessageA(&m);
    }
    return 0;
}

// Scripted d-pad (HOTD2_INPUT_SCRIPT buttons 8 up, 16 down, 32 left, 64 right): the gun record's pad bits 0x10 / 0x20 /
// 0x40 / 0x80 (the menus' cursor masks 0x005978C8.., 0x00589140.., 0x00597B74..; a Dreamcast gun has this d-pad). Only
// script lines carry them; raw-input devices never set these bits.
uint32_t ScriptPadBits(uint8_t b) {
    return (b & 8 ? 0x10u : 0u) | (b & 16 ? 0x20u : 0u) | (b & 32 ? 0x40u : 0u) | (b & 64 ? 0x80u : 0u);
}
uint32_t g_prevPad[input::kMaxPlayers] = {};

void ApplyPlayer(int player, const input::PlayerState& p, bool scripted = false) {
    const int slot = player * 6;

    // The crosshair follows gun aim only in modes 5/6 (0x004169C0); keep 6, else use 5.
    uint32_t& mode = At<uint32_t>(player == 0 ? kModeP1 : kModeP2);
    if (!g_joinedLogged[player]) {
        g_joinedLogged[player] = true;
        Log("P%d joined via raw input (device mode was %u%s)", player + 1, mode,
            (mode & 0xFFFF) == 5 || (mode & 0xFFFF) == 6 ? "" : ", switching to 5");
    }
    if ((mode & 0xFFFF) != 5 && (mode & 0xFFFF) != 6) {
        mode = kModeMouseGun;
    }

    hook::AimConfig cfg;
    cfg.gameW = At<int>(kGameWidth) > 0 ? At<int>(kGameWidth) : 640;
    cfg.gameH = At<int>(kGameHeight) > 0 ? At<int>(kGameHeight) : 480;
    cfg.aspect = g_aspect;
    // Bars (if any) come from the monitor's shape in fullscreen, from the client area in a window.
    RECT rc = {};
    if (At<uint8_t>(kFullscreenFlag)) {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoA(MonitorFromWindow(At<HWND>(kMainHwnd), MONITOR_DEFAULTTOPRIMARY), &mi);
        rc = { 0, 0, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top };
    } else {
        GetClientRect(At<HWND>(kMainHwnd), &rc);
    }
    cfg.displayW = static_cast<float>(rc.right);
    cfg.displayH = static_cast<float>(rc.bottom);
    hook::GunSlotState s = hook::ComputeGunSlot(p, g_prevHeld[player], cfg);
    if (scripted) {
        const uint32_t pad = ScriptPadBits(p.held);
        s.held |= pad;
        s.pressed |= ScriptPadBits(p.pressed);
        s.released |= g_prevPad[player] & ~pad;
        g_prevPad[player] = pad;
    }

    // Same writes as Input_Poll case 5 (mouse-as-gun).
    uint8_t* desc = reinterpret_cast<uint8_t*>(kDevDesc + slot * 0x78);
    uint8_t* rec = reinterpret_cast<uint8_t*>(kDevRecord + slot * 0x34);
    *reinterpret_cast<uint32_t*>(desc) = kTypeGun;
    desc[0x11] = 2;
    *reinterpret_cast<uint32_t*>(rec + 0x00) = kTypeGun;
    *reinterpret_cast<uint8_t**>(rec + 0x30) = desc;
    *reinterpret_cast<uint32_t*>(rec + 0x08) = s.held;
    *reinterpret_cast<uint32_t*>(rec + 0x0C) = ~s.held;
    *reinterpret_cast<uint32_t*>(rec + 0x10) = s.pressed;
    *reinterpret_cast<uint32_t*>(rec + 0x14) = s.released;
    At<int>(kGunFired + slot * 4) = s.fired;
    At<int>(kGunOffscreen + slot * 4) = s.offscreen;
    At<int>(kGunPos + slot * 8) = s.x;
    At<int>(kGunPos + slot * 8 + 4) = s.y;
}

// --- Scripted input (capture tool, HOTD2_INPUT_SCRIPT=<file>) ------------------------------------------------------
// Lines: <boot frame> <player 0|1> <x 0..1> <y 0..1> <buttons: 1 trigger, 2 reload, 4 start; d-pad 8 up, 16 down, 32 left,
// 64 right (ScriptPadBits)>; '#' comments. A player's
// state holds until its next line; timing uses the game's boot frame counter 0x009A5C40. It drives the same writes as a
// joined raw-input device (ApplyPlayer), so the game sees mode-5/6 gun input. This is NOT device input: it bypasses
// raw input entirely and proves nothing about physical devices (whose filtering is unchanged).
struct ScriptLine {
    uint32_t frame;
    int player;
    float x, y;
    uint8_t buttons;
};
std::vector<ScriptLine> g_script;
size_t g_scriptPos = 0;
input::PlayerState g_scripted[input::kMaxPlayers];
bool g_scriptActive = false;

void LoadScript(const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) {
        Log("input script %s: cannot open", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        ScriptLine s{};
        unsigned b = 0;
        if (sscanf_s(line, "%u %d %f %f %u", &s.frame, &s.player, &s.x, &s.y, &b) == 5 && s.player >= 0 && s.player < input::kMaxPlayers) {
            s.buttons = static_cast<uint8_t>(b);
            g_script.push_back(s);
        }
    }
    fclose(f);
    g_scriptActive = !g_script.empty();
    Log("input script %s: %zu lines (SCRIPTED input, not device input)", path, g_script.size());
}

// --- Automatic input (capture tool, HOTD2_INPUT_AUTO=1 / 2) -------------------------------------------------------
// Once per boot frame, autoplay::Decide (auto_play.cpp) chooses each auto player's gun state from game memory. A
// changed state becomes a script line: it is formatted, parsed back exactly as LoadScript does, appended to g_script
// and written to game\input_auto.txt; ApplyScript then applies it like any script line. So a replay with
// HOTD2_INPUT_SCRIPT=game\input_auto.txt feeds the game the identical input. NOT device input.
FILE* g_autoLog = nullptr;
uint32_t g_autoFrame = 0xFFFFFFFF;
autoplay::Decision g_autoPrev[2];
bool g_autoHave[2] = {};

uint32_t g_autoFrom = 0;  // HOTD2_INPUT_AUTO_FROM: script lines before this boot frame, automatic input from it

void AutoStep() {
    const uint32_t frame = At<uint32_t>(0x009A5C40);
    if (frame == g_autoFrame) return;
    g_autoFrame = frame;
    if (frame < g_autoFrom) return;
    for (int p = 0; p < autoplay::Players(); ++p) {
        const autoplay::Decision d = autoplay::Decide(p, frame, g_autoPrev[p]);
        char line[96];
        sprintf_s(line, "%u %d %.6f %.6f %u\n", frame, p, d.x, d.y, static_cast<unsigned>(d.buttons));
        ScriptLine s{};
        unsigned b = 0;
        if (sscanf_s(line, "%u %d %f %f %u", &s.frame, &s.player, &s.x, &s.y, &b) != 5) continue;
        s.buttons = static_cast<uint8_t>(b);
        const autoplay::Decision applied{ s.x, s.y, s.buttons };
        if (g_autoHave[p] && applied.x == g_autoPrev[p].x && applied.y == g_autoPrev[p].y &&
            applied.buttons == g_autoPrev[p].buttons)
            continue;
        g_autoPrev[p] = applied;
        g_autoHave[p] = true;
        g_script.push_back(s);
        g_scriptActive = true;
        if (g_autoLog) {
            fputs(line, g_autoLog);
            fflush(g_autoLog);  // the process is usually killed
        }
    }
    if (frame % 3600 == 0) autoplay::LogStats();
}

// --- Memory pokes (capture tool, HOTD2_POKE=<frame>:<addr>:<hex bytes>,...) -----------------------------------------
// Test setup for captures that need persistent data the user's save does not have (e.g. training grades so later
// lessons can be selected): the bytes are written once, at the first Input_Poll of that boot frame or later. The port
// gets the same state from its own save file (frontend_test --save-patch). Logged; never written to the save files.
struct Poke {
    uint32_t frame, addr;
    std::vector<uint8_t> bytes;
    bool done = false;
};
std::vector<Poke> g_pokes;

void LoadPokes() {
    char buf[1024] = {};
    if (GetEnvironmentVariableA("HOTD2_POKE", buf, sizeof(buf)) == 0) return;
    for (char* p = buf; *p;) {
        Poke k{};
        char hex[512] = {};
        if (sscanf_s(p, "%u:%x:%511[0-9A-Fa-f]", &k.frame, &k.addr, hex, static_cast<unsigned>(sizeof(hex))) == 3) {
            for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
                unsigned v = 0;
                sscanf_s(hex + i, "%2x", &v);
                k.bytes.push_back(static_cast<uint8_t>(v));
            }
            g_pokes.push_back(k);
        }
        char* c = strchr(p, ',');
        if (!c) break;
        p = c + 1;
    }
    Log("HOTD2_POKE: %zu poke(s) (test setup: game memory, not the save files)", g_pokes.size());
}

void ApplyPokes() {
    const uint32_t frame = At<uint32_t>(0x009A5C40);
    for (Poke& k : g_pokes) {
        if (k.done || frame < k.frame) continue;
        memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(k.addr)), k.bytes.data(), k.bytes.size());
        k.done = true;
        Log("HOTD2_POKE: frame %u: %zu byte(s) at 0x%08X", frame, k.bytes.size(), k.addr);
    }
}

uint32_t g_progressFrame = 0xFFFFFFFF;

void ApplyScript() {
    const uint32_t frame = At<uint32_t>(0x009A5C40);
    if (frame % 600 == 0 && frame != g_progressFrame) {
        g_progressFrame = frame;
        // progress (read only): game mode, stage, scene, P1 state / lives / score
        Log("progress frame %u: mode %d stage %d scene %d P1 state %d lives %d score %d", frame, At<int16_t>(0x009C8E98),
            At<int16_t>(0x009A1A08), At<int16_t>(0x009A2BC0), At<int16_t>(0x009A5C62), At<int16_t>(0x009A5C66),
            At<int>(0x009A5C6C));
    }
    for (auto& p : g_scripted) {
        p.pressed = p.released = 0;
    }
    while (g_scriptPos < g_script.size() && g_script[g_scriptPos].frame <= frame) {
        const ScriptLine& s = g_script[g_scriptPos++];
        input::PlayerState& p = g_scripted[s.player];
        const uint8_t old = p.held;
        p.connected = true;
        p.x = s.x;
        p.y = s.y;
        p.onScreen = s.x >= 0 && s.x <= 1 && s.y >= 0 && s.y <= 1;
        p.held = s.buttons;
        p.pressed |= static_cast<uint8_t>(s.buttons & ~old);
        p.released |= static_cast<uint8_t>(old & ~s.buttons);
    }
    for (int i = 0; i < input::kMaxPlayers; ++i)
        if (g_scripted[i].connected) ApplyPlayer(i, g_scripted[i], true);
}

// 0x0041E530 Input_Poll
void (__cdecl* Orig_Input_Poll)();

void __cdecl Input_Poll() {
    Orig_Input_Poll();

    if (!g_pokes.empty()) ApplyPokes();
    if (autoplay::Players() > 0) {
        std::lock_guard lock(g_lock);
        AutoStep();
    }
    if (g_scriptActive) {
        std::lock_guard lock(g_lock);
        ApplyScript();
    }
    if (!g_rawEnabled) return;
    if (!g_started.load(std::memory_order_relaxed) && At<HWND>(kMainHwnd)) {
        g_started = true;
        CreateThread(nullptr, 0, InputThread, nullptr, 0, nullptr);
    }

    std::lock_guard lock(g_lock);
    for (int i = 0; i < input::kMaxPlayers; ++i) {
        const input::PlayerState& p = g_raw.Player(i);
        if (p.connected) {
            ApplyPlayer(i, p);
        }
    }
    g_raw.BeginFrame();
}

}  // namespace

bool InputHookEnabled() {
    char buf[16] = {};
    if (GetEnvironmentVariableA("HOTD2_ASPECT", buf, sizeof(buf)) > 0 && strcmp(buf, "4:3") == 0) {
        g_aspect = input::Aspect::Pillarbox43;
    }
    buf[0] = 0;
    g_rawEnabled = GetEnvironmentVariableA("HOTD2_RAWINPUT", buf, sizeof(buf)) > 0 && buf[0] == '1';
    LoadPokes();
    char path[MAX_PATH] = {};
    buf[0] = 0;
    if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO", buf, sizeof(buf)) > 0 && (buf[0] == '1' || buf[0] == '2')) {
        autoplay::Init(buf[0] - '0');
        char out[MAX_PATH];
        GetModuleFileNameA(nullptr, out, MAX_PATH);
        if (char* slash = strrchr(out, '\\')) strcpy_s(slash + 1, out + MAX_PATH - (slash + 1), "input_auto.txt");
        fopen_s(&g_autoLog, out, "w");
        if (g_autoLog) {
            fprintf(g_autoLog, "# HOTD2_INPUT_AUTO=%c: automatic input chosen from game memory (auto_play.cpp), replayable with "
                               "HOTD2_INPUT_SCRIPT\n", buf[0]);
            fflush(g_autoLog);
        }
        Log("auto input log: %s", g_autoLog ? out : "FAILED to open");
        // HOTD2_INPUT_AUTO_FROM=<frame> with HOTD2_INPUT_SCRIPT: replay the script's lines before that boot frame, then
        // automatic input. The kept lines are copied to input_auto.txt, so it is a complete script of the run.
        char from[32] = {};
        if (GetEnvironmentVariableA("HOTD2_INPUT_AUTO_FROM", from, sizeof(from)) > 0 &&
            GetEnvironmentVariableA("HOTD2_INPUT_SCRIPT", path, sizeof(path)) > 0) {
            g_autoFrom = static_cast<uint32_t>(strtoul(from, nullptr, 0));
            LoadScript(path);
            size_t keep = 0;
            while (keep < g_script.size() && g_script[keep].frame < g_autoFrom) ++keep;
            g_script.resize(keep);
            g_scriptActive = !g_script.empty();
            for (const ScriptLine& s : g_script) {
                if (g_autoLog) fprintf(g_autoLog, "%u %d %.6f %.6f %u\n", s.frame, s.player, s.x, s.y, static_cast<unsigned>(s.buttons));
            }
            if (g_autoLog) fflush(g_autoLog);
            Log("auto input from boot frame %u; %zu script lines before it (%s)", g_autoFrom, keep, path);
        }
        return true;  // a script given as well is ignored (unless HOTD2_INPUT_AUTO_FROM)
    }
    if (GetEnvironmentVariableA("HOTD2_INPUT_SCRIPT", path, sizeof(path)) > 0) LoadScript(path);
    return g_rawEnabled || g_scriptActive || !g_pokes.empty();
}

void* InputHookReplacement() { return reinterpret_cast<void*>(Input_Poll); }
void** InputHookOriginal() { return reinterpret_cast<void**>(&Orig_Input_Poll); }
