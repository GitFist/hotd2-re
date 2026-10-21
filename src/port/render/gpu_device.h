#pragma once

// GPU backend interface of the port renderer (platform-free). ModelRenderer (model_renderer.h) does all the game-specific
// work on the CPU - PVR material decoding, culling, the pass-0 / pass-1 order, constants - and hands each frame to a
// GpuDevice as a flat draw list. A backend only has to provide two fixed pipelines (models, pre-transformed sprites),
// the D3D7-style fixed-function state of every draw, textures, three vertex streams and a colour + depth target.
//
// Backends: d3d11_device.* (Windows, the reference), sdl_gpu_device.* (SDL_GPU: D3D12 now, Vulkan / Metal once shader
// blobs exist; see notes/agents/platform.md). Shader source shared by both: hlsl_shaders.h.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/model.h"
#include "game/sprite.h"
#include "graphics_options.h"
#include "graphics_warmup.h"
#include "temporal_frame.h"
#include "gpu_timing.h"
#include "temporal_camera.h"
#include "shadow_map.h"
#include "scene_effects.h"

namespace port {

// The GPU stores reversed depth (near 1, far 0), while game materials keep D3D7 comparisons.
inline int ReversedDepthCompare(int comparison) {
    constexpr int reversed[9] = {0, 1, 5, 3, 7, 2, 6, 4, 8};
    return reversed[comparison];
}

struct RgbaImage;

using GpuTexture = uintptr_t;  // backend handle, 0 = none

// Fixed-function state of one draw, in D3D7 terms (decoded from the PVR ISP/TSP words like Mdl_ApplyPvrState 0x004A7780).
struct GpuState {
    uint8_t blendEnable = 0;
    uint8_t srcBlend = 2, dstBlend = 1;  // D3DBLEND (1 ZERO, 2 ONE, 3 SRCCOLOR, 4 INVSRCCOLOR, 5 SRCALPHA, 6 INVSRCALPHA,
                                         // 7 DESTALPHA, 8 INVDESTALPHA, 9 DESTCOLOR, 10 INVDESTCOLOR)
    uint8_t zfunc = 4;                   // D3DCMPFUNC 1..8 (NEVER, LESS, EQUAL, LESSEQUAL, GREATER, NOTEQUAL, GREATEREQUAL, ALWAYS)
    uint8_t zwrite = 1;
    uint8_t linear = 0;                  // authored magnification filter: bilinear (else point)
    uint8_t mipmapped = 0;               // filtered smaller levels for minified 3D textures; sprites keep level zero
    uint8_t addrU = 1, addrV = 1;        // D3DTEXTUREADDRESS (1 wrap, 2 mirror, 3 clamp)
    uint8_t cull = 1;                    // D3DCULL (1 none, 2 CW, 3 CCW): per strip in Mdl_Draw 0x004A7EF0 (table 0x00598B20)
};

// Constant buffer of the model pipeline (HLSL cbuffer Frame in hlsl_shaders.h; row-major matrices). D3D7 lighting is
// evaluated per vertex like the fixed-function pipeline: the directional light (litDir / litDiffuse) plus up to
// kMaxLocalLights point / spot lights (the light table 0x004AA8B0), world space.
constexpr int kMaxLocalLights = 8;
struct ModelConstants {
    float viewProj[16];
    float world[16];
    float modelView[16];  // model -> camera, kept separate from projection for depth precision
    float projection[16];
    float lightDir[4];
    float diffuse[4];
    float flags[4];       // x: textured, y: unlit, z: alpha-test threshold (<0 off), w: alpha source (1 tex*mat, 2 tex)
    float litDir[4];      // xyz: world dir to light, w: 1 = D3D7 lighting
    float litDiffuse[4];  // light diffuse (rgb), w: material ambient scale
    float litAmbient[4];  // light ambient + AMBIENT render state (rgb)
    float fogParams[4];   // x: FOGSTART, y: FOGEND, z: 1 = fog on for this material
    float fogColor[4];
    float localCount[4];  // x: local lights, y: normal transform, z: split projection, w: unit specular normal
    float matSpecular[4]; // material specular rgb, w: power (0: no specular; Mdl_Draw: words 16..18, 1 << word 9)
    float litSpecular[4]; // light 0 specular rgb (= its diffuse, 0x004AA120); SPECULARENABLE is on (0x004A7630)
    float eyePos[4];      // camera position, world space (D3DRENDERSTATE_LOCALVIEWER default TRUE)
    // per local light, 5 float4: pos.xyz range | dir.xyz falloff | diffuse.rgb type (1 point, 2 spot) |
    // att0 att1 att2 - | cos(theta / 2) cos(phi / 2) - -
    float local[kMaxLocalLights * 5][4];
    // Optional modern lighting, deliberately zero for native captures and alpha effects.
    float lightingOptions[4]; // x: per-pixel normalized lighting on opaque models
    float sampling[4];        // x: retain authored point magnification with AF; yz: U/V address modes
    float depthEffects[4];    // x: soft fade world distance; yz: 1/target size; w: backend opaque-depth ready
    float depthProjection[4]; // reversed projection A/B/C/D: m11-m10, m15-m14, m11, m15
    float materialParams[4];  // x: opted-in authored tangent normal/roughness map
};

// Constant buffer of the sprite pipeline (cbuffer Sprite).
struct SpriteConstants {
    float screen[4];  // x, y: target scale from 640x480; z: alpha-test threshold (<0 off); w: 1 = textured
};

struct GpuDraw {
    enum class Pipeline : uint8_t { Model, Sprite };
    enum class Stream : uint8_t { Static, Dynamic, Sprite };  // vertex buffer the draw reads
    Pipeline pipeline = Pipeline::Model;  // Model: triangle list of game::ModelVertex; Sprite: 4-vertex strip of game::TLVertex
    Stream stream = Stream::Static;
    uint32_t first = 0, count = 0;        // vertices
    GpuTexture texture = 0;
    GpuTexture normalRoughnessTexture = 0; // optional RGBA8 tangent normal RGB / perceptual roughness A
    GpuState state;
    uint32_t constants = 0;               // index into GpuDrawList::model / ::sprite
    bool reactive = false;               // scene alpha/animated texture; never a claim of opaque correspondence
    bool hud = false;                    // explicit screen UI, deferred only by opted-in scene processing
};

struct GpuMotionDraw {
    uint32_t first = 0, count = 0, constants = 0;
    GpuTexture texture = 0;
    GpuState state;
};

// One batch of draws into the current frame's target, executed in order with viewport (0, 0, width, height).
struct GpuDrawList {
    int width = 0, height = 0;
    std::vector<GpuDraw> draws;
    std::vector<ModelConstants> model;
    std::vector<SpriteConstants> sprite;
    uint32_t opaqueDrawCount = 0; // initial pass-0 models; depth prepass excludes mixed pass-1 and HUD
    TemporalFrameInfo temporal;
    TemporalCameraInfo temporalCamera;
    std::vector<MotionVertex> motionVertices;
    std::vector<MotionConstants> motionConstants;
    std::vector<GpuMotionDraw> motionDraws; // opaque depth-writing models only; no HUD/smoke
    std::vector<GpuDraw> shadowCasters; // optional unculled opaque models, constants index into ::model
    ShadowMapFrame shadowMap;
    SceneEffectsFrame sceneEffects;
};

struct GpuDeviceDesc {
    int width = 640, height = 480;   // render target size (the off-screen target the frame is rendered into)
    bool offscreen = true;           // headless: render into an off-screen target that ReadPixels can read back
    bool followWindowSize = false;   // window: the render target follows the window size (else fixed, scaled on present)
    int syncInterval = 0;            // window: present interval (0 = no vsync, 1 = vsync)
    void* nativeWindow = nullptr;    // backend-specific window: HWND (D3D11) / SDL_Window* (SDL_GPU)
};

// How a window device shows the rendered frame (play.exe's display settings, notes/agents/display.md): the frame is
// always rendered into an off-screen target of the internal resolution (TargetWidth / Height), then a present pass
// scales it into the rectangle (x, y, w, h) of the window's back buffer (pixels; w or h 0 = the whole window), black
// outside, with the shared present shader (hlsl_shaders.h kPresentShaderHlsl: sharp bilinear, optional scanlines).
struct PresentParams {
    int x = 0, y = 0, w = 0, h = 0;
    int crt = 0;  // 0 off; 1..3 scanline strength (per 480 game rows)
};

class GpuDevice {
public:
    virtual ~GpuDevice() = default;
    virtual const char* Name() const = 0;

