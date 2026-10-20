#include "sdl_gpu_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "asset_store.h"
#include "hlsl_shaders.h"
#include "texture_mips.h"

#ifdef _WIN32
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace port {

namespace {

// D3D7 D3DBLEND -> SDL_GPU (colour factors; alpha factors map colour blends to their alpha counterparts like D3D11).
SDL_GPUBlendFactor ToBlend(int d3d7, bool alpha) {
    switch (d3d7) {
    case 1: return SDL_GPU_BLENDFACTOR_ZERO;
    case 2: return SDL_GPU_BLENDFACTOR_ONE;
    case 3: return alpha ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case 4: return alpha ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case 6: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case 8: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return alpha ? SDL_GPU_BLENDFACTOR_DST_ALPHA : SDL_GPU_BLENDFACTOR_DST_COLOR;
    case 10: return alpha ? SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA : SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}

SDL_GPUSamplerAddressMode ToAddress(int d3d7) {
    return d3d7 == 2 ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT : d3d7 == 3 ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                                      : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}

class SdlGpuDevice final : public GpuDevice {
public:
    ~SdlGpuDevice() override;
    bool Init(const GpuDeviceDesc& desc, std::string* error);
    const char* Name() const override { return "sdlgpu"; }
    GpuTexture CreateTexture(const RgbaImage& img, bool alphaWeighted = false) override;
    void ClearTextures() override {
        SDL_WaitForGPUIdle(dev_);
        for (auto* texture : textures_) SDL_ReleaseGPUTexture(dev_, texture);
        textures_.clear();
    }
    void SetStaticVertices(const game::ModelVertex* v, size_t count) override;
    void SetDynamicVertices(const game::ModelVertex* v, size_t count) override;
    void SetSpriteVertices(const game::TLVertex* v, size_t count) override;
    bool BeginFrame(const float clear[4]) override;
    void Submit(const GpuDrawList& list) override;
    void EndFrame() override;
    bool ReadPixels(std::vector<uint8_t>* rgba) override;
    int TargetWidth() const override { return width_; }
    int TargetHeight() const override { return height_; }
    void SetPresent(const PresentParams& p) override { present_ = p; }
    bool ResizeTarget(int w, int h) override;
    bool WindowSizePixels(int* w, int* h) const override;

private:
    bool CreateShaders(std::string* error);
    bool CreatePresentPipeline(std::string* error);
    bool CreateTargets(int w, int h);
    SDL_GPUGraphicsPipeline* Pipeline(GpuDraw::Pipeline kind, const GpuState& s);
    SDL_GPUSampler* Sampler(const GpuState& s);
    // Uploads bytes into a GPU buffer through a transfer buffer, in its own command buffer (immediate) or a copy pass.
    bool Upload(SDL_GPUCopyPass* pass, SDL_GPUBuffer* dst, const void* data, uint32_t bytes, bool cycle);
    void EnsureBuffer(SDL_GPUBuffer*& buf, uint32_t& capacity, uint32_t bytes);

    SDL_GPUDevice* dev_ = nullptr;
    SDL_Window* window_ = nullptr;
    bool follow_ = false;
    int sync_ = 0;
    int width_ = 0, height_ = 0;
    SDL_GPUTexture* color_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    SDL_GPUTexture* white_ = nullptr;  // bound when a draw has no texture (the shaders do not sample it then)
    SDL_GPUShader* modelVs_ = nullptr;
    SDL_GPUShader* modelPs_ = nullptr;
    SDL_GPUShader* spriteVs_ = nullptr;
    SDL_GPUShader* spritePs_ = nullptr;
    // the present pass (window): kPresentShaderHlsl into the swap chain texture, present_'s rectangle as the viewport
    SDL_GPUShader* presentVs_ = nullptr;
    SDL_GPUShader* presentPs_ = nullptr;
    SDL_GPUGraphicsPipeline* presentPipe_ = nullptr;
    SDL_GPUSampler* presentSampler_ = nullptr;
    PresentParams present_;
    std::map<uint32_t, SDL_GPUGraphicsPipeline*> pipelines_;
    std::map<int, SDL_GPUSampler*> samplers_;
    std::vector<SDL_GPUTexture*> textures_;  // handle = index + 1
    SDL_GPUBuffer* staticVb_ = nullptr;
    SDL_GPUBuffer* dynVb_ = nullptr;
    SDL_GPUBuffer* spriteVb_ = nullptr;
    uint32_t staticCap_ = 0, dynCap_ = 0, spriteCap_ = 0;
    std::vector<uint8_t> dynData_, spriteData_;
    bool dynPending_ = false, spritePending_ = false;
    SDL_GPUTransferBuffer* transfer_ = nullptr;
    uint32_t transferCap_ = 0;
    SDL_GPUTransferBuffer* readback_ = nullptr;
    SDL_GPUCommandBuffer* cmd_ = nullptr;
    bool needClear_ = false;
    float clear_[4] = {};
};

SdlGpuDevice::~SdlGpuDevice() {
    if (!dev_) return;
    if (cmd_) SDL_CancelGPUCommandBuffer(cmd_);
    SDL_WaitForGPUIdle(dev_);
    for (auto& [k, p] : pipelines_) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
    if (presentPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, presentPipe_);
    if (presentSampler_) SDL_ReleaseGPUSampler(dev_, presentSampler_);
    for (auto& [k, s] : samplers_) SDL_ReleaseGPUSampler(dev_, s);
    for (SDL_GPUTexture* t : textures_) SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUTexture* t : { color_, depth_, white_ })
        if (t) SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUBuffer* b : { staticVb_, dynVb_, spriteVb_ })
        if (b) SDL_ReleaseGPUBuffer(dev_, b);
    if (transfer_) SDL_ReleaseGPUTransferBuffer(dev_, transfer_);
    if (readback_) SDL_ReleaseGPUTransferBuffer(dev_, readback_);
    for (SDL_GPUShader* s : { modelVs_, modelPs_, spriteVs_, spritePs_, presentVs_, presentPs_ })
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (window_) SDL_ReleaseWindowFromGPUDevice(dev_, window_);
    SDL_DestroyGPUDevice(dev_);
}

bool SdlGpuDevice::Init(const GpuDeviceDesc& desc, std::string* error) {
    window_ = desc.offscreen ? nullptr : static_cast<SDL_Window*>(desc.nativeWindow);
    follow_ = desc.followWindowSize && window_;
    sync_ = desc.syncInterval;
#ifdef _WIN32
    const SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_DXBC;
#else
    const SDL_GPUShaderFormat formats = 0;
#endif
    if (!formats) {
        if (error) *error = "SDL_GPU: no shader blobs for this platform yet (needs SPIR-V / MSL builds of hlsl_shaders.h)";
        return false;
    }
    dev_ = SDL_CreateGPUDevice(formats, false, nullptr);
    if (!dev_) {
        if (error) *error = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
        return false;
    }
    if (window_) {
        if (!SDL_ClaimWindowForGPUDevice(dev_, window_)) {
            if (error) *error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
            window_ = nullptr;
            return false;
        }
        SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
        if (sync_ == 0) {
            if (SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE)) mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            else if (SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_MAILBOX)) mode = SDL_GPU_PRESENTMODE_MAILBOX;
        }
        SDL_SetGPUSwapchainParameters(dev_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
    }
    int w = desc.width, h = desc.height;
    if (follow_) SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (!CreateShaders(error)) return false;
    if (window_ && !CreatePresentPipeline(error)) return false;
    if (!CreateTargets(w, h)) {
        if (error) *error = std::string("SDL_GPU targets: ") + SDL_GetError();
        return false;
    }
    RgbaImage white;
    white.width = white.height = 1;
    white.pixels = { 255, 255, 255, 255 };
    const GpuTexture t = CreateTexture(white);
    if (!t) {
        if (error) *error = std::string("SDL_GPU texture: ") + SDL_GetError();
        return false;
    }
    white_ = textures_[t - 1];
    textures_.pop_back();
    return true;
}

