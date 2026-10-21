#pragma once

// Renderer for HOTD2 models (port), platform-free: decodes materials, culls, orders pass 0 / pass 1 and builds the
// constants of every draw, then hands the frame to a GpuDevice backend (gpu_device.h: D3D11, SDL_GPU) as a draw list.
// Each model's strips are triangulated and uploaded once (model space); scene items draw them with a per-item world
// matrix, so per-frame scenes (actors, particles) only change the item list.
// Strip cull-mode bits are not mapped yet (culling off).

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <tuple>

#include "asset_store.h"
#include "gpu_device.h"
#include "game/model.h"
#include "game/sprite.h"

namespace port {

struct Camera {
    float yawDeg = 35, pitchDeg = 25;
    float distanceScale = 2.6f;  // multiples of the scene radius
};

class ModelRenderer {
public:
    // The device must outlive the renderer.
    bool Init(GpuDevice* device, std::string* error);
    void ClearCache();  // between frames, before the AssetStore releases its image pointers

    // Uploads models (replacing the current scene). polName: the pol file the models come from (selects the
    // texture table their material texture indexes refer to).
    void SetScene(AssetStore& assets, const std::vector<const game::Model*>& models, const std::string& polName);

    // One model placed in the scene: its pol file (for textures) and a row-vector world transform.
    struct SceneItem {
        const game::Model* model = nullptr;
        std::string polName;
        float transform[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        bool cullable = true;  // whole-model frustum culling (off for camera-following skies)
        uint32_t priority = 8;  // pass-1 list priority (0x007E78BC; 8 default, 7 for stage objects with draw mode 2)
        // >= 0: alpha draw path (0x004185A0 -> 0x004AA350 -> Mdl_DrawAlt 0x004A8440): pass-1 key | 0x60000000, every mesh
        // drawn in pass 1 with TSP forced to SRCALPHA/INVSRCALPHA ((tsp & 0x03FFFF7F) | 0x94000080) and material alpha *
        // this value (e.g. rain 0.5). HYPOTHESIS: opaque meshes of such items are not drawn in pass 0.
        float alpha = -1;
        bool unitSpecular = false;  // fish bodies: normalize only the specular normal
        bool sortBackToFront = false;  // mutually overlapping alpha effects; retain other pass-1 slots
        // Geometry changes every frame (skinned attachments deformed on the CPU): triangulated into a per-frame dynamic
        // vertex buffer instead of the per-model cache.
        bool dynamicGeometry = false;
        // >= 0: index into DrawGame's itemLights (the light this submission snapshotted, Mdl_Submit 0x004A7E50);
        // -1: DrawGame's frame light
        int light = -1;
        // The model's strip control words have bit 0 flipped in memory (cull mode CW <-> CCW): the models of list
        // 0x0057A280 loaded on their own (Res_LoadModel 0x00418B68 -> 0x00419270). game::CullFlipModel.
        bool cullFlip = false;
        // No per-mesh visibility test (the pause overlay 0x93E: scaled to the near plane, its material sphere misses the
        // frustum test although the quad covers the screen).
        bool noMeshCull = false;
        bool motionTracked = false;
        MotionIdentity motionIdentity;
        bool motionReactive = false; // scrolling/environment texture has no single geometric correspondence
        bool refineGeometry = false; // explicit author/test opt-in in addition to the active pack selection
        bool shadowCaster = true; // Scene excludes sky/view-space overlays/special effect sources explicitly
    };
    void SetScene(AssetStore& assets, const std::vector<SceneItem>& items);
    void SetTemporalFrame(const TemporalFrameInfo& frame) { temporalFrame_ = frame; }
    void SetTemporalScene(const TemporalSceneStamp& scene) { temporalScene_ = scene; }
    void SetCinematicEffectsAllowed(bool allowed) { cinematicEffectsAllowed_ = allowed; }

    void Draw(int width, int height, const Camera& cam);

    // Draws with an explicit row-vector view-projection matrix (e.g. the game's camera). unlit: texture colour
    // only (the game's lighting/material state is not reproduced yet).
    void DrawWithMatrix(int width, int height, const float viewProj[16], bool unlit);

