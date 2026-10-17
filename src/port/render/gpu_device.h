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
    uint8_t linear = 0;                  // bilinear filter (else point), no mips
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
    float localCount[4];  // x: local lights, y: normal transform (0 normalised, 1 WORLD, 2 inverse transpose), z: split projection
    float matSpecular[4]; // material specular rgb, w: power (0: no specular; Mdl_Draw: words 16..18, 1 << word 9)
    float litSpecular[4]; // light 0 specular rgb (= its diffuse, 0x004AA120); SPECULARENABLE is on (0x004A7630)
    float eyePos[4];      // camera position, world space (D3DRENDERSTATE_LOCALVIEWER default TRUE)
    // per local light, 5 float4: pos.xyz range | dir.xyz falloff | diffuse.rgb type (1 point, 2 spot) |
    // att0 att1 att2 - | cos(theta / 2) cos(phi / 2) - -
    float local[kMaxLocalLights * 5][4];
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
    GpuState state;
    uint32_t constants = 0;               // index into GpuDrawList::model / ::sprite
};

// One batch of draws into the current frame's target, executed in order with viewport (0, 0, width, height).
struct GpuDrawList {
    int width = 0, height = 0;
    std::vector<GpuDraw> draws;
    std::vector<ModelConstants> model;
    std::vector<SpriteConstants> sprite;
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
    virtual GpuTexture CreateTexture(const RgbaImage& img) = 0;
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
    virtual void EndFrame() = 0;
    // Off-screen only, between Submit and EndFrame: the target's pixels as RGBA8, top row first.
    virtual bool ReadPixels(std::vector<uint8_t>* rgba) = 0;

    virtual int TargetWidth() const = 0;
    virtual int TargetHeight() const = 0;

    // Window devices (no-ops off-screen): the present mapping, the render target's size (internal resolution;
    // ignored with followWindowSize), and the window's current back-buffer size in pixels (false off-screen).
    virtual void SetPresent(const PresentParams&) {}
    virtual bool ResizeTarget(int, int) { return false; }
    virtual bool WindowSizePixels(int*, int*) const { return false; }
};

}  // namespace port