bool SdlGpuDevice::CreateShaders(std::string* error) {
#ifdef _WIN32
    struct Def {
        const char* src;
        size_t size;
        const char* entry;
        const char* target;
        std::vector<D3D_SHADER_MACRO> macros;
        SDL_GPUShaderStage stage;
        Uint32 samplers, uniforms;
        SDL_GPUShader** out;
    };
    const D3D_SHADER_MACRO vsIn[] = { { "IN0", "TEXCOORD0" }, { "IN1", "TEXCOORD1" }, { "IN2", "TEXCOORD2" } };
    std::vector<D3D_SHADER_MACRO> vs(std::begin(vsIn), std::end(vsIn));
    vs.push_back({ "CB_REG", "register(b0, space1)" });
    std::vector<D3D_SHADER_MACRO> spriteVs = vs;
    spriteVs.push_back({ "SDLGPU_UBYTE4", "1" });
    const std::vector<D3D_SHADER_MACRO> ps = { { "CB_REG", "register(b0, space3)" }, { "TEX_REG", "register(t0, space2)" },
                                               { "SAMP_REG", "register(s0, space2)" } };
    Def defs[] = {
        { kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "vsmain", "vs_5_1", vs, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, &modelVs_ },
        { kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "psmain", "ps_5_1", ps, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, &modelPs_ },
        { kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "vsmain", "vs_5_1", spriteVs, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, &spriteVs_ },
        { kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "psmain", "ps_5_1", ps, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, &spritePs_ },
        { kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "vsmain", "vs_5_1", vs, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, &presentVs_ },
        { kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "psmain", "ps_5_1", ps, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, &presentPs_ },
    };
    for (Def& d : defs) {
        d.macros.push_back({ nullptr, nullptr });
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(d.src, d.size, d.entry, d.macros.data(), nullptr, d.entry, d.target, 0, 0, &blob, &err))) {
            if (error) *error = std::string(d.entry) + " " + d.target + ": " + (err ? static_cast<const char*>(err->GetBufferPointer()) : "shader compile failed");
            if (err) err->Release();
            return false;
        }
        if (err) err->Release();
        SDL_GPUShaderCreateInfo si = {};
        si.code = static_cast<const Uint8*>(blob->GetBufferPointer());
        si.code_size = blob->GetBufferSize();
        si.entrypoint = d.entry;
        si.format = SDL_GPU_SHADERFORMAT_DXBC;
        si.stage = d.stage;
        si.num_samplers = d.samplers;
        si.num_uniform_buffers = d.uniforms;
        *d.out = SDL_CreateGPUShader(dev_, &si);
        blob->Release();
        if (!*d.out) {
            if (error) *error = std::string("SDL_CreateGPUShader: ") + SDL_GetError();
            return false;
        }
    }
    return true;