    // Game materials: per-mesh blend/depth/filter/address/alpha state from the PVR words, opaque pass then
    // translucent pass sorted back to front (like 0x004A7A10 / 0x004A88E0). Native fixed-function lighting
    // is retained by default; optional per-pixel lighting affects opaque lit surfaces only.
    // D3D7 fixed-function lighting inputs (directional light 0 + AMBIENT render state). dirToLight: world-space unit
    // vector from surfaces toward the light (= -D3DLIGHT7.dvDirection in world space).
    struct Lighting {
        bool enabled = false;
        float dirToLight[3] = { 0, 1, 0 };
        float diffuse[3] = { 1, 1, 1 };     // directional light 0 (0 when disabled, e.g. the light table)
        float ambient[3] = { 0, 0, 0 };     // light's own ambient
        float ambientRS[3] = { 0, 0, 0 };   // D3DRENDERSTATE_AMBIENT
        // point / spot lights (the light table 0x004AA8B0), world space, D3DLIGHT7 semantics
        struct Local {
            int type = 1;                   // 1 point, 2 spot
            float diffuse[3] = { 1, 1, 1 };
            float pos[3] = {}, dir[3] = { 0, 0, 1 };
            float range = 1e30f, falloff = 1, att[3] = { 1, 0, 0 }, theta = 0, phi = 0;
        };
        int localCount = 0;
        Local local[kMaxLocalLights];
    };
    // D3DFOG_LINEAR table fog on eye depth (FOGSTART / FOGEND / FOGCOLOR), applied to materials with FOGENABLE.
    struct Fog {
        bool enabled = false;
        float start = 0, end = 1;
        float color[3] = { 0, 0, 0 };
    };

    // 2D sprites (game/sprite.h): pre-transformed quads in the game's 640x480 screen space, drawn in the given order
    // with the state Spr_DrawRecord 0x004A7AB0 sets (ISP/TSP from SpriteIsp/SpriteTsp, texture x diffuse,
    // alpha test >= 1/255 and SRCALPHA blending when translucent, point sampling, clamp).
    struct SpriteDraw {
        game::TLVertex v[4];
        const RgbaImage* texture = nullptr;
        uint32_t isp = 0, tsp = 0;
        uint32_t key = 0x8000000F;  // pass-1 list key (record key | list priority)
        float depth = 1;            // pass-1 sort depth (record depth)
        uint32_t textureId = 0;     // global texture id (diagnostics)
        bool linear = false;        // explicit port UI atlas opt-in; original sprites retain authored state
        bool hud = true;             // false for scene-space effects; only consumed by opted-in processing
    };
    void DrawSprites(int width, int height, const std::vector<SpriteDraw>& sprites);

    // gameView: the game's row-vector view matrix (game::ViewMatrix); with it, pass 1 reproduces the game's mixed list of
    // translucent models and sprites (Mdl_DrawPass1 0x004A88E0). Without it, translucent batches are sorted by clip w.
    // itemLights: per-item lights (SceneItem::light indexes it).
    // projection: camera -> clip (including DeviceView and screen shift). With gameView, separates
    // projection from model/view transforms to retain depth precision on distant stage geometry.
    void DrawGame(int width, int height, const float viewProj[16], const Lighting* light = nullptr,
                  const Fog* fog = nullptr, const float* gameView = nullptr, const std::vector<SpriteDraw>* sprites = nullptr,
                  const std::vector<Lighting>* itemLights = nullptr, const float* projection = nullptr);


    // Pass-1 list of the last DrawGame with a game view, in draw order (item = scene item index or -1; sprite = index).
    struct Pass1Entry {
        uint32_t key;
        float depth;
        int item, sprite;
    };
    std::vector<Pass1Entry> lastPass1;

