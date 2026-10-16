#pragma once

// Player shooting: attract-demo auto-gun, trigger handling, shot ray, sphere hit test, hit sort, gun calibration.
// Spec and evidence: notes/agents/shooting.md. All screen coordinates here are the game's centred gun space
// (x right, y up; 640x480 screen -> x in [-320, 319], y in [-240, 239]).

#include <cstdint>
#include <utility>
#include <vector>

#include "rng.h"  // CrtRand: MSVC CRT rand() 0x004ADED2 (called through the thunk 0x004ABE60)

namespace game {

// Per-player gun record 0x009C8FC0 + p*0x28 (written by Gun_UpdateRecords 0x0040CBB0, read by the shot code).
constexpr uint32_t kGunRecordBase = 0x009C8FC0;
constexpr uint32_t kGunRecordStride = 0x28;
struct GunRecord {
    int16_t rawX = 0, rawY = 0;  // +0x00/+0x02 screen pixels from the shot input (0xFFFF = off screen)
    int16_t x = 0, y = 0;        // +0x04/+0x06 centred aim (calibrated)
    float fx = 0, fy = 0;        // +0x08/+0x0C pad cursor (float centred)
    int16_t valid = 0;           // +0x10 aim inside the calibrated area (shots need it)
    int16_t onScreen = 0;        // +0x12 raw aim != 0xFFFF,0xFFFF
    int16_t fired = 0;           // +0x14 trigger pressed this frame
};

// Demo auto-gun, 0x00415710 (branch taken when 0x009C71C4 == 1, i.e. game state 0x009C8E98 == 5, the attract demo).
// One roll per player per frame, P1 then P2 (the player tasks are the first rand consumers of a frame):
//   r = rand() % 60; fire = r < 1 (no live enemy, 0x009C7006 == 0) or r < 6 (live enemies)
//   fire:    x = (rand() % 150) * 2 - 150 (-150..148), y = (rand() % 100) * 2 - 100 (-100..98)
//   no fire: x = y = low 16 bits of the record's address (the code stores its own pointer argument)
// The record also gets fired = fire and valid = 1.
struct DemoShot {
    bool fired;
    int16_t x, y;
};
DemoShot DemoAutoGun(CrtRand& rng, int player, bool enemiesAlive);  // 0x00415710
void ApplyDemoShot(const DemoShot& s, GunRecord& rec);               // 0x00415785..0x00415792

// Trigger handling of the normal weapon, Player_Trigger 0x00414940 (the special-weapon twin 0x00414B90 adds
// per-weapon cooldowns and a rand() spread). Pure decision part.
struct TriggerInput {
    bool fired;            // gun record +0x14
    bool valid;            // gun record +0x10
    bool gunDevice;        // player +0x125 (0x009A5D85) == 1
    bool padReload;        // reload button (0x009C9028 & mask) and player +0x125 == 0
    bool canShoot;         // 0x009C8E00 (letterbox state 0x009CA0F4 open)
    bool demo;             // state 0x009C8E98 == 5: ammo is not consumed
    bool infiniteAmmo;     // 0x009C9FD9 + p*0x7C
    int16_t fullAmmo = 6;  // 6 (special weapon mode 0x009CA08C == 1 uses 0x009A2248 + p*0x14)
};
enum class TriggerAction { None, Shot, Reload };
struct TriggerResult {
    TriggerAction action = TriggerAction::None;
    int16_t ammo = 0;   // player +0x1C after the call
    bool empty = false; // player +0x1E set (ammo reached 0)
};
TriggerResult PlayerTrigger(const TriggerInput& in, int16_t ammo);  // 0x00414940

// Focal length 0x009A2D70 (Render_BeginWorld 0x004184C0): 240 / tan(halfFov), halfFov = double at 0x004ED1D0.
constexpr uint32_t kHalfFovVa = 0x004ED1D0;
float ShotFocalLength(double halfFov);

// Shot ray in view space, Shot_BuildRay 0x00406110 (block 0x009A2CB8 + p*0x68): direction = normalize(x, y, -F),
// pitch/yaw = Cam_DirToAngles(direction); the block keeps -pitch/-yaw and their sin/cos.
struct ShotRay {
    float dir[3];  // +0x3C view-space unit direction
    int pitch;     // Cam_DirToAngles outputs (block +0x18 = -pitch, +0x1C = -yaw)
    int yaw;
    float sinP, cosP, sinY, cosY;  // +0x20..+0x2C: sin/cos of -pitch and -yaw
};
ShotRay MakeShotRay(float x, float y, float focal);

// Shot_RayDistance 0x004062A0: distance of a view-space point from the shot line through the eye.
float ShotRayDistance(const ShotRay& r, float x, float y, float z);
inline bool ShotHitsSphere(const ShotRay& r, const float viewPos[3], float radius) {
    return ShotRayDistance(r, viewPos[0], viewPos[1], viewPos[2]) <= radius;
}

// Shot-target registration, Hit_RegisterTarget 0x00405160: flags = object +0x34, viewZ = object +0x78.
bool IsShotTarget(uint32_t flags, float viewZ);

// Hit list sort key: __ftol(-viewZ * 10) (0x00404630, 0x004047D0, 0x00404CB0; 10.0 at 0x004C43A4).
int32_t HitSortKey(float viewZ);

// Hit_SortByDepth 0x00405080: stable two-pass LSD radix sort over bits 0-15 of the key (entries {key, index}).
void SortHitKeys(std::vector<std::pair<uint32_t, int32_t>>& entries);

// Gun_Calibrate 0x0040D4F0: piecewise linear raw -> centred mapping through three calibration points.
// cal = {p0, p1, p2, offset}, out = {v0, v1, v2}; raw -= offset (+wrap if negative).
int GunCalibrate(int raw, const int16_t cal[4], const int16_t out[3], int wrap = 0);
// Gun_UpdateRecords 0x0040CBB0 for device mode 6 (fixed calibration in code): x from {-265, 0, 265, +320} to
// {-264, 0, 264} (table 0x004D1CF0), y from {50, 240, 430, 0} to {192, 0, -192} (0x004D1CF8); valid when
// x in [-320, 319] and y in [-240, 239].
void GunCalibrateMode6(int rawX, int rawY, int16_t* x, int16_t* y, bool* valid);

}  // namespace game
