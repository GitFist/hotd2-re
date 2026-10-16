#pragma once

// Multi-device pointer input via Windows Raw Input.
//
// Each physical mouse or light gun (Sinden, GUN4IR, Wiimote-as-mouse, ...) is tracked
// separately, so two players can aim independently. Light guns report absolute
// positions; regular mice report relative motion, which is accumulated and clamped.
// Players are assigned in the order devices first pull the trigger.
//
// Reports without a device handle (synthetic input from SendInput, automation, some
// touchpad gestures) are always ignored, so injected clicks cannot join or fire.

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "aim_map.h"
#include "player_state.h"  // PlayerState, Button, kMaxPlayers

namespace input {

// Screen geometry used to map absolute reports; all rects in screen pixels.
struct Geometry {
    Rect client;       // target window client area
    Rect primary;      // primary monitor
    Rect virtualDesk;  // whole virtual desktop
};

class RawInput {
public:
    // Registers for mouse raw input on `window` (receives input even when unfocused).
    // Positions are normalized to `coordWindow`'s client area (defaults to `window`).
    bool Init(HWND window, HWND coordWindow = nullptr);
    void SetCoordinateWindow(HWND coordWindow) { coordWindow_ = coordWindow; }

    // Feed every WM_INPUT / WM_INPUT_DEVICE_CHANGE message here. Returns true if handled.
    bool HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Applies one mouse report from device `handle`. Null handles are rejected (returns false).
    bool ProcessMouse(HANDLE handle, const RAWMOUSE& m);

    // Forgets a device (unplugged).
    void RemoveDevice(HANDLE handle);

    // Clears per-frame edge flags. Call once at the start of each game frame.
    void BeginFrame();

    const PlayerState& Player(int index) const { return players_[index]; }

    // Unassigns all devices so the next trigger pulls pick P1/P2 again.
    void ResetAssignments();

    // Mouse sensitivity for relative devices, in window-widths per 1000 counts.
    float relativeSensitivity = 0.6f;

    // Geometry source; defaults to querying Win32 for coordWindow. Tests replace it.
    std::function<Geometry()> geometryProvider;

private:
    struct Device {
        int player = -1;
        float relX = 0.5f, relY = 0.5f;  // accumulated position for relative mice
    };

    Geometry QueryGeometry() const;
    int AssignPlayer(Device& dev);

    HWND window_ = nullptr;
    HWND coordWindow_ = nullptr;
    std::unordered_map<HANDLE, Device> devices_;
    std::array<PlayerState, kMaxPlayers> players_{};
};

}  // namespace input
