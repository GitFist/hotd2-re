#pragma once

// Per-player gun / pointer state shared by every input source (platform-free): Windows raw input (raw_input.h),
// SDL mouse (pointer_guns.h), touch (touch_guns.h).

#include <cstdint>

namespace input {

constexpr int kMaxPlayers = 2;

enum Button : uint8_t {
    kTrigger = 1 << 0,  // left button
    kReload  = 1 << 1,  // right button (HOTD2: shoot off-screen also reloads)
    kStart   = 1 << 2,  // middle button
};

struct PlayerState {
    bool connected = false;
    float x = 0.5f;            // aim, normalized 0..1 across the target window's client area
    float y = 0.5f;
    bool onScreen = true;      // false when an absolute device points outside the window
    bool relative = false;     // the device reports relative motion (a regular mouse)
    uint8_t held = 0;          // Button bits currently down
    uint8_t pressed = 0;       // went down since last BeginFrame()
    uint8_t released = 0;      // went up since last BeginFrame()
};

// A source of per-player gun input for the port's game loop. Player() is the state for the coming game frame (aim
// normalized over the game image; a relative mouse already resolved to the pointer where the source does that);
// BeginFrame() clears the edge flags once per game frame.
class GunSource {
public:
    virtual ~GunSource() = default;
    virtual const char* Name() const = 0;
    virtual PlayerState Player(int index) const = 0;
    virtual void BeginFrame() = 0;
};

}  // namespace input
