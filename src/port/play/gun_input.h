#pragma once

// Native player gun input for the port: device state (src/port/input, one per player) -> the game's per-player gun
// record and trigger decision, mirroring what the original does with the shot input the hook writes:
//   Input_LatchShots 0x0041EB30 / Game_UpdateShots 0x0040CE70: aim in 640x480 game pixels, 0xFFFF,0xFFFF = off
//   screen (trigger while aimed off the image, or the reload button, which the hook maps to an off-screen shot);
//   Gun_UpdateRecords 0x0040CBB0 with device mode 6 (the Hod2.ini default for P1, kept by the hook): fixed
//   calibration (GunCalibrateMode6) -> centred aim + valid flag; fired = trigger went down this frame;
//   Player_Trigger 0x00414940: shot / reload / nothing, ammo bookkeeping (game::PlayerTrigger).
// Pure functions: unit-tested in tests/gun_input_test.cpp. Physical two-device behaviour is NOT verified here.

#include <cstdint>

#include "game/shooting.h"
#include "game/world.h"

namespace play {

// play.exe's per-device conversion for game::World (src/port/tools/play.cpp): one gun source player (src/port/input
// PlayerState: aim normalized over the game image, buttons 1 trigger / 2 reload / 4 start held or pressed since the
// last game frame, so a click shorter than a frame still counts) -> the world's input for the next game frame.
//   * a player who is not in the game (world player state 9: title, demos, mode select, idle in a game) joins with the
//     trigger as well as with start (the original's mode select takes either; elsewhere start only);
//   * an aim outside the game image is the hook's off-screen position (-1, -1).
// prevButtons: the buttons this function returned for the player on the previous game frame (updated).
struct DevicePlayer {
    bool connected = false;
    float x = 0.5f, y = 0.5f;
    bool onScreen = true;
    uint8_t held = 0, pressed = 0;  // input::Button bits (kTrigger 1, kReload 2, kStart 4)
};
game::WorldInput DeviceWorldInput(const DevicePlayer& d, int worldPlayerState, uint8_t& prevButtons);

struct DeviceSample {
    bool connected = false;
    float nx = 0.5f, ny = 0.5f;  // aim normalized over the game image (0..1, origin top-left)
    bool onScreen = true;        // false: the device points outside the game image
    bool triggerPressed = false; // trigger went down this frame
    bool reloadPressed = false;  // reload button went down this frame
};

// Device sample -> raw shot input (game pixels, 0xFFFF = off screen) and the fired flag.
struct ShotInput {
    uint16_t x = 0xFFFF, y = 0xFFFF;
    bool fired = false;
};
ShotInput MakeShotInput(const DeviceSample& s);

// Gun_UpdateRecords for mode 6: raw shot input -> gun record (raw, centred aim, valid, onScreen, fired).
game::GunRecord MakeGunRecord(const ShotInput& in);

// Per-player shooting state kept by the port between frames (player +0x1C ammo / +0x1E empty).
struct PlayerGun {
    int16_t ammo = 6;
    bool empty = false;
};

// One frame of trigger handling (0x00414940) for a live player. canShoot = 0x009C8E00 (letterbox open).
// Returns the action; on Shot the caller builds the ray from rec.x / rec.y (game::MakeShotRay).
game::TriggerAction UpdatePlayerGun(const game::GunRecord& rec, bool canShoot, PlayerGun& gun, bool infiniteAmmo = false);

}  // namespace play
