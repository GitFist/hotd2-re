#pragma once

// Scene assembly for the port: static level chunks, camera-following sky models and a camera path, rendered
// with the game's view/projection conventions (game::ViewMatrix, DeviceView, DeviceProjection).

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/camera.h"
#include "game/effects.h"
#include "game/environment.h"
#include "game/world.h"
#include "game/model.h"
#include "render/asset_store.h"
#include "render/model_renderer.h"

namespace port {

struct SceneDef {
    int cameraId = -1;
    std::vector<int> statics;
    int stage = -1, section = -1;  // "stage S section N": level chunks from the stage section tables
    struct Sky {
        int modelId;
        float scale = 1;
        float yaw0 = 0, yawPerFrame = 0;
    };
    std::vector<Sky> skies;
    // "sky_task <type> <frame0> [yaw0]": the traced sky task (Sky_Task 0x004132D0, table 0x00579968; game::Sky):
    // yaw(frame) = yaw0 + table step * (frame - frame0) (yaw0 default: the table's yawInit). Replaces "sky" lines.
    // "world <event table index> <stage start frame> <first task serial>": run the native world runtime (game::World) from the
    // stage start to the requested frame and take every model draw and the camera from it (statics, skies and the camera
    // path directives are then unused; env / overlays still apply).
    bool useWorld = false, externalWorld = false;  // "world external": the program steps its own world
    int worldEventMode = 0, worldStart = 2786;
    uint32_t worldSerial = 0x10000;
    int skyType = -1, skyFrame0 = 0;
    bool skyYaw0Set = false;
    int skyYaw0 = 0;
    // "light env <pitch> <yaw>" (event op 0x18 angles) and "light_color <diffuse> <ambient> <ambientRS>"
    bool hasLight = false;
    int lightPitch = 0, lightYaw = 0;
    float lightDiffuse = 0.98f, lightAmbient = 0.3f, ambientRS = 0.698f;
    // "env <stage event table> <from addr> <to addr>": executes the environment ops (0x18 light angles, 0x20 fog /
    // light channels) of that script range with game::Environment; replaces "light" / "light_color".
    std::string envTable;
    uint32_t envFrom = 0, envTo = 0;
    // 2D layers: "overlay title" (Title_DrawLogo 0x004960A0), "overlay press_start [phase P]" (0x004067D0;
    // visible while ((frame + P) & 0x20) == 0, P fitted per capture run because the counter starts at HUD_Init).
    bool overlayTitle = false, overlayPressStart = false;
    int pressStartPhase = 0;
    bool Load(const std::string& path, std::string* error);
};

class Scene {
public:
    bool Init(AssetStore& assets, const SceneDef& def, std::string* error);

    // Camera time in frames since the camera started; duration of the camera path.
    float Duration() const { return duration_; }

    // Builds the frame at camera time t: uploads items to the renderer and returns the view-projection matrix.
    // frame: game frame for frame-driven parts (sky task); < 0 = derived from t.
    game::Mat4 Build(ModelRenderer& renderer, float t, int frame = -1);

    // 2D sprites of the scene's overlays at game frame rame (for the blink counter), in pass-1 order.
    std::vector<ModelRenderer::SpriteDraw> BuildSprites(int frame);

    // Renderer items + view-projection from a world's current frame (draws, skinning, culling flags, camera).
    // draws: another draw list in place of world.Draws() (play.exe: the last unpaused frame's draws re-rendered behind
    // the pause menu's display page, so the settings show live); camera / lights / env stay the world's current ones.
    game::Mat4 BuildFromWorld(ModelRenderer& renderer, const game::World& world, const std::vector<game::WorldDraw>* draws = nullptr);
    // Call before texture prefetch, between GPU frames. Chapters and menu transitions retire all old caches
    // together so the 32-bit client does not retain every chapter's high-resolution textures indefinitely.
    bool PrepareWorld(ModelRenderer& renderer, const game::World& world);
    // Sprite definitions -> pass-1 sprite draws (records, quads, textures, ISP/TSP, keys).
    // zNear: the camera near plane (0x004194C0) the sprites' z is mapped with (far 8000): 0.8 in play, 0.001 while paused.
    // texFiles: the world's texture-file states (WorldHud::texFiles; empty = all loaded): Spr_Draw 0x0041C630 draws a
    // sprite only when its file 0x0057A5BC[id] is in state 4.
    std::vector<ModelRenderer::SpriteDraw> MakeSpriteDraws(const std::vector<game::SpriteDef>& defs, float zNear = 0.8f,
                                                           const std::vector<uint8_t>* texFiles = nullptr);