#else
    if (error) *error = "SDL_GPU: no shader compiler on this platform";
    return false;
#endif
}

bool SdlGpuDevice::CreatePresentPipeline(std::string* error) {
    SDL_GPUColorTargetDescription ct = {};
    ct.format = SDL_GetGPUSwapchainTextureFormat(dev_, window_);
    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader = presentVs_;
    pi.fragment_shader = presentPs_;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;  // one full-viewport triangle from SV_VertexID, no vertex buffer
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets = 1;
    presentPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    presentSampler_ = SDL_CreateGPUSampler(dev_, &si);
    if (!presentPipe_ || !presentSampler_) {
        if (error) *error = std::string("SDL_GPU present pipeline: ") + SDL_GetError();
        return false;
    }
    return true;
}

bool SdlGpuDevice::ResizeTarget(int w, int h) {
    if (follow_ || w <= 0 || h <= 0 || cmd_) return false;  // not inside a frame
    if (w == width_ && h == height_) return true;
    return CreateTargets(w, h);
}

bool SdlGpuDevice::WindowSizePixels(int* w, int* h) const {
    if (!window_) return false;
    SDL_GetWindowSizeInPixels(window_, w, h);
    if (*w <= 0) *w = 1;
    if (*h <= 0) *h = 1;
    return true;
}

