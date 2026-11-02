#pragma once

#include "input/player_state.h"
#include "input/gamepad_settings.h"
#include "play/display_menu.h"
#include "play/gun_input.h"

namespace play {

// A stick-controlled sight is useful in play, but is not a mouse hovering over
// every menu. Front-end UI uses the same navigation path as the keyboard.
inline MenuInput GamepadMenuInput(const input::PlayerState& pad, bool resumeWithStart = false) {
    MenuInput out;
    out.pressed = pad.menuPressed & 0xFF;
    if (pad.pressed & input::kTrigger) out.pressed |= 8;
    const uint32_t categories = pad.menuPressed & (input::gamepad::kMenuPreviousCategory | input::gamepad::kMenuNextCategory);
    out.pageDelta = ((categories & input::gamepad::kMenuNextCategory) != 0) -
                    ((categories & input::gamepad::kMenuPreviousCategory) != 0);
    // A bumper rebound to Fire must change category without selecting a row.
    if (categories) out.pressed &= ~0xCu;
    out.start = resumeWithStart && (pad.menuPressed & input::gamepad::kMenuStart) != 0;
    if (out.start) out.pressed &= ~8u;
    return out;
}

inline game::WorldInput GamepadWorldInput(const input::PlayerState& pad, int playerState,
                                         bool gameplay, uint8_t& previousButtons) {
    if (!gameplay) {
        previousButtons = 0;
        game::WorldInput out;
        out.held = pad.menuHeld & 0xFF;
        out.pressed = GamepadMenuInput(pad).pressed;
        if (pad.held & input::kTrigger) out.held |= 8;
        return out;
    }
    DevicePlayer device;
    device.connected = pad.connected;
    device.x = pad.x;
    device.y = pad.y;
    device.onScreen = pad.onScreen;
    device.held = pad.held;
    device.pressed = pad.pressed;
    // Fixed menu actions must not leak into remapped gameplay actions (e.g.
    // B/Circle assigned to fire must not also reload because it is menu Back).
    auto out = DeviceWorldInput(device, playerState, previousButtons);
    out.held |= pad.menuHeld & 0xF0;
    out.pressed |= pad.menuPressed & 0xF0;
    return out;
}

} // namespace play