    // Sound commands (Snd_PlayGameSound ids) the world issued in the frames stepped since the last call (world scenes).
    std::vector<game::World::SoundEvent> TakeSounds() { std::vector<game::World::SoundEvent> s; s.swap(pendingSounds_); return s; }

    // Model id of the scene item at index i of the last Build (skies first, then statics).
    int ItemModelId(int i) const { return i < static_cast<int>(itemIds_.size()) ? itemIds_[i] : -1; }

    // Lighting for DrawGame (disabled unless the scene defines a light). World scenes (BuildFromWorld) set lighting / fog
    // from the world's environment every frame and give each item the light its submission snapshotted (itemLights).
    ModelRenderer::Lighting lighting;
    ModelRenderer::Fog fog;
    std::vector<ModelRenderer::Lighting> itemLights;
    bool worldEnv = true;  // false: keep the scene file's lighting / fog for world scenes (the old behaviour)
    // game::WorldLight -> renderer lighting (0x004AA120 / 0x004AA8B0 with the frame's light table).
    static ModelRenderer::Lighting MakeLighting(const game::WorldLight& l, const game::WorldLights& frame);

    // Camera position/target/angles of the last Build (for logging and comparisons).
    float pos[3] = {}, target[3] = {};
    game::Mat4 view;  // game view matrix of the last Build (pass-1 depths)
    game::Mat4 projection;  // camera -> clip of the last Build (DeviceView, projection, screen shift)
    int pitch = 0, yaw = 0;

private:
    struct Item {
        game::Model model;
        std::string polName;
    };
    AssetStore* assets_ = nullptr;
    SceneDef def_;
    std::vector<Item> statics_;
    std::vector<Item> skies_;
    std::vector<game::SkyEntry> skyTable_;
    std::map<int, Item> skyModels_;  // sky_task models by id
    game::CameraFile camera_;
    int cameraIndex_ = -1;
    float duration_ = 0;
    float lightColor_[3] = { 1, 1, 1 };
    std::vector<int> itemIds_, staticIds_;
    std::vector<uint32_t> staticPriority_;
    std::unique_ptr<game::World> world_;
    int worldFrame_ = -1;
    int cacheMode_ = -1, cacheChapter_ = -1, cacheFrame_ = -1;
    std::map<int, Item> worldModels_;  // models drawn by the world, by id (loaded on first use)
    // sky panel uv scroll 0x00413560 (WorldDraw::uvScroll), advanced incrementally across frames
    struct UvScrollState {
        int steps = 0;
        bool wrap = false;  // sky task +0x50: a vertex reached the threshold on the last run
        std::unique_ptr<game::Model> model;
        std::vector<float> v;                                  // per inline record (sorted offsets)
        std::vector<std::vector<game::ModelVertex*>> uses;     // the model's vertices reading each record
    };
    std::map<int, UvScrollState> uvScroll_;
    std::vector<int> missingModels_;
    std::vector<game::World::SoundEvent> pendingSounds_;
    std::map<int, uint32_t> skinDefs_;                  // skinned attachment definitions by model id
    std::vector<std::unique_ptr<game::Model>> skinned_;  // this frame's deformed attachment copies
public:
    long long skinnedDraws_ = 0, altDraws_ = 0;  // diagnostics: world draws not fully applied by the renderer
private:
    bool BuildWorld(ModelRenderer& renderer, float t, int frame, game::Mat4* viewProj);  // env light colour (sprites with flag 0x2000)
};

}  // namespace port