bool SdlGpuDevice::CreateTargets(int w, int h) {
    if (color_) SDL_ReleaseGPUTexture(dev_, color_);
    if (depth_) SDL_ReleaseGPUTexture(dev_, depth_);
    if (readback_) SDL_ReleaseGPUTransferBuffer(dev_, readback_);
    color_ = depth_ = nullptr;
    readback_ = nullptr;
    width_ = std::max(w, 1);
    height_ = std::max(h, 1);
    SDL_GPUTextureCreateInfo ti = {};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = width_;
    ti.height = height_;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    color_ = SDL_CreateGPUTexture(dev_, &ti);
    ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_ = SDL_CreateGPUTexture(dev_, &ti);
    if (!window_) {
        SDL_GPUTransferBufferCreateInfo bi = {};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        bi.size = static_cast<Uint32>(width_) * height_ * 4;
        readback_ = SDL_CreateGPUTransferBuffer(dev_, &bi);
    }
    return color_ && depth_ && (window_ || readback_);
}

bool SdlGpuDevice::Upload(SDL_GPUCopyPass* pass, SDL_GPUBuffer* dst, const void* data, uint32_t bytes, bool cycle) {
    if (!bytes) return true;
    if (bytes > transferCap_) {
        if (transfer_) SDL_ReleaseGPUTransferBuffer(dev_, transfer_);
        SDL_GPUTransferBufferCreateInfo bi = {};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        bi.size = std::max<uint32_t>(bytes, 1u << 20);
        transfer_ = SDL_CreateGPUTransferBuffer(dev_, &bi);
        transferCap_ = transfer_ ? bi.size : 0;
        if (!transfer_) return false;
    }
    void* p = SDL_MapGPUTransferBuffer(dev_, transfer_, true);  // cycle: earlier uploads may still be pending
    if (!p) return false;
    memcpy(p, data, bytes);
    SDL_UnmapGPUTransferBuffer(dev_, transfer_);
    SDL_GPUCommandBuffer* own = nullptr;
    if (!pass) {
        own = SDL_AcquireGPUCommandBuffer(dev_);
        pass = SDL_BeginGPUCopyPass(own);
    }
    SDL_GPUTransferBufferLocation src = { transfer_, 0 };
    SDL_GPUBufferRegion region = { dst, 0, bytes };
    SDL_UploadToGPUBuffer(pass, &src, &region, cycle);
    if (own) {
        SDL_EndGPUCopyPass(pass);
        SDL_SubmitGPUCommandBuffer(own);
    }
    return true;
}

void SdlGpuDevice::EnsureBuffer(SDL_GPUBuffer*& buf, uint32_t& capacity, uint32_t bytes) {
    if (bytes <= capacity && buf) return;
    if (buf) SDL_ReleaseGPUBuffer(dev_, buf);  // released once the GPU is done with it
    SDL_GPUBufferCreateInfo bi = {};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = bytes;
    buf = SDL_CreateGPUBuffer(dev_, &bi);
    capacity = buf ? bytes : 0;
}

GpuTexture SdlGpuDevice::CreateTexture(const RgbaImage& img, bool alphaWeighted) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.size() != static_cast<size_t>(img.width) * img.height * 4) return 0;
    const auto mips = BuildTextureMips(img, alphaWeighted);
    std::vector<const RgbaImage*> levels = {&img};
    for (const auto& mip : mips) levels.push_back(&mip);
    SDL_GPUTextureCreateInfo ti = {};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = img.width;
    ti.height = img.height;
    ti.layer_count_or_depth = 1;
    ti.num_levels = static_cast<Uint32>(levels.size());
    SDL_GPUTexture* t = SDL_CreateGPUTexture(dev_, &ti);
    if (!t) return 0;
    // D3D12 row/offset alignment avoids an additional SDL staging copy on small levels.
    std::vector<uint32_t> offsets, pitches;
    uint32_t bytes = 0;
    for (const auto* level : levels) {
        bytes = (bytes + 511u) & ~511u;
        offsets.push_back(bytes);
        const uint32_t pitch = (static_cast<uint32_t>(level->width) * 4 + 255u) & ~255u;
        pitches.push_back(pitch);
        bytes += pitch * level->height;
    }
    SDL_GPUTransferBufferCreateInfo bi = {};
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    bi.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &bi);
    if (!tb) {
        SDL_ReleaseGPUTexture(dev_, t);
        return 0;
    }
    void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
    if (!p) {
        SDL_ReleaseGPUTransferBuffer(dev_, tb);
        SDL_ReleaseGPUTexture(dev_, t);
        return 0;
    }
    for (size_t i = 0; i < levels.size(); ++i)
        for (int y = 0; y < levels[i]->height; ++y)
            memcpy(static_cast<uint8_t*>(p) + offsets[i] + y * pitches[i],
                   levels[i]->pixels.data() + static_cast<size_t>(y) * levels[i]->width * 4, levels[i]->width * 4);
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    for (size_t i = 0; i < levels.size(); ++i) {
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = tb;
        src.offset = offsets[i];
        src.pixels_per_row = pitches[i] / 4;
        src.rows_per_layer = levels[i]->height;
        SDL_GPUTextureRegion dst = {};
        dst.texture = t;
        dst.mip_level = static_cast<Uint32>(i);
        dst.w = levels[i]->width;
        dst.h = levels[i]->height;
        dst.d = 1;
        SDL_UploadToGPUTexture(pass, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev_, tb);  // deferred until the upload is done
    textures_.push_back(t);
    return textures_.size();
}