    // Immutable RGBA8 texture (rows in the game's stored order). Returns 0 on failure.
    // Opaque materials can contain meaningful RGB beneath zero alpha. Only
    // alpha-blended material variants weight smaller levels by alpha coverage.
    virtual GpuTexture CreateTexture(const RgbaImage& img, bool alphaWeighted = false) = 0;
    // Between frames: invalidate every texture handle when the owning scene cache is discarded.
    virtual void ClearTextures() = 0;
    // Vertex streams. Static: the model cache (replaced whole when it grows); Dynamic: this frame's CPU-skinned
    // geometry; Sprite: 4 vertices per sprite. Data is consumed by the next Submit.
    virtual void SetStaticVertices(const game::ModelVertex* v, size_t count) = 0;
    virtual void SetDynamicVertices(const game::ModelVertex* v, size_t count) = 0;
    virtual void SetSpriteVertices(const game::TLVertex* v, size_t count) = 0;

    // Frame: BeginFrame binds the target and clears colour (RGBA) and reversed depth (0.0); Submit draws (may be called several
    // times); EndFrame presents (window) or finishes (off-screen).
    virtual bool BeginFrame(const float clear[4]) = 0;
    virtual void Submit(const GpuDrawList& list) = 0;
    // Optional temporal/native SDK boundary, after scene effects and before HUD.
    // Default rendering keeps the original mixed pass-1 order exactly.
    virtual bool RequiresSceneProcessing() const { return false; }
    virtual bool NeedsShadowCasters() const { return false; }
    virtual bool ProcessScene(std::string* error = nullptr) { return true; }
    // Diagnostic metadata (RGBA float: previous-current pixels,previous depth,valid).
    virtual bool ReadMotionPixels(std::vector<float>* rgba) { return false; }
    virtual bool SetGpuTimingEnabled(bool enabled,std::string* error=nullptr) {
        if(enabled&&error)*error="GPU timestamps are unavailable on this renderer";return !enabled;
    }
    virtual bool PollGpuTiming(GpuTimingSample* sample) { return false; }
    virtual void EndFrame() = 0;
    // Off-screen only, between Submit and EndFrame: the target's pixels as RGBA8, top row first.
    virtual bool ReadPixels(std::vector<uint8_t>* rgba) = 0;
    // Off-screen presentation capture at a specified output size, including the actual
    // spatial upscaler/final pass. Existing ReadPixels keeps its internal-size contract.
    virtual bool ReadPresentation(int width, int height, std::vector<uint8_t>* rgba) { return false; }
    // Diagnostic FP16 presentation readback, decoded to float scRGB (1.0 = 80 nits).
    // Does not enable HDR on a display and is available on an off-screen device.
    virtual bool ReadLinearPresentation(int width, int height, std::vector<float>* rgba) { return false; }