    struct Stats {
        size_t triangles = 0, meshes = 0, texturedMeshes = 0, missingTextures = 0;
    };
    const Stats& SceneStats() const { return stats_; }

private:
    // One mesh of a cached model: model-space vertices in the shared buffer + material state, decoded like
    // Mdl_ApplyPvrState 0x004A7780 from the PVR ISP/TSP words.
    struct Mesh {
        uint32_t first = 0, count = 0;
        GpuTexture tex = 0;         // 0 = untextured (or missing)
        GpuTexture normalRoughness = 0; // optional manifest material map; opaque models only
        bool textured = false;
        float diffuse[4] = { 1, 1, 1, 1 };
        float ambientScale = 1;     // material word 10: D3DMATERIAL7 ambient = scale * diffuse
        float specular[4] = {};     // material words 16..18 and power 1 << word 9 (power 0: no specular, Mdl_Draw)
        bool translucent = false;   // drawn in pass 1 (TSP & 0x180000 != 0x80000): alpha test >= 1/255 + blending
        int srcBlend = 1, dstBlend = 0;  // D3D7 D3DBLEND values
        int zfunc = 4;              // D3D7 D3DCMPFUNC
        bool zwrite = true;
        bool linear = false;        // TSP bits 14-13 != 0
        int addrU = 1, addrV = 1;   // D3D7 D3DTEXTUREADDRESS (1 wrap, 2 mirror, 3 clamp)
        bool alphaFromTexture = false;  // TSP shading instruction 1: ALPHAOP SELECTARG1 (texture alpha only)
        bool fog = false;           // FOGENABLE: TSP bit 23 clear
        float sphere[4] = {};       // material bounding sphere (words 4-7), model space
        bool dynamic = false;       // vertices live in the per-frame dynamic buffer
        int cull = 1;               // D3DCULL of this run of strips (1 none, 2 CW, 3 CCW)
        bool firstRun = true;       // first run of its material mesh (statistics count material meshes)
        bool layeredCards = false;  // an alpha billboard whose overlapping cards need back-to-front drawing
        float cardZ = 0;            // common local Z of this card; ignore its vertical offset when sorting planes
        uint64_t topology = 0;      // source strip structure, independent of skin deformation and UV animation
        uint32_t ordinal = 0;       // stable run within model, independent of cache vertex offsets
        float shadowBounds[6] = {}; // actual deformed mesh AABB; source material spheres can be absent/loose
    };
    struct ModelGpu {
        std::vector<Mesh> meshes;
    };
    // A mesh placed by a scene item (per SetScene).
    struct Batch {
        const Mesh* mesh = nullptr;
        float center[3] = {};       // world-space sphere centre of the item (fallback pass-1 sort)
        float meshCenter[3] = {};   // world-space material sphere centre (pass-1 model depth)
        float meshRadius = 0;       // world-space material sphere radius (visibility)
        int item = -1;              // index of the scene item
        bool noMeshCull = false;    // SceneItem::noMeshCull
    };
    const ModelGpu& CachedModel(AssetStore& assets, const game::Model* model, const std::string& polName,int refinement=0);
    void ComputeBounds();
    void BuildMeshes(AssetStore& assets, const game::Model* model, const std::string& polName, ModelGpu& g,
                     std::vector<game::ModelVertex>& verts, bool dynamic,int refinement=0);
    void UploadDynamic();
    // Appends one draw (state + constants) to the list.
    static void AddModelDraw(GpuDrawList& list, const Mesh& m, const ModelConstants& fc, const GpuState& st);
    void AddSpriteDraw(GpuDrawList& list, int width, int height, const SpriteDraw& s, size_t index);
    static GpuState StateOf(bool blend, int src, int dst, int zfunc, bool zwrite, bool linear, int addrU, int addrV);
    GpuTexture TextureView(const RgbaImage* img, bool alphaWeighted = false);
    bool BeginTemporal(int width, int height, const float* view, const float* projection);
    void AddMotionDraw(GpuDrawList& list, const Batch& batch, const GpuDraw& draw);
    void SubmitGame(GpuDrawList& list);

    GpuDevice* device_ = nullptr;
    std::map<std::tuple<const game::Model*, std::string,int>, ModelGpu> models_;  // geometry uploaded once per model
    std::vector<game::ModelVertex> cacheVerts_;
    bool cacheDirty_ = false, boundsValid_ = false;
    bool haveStatic_ = false, haveDynamic_ = false;  // the device holds a static / (at some point) a dynamic stream
    std::vector<std::array<float, 16>> itemWorld_;
    std::vector<std::unique_ptr<ModelGpu>> dynModels_;  // this frame's dynamic-geometry items
    std::vector<game::ModelVertex> dynVerts_;
    bool dynUploadPending_ = false;
    std::vector<std::array<float, 3>> itemOrigin_;  // world translation of each scene item
    std::vector<std::array<float, 4>> itemSphere_;  // world bounding sphere of each scene item
    std::vector<char> itemCullable_;
    std::vector<uint32_t> itemPriority_;
    std::vector<float> itemAlpha_;
    std::vector<uint8_t> itemUnitSpecular_;
    std::vector<uint8_t> itemSortBackToFront_;
    std::vector<uint8_t> itemCullFlip_;
    std::vector<int> itemLight_;
    std::vector<MotionIdentity> itemMotionIdentity_;
    std::vector<uint8_t> itemMotionTracked_, itemMotionReactive_;
    std::vector<uint8_t> itemShadowCaster_;
    std::map<std::pair<const RgbaImage*, bool>, GpuTexture> textures_;

public:
    size_t TextureCount() const { return textures_.size(); }  // GPU textures created so far (timing diagnostics)
    // Create the GPU texture of a decoded image now (asset pack: prefetched images are uploaded a few per frame ahead
    // of their first draw, so the upload is not paid in the frame that first draws them).
    void WarmTexture(const RgbaImage* img) { TextureView(img); }

private:
    std::vector<Batch> batches_;
    float center_[3] = {}, radius_ = 1;
    Stats stats_;
    struct MotionSnapshot {
        uint64_t topology = 0;
        std::array<float,16> modelView = {}, projection = {};
        std::vector<std::array<float,3>> positions;
    };
    using MotionKey = std::pair<MotionIdentity,uint32_t>;
    std::map<MotionKey,MotionSnapshot> previousMotion_, currentMotion_;
    TemporalFrameInfo temporalFrame_, previousTemporalFrame_;
    TemporalCameraInfo temporalCamera_;
    bool cinematicEffectsAllowed_ = false;
    TemporalSceneStamp temporalScene_, previousTemporalScene_;
    std::array<float,16> previousTemporalView_ = {}, previousTemporalProjection_ = {};
    int previousTemporalWidth_ = 0, previousTemporalHeight_ = 0;
    bool haveTemporalHistory_ = false, temporalActive_ = false, temporalReset_ = false;
};

}  // namespace port
