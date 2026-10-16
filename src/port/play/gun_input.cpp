#include "gun_input.h"

#include <algorithm>
#include <cmath>

namespace play {

game::WorldInput DeviceWorldInput(const DevicePlayer& d, int worldPlayerState, uint8_t& prevButtons) {
    const uint8_t bits = static_cast<uint8_t>(d.held | d.pressed);
    uint8_t b = static_cast<uint8_t>((bits & 1 ? 1 : 0) | (bits & 2 ? 2 : 0) | (bits & 4 ? 4 : 0));
    if (worldPlayerState == 9 && (b & 1)) b |= 4;  // not in the game: the trigger also presses start
    float nx = d.x, ny = d.y;
    if (!d.onScreen) nx = ny = -1.0f;
    const game::WorldInput in = game::MakeWorldInput(nx, ny, b, prevButtons);
    prevButtons = b;
    return in;
}

ShotInput MakeShotInput(const DeviceSample& s) {
    ShotInput in;
    if (!s.connected) return in;
    // Reload button = shot aimed off screen (what the hook writes for the original, Input_LatchShots 0x0041EB30).
    const bool offScreen = !s.onScreen || s.reloadPressed;
    in.fired = s.triggerPressed || s.reloadPressed;
    if (offScreen) return in;  // 0xFFFF, 0xFFFF
    // Game pixels 640x480, clamped (src/port/input/aim_map NormalizedToGame).
    const int x = std::clamp(static_cast<int>(std::floor(s.nx * 640.0f)), 0, 639);
    const int y = std::clamp(static_cast<int>(std::floor(s.ny * 480.0f)), 0, 479);
    in.x = static_cast<uint16_t>(x);
    in.y = static_cast<uint16_t>(y);
    return in;
}

// Gun_UpdateRecords 0x0040CBB0, device mode 6 (fixed calibration 0x0040CD37).
game::GunRecord MakeGunRecord(const ShotInput& in) {
    game::GunRecord r;
    r.rawX = static_cast<int16_t>(in.x);
    r.rawY = static_cast<int16_t>(in.y);
    r.onScreen = !(in.x == 0xFFFF && in.y == 0xFFFF);
    bool valid = false;
    game::GunCalibrateMode6(in.x, in.y, &r.x, &r.y, &valid);
    r.valid = valid ? 1 : 0;
    r.fired = in.fired ? 1 : 0;
    return r;
}

// Player_Trigger 0x00414940 via game::PlayerTrigger (gun device: off-screen shot reloads).
game::TriggerAction UpdatePlayerGun(const game::GunRecord& rec, bool canShoot, PlayerGun& gun, bool infiniteAmmo) {
    game::TriggerInput t{};
    t.fired = rec.fired != 0;
    t.valid = rec.valid != 0;
    t.gunDevice = true;
    t.padReload = false;
    t.canShoot = canShoot;
    t.demo = false;
    t.infiniteAmmo = infiniteAmmo;
    const game::TriggerResult r = game::PlayerTrigger(t, gun.ammo);
    gun.ammo = r.ammo;
    if (r.action == game::TriggerAction::Shot) gun.empty = r.empty;
    if (r.action == game::TriggerAction::Reload) gun.empty = false;
    return r.action;
}

}  // namespace play
