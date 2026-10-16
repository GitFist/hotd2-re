#pragma once

// HOTD2 stage ambient effects and billboard effect tasks (see notes/agents/effects.md):
//   rain            Rain_Init 0x004138A0, Rain_Task 0x004136A0 (stage1.bin model 83)
//   sky             Sky_Task 0x004132D0 (table 0x00579968)
//   billboard       Eff_Spawn 0x004073B0 (+ wrappers 0x00407340 / 0x004073A0), Eff_SpawnScaled 0x00407BC0,
//                   Eff_Task 0x00407A70, Eff_PlaySound 0x00407950
// World matrices here exclude the view: the engine's matrix stack has the view V at its base, so the matrix it hands
// to Mdl_Draw (and the capture's M lines) is world * V. Row vectors; every engine op pre-multiplies.
// Validated against the original (tests/effects_test.cpp).

#include <array>
#include <cstdint>
#include <vector>

#include "anim.h"  // Mat4
#include "exe_image.h"
#include "rng.h"

namespace game {

// The fields of a camera context (0x009A6000 + i*0x1A4) these effects read: pos +0xC0, pitch +0xCC, yaw +0xD0,
// roll +0xD4. The active context index is 0x009C6F00.
struct CameraState {
    float pos[3] = {};
    int pitch = 0, yaw = 0, roll = 0;
};

// Scale op 0x004A9CC0 as a matrix (rows scaled by x, y, z).
Mat4 ScaleMatrix(float x, float y, float z);

// ---------------------------------------------------------------------------------------------------------------
// Rain: 50 camera-relative streaks (pool 0x007C1EB8, 12 bytes each: x, y, z). Created by stage start
// (0x00460030 -> task init 0x00460710 -> 0x004138A0); updated and drawn only while 0x009C8E50 == 1 (event op
// 0x1D, 0x0045F340).
class Rain {
public:
    static constexpr int kCount = 50;
    static constexpr int kModel = 0x53;        // stage1.bin rain streak (global model id 83)
    static constexpr float kAlpha = 0.5f;      // 2nd argument of the draw (0x004185A0 -> 0x004AA350; queue word 4)

    struct Particle {
        float x, y, z;  // camera-yaw space: x in [-10, 9], y falls from [-25, 24], z in [-35, -11]
    };

    // 0x004138A0: 3 rand() per particle (x %20 - 10, y %50 - 25, z %25 - 35), 150 calls in all.
    void Init(CrtRand& rng);

    // 0x004136A0 (only when the rain flag is 1): per particle y -= 2; if y <= -7 respawn with 3 rand() calls;
    // then world = (x, y, z) * Ry(camera yaw) + camera pos, facing yaw = DirToAngles(world - camera, y = 0), and
    // the draw matrix S(1.5, 3.5, 1) * Rz(0x100) * Ry(facing) * T(world). Returns the number of respawns.
    int Update(CrtRand& rng, const CameraState& cam);

    const std::array<Particle, kCount>& Particles() const { return p_; }
    const std::array<Mat4, kCount>& World() const { return world_; }  // after Update, in draw order

private:
    std::array<Particle, kCount> p_{};
    std::array<Mat4, kCount> world_{};
};

// ---------------------------------------------------------------------------------------------------------------
// Sky (task 0x004132D0, created at stage start). Table 0x00579968 + type * 16.
struct SkyEntry {
    int16_t model = 0;      // drawn with S(1.2, 1.2, -1.2) * Ry(yaw) * T(cam + (0, yOffset, 0))
    int16_t model2 = 0;     // 0 = none; drawn with T(cam + (0, yOffset, 0)) only
    float yOffset = 0;
    int32_t yawStep = 0;    // added to the yaw (task +0x48) every frame unless the sky mode is 2
    int32_t yawInit = 0;    // yaw after a type change
};
std::vector<SkyEntry> LoadSkyTable(const ExeImage& exe, int count = 12);

class Sky {
public:
    struct Draw {
        int model;
        Mat4 world;
    };
    // One frame. type = 0x009A2C34 (event op 0x1B), mode = 0x009CA090 (op 0x1C: 0 = hidden, 2 = frozen).
    // Types 8, 10 and 11 use screen-fixed panels (0x00413620 / 0x00413500) and are not ported: returns false.
    bool Update(const std::vector<SkyEntry>& table, int type, int mode, const CameraState& cam, std::vector<Draw>* out);
    int Yaw() const { return yaw_; }
    // Sky_Create 0x004134D0 as the title calls it: 0x007C2118 = 0 (the type counts as unchanged), the task's yaw
    // +0x48 = whatever its memory held (the front end passes the value that fits the capture).
    void Create(int leftoverYaw) {
        prevType = 0;
        yaw_ = leftoverYaw;
    }

private:
    int yaw_ = 0;         // task +0x48
    int prevType = -1;    // 0x007C2118
};

// ---------------------------------------------------------------------------------------------------------------
// Billboard effect: task 0x00407A70 (0x68 bytes) drawing models first..last, one per frame, then dying.
struct EffectDef {
    int first = 0x904, last = 0x904;  // task +0x60 / +0x64
    float scale = 0.1f;               // task +0x50 before the distance rule
    int roll = 0;                     // task +0x4C
    bool randomRoll = false;          // type 0x53: roll = rand() % 0xFFFF
};
// The type switch of 0x004073B0 (types 0x5A, 0x5B, 0x5C use roll 0xC000; 0x5A also spawns 0x5C and 0x5D).
EffectDef EffectDefinition(int type);

// Parameter block of 0x004073B0 (float words p[0..11]).
struct EffectSpawn {
    float pos[3] = {};
    int pitch = 0, yaw = 0;           // p[3], p[4]; replaced when faceMode != 0
    float scale[3] = { -1, 0, 0 };    // p[6..8]; p[6] == -1 selects the type's scale and the distance rule
    int type = 0;                     // p[9] (low byte)
    int faceMode = 0;                 // p[10]: 0 keep angles, 1 face the camera, 2 face the camera with pitch 0
};

class BillboardEffect {
public:
    // 0x004073B0. Appends the created task(s) in creation order (the game appends them to the task list in that
    // order, so they run later in the same frame). view = the active camera context's view matrix (ctx +0x00).
    static void Spawn(const EffectSpawn& in, const CameraState& cam, const Mat4& view, CrtRand& rng,
                      std::vector<BillboardEffect>* out);
    // 0x00407BC0 (types 0x5A..0x5D, always faces the camera; 0x5A spawns 0x5B, 0x5C, 0x5D after itself).
    static void SpawnScaled(const float pos[3], int type, const CameraState& cam, const Mat4& view,
                            std::vector<BillboardEffect>* out);

    // One run of 0x00407A70: the model to draw and its world matrix; returns false when the task killed itself
    // after this draw (model > last, or flags & 2).
    bool Step(int* model, Mat4* world);

    float pos[3] = {};       // +0x38
    int pitch = 0, yaw = 0;  // +0x44, +0x48
    int roll = 0;            // +0x4C
    float scale[3] = {};     // +0x50
    int type = 0;            // +0x5C (byte)
    uint8_t flags = 1;       // +0x5D
    int model = 0, last = 0; // +0x60, +0x64
};

// Eff_PlaySound 0x00407950: sound command issued after a spawn (0x0041CFD0 argument), 0 = none.
// mode = 0x009A1A08 (type 0x45 has a different sound in mode 4).
uint32_t EffectSound(int type, int mode);

}  // namespace game
