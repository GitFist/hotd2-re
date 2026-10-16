// gun_input_test: unit checks of the port's gun input glue (src/port/play/gun_input.*) against the original's rules
// (shot input -> mode-6 calibration -> Player_Trigger). No devices involved; physical testing remains pending.

#include <cstdio>

#include "port/play/gun_input.h"

static int failures = 0;
#define CHECK(c)                                                            \
    do {                                                                    \
        if (!(c)) {                                                         \
            ++failures;                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);              \
        }                                                                   \
    } while (0)

int main() {
    using namespace play;
    // Centre of the image -> raw (320, 240) -> centred (0, 0), valid.
    DeviceSample s;
    s.connected = true;
    s.nx = 0.5f;
    s.ny = 0.5f;
    s.triggerPressed = true;
    ShotInput in = MakeShotInput(s);
    CHECK(in.x == 320 && in.y == 240 && in.fired);
    game::GunRecord r = MakeGunRecord(in);
    CHECK(r.x == 0 && r.y == 0 && r.valid == 1 && r.onScreen == 1 && r.fired == 1);
    // Near the top-left corner (raw (0, 4)): x = -320 -> about -319, y up positive, valid.
    s.nx = 0;
    s.ny = 4.0f / 480;
    r = MakeGunRecord(MakeShotInput(s));
    CHECK(r.valid == 1 && r.x < -300 && r.y > 200);
    // Mode-6 calibration maps raw y 0 to ~+242.5: the top rows are outside [-240, 239] -> invalid (original behaviour).
    s.ny = 0;
    r = MakeGunRecord(MakeShotInput(s));
    CHECK(r.valid == 0);
    // Off screen and reload -> 0xFFFF and invalid.
    s.onScreen = false;
    in = MakeShotInput(s);
    CHECK(in.x == 0xFFFF && in.y == 0xFFFF && in.fired);
    r = MakeGunRecord(in);
    CHECK(r.valid == 0 && r.onScreen == 0);
    // Trigger rules: shot consumes ammo; off-screen shot reloads when not full; letterbox closed -> no shot.
    PlayerGun gun;
    game::GunRecord on = MakeGunRecord(MakeShotInput(DeviceSample{ true, 0.5f, 0.5f, true, true, false }));
    CHECK(UpdatePlayerGun(on, true, gun) == game::TriggerAction::Shot && gun.ammo == 5);
    CHECK(UpdatePlayerGun(on, false, gun) == game::TriggerAction::None && gun.ammo == 5);
    CHECK(UpdatePlayerGun(r, true, gun) == game::TriggerAction::Reload && gun.ammo == 6);
    CHECK(UpdatePlayerGun(r, true, gun) == game::TriggerAction::None && gun.ammo == 6);  // full: no reload
    for (int i = 0; i < 6; ++i) UpdatePlayerGun(on, true, gun);
    CHECK(gun.ammo == 0 && gun.empty);
    CHECK(UpdatePlayerGun(on, true, gun) == game::TriggerAction::None);  // empty: no shot
    // Not connected -> nothing fires.
    CHECK(!MakeShotInput(DeviceSample{}).fired);
    printf("%s (%d failures)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
