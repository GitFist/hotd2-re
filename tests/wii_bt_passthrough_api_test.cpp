#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "wii_remote_passthrough_api.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
template <typename T>
static T Resolve(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    if (!address) {
        const std::string decorated = std::string("_") + name;
        address = GetProcAddress(module, decorated.c_str());
    }
    return reinterpret_cast<T>(address);
}
int main() {
    std::array<wchar_t, 32768> path{};
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) {
        std::fprintf(stderr, "FAIL: could not locate test executable\n");
        return 1;
    }
    const auto dllPath = std::filesystem::path(std::wstring(path.data(), count)).parent_path() /
                         L"wii_bt_passthrough.dll";
    HMODULE dll = LoadLibraryW(dllPath.c_str());
    if (!dll) {
        std::fprintf(stderr, "FAIL: wii_bt_passthrough.dll load error %lu\n", GetLastError());
        return 1;
    }
    const auto version = Resolve<uint32_t (WII_BT_CALL *)(void)>(dll, "WiiBt_GetApiVersion");
    const auto hasAdapter = Resolve<int (WII_BT_CALL *)(int32_t, int32_t)>(dll, "WiiBt_HasCompatibleAdapter");
    const auto create = Resolve<void* (WII_BT_CALL *)(int32_t, int32_t, const wchar_t*)>(
        dll, "WiiBt_CreateWithStorage");
    const auto destroy = Resolve<void (WII_BT_CALL *)(void*)>(dll, "WiiBt_Destroy");
    const auto enable = Resolve<int (WII_BT_CALL *)(void*, int, int)>(dll, "WiiBt_Enable");
    const auto scan = Resolve<int (WII_BT_CALL *)(void*)>(dll, "WiiBt_Scan");
    const auto disconnect = Resolve<int (WII_BT_CALL *)(void*, int)>(dll, "WiiBt_Disconnect");
    const auto swap = Resolve<int (WII_BT_CALL *)(void*)>(dll, "WiiBt_SwapPlayers");
    const auto poll = Resolve<int (WII_BT_CALL *)(void*, WiiBtSnapshot*)>(dll, "WiiBt_Poll");
    if (!version || !hasAdapter || !create || !destroy || !enable || !scan ||
        !disconnect || !swap || !poll || version() != WII_BT_API_VERSION) {
        std::fprintf(stderr, "FAIL: missing export or incompatible API version\n");
        FreeLibrary(dll);
        return 1;
    }
    if (hasAdapter(-1, 1) != 0 || create(-1, 1) != nullptr) {
        std::fprintf(stderr, "FAIL: incomplete VID/PID pair was not rejected\n");
        FreeLibrary(dll);
        return 1;
    }
    void* instance = create(-1, -1, nullptr);
    if (!instance || !enable(instance, 0, 0)) {
        std::fprintf(stderr, "FAIL: backend lifecycle initialization\n");
        if (instance) destroy(instance);
        FreeLibrary(dll);
        return 1;
    }
    WiiBtSnapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    const bool polled = poll(instance, &snapshot) != 0 && snapshot.struct_size == sizeof(snapshot);
    destroy(instance);
    FreeLibrary(dll);
    if (!polled) {
        std::fprintf(stderr, "FAIL: API snapshot contract\n");
        return 1;
    }
    std::puts("PASS optional Wii Bluetooth DLL exports, version, lifecycle and snapshot ABI");
    return 0;
}
