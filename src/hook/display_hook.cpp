// Debug: run the original renderer in a window (opt-in: HOTD2_WINDOWED=1).
//
// Game_PreInit calls HGL_SetDisplayModeByIndex(11): 640x480, 16 bpp, fullscreen flag 1 (mode table
// at 0x005988F0, 16 bytes/entry: width, height, bpp, fullscreen). The table's entries 0-7 are the
// same modes with fullscreen 0, and the game has a windowed present path (Game_Frame Blts to the
// client area when 0x007DE63C == 0). This remaps a fullscreen index to its windowed twin.
// The [HGLCORE] FullScreen ini key is parsed (flag 0x4) but never read, so there is no ini switch.

#include "display_hook.h"

#include <windows.h>

#include "log.h"

namespace {

// 0x004A6200 HGL_SetDisplayModeByIndex
int (__cdecl* Orig_SetDisplayModeByIndex)(int index);

int __cdecl SetDisplayModeByIndex(int index) {
    int use = index;
    if (index >= 8 && index <= 13) {
        use = index - 8;  // 8..13 are fullscreen twins of 0..5
    }
    Log("HGL_SetDisplayModeByIndex(%d) -> %d (windowed debug)", index, use);
    return Orig_SetDisplayModeByIndex(use);
}

// Debug: keep running in the background (HOTD2_KEEPACTIVE=1). HGL's window procedure clears the active flag
// 0x007DEEB4 on WM_ACTIVATEAPP(FALSE) (0x004A66EE), and HGL_MessageLoop then idles instead of running frames.
// We subclass the main window (0x007DE62C) and drop deactivation notifications, so capture and check runs
// work without taking focus from the user's desktop.
WNDPROC g_origProc = nullptr;

LRESULT CALLBACK KeepActiveProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ACTIVATEAPP && !wp) return 0;
    if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) return 0;
    return CallWindowProcA(g_origProc, hwnd, msg, wp, lp);
}

DWORD WINAPI KeepActiveThread(LPVOID) {
    HWND hwnd = nullptr;
    while (!(hwnd = *reinterpret_cast<HWND*>(0x007DE62C))) Sleep(50);
    g_origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(KeepActiveProc)));
    *reinterpret_cast<volatile int*>(0x007DEEB4) = 1;
    PostMessageA(hwnd, WM_NULL, 0, 0);  // wake HGL_MessageLoop from WaitMessage
    Log("keep-active: main window %p subclassed", hwnd);
    return 0;
}

}  // namespace

void StartKeepActive() {
    char buf[8] = {};
    if (GetEnvironmentVariableA("HOTD2_KEEPACTIVE", buf, sizeof(buf)) > 0 && buf[0] == '1')
        CreateThread(nullptr, 0, KeepActiveThread, nullptr, 0, nullptr);
}

bool DisplayHookEnabled() {
    char buf[8] = {};
    return GetEnvironmentVariableA("HOTD2_WINDOWED", buf, sizeof(buf)) > 0 && buf[0] == '1';
}

void* DisplayHookReplacement() { return reinterpret_cast<void*>(SetDisplayModeByIndex); }
void** DisplayHookOriginal() { return reinterpret_cast<void**>(&Orig_SetDisplayModeByIndex); }
