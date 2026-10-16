// game::World - environment interpolator, light globals and the light table (renderer state only: nothing here feeds
// back into the game logic). Report: notes/agents/lighting.md.
//
// The engine keeps one set of "light globals" (0x007E7998 colour, 0x007E79A4 intensity, 0x007E79A8 direction) that every
// model submission snapshots (Mdl_Submit 0x004A7E50 -> 0x004AA120, the pass-1 list keeps the copy). They are set
//   - every frame by the env task 0x0040AD90 (block-0 colour / intensity) and the camera task 0x00401F40 (block-0
//     direction);
//   - around character draws by 0x0041DC70 (env block 1) / 0x0041DC10 (explicit values) and back by 0x0041DCC0;
//   - by a few effects directly (0x004AA0A0 colour, 0x004AA070 intensity).
// Alternative-model submissions (0x004185E0 -> 0x004AA500, key bit 0x04000000) use the light table instead
// (0x004AA8B0): AMBIENT 0x007E8138 and the D3D lights of the enabled slots of 0x009A1A20, set every frame by the task
// 0x00480970 when 0x009A2BB4 != 0 (player flashlights 0x00480AC0 when 0x009C8D4C == 1, light actors 0x2B).

#include "game/world_internal.h"

#include "game/mstack.h"
#include "game/shoot_camera.h"

namespace game {

// 0x0041DCC0
void World::Impl::LightScene() {
    light.intensity = env.lightIntensity;  // 0x004AA070(0x009A378C)
    light.pitch = lightDir0[0];             // 0x004AA0E0(0x009A354C): the direction the camera task built
    light.yaw = lightDir0[1];
    for (int k = 0; k < 3; ++k) light.color[k] = env.lightColor[k];  // 0x004AA0A0(0x009A3780..)
}

// 0x0041DC70: env block 1 (0x0040E0B0 builds its direction with the active context's view; world space: the angles)
void World::Impl::LightChar() {
    light.intensity = env.block1.lightIntensity;
    light.pitch = env.block1.lightPitch;
    light.yaw = env.block1.lightYaw;
    for (int k = 0; k < 3; ++k) light.color[k] = env.block1.lightColor[k];
}

// 0x0041DC10(intensity, pitch, yaw, r, g, b)
void World::Impl::LightCustom(float intensity, int pitch, int yaw, const float color[3]) {
    light.intensity = intensity;
    light.pitch = pitch;
    light.yaw = yaw;
    for (int k = 0; k < 3; ++k) light.color[k] = color[k];
}

void World::Impl::LightColor(float r, float g, float b) {  // 0x004AA0A0
    light.color[0] = r;
    light.color[1] = g;
    light.color[2] = b;
}

void World::Impl::LightIntensity(float i) { light.intensity = i; }  // 0x004AA070

// 0x0040AD90: steps the channels of both blocks, then the fog range (0x0040C320 -> 0x004ABDF0) and the light colour /
// intensity of block 0 (0x004AA0A0 / 0x004AA070)
void World::Impl::TaskEnv(Task&) {
    env.Step();
    for (int k = 0; k < 3; ++k) light.color[k] = env.lightColor[k];
    light.intensity = env.lightIntensity;
}

// 0x00480970: the light table, rebuilt every frame (0x004AA830 resets it); only with 0x009A2BB4 != 0
void World::Impl::TaskLightTable(Task&) {
    lights.table.clear();
    if (altModels == 0) return;
    // 0x00480AC0: the players' flashlights (slots 1 / 2): spot lights from the camera through the aim point
    for (int p = 0; p < 2; ++p) {
        LightSlot& s = lightSlots[1 + p];
        const Player& P = player[p];
        if (flashlightOn == 1 && (P.state == 5 || gameMode == 5) && P.gun.valid != 0) {
            const float k = 1.0f / focal;  // 0x004C4380 / 0x009A2D70
            Mat4 m = cam.inverse;          // camera matrix of the active context (0x009A6040 + ctx * 0x1A4)
            mx::Translate(m, k * P.aim[0], k * P.aim[1], -1.0f);
            const float pt[3] = { m.m[12], m.m[13], m.m[14] };
            int pitch = 0, yaw = 0;
            DirToAngles(pt[0] - cam.pos[0], pt[1] - cam.pos[1], pt[2] - cam.pos[2], &pitch, &yaw);
            Mat4 r = Mat4::Identity();
            mx::RotZ(r, 0);
            mx::RotY(r, yaw);
            mx::RotX(r, pitch);
            WorldLights::Light& l = s.l;
            l.type = 2;  // D3DLIGHT_SPOT
            for (int i = 0; i < 3; ++i) {
                l.pos[i] = pt[i];
                l.dir[i] = r.m[8 + i];
                l.diffuse[i] = 1.0f;
            }
            l.falloff = 1.0f;
            l.att[0] = 0.5f;
            l.att[1] = l.att[2] = 0.0f;
            l.theta = l.phi = 0.3926991f;
            l.range = 1.8446744e19f;  // sqrt((float) 3.4e38 double 0x005691D8)
            s.enabled = true;
        } else {
            s.enabled = false;
        }
    }
    for (int k = 0; k < 3; ++k) lights.tableAmbient[k] = tableAmbient[k];  // 0x004AA720(0x009C71D0)
    for (const LightSlot& s : lightSlots)
        if (s.enabled) lights.table.push_back(s.l);
}

// End of a frame: the frame-level state the renderer reads.
void World::Impl::UpdateLightsFrame() {
    lights.valid = true;
    lights.fogOn = true;
    for (int k = 0; k < 3; ++k) lights.fogColor[k] = env.fogColor[k];
    lights.fogStart = env.FogStart();
    lights.fogEnd = env.FogEnd();
    if (altModels == 0) lights.table.clear();
}

const WorldLights& World::Lights() const { return impl_->lights; }
const Environment& World::Env() const { return impl_->env; }

}  // namespace game
