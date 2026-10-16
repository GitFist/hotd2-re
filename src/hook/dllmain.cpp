#include <windows.h>

#include <detours.h>

#include "hooks.h"
#include "log.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (DetourIsHelperProcess()) {
        return TRUE;
    }
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        DetourRestoreAfterWith();
        Log("hotd2_hook loaded");
        if (!InstallHooks()) {
            Log("hook install failed; running original code only");
        }
        break;
    case DLL_PROCESS_DETACH:
        RemoveHooks();
        break;
    }
    return TRUE;
}