void SdlGpuDevice::SetStaticVertices(const game::ModelVertex* v, size_t count) {
    const uint32_t bytes = static_cast<uint32_t>(count * sizeof(game::ModelVertex));
    if (staticVb_) SDL_ReleaseGPUBuffer(dev_, staticVb_);
    staticVb_ = nullptr;
    staticCap_ = 0;
    if (!bytes) return;
    EnsureBuffer(staticVb_, staticCap_, bytes);
    if (staticVb_) Upload(nullptr, staticVb_, v, bytes, false);
}

void SdlGpuDevice::SetDynamicVertices(const game::ModelVertex* v, size_t count) {
    dynData_.assign(reinterpret_cast<const uint8_t*>(v), reinterpret_cast<const uint8_t*>(v + count));
    dynPending_ = count > 0;
}

void SdlGpuDevice::SetSpriteVertices(const game::TLVertex* v, size_t count) {
    spriteData_.assign(reinterpret_cast<const uint8_t*>(v), reinterpret_cast<const uint8_t*>(v + count));
    spritePending_ = count > 0;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::Pipeline(GpuDraw::Pipeline kind, const GpuState& s) {
    const bool sprite = kind == GpuDraw::Pipeline::Sprite;
    const uint32_t key = (sprite ? 1u : 0u) | (s.blendEnable ? 2u : 0u) | (s.blendEnable ? (s.srcBlend << 2) | (s.dstBlend << 6) : 0u) |
                         (static_cast<uint32_t>(s.zfunc) << 10) | (s.zwrite ? 1u << 14 : 0u) | (static_cast<uint32_t>(s.cull & 3) << 15);
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second;

    SDL_GPUVertexBufferDescription vb = {};
    vb.slot = 0;
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[3] = {};
    if (sprite) {
        vb.pitch = sizeof(game::TLVertex);
        attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0 };
        attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16 };  // ARGB dword: bytes B, G, R, A (swizzled in the shader)
        attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24 };
    } else {
        vb.pitch = sizeof(game::ModelVertex);
        attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };
        attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12 };
        attrs[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24 };
    }
    SDL_GPUColorTargetDescription ct = {};
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ct.blend_state.enable_blend = s.blendEnable != 0;
    ct.blend_state.src_color_blendfactor = ToBlend(s.srcBlend, false);
    ct.blend_state.dst_color_blendfactor = ToBlend(s.dstBlend, false);
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = ToBlend(s.srcBlend, true);
    ct.blend_state.dst_alpha_blendfactor = ToBlend(s.dstBlend, true);
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pi = {};
    pi.vertex_shader = sprite ? spriteVs_ : modelVs_;
    pi.fragment_shader = sprite ? spritePs_ : modelPs_;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attrs;
    pi.vertex_input_state.num_vertex_attributes = 3;
    pi.primitive_type = sprite ? SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // D3DCULL 1 none / 2 CW / 3 CCW (screen-space winding; front faces are clockwise here, as in the D3D11 backend)
    pi.rasterizer_state.cull_mode = (sprite || s.cull <= 1) ? SDL_GPU_CULLMODE_NONE
                                    : (s.cull == 2 ? SDL_GPU_CULLMODE_FRONT : SDL_GPU_CULLMODE_BACK);
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    pi.rasterizer_state.enable_depth_clip = true;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = s.zwrite != 0;
    pi.depth_stencil_state.compare_op = static_cast<SDL_GPUCompareOp>(ReversedDepthCompare(s.zfunc));
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets = 1;
    pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pi.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
    if (p) pipelines_[key] = p;
    else SDL_Log("SDL_CreateGPUGraphicsPipeline: %s", SDL_GetError());
    return p;
}

