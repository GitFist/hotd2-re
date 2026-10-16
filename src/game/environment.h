#pragma once

// Stage environment (fog, light colour, light angles) as set by event ops 0x17..0x27 and turned into D3D7 state
// by the engine. See notes/formats.md "Environment".
//   env block 0 0x009A3540 (scene light + fog), block 1 0x009A59E0 (the character light, 0x0041DC70):
//   +0x18 / +0x1C light pitch / yaw (op 0x18 / 0x19, 0x0040E140), +0x24/+0x28/+0x2C fog colour, +0x30 / +0x34 fog
//   near / far, +0x240..+0x248 light colour, +0x24C light intensity (op 0x20 / 0x24, Env_SetChannel 0x0040B3F0;
//   Stage_Start 0x00460030 writes the defaults).
//   Interpolation (ops 0x21 / 0x25 per-frame step 0x0040B650, 0x23 / 0x27 over N frames 0x0040BA90, 0x22 / 0x26 stop
//   0x0040C1F0): 9 channel records {active, value, target, step} per block (0x009C89E0 / 0x009C8920), stepped every
//   frame by the env task 0x0040AD90 (created by Env_InitInterp 0x0040AE60 in Stage_Task), which then sets the fog
//   range and the light colour / intensity from block 0.
//   Fog: FOGCOLOR from +0x24.. (0x0040D5B0 -> 0x004ABDD0); FOGSTART/END = 2*min / 2*max of near/far
//   (0x0040C2E0 -> 0x004ABDF0); FOGTABLEMODE LINEAR (captured).
//   Light (0x0041DCC0 -> 0x004AA070 / 0x004AA0A0, built in 0x004AA120): AMBIENT = trunc(255 * i * colour),
//   light diffuse = 1.4 * i * colour, light ambient = 0.3 * colour.

#include <cstdint>

#include "game/event.h"

namespace game {

// One env block (the fields the renderer reads).
struct EnvBlock {
    int lightPitch = 0, lightYaw = 0;           // +0x18 / +0x1C, s16 angles (65536 = full turn); title: 0, 0 (captured)
    // boot values (the blocks are initialised data; captured from frame 0, HOTD2_SCENECAP_ENV): fog colour 0, near 65535,
    // far 65536 (FOGSTART 131070: no visible fog), colour 1, intensity 0.7, angles 0; the stage scripts set their own
    int fogColor[3] = { 0, 0, 0 };              // +0x24..
    float fogNear = 65535, fogFar = 65536;      // +0x30 / +0x34
    // Light colour / intensity as the title runs them (captured AMBIENT 0xB2B2B2, diffuse 0.98, ambient 0.3, no
    // event ops): the interpolation seeds of Env_InitInterp 0x0040AE60 (colour 0x0057969C = 1.0, intensity
    // 0x005796AC = 0.7). Stage_Start writes (1, 0.2, 0.1) / 1.0 (block 1) first; the stage scripts set their own.
    float lightColor[3] = { 1.0f, 1.0f, 1.0f };  // +0x240..+0x248
    float lightIntensity = 0.7f;                  // +0x24C
};

// Channel record of the env interpolator (16 bytes: active, value, target, step); channels 0 near, 1 far, 2..4 fog
// colour, 5..7 light colour, 8 intensity.
struct EnvChannel {
    int active = 0;
    float value = 0, target = 0, step = 0;
};

struct Environment : EnvBlock {  // the EnvBlock part is block 0
    bool hasLightAngles = false;
    EnvBlock block1;
    EnvChannel chan[2][9];

    EnvBlock& Block(int b) { return b ? block1 : static_cast<EnvBlock&>(*this); }
    const EnvBlock& Block(int b) const { return b ? block1 : static_cast<const EnvBlock&>(*this); }

    // Stage_Start 0x00460030 (part at 0x00460250): fog colour (255, 0, 0), near -600, far 1800, light colour
    // (1, 0.2, 0.1) in both blocks, block-1 intensity 1.0 (current values and channel records).
    void StageDefaults();
    // Stage start (captured at every stage start of a whole game, HOTD2_SCENECAP_ENV): both blocks back to their boot
    // values (angles 0, fog colour 0, near 65535, far 65536, colour 1, intensity 0.7). The writer is not traced.
    void ResetBlocks();
    // Env_InitInterp 0x0040AE60: channel records of both blocks seeded (inactive; colour 1.0, intensity 0.7).
    void InitInterp();
    // The env task 0x0040AD90: steps every active channel of both blocks (0x0040B0F0) and copies its value to the block.
    void Step();

    // Executes op 0x18 / 0x19 (light angles), 0x20 / 0x24 (set a channel), 0x21 / 0x25 (interpolate by a step per
    // frame), 0x22 / 0x26 (stop), 0x23 / 0x27 (interpolate over N frames). Returns false for other ops and for
    // sub-ops that read values from memory that cannot be resolved.
    bool Apply(const EventMemory& mem, const EventOp& op);

    // D3D7 values the engine derives (block 0).
    float FogStart() const;
    float FogEnd() const;
    void LightDiffuse(float out[3]) const;
    void LightAmbient(float out[3]) const;
    uint32_t AmbientRenderState() const;  // 0xFFrrggbb
};

// 0x004AA120 light build from a colour and intensity (the light globals 0x007E7998.. at submit time).
void LightDiffuseOf(const float color[3], float intensity, float out[3]);  // 1.4 * i * c
void LightAmbientOf(const float color[3], float out[3]);                  // 0.3 * c
uint32_t AmbientRenderStateOf(const float color[3], float intensity);     // 0xFF000000 | trunc(255 * i * c)

}  // namespace game
