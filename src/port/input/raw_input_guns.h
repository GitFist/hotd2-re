#pragma once

// GunSource over Windows raw input (raw_input.h): one physical mouse / light gun per player, joined by its first
// trigger pull, null-handle (synthetic) reports ignored. The default gun source on Windows until two-device play is
// physically tested. A regular (relative) mouse aims at the Windows pointer, like the original's mouse mode
// (Input_GetMouseClientPos 0x0041EA80); only the first such player owns the pointer.
// Feed it the window's WM_INPUT / WM_INPUT_DEVICE_CHANGE messages (platform layer: SDL_SetWindowsMessageHook).

#include <windows.h>

#include "player_state.h"
#include "raw_input.h"

namespace input {

class RawInputGuns final : public GunSource {
public:
    bool Init(HWND window) {
        hwnd_ = window;
        return raw_.Init(window);
    }
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp) { return raw_.HandleMessage(msg, wp, lp); }

    const char* Name() const override { return "raw input"; }
    PlayerState Player(int index) const override;
    void BeginFrame() override { raw_.BeginFrame(); }
    RawInput& Raw() { return raw_; }

private:
    RawInput raw_;
    HWND hwnd_ = nullptr;
};

}  // namespace input