SDL_GPUSampler* SdlGpuDevice::Sampler(const GpuState& s) {
    const int key = (s.linear ? 1 : 0) | (s.addrU << 1) | (s.addrV << 4) | (s.mipmapped ? 1 << 8 : 0);
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter = si.mag_filter = s.linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    if (s.mipmapped) si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = s.mipmapped ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = ToAddress(s.addrU);
    si.address_mode_v = ToAddress(s.addrV);
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.min_lod = 0;
    si.max_lod = s.mipmapped ? 1000.f : 0.f;
    SDL_GPUSampler* smp = SDL_CreateGPUSampler(dev_, &si);
    if (smp) samplers_[key] = smp;
    return smp;
}

bool SdlGpuDevice::BeginFrame(const float clear[4]) {
    if (follow_) {
        int w, h;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if ((w != width_ || h != height_) && w > 0 && h > 0) CreateTargets(w, h);
    }
    if (!cmd_) cmd_ = SDL_AcquireGPUCommandBuffer(dev_);
    memcpy(clear_, clear, sizeof(clear_));
    needClear_ = true;
    return cmd_ != nullptr;
}

void SdlGpuDevice::Submit(const GpuDrawList& list) {
    if (!cmd_) return;
    // Pending per-frame streams first (copy pass), then one render pass for the list.
    if (dynPending_ || spritePending_) {
        const uint32_t dynBytes = static_cast<uint32_t>(dynData_.size()), sprBytes = static_cast<uint32_t>(spriteData_.size());
        if (dynPending_ && dynBytes > dynCap_) EnsureBuffer(dynVb_, dynCap_, dynBytes * 2);
        if (spritePending_ && sprBytes > spriteCap_) EnsureBuffer(spriteVb_, spriteCap_, sprBytes * 2);
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd_);
        if (dynPending_ && dynVb_) Upload(pass, dynVb_, dynData_.data(), dynBytes, true);
        SDL_EndGPUCopyPass(pass);
        // One transfer buffer: the second upload needs its own pass so the first copy has been recorded before the
        // transfer buffer is cycled (cycling keeps the earlier contents for the recorded copy).
        if (spritePending_ && spriteVb_) {
            pass = SDL_BeginGPUCopyPass(cmd_);
            Upload(pass, spriteVb_, spriteData_.data(), sprBytes, true);
            SDL_EndGPUCopyPass(pass);
        }
        dynPending_ = spritePending_ = false;
    }
    SDL_GPUColorTargetInfo ct = {};
    ct.texture = color_;
    ct.clear_color = { clear_[0], clear_[1], clear_[2], clear_[3] };
    ct.load_op = needClear_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dt = {};
    dt.texture = depth_;
    dt.clear_depth = 0.0f;
    dt.load_op = needClear_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    dt.store_op = SDL_GPU_STOREOP_STORE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    needClear_ = false;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd_, &ct, 1, &dt);
    if (!rp) return;
    const SDL_GPUViewport vp = { 0, 0, static_cast<float>(list.width), static_cast<float>(list.height), 0, 1 };
    SDL_SetGPUViewport(rp, &vp);
    SDL_GPUGraphicsPipeline* boundPipe = nullptr;
    SDL_GPUBuffer* boundVb = nullptr;
    for (const GpuDraw& d : list.draws) {
        SDL_GPUGraphicsPipeline* p = Pipeline(d.pipeline, d.state);
        SDL_GPUBuffer* vb = d.stream == GpuDraw::Stream::Static ? staticVb_ : d.stream == GpuDraw::Stream::Dynamic ? dynVb_ : spriteVb_;
        if (!p || !vb) continue;
        if (p != boundPipe) {
            SDL_BindGPUGraphicsPipeline(rp, p);
            boundPipe = p;
        }
        if (vb != boundVb) {
            const SDL_GPUBufferBinding b = { vb, 0 };
            SDL_BindGPUVertexBuffers(rp, 0, &b, 1);
            boundVb = vb;
        }
        SDL_GPUTexture* tex = d.texture && d.texture <= textures_.size() ? textures_[d.texture - 1] : white_;
        const SDL_GPUTextureSamplerBinding ts = { tex, Sampler(d.state) };
        SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
        if (d.pipeline == GpuDraw::Pipeline::Model) {
            SDL_PushGPUVertexUniformData(cmd_, 0, &list.model[d.constants], sizeof(ModelConstants));
            SDL_PushGPUFragmentUniformData(cmd_, 0, &list.model[d.constants], sizeof(ModelConstants));
        } else {
            SDL_PushGPUFragmentUniformData(cmd_, 0, &list.sprite[d.constants], sizeof(SpriteConstants));
        }
        SDL_DrawGPUPrimitives(rp, d.count, 1, d.first, 0);
    }
    SDL_EndGPURenderPass(rp);
}

