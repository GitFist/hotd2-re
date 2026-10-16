#pragma once

// GunSource over pointer (mouse) events, platform-free; the SDL platform layer feeds it SDL mouse events.
// Left = trigger, right = reload, middle = start (as raw input). A device joins on its first trigger press.
//
// SDL3 gives every mouse its own id only where it reads raw device input (Windows: relative mouse mode only, which
// also takes over the process's raw-input registration; X11 via XInput2); motion positions are always the one system
// cursor. So by default all devices drive P1 at the system pointer (multiDevice = false). With multiDevice each device
// id joins as its own player and aims at the positions of its own motion events - only useful where a platform
// reports per-device absolute positions (see notes/agents/platform.md, multi-mouse findings).

#include <cstdint>
#include <functional>
#include <map>

#include "player_state.h"

namespace input {

class PointerGuns final : public GunSource {
public:
    bool multiDevice = false;
    // Optional: the system pointer over the window, normalized (may lie outside 0..1). Queried by Player() for players
    // driven by the system pointer, so a pointer outside the window aims off-screen (as the raw-input mouse mode).
    std::function<bool(float* nx, float* ny)> pointerQuery;

    void OnMotion(uint32_t device, float nx, float ny);
    void OnButton(uint32_t device, int button, bool down, float nx, float ny);  // button: 1 left, 2 middle, 3 right
    void OnStartKey(bool down);  // keyboard start (Enter) for P1, joins P1

    const char* Name() const override { return "mouse"; }
    PlayerState Player(int index) const override;
    void BeginFrame() override;

private:
    int PlayerOf(uint32_t device, bool join);
    std::map<uint32_t, int> devicePlayer_;
    PlayerState players_[kMaxPlayers];
};

}  // namespace input
