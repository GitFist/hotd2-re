#include "wii_remote.h"
#include "wii_remote_passthrough_api.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
namespace input::wii {
namespace {
class WindowsNoticeBackend final : public Backend {
public:
    explicit WindowsNoticeBackend(std::string notice)
        : native_(CreateWindowsBackend()), notice_(std::move(notice)) {}
    void Enable(bool enabled, bool continuous) override { native_->Enable(enabled, continuous); }
    void Scan() override { native_->Scan(); }
    void Disconnect(int player) override { native_->Disconnect(player); }
    void SwapPlayers() override { native_->SwapPlayers(); }
    Snapshot Poll() override {
        Snapshot result = native_->Poll();
        if (!notice_.empty())
            result.message = notice_ + (result.message.empty() ? "" : " | " + result.message);
        return result;
    }
private:
    std::unique_ptr<Backend> native_;
    std::string notice_;
};
class PassthroughDllBackend final : public Backend {
public:
    using GetVersionFn = uint32_t (WII_BT_CALL *)(void);
    using HasAdapterFn = int (WII_BT_CALL *)(int32_t, int32_t);
    using CreateWithStorageFn = void* (WII_BT_CALL *)(int32_t, int32_t, const wchar_t*);
    using DestroyFn = void (WII_BT_CALL *)(void*);
    using EnableFn = int (WII_BT_CALL *)(void*, int, int);
    using ScanFn = int (WII_BT_CALL *)(void*);
    using DisconnectFn = int (WII_BT_CALL *)(void*, int);
    using SwapFn = int (WII_BT_CALL *)(void*);
    using PollFn = int (WII_BT_CALL *)(void*, WiiBtSnapshot*);
    ~PassthroughDllBackend() override {
        if (instance_) {
            if (enable_) (void)enable_(instance_, 0, 0);
            if (destroy_) destroy_(instance_);
        }
        if (module_) FreeLibrary(module_);
    }
    bool Open(std::string& error, int usbVid, int usbPid) {
        std::array<wchar_t, 32768> path{};
        const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!count || count >= path.size()) {
            error = "Could not locate the game executable directory";
            return false;
        }
        const auto dllPath = std::filesystem::path(std::wstring(path.data(), count)).parent_path() /
                             L"wii_bt_passthrough.dll";
        module_ = LoadLibraryW(dllPath.c_str());
        if (!module_) {
            error = "Could not load wii_bt_passthrough.dll or one of its runtime dependencies; using Windows Bluetooth HID";
            return false;
        }
        getVersion_ = Resolve<GetVersionFn>("WiiBt_GetApiVersion");
        hasAdapter_ = Resolve<HasAdapterFn>("WiiBt_HasCompatibleAdapter");
        createWithStorage_ = Resolve<CreateWithStorageFn>("WiiBt_CreateWithStorage");
        destroy_ = Resolve<DestroyFn>("WiiBt_Destroy");
        enable_ = Resolve<EnableFn>("WiiBt_Enable");
        scan_ = Resolve<ScanFn>("WiiBt_Scan");
        disconnect_ = Resolve<DisconnectFn>("WiiBt_Disconnect");
        swap_ = Resolve<SwapFn>("WiiBt_SwapPlayers");
        poll_ = Resolve<PollFn>("WiiBt_Poll");
        if (!getVersion_ || !hasAdapter_ || !createWithStorage_ || !destroy_ || !enable_ ||
            !scan_ || !disconnect_ || !swap_ || !poll_ ||
            getVersion_() != WII_BT_API_VERSION) {
            error = "wii_bt_passthrough.dll has an incompatible API; using Windows Bluetooth HID";
            return false;
        }
        if (!hasAdapter_(usbVid, usbPid)) {
            error = "No claimable USB Bluetooth HCI adapter; using Windows Bluetooth HID";
            return false;
        }
        std::array<wchar_t, 32768> localAppData{};
        const DWORD appDataLength = GetEnvironmentVariableW(
            L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
        std::wstring storageDirectory;
        if (appDataLength > 0 && appDataLength < localAppData.size()) {
            storageDirectory = (std::filesystem::path(
                std::wstring(localAppData.data(), appDataLength)) / L"HOTD2RE").wstring();
        }
        instance_ = createWithStorage_(usbVid, usbPid,
            storageDirectory.empty() ? nullptr : storageDirectory.c_str());
        if (!instance_) {
            error = "Could not create USB Bluetooth passthrough runtime; using Windows Bluetooth HID";
            return false;
        }
        return true;
    }
    void Enable(bool enabled, bool continuous) override {
        if (instance_) (void)enable_(instance_, enabled ? 1 : 0, continuous ? 1 : 0);
    }
    void Scan() override { if (instance_) (void)scan_(instance_); }
    void Disconnect(int player) override { if (instance_) (void)disconnect_(instance_, player); }
    void SwapPlayers() override { if (instance_) (void)swap_(instance_); }
    Snapshot Poll() override {
        Snapshot result;
        if (!instance_) {
            result.message = "USB passthrough runtime is not initialized";
            return result;
        }
        WiiBtSnapshot wire{};
        wire.struct_size = sizeof(wire);
        if (!poll_(instance_, &wire)) {
            result.message = "USB passthrough DLL returned an invalid snapshot";
            return result;
        }
        result.scanning = wire.scanning != 0;
        for (size_t i = 0; i < result.player.size(); ++i) {
            const WiiBtPlayerState& in = wire.player[i];
            Remote& out = result.player[i];
            out.connected = in.connected != 0;
            out.reserved = in.reserved != 0;
            out.tracking = in.tracking != 0;
            out.dots = in.dots;
            out.buttons = in.buttons;
            out.pressed = in.pressed;
            out.battery = in.battery;
            out.raw = {in.raw_x, in.raw_y};
        }
        const auto end = std::find(wire.message, wire.message + sizeof(wire.message), '\0');
        result.message.assign(wire.message, end);
        return result;
    }
private:
    template <typename T>
    T Resolve(const char* name) const {
        if (!module_) return nullptr;
        FARPROC address = GetProcAddress(module_, name);
        // MSVC x86 may retain the leading underscore of a __cdecl C export.
        if (!address) {
            const std::string decorated = std::string("_") + name;
            address = GetProcAddress(module_, decorated.c_str());
        }
        return reinterpret_cast<T>(address);
    }
    HMODULE module_ = nullptr;
    void* instance_ = nullptr;
    GetVersionFn getVersion_ = nullptr;
    HasAdapterFn hasAdapter_ = nullptr;
    CreateWithStorageFn createWithStorage_ = nullptr;
    DestroyFn destroy_ = nullptr;
    EnableFn enable_ = nullptr;
    ScanFn scan_ = nullptr;
    DisconnectFn disconnect_ = nullptr;
    SwapFn swap_ = nullptr;
    PollFn poll_ = nullptr;
};
} // namespace
std::unique_ptr<Backend> CreatePassthroughBackend(int usbVid, int usbPid) {
    auto backend = std::make_unique<PassthroughDllBackend>();
    std::string error;
    if (backend->Open(error, usbVid, usbPid))
        return backend;
    return std::make_unique<WindowsNoticeBackend>(std::move(error));
}
} // namespace input::wii
#endif