bool SdlGpuDevice::ReadPixels(std::vector<uint8_t>* rgba) {
    if (!cmd_ || !readback_) return false;
    if (needClear_) Submit(GpuDrawList{ width_, height_ });  // nothing drawn: still clear
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd_);
    SDL_GPUTextureRegion src = {};
    src.texture = color_;
    src.w = width_;
    src.h = height_;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst = {};
    dst.transfer_buffer = readback_;
    dst.pixels_per_row = width_;
    dst.rows_per_layer = height_;
    SDL_DownloadFromGPUTexture(pass, &src, &dst);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_);
    cmd_ = nullptr;
    if (!fence) return false;
    SDL_WaitForGPUFences(dev_, true, &fence, 1);
    SDL_ReleaseGPUFence(dev_, fence);
    const uint8_t* p = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(dev_, readback_, false));
    if (!p) return false;
    rgba->assign(p, p + static_cast<size_t>(width_) * height_ * 4);
    SDL_UnmapGPUTransferBuffer(dev_, readback_);
    return true;
}

void SdlGpuDevice::EndFrame() {
    if (!cmd_) return;
    if (needClear_) Submit(GpuDrawList{ width_, height_ });
    if (window_) {
        // The present pass: the swap chain texture cleared black, the frame scaled into the image rectangle (the
        // viewport) with the shared present shader.
        SDL_GPUTexture* swap = nullptr;
        Uint32 sw = 0, sh = 0;
        if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd_, window_, &swap, &sw, &sh) && swap) {
            PresentParams p = present_;
            if (p.w <= 0 || p.h <= 0) {
                p.x = p.y = 0;
                p.w = static_cast<int>(sw);
                p.h = static_cast<int>(sh);
            }
            SDL_GPUColorTargetInfo ct = {};
            ct.texture = swap;
            ct.clear_color = { 0, 0, 0, 1 };
            ct.load_op = SDL_GPU_LOADOP_CLEAR;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            if (SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd_, &ct, 1, nullptr)) {
                const SDL_GPUViewport vp = { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.w), static_cast<float>(p.h), 0, 1 };
                SDL_SetGPUViewport(rp, &vp);
                SDL_BindGPUGraphicsPipeline(rp, presentPipe_);
                const SDL_GPUTextureSamplerBinding ts = { color_, presentSampler_ };
                SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
                const float cb[8] = { static_cast<float>(width_), static_cast<float>(height_), 1.0f / width_, 1.0f / height_,
                                      static_cast<float>(p.w), static_cast<float>(p.h), static_cast<float>(p.crt), 480.0f };
                SDL_PushGPUFragmentUniformData(cmd_, 0, cb, sizeof(cb));
                SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
                SDL_EndGPURenderPass(rp);
            }
        }
    }
    SDL_SubmitGPUCommandBuffer(cmd_);
    cmd_ = nullptr;
}

}  // namespace

std::unique_ptr<GpuDevice> CreateSdlGpuDevice(const GpuDeviceDesc& desc, std::string* error) {
    auto d = std::make_unique<SdlGpuDevice>();
    if (!d->Init(desc, error)) return nullptr;
    return d;
}

}  // namespace port