    virtual int TargetWidth() const = 0;
    virtual int TargetHeight() const = 0;
    // After ProcessScene, explicit HUD may use a separate output-resolution
    // target. Scene rendering/motion and TargetWidth/Height stay at input size.
    virtual int HudTargetWidth() const { return TargetWidth(); }
    virtual int HudTargetHeight() const { return TargetHeight(); }
    virtual const std::string& HudCompositionError() const { static const std::string empty;return empty; }

    // Between frames. Invalid or unsupported options fail without changing live targets/options.
    // A successful call reports the actual accepted state (including present-mode fallback).
    virtual bool ConfigureGraphics(const GraphicsOptions& options, std::string* error = nullptr) {
        if (error) *error = "This renderer does not support configurable graphics";
        return false;
    }
    virtual const GraphicsOptions& EffectiveGraphics() const {
        static const GraphicsOptions defaults;
        return defaults;
    }
    virtual const GraphicsCapabilities& GetGraphicsCapabilities() const {
        static const GraphicsCapabilities none;
        return none;
    }
    // Between frames: create a bounded set of real common states/PSOs for the
    // accepted options. Never draws, dispatches, or modifies simulation state.
    virtual bool WarmupGraphics(GraphicsWarmupStats* stats = nullptr,std::string* error = nullptr) {
        if(error)*error="Graphics state warmup is unavailable on this renderer";return false;
    }

    // Window devices (no-ops off-screen): the present mapping, the render target's size (internal resolution;
    // ignored with followWindowSize), and the window's current back-buffer size in pixels (false off-screen).
    virtual void SetPresent(const PresentParams&) {}
    virtual bool ResizeTarget(int, int) { return false; }
    virtual bool WindowSizePixels(int*, int*) const { return false; }
};

}  // namespace port
