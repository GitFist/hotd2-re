#pragma once

// GunSource for touch screens (phones / tablets), platform-free; the SDL platform layer feeds it SDL finger events
// (positions normalized over the window). One player (P1). Gestures:
//   tap on the game image      -> trigger at the touch point (held while the finger stays down; the aim follows it);
//                                 the first tap joins P1 (play.exe turns a trigger of a waiting player into start)
//   tap outside the game image -> reload (= the arcade's off-screen shot): the pillarbox bars of a wide phone screen
//                                 around the 4:3 image (config.aspect = Pillarbox43)
//   second finger while one is down, or a long press (finger held still for config.longPressSeconds) -> reload
//   three fingers              -> start (pause / join)
// Desktop testing: SDL_HINT_MOUSE_TOUCH_EVENTS=1 turns left-button drags into finger events (play.exe --input touch):
// click = shot, click-and-hold = reload. Unit test: tests/touch_guns_test.cpp.

#include <cstdint>
#include <vector>

#include "aim_map.h"
#include "player_state.h"

namespace input {

struct TouchConfig {
    double longPressSeconds = 0.5;
    float moveTolerance = 0.03f;      // a finger that moved farther (normalized window units) is not a long press
    Aspect aspect = Aspect::Stretch;  // how the game image sits in the window
    float displayW = 4, displayH = 3; // window size (its aspect ratio is used with Pillarbox43)
};

class TouchGuns final : public GunSource {
public:
    TouchConfig config;

    void OnFingerDown(uint64_t finger, float nx, float ny, double t);
    void OnFingerMove(uint64_t finger, float nx, float ny, double t);
    void OnFingerUp(uint64_t finger, double t);
    void Update(double t);       // long-press detection; call once per event pump
    void OnStartKey(bool down);  // keyboard start (desktop testing)

    const char* Name() const override { return "touch"; }
    PlayerState Player(int index) const override { return index == 0 ? p1_ : PlayerState{}; }
    void BeginFrame() override { p1_.pressed = p1_.released = 0; }

private:
    struct Finger {
        uint64_t id;
        float x0, y0;
        double t0;
        bool moved = false, longFired = false;
    };
    void Press(uint8_t bit);
    void Aim(float nx, float ny);
    std::vector<Finger> fingers_;
    bool havePrimary_ = false;
    uint64_t primary_ = 0;
    PlayerState p1_;
};

}  // namespace input
