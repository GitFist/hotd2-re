#include "sdl_gpu_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <chrono>
#include <map>
#include <vector>

#include "asset_store.h"
#include "hlsl_shaders.h"
#include "texture_mips.h"
#include "postprocess_shaders.h"
#include "depth_shaders.h"
#include "ao_shaders.h"
#include "hdr_color.h"
#include "motion_shaders.h"
#include "taa_shaders.h"
#include "cmaa_shaders.h"
#include "shader_cache.h"
#include "hud_composition.h"
#include "warmup_states.h"
#include "third_party/smaa/AreaTex.h"
#include "third_party/smaa/SearchTex.h"

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
        motionReady_=previousDepthReady_=false;
        taaHistoryReady_=taaResolved_=false;
    }
    void SetStaticVertices(const game::ModelVertex* v, size_t count) override;
    void SetDynamicVertices(const game::ModelVertex* v, size_t count) override;
    void SetSpriteVertices(const game::TLVertex* v, size_t count) override;
    bool BeginFrame(const float clear[4]) override;
    void Submit(const GpuDrawList& list) override;
    void EndFrame() override;
    bool ReadPixels(std::vector<uint8_t>* rgba) override;
    bool ReadPresentation(int width,int height,std::vector<uint8_t>* rgba) override;
    bool ReadLinearPresentation(int width,int height,std::vector<float>* rgba) override;
    bool ReadMotionPixels(std::vector<float>* rgba) override;
    bool NeedsShadowCasters() const override {return graphics_.shadowMaps>0||graphics_.volumetricFog>0;}
    void SetMenuOverlay(bool enabled) override {menuOverlay_=enabled;}
    bool RequiresSceneProcessing() const override{return graphics_.temporalAA||SceneEffectsEnabled(graphics_)||graphics_.outputResolutionHud||menuOverlay_;}
    bool ProcessScene(std::string* error=nullptr) override;
    bool WarmupGraphics(GraphicsWarmupStats*,std::string*) override;
    int TargetWidth() const override { return width_; }
    int TargetHeight() const override { return height_; }
    int HudTargetWidth() const override { return hudReady_?hudColor_.width:width_; }
    int HudTargetHeight() const override { return hudReady_?hudColor_.height:height_; }
    const std::string& HudCompositionError() const override { return hudError_; }
    void SetPresent(const PresentParams& p) override { present_ = p; }
    bool ResizeTarget(int w, int h) override;
    bool WindowSizePixels(int* w, int* h) const override;
    bool ConfigureGraphics(const GraphicsOptions&, std::string* error) override;
    const GraphicsOptions& EffectiveGraphics() const override { return graphics_; }
    const GraphicsCapabilities& GetGraphicsCapabilities() const override { UpdateHdrCapabilities();return capabilities_; }

private:
    bool CreateShaders(std::string* error);
    bool CreatePresentPipeline(std::string* error);
    bool CreatePostPipelines(std::string* error);
    bool CreateTargets(int w, int h, int samples = 0);
    SDL_GPUPresentMode SelectPresentMode(const GraphicsOptions&) const;
    void DrawPresent(SDL_GPUTexture* target, int x, int y, int w, int h, int crt,bool hdr=false);
    void UpdateHdrCapabilities() const;
    struct PostTarget {SDL_GPUTexture* texture=nullptr;int width=0,height=0;};
    bool EnsurePostTarget(PostTarget&,int w,int h,SDL_GPUTextureFormat format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    bool EnsureDepthTargets();
    bool EnsureMotionTargets();
    SDL_GPUGraphicsPipeline* MotionPipeline(const GpuState&);
    void DrawMotion(const GpuDrawList&);
    bool EnsureTaaTargets();
    SDL_GPUGraphicsPipeline* ReactivePipeline(GpuDraw::Pipeline,const GpuState&);
    SDL_GPUGraphicsPipeline* TaaCopyPipeline(int samples);
    void DrawReactive(const GpuDrawList&);
    bool EnsureCmaa(std::string* error=nullptr);
    SDL_GPUTexture* DrawCmaa();
    SDL_GPUGraphicsPipeline* DepthPipeline(const GpuState&);
    void DrawDepthPrepass(const GpuDrawList&);
    void DrawAmbientOcclusion(const GpuDrawList&);
    SDL_GPUGraphicsPipeline* AoPipeline();
    void DrawPost(PostTarget&,SDL_GPUGraphicsPipeline*,SDL_GPUTexture*,SDL_GPUTexture*,SDL_GPUTexture*,const float cb[8]);
    SDL_GPUTexture* PostSource(int outWidth,int outHeight,int* sourceWidth,int* sourceHeight,bool* fsr);
    SDL_GPUGraphicsPipeline* Pipeline(GpuDraw::Pipeline kind, const GpuState& s, bool coverage = false);
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
    SDL_GPUTexture* multisampleColor_ = nullptr;
    SDL_GPUTexture* postColor_ = nullptr;
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
    SDL_GPUGraphicsPipeline* presentHdrPipe_ = nullptr;
    SDL_GPUSampler* presentSampler_ = nullptr;
    SDL_GPUSampler* postPointSampler_ = nullptr;
    SDL_GPUShader* smaaPs_[3]={};
    SDL_GPUShader* fsrPs_[2]={};
    SDL_GPUGraphicsPipeline* smaaPipe_[3]={};
    SDL_GPUGraphicsPipeline* fsrPipe_[2]={};
    SDL_GPUTexture* smaaArea_=nullptr;
    SDL_GPUTexture* smaaSearch_=nullptr;
    PostTarget smaaEdge_,smaaWeight_,smaaColor_,fsrColor_,fsrSharp_,captureColor_,floatCaptureColor_;
    PostTarget opaqueDepthColor_;
    SDL_GPUTexture* opaqueDepth_=nullptr;
    SDL_GPUShader* depthVs_=nullptr;
    SDL_GPUShader* depthPs_=nullptr;
    SDL_GPUShader* aoPs_=nullptr;
    SDL_GPUGraphicsPipeline* aoPipelines_[3]={};
    std::map<int,SDL_GPUGraphicsPipeline*> depthPipelines_;
    int opaqueWidth_=0,opaqueHeight_=0;
    bool opaqueCleared_=false,opaqueReady_=false;
    PostTarget motionColor_,previousDepthColor_;
    SDL_GPUTexture* motionDepth_=nullptr;
    SDL_GPUShader* motionVs_=nullptr;
    SDL_GPUShader* motionPs_=nullptr;
    SDL_GPUBuffer* motionVb_=nullptr;
    uint32_t motionCapacity_=0;
    std::map<int,SDL_GPUGraphicsPipeline*> motionPipelines_;
    int motionWidth_=0,motionHeight_=0;
    bool motionReady_=false,motionCleared_=false,previousDepthReady_=false;
    uint64_t motionFrameId_=0;
    TemporalFrameInfo temporal_;
    PostTarget taaResult_,taaHistory_,reactive_,previousReactive_;
    SDL_GPUShader* taaPs_=nullptr;
    SDL_GPUShader* taaCopyPs_=nullptr;
    SDL_GPUShader* reactiveModelPs_=nullptr;
    SDL_GPUShader* reactiveSpritePs_=nullptr;
    SDL_GPUGraphicsPipeline* taaPipe_=nullptr;
    SDL_GPUGraphicsPipeline* taaCopyPipelines_[3]={};
    std::map<int,SDL_GPUGraphicsPipeline*> reactivePipelines_;
    bool taaHistoryReady_=false,taaResolved_=false,reactiveCleared_=false,sceneProcessed_=false;
    struct CmaaState {PostTarget output;SDL_GPUTexture* edges=nullptr;SDL_GPUTexture* heads=nullptr;SDL_GPUBuffer* buffers[5]={};} cmaa_;
    void ReleaseCmaa(CmaaState& state);
    SDL_GPUComputePipeline* cmaaCs_[5]={};
    PresentParams present_;
    GraphicsOptions graphics_;
    mutable GraphicsCapabilities capabilities_;
    SDL_GPUPresentMode presentMode_ = SDL_GPU_PRESENTMODE_VSYNC;
    SDL_GPUTexture* acquiredSwap_ = nullptr;
    Uint32 acquiredWidth_ = 0, acquiredHeight_ = 0;
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
    // Isolated directional map resources; main target sample count is unchanged.
    bool EnsureShadowMap(int quality);
    SDL_GPUGraphicsPipeline* ShadowMapPipeline(int cull);
    void DrawShadowMap(const GpuDrawList&);
    PostTarget shadowMapColor_;
    SDL_GPUTexture* shadowMapDepth_=nullptr;
    SDL_GPUShader* shadowMapVs_=nullptr;
    SDL_GPUShader* shadowMapPs_=nullptr;
    SDL_GPUGraphicsPipeline* shadowMapPipelines_[3]={};
    ShadowConstants shadowMapConstants_;
    bool ResolveTemporal(std::string* error);
    bool ProcessSceneEffects(std::string* error);
    bool EnsureHudTargets(std::string* error=nullptr);
    bool ComposeHudScene(std::string* error=nullptr);
    PostTarget hudColor_;
    SDL_GPUTexture* hudDepth_=nullptr;
    SDL_GPUGraphicsPipeline* hudCopyPipeline_=nullptr;
    bool hudReady_=false,menuOverlay_=false;
    std::string hudError_;
    bool EnsureSceneEffects(std::string* error=nullptr);
    void DrawReflectionSurface(const GpuDrawList&);
    TemporalCameraInfo sceneEffectsCamera_;
    SceneEffectsFrame sceneEffectsFrame_;
    float sceneEffectsJitter_[2]={};
    bool sceneEffectsApplied_=false,reflectionSurfaceCleared_=false;
    PostTarget sceneEffectsTargets_[2],reflectionSurface_;
    SDL_GPUGraphicsPipeline* ReflectionSurfacePipeline(const GpuState&);
    SDL_GPUShader* sceneEffectsPs_[3]={};
    SDL_GPUShader* reflectionSurfacePs_=nullptr;
    SDL_GPUShader* reflectionSurfaceVs_=nullptr;
    SDL_GPUGraphicsPipeline* sceneEffectsPipelines_[3]={};
    std::map<int,SDL_GPUGraphicsPipeline*> reflectionSurfacePipelines_;

};

SdlGpuDevice::~SdlGpuDevice() {
    if (!dev_) return;
    if (cmd_) SDL_CancelGPUCommandBuffer(cmd_);
    SDL_WaitForGPUIdle(dev_);
    if(hudColor_.texture)SDL_ReleaseGPUTexture(dev_,hudColor_.texture);
    if(hudDepth_)SDL_ReleaseGPUTexture(dev_,hudDepth_);
    if(hudCopyPipeline_)SDL_ReleaseGPUGraphicsPipeline(dev_,hudCopyPipeline_);
    for(auto& target:sceneEffectsTargets_)if(target.texture)SDL_ReleaseGPUTexture(dev_,target.texture);
    if(reflectionSurface_.texture)SDL_ReleaseGPUTexture(dev_,reflectionSurface_.texture);
    for(auto* shader:sceneEffectsPs_)if(shader)SDL_ReleaseGPUShader(dev_,shader);
    for(auto* shader:{reflectionSurfaceVs_,reflectionSurfacePs_})if(shader)SDL_ReleaseGPUShader(dev_,shader);
    for(auto* pipeline:sceneEffectsPipelines_)if(pipeline)SDL_ReleaseGPUGraphicsPipeline(dev_,pipeline);
    for(auto& item:reflectionSurfacePipelines_)SDL_ReleaseGPUGraphicsPipeline(dev_,item.second);
    if(shadowMapColor_.texture)SDL_ReleaseGPUTexture(dev_,shadowMapColor_.texture);
    if(shadowMapDepth_)SDL_ReleaseGPUTexture(dev_,shadowMapDepth_);
    for(auto* shader:{shadowMapVs_,shadowMapPs_})if(shader)SDL_ReleaseGPUShader(dev_,shader);
    for(auto* pipeline:shadowMapPipelines_)if(pipeline)SDL_ReleaseGPUGraphicsPipeline(dev_,pipeline);
    for (auto& [k, p] : pipelines_) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
    if (presentPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, presentPipe_);
    if(presentHdrPipe_)SDL_ReleaseGPUGraphicsPipeline(dev_,presentHdrPipe_);
    if (presentSampler_) SDL_ReleaseGPUSampler(dev_, presentSampler_);
    if (postPointSampler_) SDL_ReleaseGPUSampler(dev_,postPointSampler_);
    for(auto* p:smaaPipe_)if(p)SDL_ReleaseGPUGraphicsPipeline(dev_,p);
    for(auto* p:fsrPipe_)if(p)SDL_ReleaseGPUGraphicsPipeline(dev_,p);
    for(auto* s:smaaPs_)if(s)SDL_ReleaseGPUShader(dev_,s);
    for(auto* s:fsrPs_)if(s)SDL_ReleaseGPUShader(dev_,s);
    for(auto* s:{depthVs_,depthPs_})if(s)SDL_ReleaseGPUShader(dev_,s);
    if(aoPs_)SDL_ReleaseGPUShader(dev_,aoPs_);
    for(auto* p:aoPipelines_)if(p)SDL_ReleaseGPUGraphicsPipeline(dev_,p);
    for(auto& [k,p]:depthPipelines_)SDL_ReleaseGPUGraphicsPipeline(dev_,p);
    for(auto& [k,p]:motionPipelines_)SDL_ReleaseGPUGraphicsPipeline(dev_,p);
    for(auto* s:{motionVs_,motionPs_})if(s)SDL_ReleaseGPUShader(dev_,s);
    for(auto* t:{motionColor_.texture,previousDepthColor_.texture,motionDepth_})if(t)SDL_ReleaseGPUTexture(dev_,t);
    if(motionVb_)SDL_ReleaseGPUBuffer(dev_,motionVb_);
    for(auto* texture:{taaResult_.texture,taaHistory_.texture,reactive_.texture,previousReactive_.texture})if(texture)SDL_ReleaseGPUTexture(dev_,texture);
    for(auto* shader:{taaPs_,taaCopyPs_,reactiveModelPs_,reactiveSpritePs_})if(shader)SDL_ReleaseGPUShader(dev_,shader);
    if(taaPipe_)SDL_ReleaseGPUGraphicsPipeline(dev_,taaPipe_);
    for(auto* pipeline:taaCopyPipelines_)if(pipeline)SDL_ReleaseGPUGraphicsPipeline(dev_,pipeline);
    for(auto& [key,pipeline]:reactivePipelines_)SDL_ReleaseGPUGraphicsPipeline(dev_,pipeline);
    ReleaseCmaa(cmaa_);for(auto* pipeline:cmaaCs_)if(pipeline)SDL_ReleaseGPUComputePipeline(dev_,pipeline);
    for(auto* t:{smaaEdge_.texture,smaaWeight_.texture,smaaColor_.texture,fsrColor_.texture,fsrSharp_.texture,captureColor_.texture,floatCaptureColor_.texture,smaaArea_,smaaSearch_,opaqueDepthColor_.texture,opaqueDepth_})if(t)SDL_ReleaseGPUTexture(dev_,t);
    for (auto& [k, s] : samplers_) SDL_ReleaseGPUSampler(dev_, s);
    for (SDL_GPUTexture* t : textures_) SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUTexture* t : { color_, multisampleColor_, postColor_, depth_, white_ })
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
    graphics_.syncInterval = sync_;
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
        capabilities_.tearing = SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE);
        capabilities_.lowLatency = true; // SDL's allowed-frames API is a required device operation.
        SDL_GPUPresentMode mode = SelectPresentMode(graphics_);
        if (!SDL_SetGPUSwapchainParameters(dev_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
            if (error) *error = std::string("SDL swap chain: ") + SDL_GetError();
            return false;
        }
        if (mode == SDL_GPU_PRESENTMODE_VSYNC) graphics_.syncInterval = sync_ = 1;
        presentMode_ = mode;
    }
    int w = desc.width, h = desc.height;
    if (follow_) SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (!CreateShaders(error)) return false;
    if (!CreatePresentPipeline(error)) return false;
    if (!CreateTargets(w, h)) {
        if (error) *error = std::string("SDL_GPU targets: ") + SDL_GetError();
        return false;
    }
    auto supports = [this](SDL_GPUSampleCount samples) {
        return SDL_GPUTextureSupportsSampleCount(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, samples) &&
               SDL_GPUTextureSupportsSampleCount(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, samples);
    };
    capabilities_.msaa2 = supports(SDL_GPU_SAMPLECOUNT_2); capabilities_.msaa4 = supports(SDL_GPU_SAMPLECOUNT_4);
    capabilities_.maxAnisotropy = 16;
    capabilities_.temporalAA=true;
    capabilities_.pipelineWarmup=true;
    capabilities_.outputResolutionHud=true;
    capabilities_.smaa=capabilities_.spatialFSR1=capabilities_.softParticles=capabilities_.materialMaps=capabilities_.ambientOcclusion=capabilities_.shadowMaps=true;capabilities_.sceneEffects=true;
    capabilities_.cmaa2=SDL_GPUTextureSupportsFormat(dev_,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE)&&
        SDL_GPUTextureSupportsFormat(dev_,SDL_GPU_TEXTUREFORMAT_R8_UINT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE)&&
        SDL_GPUTextureSupportsFormat(dev_,SDL_GPU_TEXTUREFORMAT_R32_UINT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
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
    return CreatePostPipelines(error);
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
        { "SAMP_REG", "register(s0, space2)" }, {"TEX1_REG","register(t1, space2)"},{"TEX2_REG","register(t2, space2)"},
        {"POINT_SAMP_REG","register(s1, space2)"},{"DEPTH_TEX_REG","register(t1, space2)"},{"DEPTH_SAMP_REG","register(s1, space2)"},
        {"MATERIAL_TEX_REG","register(t2, space2)"},{"MATERIAL_SAMP_REG","register(s2, space2)"},
        {"PREVIOUS_DEPTH_REG","register(t1, space2)"},{"TEX3_REG","register(t3, space2)"},{"TEX4_REG","register(t4, space2)"} };
    const auto smaa=SmaaShaderSource(),fsr=FsrShaderSource();
    auto aoMacros=ps;aoMacros.push_back({"AO_MODULATE_ONLY","1"});
    const auto reactiveModel=ReactiveModelShaderSource(true),reactiveSprite=ReactiveSpriteShaderSource();
    auto shadowVs=vs;shadowVs.push_back({"SHADOW_CB_REG","register(b1, space1)"});
    auto modelPs=ps;modelPs.push_back({"RASTER_SHADOW_MAP","1"});
    modelPs.push_back({"SHADOW_CB_REG","register(b1, space3)"});modelPs.push_back({"SHADOW_TEX_REG","register(t3, space2)"});modelPs.push_back({"SHADOW_SAMP_REG","register(s3, space2)"});
    Def defs[] = {
        {kSceneEffectsShaderHlsl,sizeof(kSceneEffectsShaderHlsl)-1,"reflectionps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,4,1,&sceneEffectsPs_[0]},
        {kSceneEffectsShaderHlsl,sizeof(kSceneEffectsShaderHlsl)-1,"fogps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,4,1,&sceneEffectsPs_[1]},
        {kSceneEffectsShaderHlsl,sizeof(kSceneEffectsShaderHlsl)-1,"dofps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,4,1,&sceneEffectsPs_[2]},
        {kReflectionSurfaceShaderHlsl,sizeof(kReflectionSurfaceShaderHlsl)-1,"vsmain","vs_5_1",vs,SDL_GPU_SHADERSTAGE_VERTEX,0,1,&reflectionSurfaceVs_},
        {kReflectionSurfaceShaderHlsl,sizeof(kReflectionSurfaceShaderHlsl)-1,"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,2,1,&reflectionSurfacePs_},

        { kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "vsmain", "vs_5_1", vs, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, &modelVs_ },
        { kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "psmain", "ps_5_1", modelPs, SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 2, &modelPs_ },
        { kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "vsmain", "vs_5_1", spriteVs, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, &spriteVs_ },
        { kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "psmain", "ps_5_1", ps, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, &spritePs_ },
        { kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "vsmain", "vs_5_1", vs, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, &presentVs_ },
        { kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "psmain", "ps_5_1", ps, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, &presentPs_ },
        {smaa.data(),smaa.size(),"edgeps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,3,1,&smaaPs_[0]},
        {smaa.data(),smaa.size(),"weightps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,3,1,&smaaPs_[1]},
        {smaa.data(),smaa.size(),"blendps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,3,1,&smaaPs_[2]},
        {fsr.data(),fsr.size(),"easups","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,3,1,&fsrPs_[0]},
        {fsr.data(),fsr.size(),"rcasps","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,3,1,&fsrPs_[1]},
        {kDepthShaderHlsl,sizeof(kDepthShaderHlsl)-1,"vsmain","vs_5_1",vs,SDL_GPU_SHADERSTAGE_VERTEX,0,1,&depthVs_},
        {kDepthShaderHlsl,sizeof(kDepthShaderHlsl)-1,"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,1,1,&depthPs_},
        {kAoShaderHlsl,sizeof(kAoShaderHlsl)-1,"psmain","ps_5_1",aoMacros,SDL_GPU_SHADERSTAGE_FRAGMENT,2,1,&aoPs_},
        {kMotionShaderHlsl,sizeof(kMotionShaderHlsl)-1,"vsmain","vs_5_1",vs,SDL_GPU_SHADERSTAGE_VERTEX,0,1,&motionVs_},
        {kMotionShaderHlsl,sizeof(kMotionShaderHlsl)-1,"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,2,1,&motionPs_},
        {kTaaShaderHlsl,sizeof(kTaaShaderHlsl)-1,"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,5,1,&taaPs_},
        {kTaaCopyShaderHlsl,sizeof(kTaaCopyShaderHlsl)-1,"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,1,0,&taaCopyPs_},
        {reactiveModel.data(),reactiveModel.size(),"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,1,1,&reactiveModelPs_},
        {reactiveSprite.data(),reactiveSprite.size(),"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,1,1,&reactiveSpritePs_},
        {kShadowMapShaderHlsl,sizeof(kShadowMapShaderHlsl)-1,"vsmain","vs_5_1",shadowVs,SDL_GPU_SHADERSTAGE_VERTEX,0,2,&shadowMapVs_},
        {kShadowMapShaderHlsl,sizeof(kShadowMapShaderHlsl)-1,"psmain","ps_5_1",ps,SDL_GPU_SHADERSTAGE_FRAGMENT,0,0,&shadowMapPs_},
    };
    for (Def& d : defs) {
        d.macros.push_back({ nullptr, nullptr });
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        if (FAILED(CompileShaderCached(d.src, d.size, d.entry, d.macros.data(), nullptr, d.entry, d.target, 0, 0, &blob, &err))) {
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
    ct.format = window_ ? SDL_GetGPUSwapchainTextureFormat(dev_, window_) : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
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
    ct.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if(SDL_GPUTextureSupportsFormat(dev_,ct.format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))
        presentHdrPipe_=SDL_CreateGPUGraphicsPipeline(dev_,&pi);
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

bool SdlGpuDevice::CreatePostPipelines(std::string* error) {
    for(int i=0;i<5;++i) {
        SDL_GPUColorTargetDescription target{};target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=presentVs_;info.fragment_shader=i<3?smaaPs_[i]:fsrPs_[i-3];
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
        auto* pipeline=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(!pipeline){if(error)*error=SDL_GetError();return false;}
        if(i<3)smaaPipe_[i]=pipeline;else fsrPipe_[i-3]=pipeline;
    }
    {SDL_GPUColorTargetDescription target{};target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=presentVs_;info.fragment_shader=taaPs_;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
        taaPipe_=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(!taaPipe_){if(error)*error=SDL_GetError();return false;}}
    SDL_GPUSamplerCreateInfo sampler{};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    postPointSampler_=SDL_CreateGPUSampler(dev_,&sampler);if(!postPointSampler_)return false;
    auto lookup=[this](int w,int h,const uint8_t* bytes,int channels,SDL_GPUTexture*& out) {
        RgbaImage image;image.width=w;image.height=h;image.pixels.resize(static_cast<size_t>(w)*h*4,0);
        for(int p=0;p<w*h;++p){for(int c=0;c<channels;++c)image.pixels[p*4+c]=bytes[p*channels+c];image.pixels[p*4+3]=255;}
        const auto handle=CreateTexture(image);if(!handle)return false;out=textures_.back();textures_.pop_back();return true;
    };
    return lookup(AREATEX_WIDTH,AREATEX_HEIGHT,areaTexBytes,2,smaaArea_)&&lookup(SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,searchTexBytes,1,smaaSearch_);
}

bool SdlGpuDevice::EnsurePostTarget(PostTarget& target,int w,int h,SDL_GPUTextureFormat format) {
    if(target.texture&&target.width==w&&target.height==h)return true;
    SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=format;
    info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=w;info.height=h;
    info.layer_count_or_depth=info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
    auto* next=SDL_CreateGPUTexture(dev_,&info);if(!next)return false;
    if(target.texture)SDL_ReleaseGPUTexture(dev_,target.texture);
    target.texture=next;target.width=w;target.height=h;return true;
}

bool SdlGpuDevice::EnsureDepthTargets() {
    if(opaqueDepth_&&opaqueWidth_==width_&&opaqueHeight_==height_)return true;
    if(!EnsurePostTarget(opaqueDepthColor_,width_,height_,SDL_GPU_TEXTUREFORMAT_R32_FLOAT))return false;
    SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;info.width=width_;info.height=height_;
    info.layer_count_or_depth=info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
    auto* texture=SDL_CreateGPUTexture(dev_,&info);if(!texture)return false;
    if(opaqueDepth_)SDL_ReleaseGPUTexture(dev_,opaqueDepth_);opaqueDepth_=texture;
    opaqueWidth_=width_;opaqueHeight_=height_;opaqueCleared_=opaqueReady_=false;return true;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::DepthPipeline(const GpuState& state) {
    const int key=state.zfunc|(state.cull<<4);auto found=depthPipelines_.find(key);if(found!=depthPipelines_.end())return found->second;
    SDL_GPUVertexBufferDescription buffer{};buffer.slot=0;buffer.pitch=sizeof(game::ModelVertex);buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[]={{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,12},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,24}};
    SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=depthVs_;info.fragment_shader=depthPs_;
    info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;
    info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=3;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
    info.rasterizer_state.cull_mode=state.cull<=1?SDL_GPU_CULLMODE_NONE:state.cull==2?SDL_GPU_CULLMODE_FRONT:SDL_GPU_CULLMODE_BACK;
    info.depth_stencil_state.enable_depth_test=info.depth_stencil_state.enable_depth_write=true;
    info.depth_stencil_state.compare_op=static_cast<SDL_GPUCompareOp>(ReversedDepthCompare(state.zfunc));
    info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;
    info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
    auto* pipeline=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(pipeline)depthPipelines_[key]=pipeline;return pipeline;
}

void SdlGpuDevice::DrawDepthPrepass(const GpuDrawList& list) {
    if((!graphics_.softParticles&&!graphics_.ambientOcclusion&&!SceneEffectsEnabled(graphics_)&&!list.temporal.enabled)||!list.opaqueDrawCount||!EnsureDepthTargets())return;
    SDL_GPUColorTargetInfo color{};color.texture=opaqueDepthColor_.texture;color.load_op=opaqueCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};depth.texture=opaqueDepth_;depth.clear_depth=0;
    depth.load_op=opaqueCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,&depth);if(!pass)return;opaqueCleared_=true;
    const SDL_GPUViewport viewport={0,0,static_cast<float>(list.width),static_cast<float>(list.height),0,1};SDL_SetGPUViewport(pass,&viewport);
    for(size_t i=0;i<list.draws.size()&&i<list.opaqueDrawCount;++i) {
        const auto& draw=list.draws[i];const auto& state=draw.state;if(draw.pipeline!=GpuDraw::Pipeline::Model||state.blendEnable||!state.zwrite)continue;
        auto* pipeline=DepthPipeline(state);auto* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_:dynVb_;if(!pipeline||!vertices)continue;
        SDL_BindGPUGraphicsPipeline(pass,pipeline);const SDL_GPUBufferBinding buffer={vertices,0};SDL_BindGPUVertexBuffers(pass,0,&buffer,1);
        auto* texture=draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1]:white_;
        const SDL_GPUTextureSamplerBinding sampler={texture,Sampler(state)};SDL_BindGPUFragmentSamplers(pass,0,&sampler,1);
        SDL_PushGPUVertexUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));SDL_PushGPUFragmentUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));
        SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);opaqueReady_=true;
    }
    SDL_EndGPURenderPass(pass);
}

bool SdlGpuDevice::EnsureMotionTargets() {
    if(motionDepth_&&motionWidth_==width_&&motionHeight_==height_)return true;
    if(!EnsurePostTarget(motionColor_,width_,height_,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT)||
       !EnsurePostTarget(previousDepthColor_,width_,height_,SDL_GPU_TEXTUREFORMAT_R32_FLOAT))return false;
    SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;info.width=width_;info.height=height_;
    info.layer_count_or_depth=info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
    auto* next=SDL_CreateGPUTexture(dev_,&info);if(!next)return false;
    if(motionDepth_)SDL_ReleaseGPUTexture(dev_,motionDepth_);motionDepth_=next;
    motionWidth_=width_;motionHeight_=height_;motionReady_=previousDepthReady_=false;return true;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::MotionPipeline(const GpuState& state) {
    const int key=state.zfunc|(state.cull<<4);auto found=motionPipelines_.find(key);if(found!=motionPipelines_.end())return found->second;
    SDL_GPUVertexBufferDescription buffer{};buffer.slot=0;buffer.pitch=sizeof(MotionVertex);buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[]={{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,12},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,24}};
    SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=motionVs_;info.fragment_shader=motionPs_;
    info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;
    info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=3;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
    info.rasterizer_state.cull_mode=state.cull<=1?SDL_GPU_CULLMODE_NONE:state.cull==2?SDL_GPU_CULLMODE_FRONT:SDL_GPU_CULLMODE_BACK;
    info.depth_stencil_state.enable_depth_test=info.depth_stencil_state.enable_depth_write=true;
    info.depth_stencil_state.compare_op=static_cast<SDL_GPUCompareOp>(ReversedDepthCompare(state.zfunc));
    info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;
    info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
    auto* next=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(next)motionPipelines_[key]=next;return next;
}

void SdlGpuDevice::DrawMotion(const GpuDrawList& list) {
    if(!list.temporal.enabled||!EnsureMotionTargets())return;
    if(list.temporal.reset||(!motionCleared_&&list.temporal.frameId!=motionFrameId_+1))previousDepthReady_=false;
    const size_t bytes=list.motionVertices.size()*sizeof(MotionVertex);if(bytes>UINT32_MAX)return;
    if(bytes>motionCapacity_)EnsureBuffer(motionVb_,motionCapacity_,static_cast<uint32_t>(bytes));
    if(bytes&&!motionVb_)return;
    if(bytes){auto* copy=SDL_BeginGPUCopyPass(cmd_);const bool uploaded=Upload(copy,motionVb_,list.motionVertices.data(),static_cast<uint32_t>(bytes),true);SDL_EndGPUCopyPass(copy);if(!uploaded)return;}
    SDL_GPUColorTargetInfo color{};color.texture=motionColor_.texture;color.load_op=motionCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};depth.texture=motionDepth_;depth.clear_depth=0;
    depth.load_op=motionCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,&depth);if(!pass)return;motionCleared_=true;
    const SDL_GPUViewport viewport={0,0,static_cast<float>(list.width),static_cast<float>(list.height),0,1};SDL_SetGPUViewport(pass,&viewport);
    const SDL_GPUBufferBinding vertices={motionVb_,0};if(motionVb_)SDL_BindGPUVertexBuffers(pass,0,&vertices,1);
    for(const auto& draw:list.motionDraws) {
        if(draw.constants>=list.motionConstants.size()||static_cast<size_t>(draw.first)+draw.count>list.motionVertices.size())continue;
        auto* pipeline=MotionPipeline(draw.state);if(!pipeline)continue;
        auto constants=list.motionConstants[draw.constants];constants.target[3]=previousDepthReady_?1.f:0.f;
        SDL_BindGPUGraphicsPipeline(pass,pipeline);
        SDL_PushGPUVertexUniformData(cmd_,0,&constants,sizeof(constants));SDL_PushGPUFragmentUniformData(cmd_,0,&constants,sizeof(constants));
        auto* texture=draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1]:white_;
        const SDL_GPUTextureSamplerBinding textures[]={{texture,Sampler(draw.state)},{previousDepthReady_?previousDepthColor_.texture:white_,postPointSampler_}};
        SDL_BindGPUFragmentSamplers(pass,0,textures,2);SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);
    }
    SDL_EndGPURenderPass(pass);motionFrameId_=list.temporal.frameId;motionReady_=true;temporal_=list.temporal;
}

bool SdlGpuDevice::EnsureTaaTargets() {
    return EnsurePostTarget(taaResult_,width_,height_)&&EnsurePostTarget(taaHistory_,width_,height_)&&
        EnsurePostTarget(reactive_,width_,height_,SDL_GPU_TEXTUREFORMAT_R8_UNORM)&&EnsurePostTarget(previousReactive_,width_,height_,SDL_GPU_TEXTUREFORMAT_R8_UNORM);
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::ReactivePipeline(GpuDraw::Pipeline kind,const GpuState& state) {
    const bool sprite=kind==GpuDraw::Pipeline::Sprite;const int key=state.zfunc|(state.cull<<4)|(sprite?1<<8:0);
    auto found=reactivePipelines_.find(key);if(found!=reactivePipelines_.end())return found->second;
    SDL_GPUVertexBufferDescription buffer{};buffer.slot=0;buffer.pitch=sprite?sizeof(game::TLVertex):sizeof(game::ModelVertex);buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]={{0,0,sprite?SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4:SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},
        {1,0,sprite?SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM:SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,sprite?16u:12u},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,24}};
    SDL_GPUColorTargetDescription target{};target.format=SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=sprite?spriteVs_:depthVs_;info.fragment_shader=sprite?reactiveSpritePs_:reactiveModelPs_;
    info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;
    info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=3;
    info.primitive_type=sprite?SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP:SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
    info.rasterizer_state.cull_mode=sprite||state.cull<=1?SDL_GPU_CULLMODE_NONE:state.cull==2?SDL_GPU_CULLMODE_FRONT:SDL_GPU_CULLMODE_BACK;
    info.depth_stencil_state.enable_depth_test=true;info.depth_stencil_state.enable_depth_write=false;
    info.depth_stencil_state.compare_op=static_cast<SDL_GPUCompareOp>(ReversedDepthCompare(state.zfunc));
    info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
    info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
    auto* pipeline=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(pipeline)reactivePipelines_[key]=pipeline;return pipeline;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::TaaCopyPipeline(int samples) {
    const int index=samples==4?2:samples==2?1:0;if(taaCopyPipelines_[index])return taaCopyPipelines_[index];
    SDL_GPUColorTargetDescription target{};target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=presentVs_;info.fragment_shader=taaCopyPs_;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=index==2?SDL_GPU_SAMPLECOUNT_4:index==1?SDL_GPU_SAMPLECOUNT_2:SDL_GPU_SAMPLECOUNT_1;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
    taaCopyPipelines_[index]=SDL_CreateGPUGraphicsPipeline(dev_,&info);return taaCopyPipelines_[index];
}

void SdlGpuDevice::DrawReactive(const GpuDrawList& list) {
    if(!graphics_.temporalAA||sceneProcessed_||!EnsureTaaTargets()||!EnsureDepthTargets())return;
    SDL_GPUColorTargetInfo color{};color.texture=reactive_.texture;color.load_op=reactiveCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};depth.texture=opaqueDepth_;depth.load_op=opaqueCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,&depth);if(!pass)return;reactiveCleared_=true;
    const SDL_GPUViewport viewport={0,0,static_cast<float>(list.width),static_cast<float>(list.height),0,1};SDL_SetGPUViewport(pass,&viewport);
    for(const auto& draw:list.draws){if(!draw.reactive||draw.hud)continue;
        auto* pipeline=ReactivePipeline(draw.pipeline,draw.state);auto* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_:draw.stream==GpuDraw::Stream::Dynamic?dynVb_:spriteVb_;if(!pipeline||!vertices)continue;
        SDL_BindGPUGraphicsPipeline(pass,pipeline);const SDL_GPUBufferBinding buffer={vertices,0};SDL_BindGPUVertexBuffers(pass,0,&buffer,1);
        if(draw.pipeline==GpuDraw::Pipeline::Model){SDL_PushGPUVertexUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));SDL_PushGPUFragmentUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));}
        else SDL_PushGPUFragmentUniformData(cmd_,0,&list.sprite[draw.constants],sizeof(SpriteConstants));
        const SDL_GPUTextureSamplerBinding sampler={draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1]:white_,Sampler(draw.state)};
        SDL_BindGPUFragmentSamplers(pass,0,&sampler,1);SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);
    }
    SDL_EndGPURenderPass(pass);
}

bool SdlGpuDevice::ResolveTemporal(std::string* error) {
    if(!graphics_.temporalAA||sceneProcessed_)return true;
    if(!cmd_||!motionReady_||!temporal_.enabled||!reactiveCleared_) {
        taaHistoryReady_=false;if(error)*error="Temporal AA requires current geometry motion and reactive coverage";return false;
    }
    auto* copyPipeline=TaaCopyPipeline(graphics_.msaa);if(!copyPipeline){if(error)*error=SDL_GetError();return false;}
    SDL_GPUColorTargetInfo target{};target.texture=taaResult_.texture;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&target,1,nullptr);if(!pass)return false;
    const SDL_GPUViewport viewport={0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};SDL_SetGPUViewport(pass,&viewport);SDL_BindGPUGraphicsPipeline(pass,taaPipe_);
    TaaConstants constants{{static_cast<float>(width_),static_cast<float>(height_),1.f/width_,1.f/height_},
        {taaHistoryReady_&&previousDepthReady_&&!temporal_.reset?1.f:0.f,temporal_.jitterPixels[0],temporal_.jitterPixels[1],.9f}};
    const SDL_GPUTextureSamplerBinding textures[]={{color_,presentSampler_},{taaHistory_.texture,presentSampler_},{motionColor_.texture,postPointSampler_},{reactive_.texture,postPointSampler_},{previousReactive_.texture,postPointSampler_}};
    SDL_BindGPUFragmentSamplers(pass,0,textures,5);SDL_PushGPUFragmentUniformData(cmd_,0,&constants,sizeof(constants));SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    target.texture=multisampleColor_?multisampleColor_:color_;target.load_op=SDL_GPU_LOADOP_LOAD;
    target.store_op=multisampleColor_?SDL_GPU_STOREOP_RESOLVE_AND_STORE:SDL_GPU_STOREOP_STORE;target.resolve_texture=multisampleColor_?color_:nullptr;
    pass=SDL_BeginGPURenderPass(cmd_,&target,1,nullptr);if(!pass)return false;
    SDL_SetGPUViewport(pass,&viewport);SDL_BindGPUGraphicsPipeline(pass,copyPipeline);
    const SDL_GPUTextureSamplerBinding resolved={taaResult_.texture,presentSampler_};SDL_BindGPUFragmentSamplers(pass,0,&resolved,1);SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    taaResolved_=sceneProcessed_=true;return true;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::AoPipeline() {
    const int index=graphics_.msaa==4?2:graphics_.msaa==2?1:0;if(aoPipelines_[index])return aoPipelines_[index];
    SDL_GPUColorTargetDescription target{};target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    target.blend_state.enable_blend=true;target.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_DST_COLOR;
    target.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;target.blend_state.color_blend_op=SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_DST_ALPHA;target.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;target.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=presentVs_;info.fragment_shader=aoPs_;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count=index==2?SDL_GPU_SAMPLECOUNT_4:index==1?SDL_GPU_SAMPLECOUNT_2:SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
    aoPipelines_[index]=SDL_CreateGPUGraphicsPipeline(dev_,&info);return aoPipelines_[index];
}

void SdlGpuDevice::DrawAmbientOcclusion(const GpuDrawList& list) {
    if(!graphics_.ambientOcclusion||!opaqueReady_||!list.opaqueDrawCount)return;
    AoConstants constants{};bool projection=false;
    for(size_t i=0;i<list.draws.size()&&i<list.opaqueDrawCount;++i) {
        const auto& draw=list.draws[i];if(draw.pipeline!=GpuDraw::Pipeline::Model)continue;const auto& model=list.model[draw.constants];
        if(model.localCount[2]>.5f&&MakeAoConstants(width_,height_,model.projection,graphics_.ambientOcclusion,&constants)){projection=true;break;}
    }
    if(!projection)return;auto* pipeline=AoPipeline();if(!pipeline)return;
    SDL_GPUColorTargetInfo color{};color.texture=multisampleColor_?multisampleColor_:color_;color.load_op=SDL_GPU_LOADOP_LOAD;
    color.store_op=multisampleColor_?SDL_GPU_STOREOP_RESOLVE_AND_STORE:SDL_GPU_STOREOP_STORE;color.resolve_texture=multisampleColor_?color_:nullptr;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,nullptr);if(!pass)return;
    const SDL_GPUViewport viewport={0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};SDL_SetGPUViewport(pass,&viewport);SDL_BindGPUGraphicsPipeline(pass,pipeline);
    const SDL_GPUTextureSamplerBinding textures[]={{white_,postPointSampler_},{opaqueDepthColor_.texture,postPointSampler_}};
    SDL_BindGPUFragmentSamplers(pass,0,textures,2);SDL_PushGPUFragmentUniformData(cmd_,0,&constants,sizeof(constants));
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
}

void SdlGpuDevice::DrawPost(PostTarget& target,SDL_GPUGraphicsPipeline* pipeline,SDL_GPUTexture* a,
                          SDL_GPUTexture* b,SDL_GPUTexture* c,const float constants[8]) {
    SDL_GPUColorTargetInfo color{};color.texture=target.texture;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    if(auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,nullptr)) {
        const SDL_GPUViewport vp={0,0,static_cast<float>(target.width),static_cast<float>(target.height),0,1};
        SDL_SetGPUViewport(pass,&vp);SDL_BindGPUGraphicsPipeline(pass,pipeline);
        const SDL_GPUTextureSamplerBinding textures[]={{a,presentSampler_},{b,postPointSampler_},{c,presentSampler_}};
        SDL_BindGPUFragmentSamplers(pass,0,textures,3);SDL_PushGPUFragmentUniformData(cmd_,0,constants,32);
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    }
}

void SdlGpuDevice::ReleaseCmaa(CmaaState& state) {
    for(auto* texture:{state.output.texture,state.edges,state.heads})if(texture)SDL_ReleaseGPUTexture(dev_,texture);
    for(auto* buffer:state.buffers)if(buffer)SDL_ReleaseGPUBuffer(dev_,buffer);state={};
}

bool SdlGpuDevice::EnsureCmaa(std::string* error) {
#ifdef _WIN32
    if(!cmaaCs_[4]) {
        const auto source=CmaaShaderSource(true);
        const char* entries[]={"ResetCS","EdgesColor2x2CS","ComputeDispatchArgsCS","ProcessCandidatesCS","DeferredColorApply2x2CS"};
        const uint32_t threads[5][3]={{16,1,1},{16,16,1},{1,1,1},{128,1,1},{4,32,1}};
        SDL_GPUComputePipeline* pipelines[5]={};bool success=true;
        for(int index=0;index<5&&success;++index){ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;
            const HRESULT compiled=CompileShaderCached(source.data(),source.size(),"Intel CMAA2",nullptr,nullptr,entries[index],"cs_5_1",0,0,&blob,&errors);
            if(SUCCEEDED(compiled)){
                SDL_GPUComputePipelineCreateInfo info{};info.code=static_cast<const Uint8*>(blob->GetBufferPointer());info.code_size=blob->GetBufferSize();info.entrypoint=entries[index];info.format=SDL_GPU_SHADERFORMAT_DXBC;
                info.num_samplers=1;info.num_readwrite_storage_textures=3;info.num_readwrite_storage_buffers=5;
                info.threadcount_x=threads[index][0];info.threadcount_y=threads[index][1];info.threadcount_z=threads[index][2];
                pipelines[index]=SDL_CreateGPUComputePipeline(dev_,&info);success=pipelines[index]!=nullptr;
            }else success=false;
            if(!success&&error)*error=errors?static_cast<const char*>(errors->GetBufferPointer()):SDL_GetError();
            if(errors)errors->Release();if(blob)blob->Release();
        }
        if(!success){for(auto* pipeline:pipelines)if(pipeline)SDL_ReleaseGPUComputePipeline(dev_,pipeline);return false;}
        for(int index=0;index<5;++index)cmaaCs_[index]=pipelines[index];
    }
#else
    if(error)*error="CMAA2 requires a supported compute shader compiler";return false;
#endif
    if(cmaa_.output.texture&&cmaa_.output.width==width_&&cmaa_.output.height==height_)return true;
    const uint64_t pixels=static_cast<uint64_t>(width_)*height_;
    // SDL's portable path launches bounded work instead of binding a buffer for
    // simultaneous UAV writes and indirect dispatch reads. Each shader retains
    // Intel's candidate-count guards; D3D11 uses the original indirect calls.
    if(pixels>65535u*128u){if(error)*error="CMAA2 portable dispatch supports up to 8.3 million internal pixels";return false;}
    CmaaState next;
    auto texture=[&](int width,int height,SDL_GPUTextureFormat format,SDL_GPUTextureUsageFlags usage){SDL_GPUTextureCreateInfo info{};
        info.type=SDL_GPU_TEXTURETYPE_2D;info.format=format;info.usage=usage;info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;return SDL_CreateGPUTexture(dev_,&info);};
    next.output.texture=texture(width_,height_,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
    next.edges=texture((width_+1)/2,height_,SDL_GPU_TEXTUREFORMAT_R8_UINT,SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
    next.heads=texture((width_+1)/2,(height_+1)/2,SDL_GPU_TEXTUREFORMAT_R32_UINT,SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
    const Uint32 quads=static_cast<Uint32>((width_+1)/2)*((height_+1)/2);
    const uint32_t bytes[]={static_cast<uint32_t>(pixels*4),quads*4,static_cast<uint32_t>(pixels*8),64,16};
    bool success=next.output.texture&&next.edges&&next.heads;
    for(int index=0;index<5&&success;++index){SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;info.size=bytes[index];next.buffers[index]=SDL_CreateGPUBuffer(dev_,&info);success=next.buffers[index]!=nullptr;}
    if(!success){ReleaseCmaa(next);if(error)*error=SDL_GetError();return false;}
    next.output.width=width_;next.output.height=height_;ReleaseCmaa(cmaa_);cmaa_=next;return true;
}

SDL_GPUTexture* SdlGpuDevice::DrawCmaa() {
    SDL_GPUColorTargetInfo output{};output.texture=cmaa_.output.texture;output.load_op=SDL_GPU_LOADOP_DONT_CARE;output.store_op=SDL_GPU_STOREOP_STORE;
    auto* copy=SDL_BeginGPURenderPass(cmd_,&output,1,nullptr);
    if(!copy)return color_;
    const SDL_GPUViewport viewport={0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};SDL_SetGPUViewport(copy,&viewport);
    auto* pipeline=TaaCopyPipeline(1);if(!pipeline){SDL_EndGPURenderPass(copy);return color_;}
    SDL_BindGPUGraphicsPipeline(copy,pipeline);const SDL_GPUTextureSamplerBinding source={color_,presentSampler_};SDL_BindGPUFragmentSamplers(copy,0,&source,1);SDL_DrawGPUPrimitives(copy,3,1,0,0);SDL_EndGPURenderPass(copy);
    const SDL_GPUStorageTextureReadWriteBinding textures[]={{cmaa_.output.texture,0,0,false},{cmaa_.edges,0,0,false},{cmaa_.heads,0,0,false}};
    SDL_GPUStorageBufferReadWriteBinding buffers[5];for(int index=0;index<5;++index)buffers[index]={cmaa_.buffers[index],false};
    auto dispatch=[&](int index,Uint32 x,Uint32 y){auto* pass=SDL_BeginGPUComputePass(cmd_,textures,3,buffers,5);if(!pass)return;
        SDL_BindGPUComputePipeline(pass,cmaaCs_[index]);SDL_BindGPUComputeSamplers(pass,0,&source,1);SDL_DispatchGPUCompute(pass,x,y,1);SDL_EndGPUComputePass(pass);};
    dispatch(0,1,1);dispatch(1,(width_+27)/28,(height_+27)/28);dispatch(2,2,1);
    const Uint32 pixels=static_cast<Uint32>(width_)*height_;
    const Uint32 quads=static_cast<Uint32>((width_+1)/2)*((height_+1)/2);
    dispatch(3,(pixels+127)/128,1);dispatch(2,1,2);dispatch(4,1,(quads+31)/32);
    return cmaa_.output.texture;
}

SDL_GPUTexture* SdlGpuDevice::PostSource(int outW,int outH,int* w,int* h,bool* fsr) {
    if(hudReady_){*w=hudColor_.width;*h=hudColor_.height;*fsr=false;return hudColor_.texture;}
    *w=width_;*h=height_;*fsr=false;auto* source=color_;
    if(graphics_.postAA==3&&EnsureCmaa())source=DrawCmaa();
    if(graphics_.postAA==2&&EnsurePostTarget(smaaEdge_,*w,*h)&&EnsurePostTarget(smaaWeight_,*w,*h)&&EnsurePostTarget(smaaColor_,*w,*h)) {
        const float constants[8]={1.f / *w,1.f / *h,static_cast<float>(*w),static_cast<float>(*h),0,0,0,0};
        DrawPost(smaaEdge_,smaaPipe_[0],source,smaaArea_,smaaSearch_,constants);
        DrawPost(smaaWeight_,smaaPipe_[1],smaaEdge_.texture,smaaArea_,smaaSearch_,constants);
        DrawPost(smaaColor_,smaaPipe_[2],source,smaaWeight_.texture,smaaSearch_,constants);source=smaaColor_.texture;
    }
    if(graphics_.spatialUpscaler==1&&outW>=*w&&outH>=*h&&(outW>*w||outH>*h)&&EnsurePostTarget(fsrColor_,outW,outH)) {
        float constants[8]={static_cast<float>(*w),static_cast<float>(*h),0,0,static_cast<float>(outW),static_cast<float>(outH),0,0};
        DrawPost(fsrColor_,fsrPipe_[0],source,smaaArea_,smaaSearch_,constants);source=fsrColor_.texture;*w=outW;*h=outH;*fsr=true;
        if(graphics_.sharpening&&EnsurePostTarget(fsrSharp_,outW,outH)) {
            constants[0]=static_cast<float>(outW);constants[1]=static_cast<float>(outH);constants[6]=2.5f-graphics_.sharpening*.75f;
            DrawPost(fsrSharp_,fsrPipe_[1],source,smaaArea_,smaaSearch_,constants);source=fsrSharp_.texture;
        }
    }
    return source;
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

bool SdlGpuDevice::CreateTargets(int w, int h, int samples) {
    if (!samples) samples = graphics_.msaa;
    w = std::max(w, 1); h = std::max(h, 1);
    SDL_GPUTexture* color = nullptr;
    SDL_GPUTexture* depth = nullptr;
    SDL_GPUTexture* multisample = nullptr;
    SDL_GPUTexture* post = nullptr;
    SDL_GPUTransferBuffer* readback = nullptr;
    SDL_GPUTextureCreateInfo ti = {};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    color = SDL_CreateGPUTexture(dev_, &ti);
    if (samples > 1) {
        ti.sample_count = samples == 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_4;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        multisample = SDL_CreateGPUTexture(dev_, &ti);
    }
    ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth = SDL_CreateGPUTexture(dev_, &ti);
    if (!window_) {
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        post = SDL_CreateGPUTexture(dev_, &ti);
        SDL_GPUTransferBufferCreateInfo bi = {};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        bi.size = static_cast<Uint32>(w) * h * 4;
        readback = SDL_CreateGPUTransferBuffer(dev_, &bi);
    }
    if (!color || !depth || (samples > 1 && !multisample) || (!window_ && (!readback || !post))) {
        for (auto* t : {color, depth, multisample, post}) if (t) SDL_ReleaseGPUTexture(dev_, t);
        if (readback) SDL_ReleaseGPUTransferBuffer(dev_, readback);
        return false;
    }
    for (auto* t : {color_, depth_, multisampleColor_, postColor_}) if (t) SDL_ReleaseGPUTexture(dev_, t);
    if (readback_) SDL_ReleaseGPUTransferBuffer(dev_, readback_);
    color_ = color; depth_ = depth; multisampleColor_ = multisample; postColor_ = post; readback_ = readback;
    width_ = w; height_ = h;
    return true;
}

SDL_GPUPresentMode SdlGpuDevice::SelectPresentMode(const GraphicsOptions& options) const {
    if (options.syncInterval == 0) {
        if (options.allowTearing && SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE))
            return SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (SDL_WindowSupportsGPUPresentMode(dev_, window_, SDL_GPU_PRESENTMODE_MAILBOX)) return SDL_GPU_PRESENTMODE_MAILBOX;
    }
    return SDL_GPU_PRESENTMODE_VSYNC;
}

void SdlGpuDevice::UpdateHdrCapabilities() const {
    capabilities_.hdrOutput=window_&&presentHdrPipe_&&
        SDL_GetBooleanProperty(SDL_GetWindowProperties(window_),SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN,false)&&
        SDL_WindowSupportsGPUSwapchainComposition(dev_,window_,SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR);
}

bool SdlGpuDevice::ConfigureGraphics(const GraphicsOptions& options, std::string* error) {
    if (!ValidateGraphicsOptions(options, error)) return false;
    if (cmd_) { if (error) *error = "Apply graphics options between frames"; return false; }
    if(options.outputResolutionHud&&!EnsureHudTargets(error))return false;
    if((options.shadowMaps||options.volumetricFog)&&!EnsureShadowMap(options.shadowMaps?options.shadowMaps:1)){if(error)*error="Directional shadow map resources are unavailable";return false;}
    if(SceneEffectsEnabled(options)&&!EnsureSceneEffects(error))return false;
    UpdateHdrCapabilities();
    if(options.hdrOutput&&!capabilities_.hdrOutput){if(error)*error="Enable HDR in Windows on this display before selecting HDR output";return false;}
    if(options.postAA==2&&(!EnsurePostTarget(smaaEdge_,width_,height_)||!EnsurePostTarget(smaaWeight_,width_,height_)||!EnsurePostTarget(smaaColor_,width_,height_))) {
        if(error)*error="Not enough GPU memory for SMAA targets";return false;
    }
    if(options.postAA==3&&!capabilities_.cmaa2){if(error)*error="CMAA2 compute resources are unavailable";return false;}
    if(options.postAA==3&&!EnsureCmaa(error))return false;
    if((options.softParticles||options.ambientOcclusion)&&!EnsureDepthTargets()){if(error)*error="Not enough GPU memory for the opaque depth prepass";return false;}
    if(options.temporalAA&&(!EnsureDepthTargets()||!EnsureMotionTargets()||!EnsureTaaTargets()||!TaaCopyPipeline(options.msaa))){if(error)*error="Not enough GPU memory for temporal targets";return false;}
    if (options.allowTearing && !capabilities_.tearing) { if (error) *error = "VRR / tearing presentation is unavailable on this device"; return false; }
    if (options.lowLatency && !capabilities_.lowLatency) { if (error) *error = "Low latency presentation is unavailable on this device"; return false; }
    const auto samples = options.msaa == 4 ? SDL_GPU_SAMPLECOUNT_4 : options.msaa == 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
    if (!SDL_GPUTextureSupportsSampleCount(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, samples) ||
        !SDL_GPUTextureSupportsSampleCount(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, samples)) {
        if (error) *error = "Requested MSAA sample count is unavailable on this device";
        return false;
    }
    GraphicsOptions effective = options;
    const auto mode = window_ ? SelectPresentMode(options) : presentMode_;
    const auto composition=options.hdrOutput?SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR:SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    const auto previousComposition=graphics_.hdrOutput?SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR:SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    const bool swapChanged=mode!=presentMode_||composition!=previousComposition;
    if (window_ && swapChanged) {
        if (!SDL_SetGPUSwapchainParameters(dev_, window_, composition, mode)) {
            if (error) *error = std::string("Cannot change synchronization: ") + SDL_GetError();
            return false;
        }
    }
    if (window_) effective.syncInterval = mode == SDL_GPU_PRESENTMODE_VSYNC ? 1 : 0;
    if (window_ && options.lowLatency != graphics_.lowLatency && !SDL_SetGPUAllowedFramesInFlight(dev_, options.lowLatency ? 1 : 2)) {
        if (swapChanged) SDL_SetGPUSwapchainParameters(dev_, window_, previousComposition, presentMode_);
        if (error) *error = std::string("Cannot change the presentation queue limit: ") + SDL_GetError();
        return false;
    }
    if (options.msaa != graphics_.msaa && !CreateTargets(width_, height_, options.msaa)) {
        if (window_ && swapChanged)
            SDL_SetGPUSwapchainParameters(dev_, window_, previousComposition, presentMode_);
        if (window_ && options.lowLatency != graphics_.lowLatency)
            SDL_SetGPUAllowedFramesInFlight(dev_, graphics_.lowLatency ? 1 : 2);
        if (error) *error = "Not enough GPU memory for requested graphics target";
        return false;
    }
    SDL_WaitForGPUIdle(dev_);
    // Scene/HUD color formats remain RGBA8. HDR has its own present pipeline.
    // Coverage and single-sample output HUD are explicit key bits; only the
    // global scene sample count is absent from the main raster PSO key.
    if(graphics_.msaa!=effective.msaa){
        for(auto& [key,pipeline]:pipelines_)SDL_ReleaseGPUGraphicsPipeline(dev_,pipeline);
        pipelines_.clear();
    }
    if(graphics_.anisotropy!=effective.anisotropy){
        for(auto& [key,sampler]:samplers_)SDL_ReleaseGPUSampler(dev_,sampler);
        samplers_.clear();
    }
    if(graphics_.temporalAA!=effective.temporalAA)taaHistoryReady_=taaResolved_=false;
    graphics_ = effective;
    hudReady_=false;hudError_.clear();
    presentMode_ = mode;
    sync_ = graphics_.syncInterval;
    return true;
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

SDL_GPUGraphicsPipeline* SdlGpuDevice::Pipeline(GpuDraw::Pipeline kind, const GpuState& s, bool coverage) {
    const bool sprite = kind == GpuDraw::Pipeline::Sprite;
    const uint32_t key = (sprite ? 1u : 0u) | (s.blendEnable ? 2u : 0u) | (s.blendEnable ? (s.srcBlend << 2) | (s.dstBlend << 6) : 0u) |
                         (static_cast<uint32_t>(s.zfunc) << 10) | (s.zwrite ? 1u << 14 : 0u) | (static_cast<uint32_t>(s.cull & 3) << 15) |
                         (coverage ? 1u << 17 : 0u) | (hudReady_?1u<<18:0u);
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
    pi.multisample_state.sample_count = hudReady_?SDL_GPU_SAMPLECOUNT_1:graphics_.msaa == 4 ? SDL_GPU_SAMPLECOUNT_4 : graphics_.msaa == 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
    pi.multisample_state.enable_alpha_to_coverage = coverage;
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
    if (s.mipmapped && graphics_.anisotropy > 1) {
        // D3D12's anisotropic filter encoding requires all three linear bits.
        // ModelSample supplies exact point magnification for authored point materials.
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        si.enable_anisotropy = true;
        si.max_anisotropy = static_cast<float>(graphics_.anisotropy);
    }
    SDL_GPUSampler* smp = SDL_CreateGPUSampler(dev_, &si);
    if (smp) samplers_[key] = smp;
    return smp;
}

bool SdlGpuDevice::BeginFrame(const float clear[4]) {
    hudReady_=false;hudError_.clear();
    if(graphics_.hdrOutput){UpdateHdrCapabilities();if(!capabilities_.hdrOutput){auto options=graphics_;options.hdrOutput=false;ConfigureGraphics(options,nullptr);}}
    if (follow_) {
        int w, h;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if ((w != width_ || h != height_) && w > 0 && h > 0) CreateTargets(w, h);
    }
    if (!cmd_) cmd_ = SDL_AcquireGPUCommandBuffer(dev_);
    if (cmd_ && window_ && graphics_.lowLatency) {
        acquiredSwap_ = nullptr;
        if (!SDL_AcquireGPUSwapchainTexture(cmd_, window_, &acquiredSwap_, &acquiredWidth_, &acquiredHeight_) || !acquiredSwap_) {
            SDL_CancelGPUCommandBuffer(cmd_);
            cmd_ = nullptr;
            return false; // presentation busy/minimized; caller keeps pumping input/network
        }
    }
    memcpy(clear_, clear, sizeof(clear_));
    previousDepthReady_=motionReady_&&opaqueReady_&&opaqueWidth_==width_&&opaqueHeight_==height_&&
        motionWidth_==width_&&motionHeight_==height_&&previousDepthColor_.texture;
    if(previousDepthReady_)std::swap(opaqueDepthColor_,previousDepthColor_);
    taaHistoryReady_=graphics_.temporalAA&&taaResolved_&&taaResult_.width==width_&&taaResult_.height==height_;
    if(taaHistoryReady_){std::swap(taaResult_,taaHistory_);std::swap(reactive_,previousReactive_);}
    taaResolved_=reactiveCleared_=sceneProcessed_=false;
    sceneEffectsApplied_=reflectionSurfaceCleared_=false;sceneEffectsCamera_={};sceneEffectsFrame_={};
    motionReady_=motionCleared_=false;opaqueCleared_=opaqueReady_=false;
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
    if(!hudReady_){
    DrawShadowMap(list);
    DrawDepthPrepass(list);
    if(!sceneEffectsApplied_&&list.temporalCamera.valid){sceneEffectsCamera_=list.temporalCamera;sceneEffectsFrame_=list.sceneEffects;for(int k=0;k<2;++k)sceneEffectsJitter_[k]=list.temporal.enabled?list.temporal.jitterPixels[k]:0.f;}
    DrawReflectionSurface(list);
    DrawMotion(list);
    DrawReactive(list);
    }
    SDL_GPUColorTargetInfo ct = {};
    ct.texture = hudReady_?hudColor_.texture:multisampleColor_ ? multisampleColor_ : color_;
    ct.clear_color = { clear_[0], clear_[1], clear_[2], clear_[3] };
    ct.load_op = needClear_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.store_op = !hudReady_&&multisampleColor_ ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_STORE;
    ct.resolve_texture = !hudReady_&&multisampleColor_ ? color_ : nullptr;
    SDL_GPUDepthStencilTargetInfo dt = {};
    dt.texture = hudReady_?hudDepth_:depth_;
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
    size_t drawIndex=0;
    for (const GpuDraw& d : list.draws) {
        if(drawIndex++==list.opaqueDrawCount&&list.opaqueDrawCount&&graphics_.ambientOcclusion) {
            SDL_EndGPURenderPass(rp);DrawAmbientOcclusion(list);ct.load_op=dt.load_op=SDL_GPU_LOADOP_LOAD;
            rp=SDL_BeginGPURenderPass(cmd_,&ct,1,&dt);if(!rp)return;SDL_SetGPUViewport(rp,&vp);boundPipe=nullptr;boundVb=nullptr;
        }
        GpuState state = d.state;
        if (d.pipeline == GpuDraw::Pipeline::Model && graphics_.textureFilter) state.linear = graphics_.textureFilter == 2 ? 1 : 0;
        const bool coverage = d.pipeline == GpuDraw::Pipeline::Model && graphics_.alphaToCoverage && graphics_.msaa > 1 &&
            !state.blendEnable && list.model[d.constants].flags[2] >= 0;
        SDL_GPUGraphicsPipeline* p = Pipeline(d.pipeline, state, coverage);
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
        const SDL_GPUTextureSamplerBinding ts = { tex, Sampler(state) };
        SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
        if (d.pipeline == GpuDraw::Pipeline::Model) {
            ModelConstants c = list.model[d.constants];
            c.depthEffects[3]=opaqueReady_?1.0f:0.0f;
            const bool material=d.normalRoughnessTexture&&d.normalRoughnessTexture<=textures_.size();
            if(!material)c.materialParams[0]=0;
            const SDL_GPUTextureSamplerBinding extra[]={{opaqueReady_?opaqueDepthColor_.texture:white_,postPointSampler_},{material?textures_[d.normalRoughnessTexture-1]:white_,Sampler(state)}};
            SDL_BindGPUFragmentSamplers(rp,1,extra,2);
            const SDL_GPUTextureSamplerBinding shadow={shadowMapConstants_.parameters[3]>.5f?shadowMapColor_.texture:white_,postPointSampler_};
            SDL_BindGPUFragmentSamplers(rp,3,&shadow,1);
            c.sampling[0] = state.mipmapped && graphics_.anisotropy > 1 && !state.linear ? 1.0f : 0.0f;
            c.sampling[1] = static_cast<float>(state.addrU); c.sampling[2] = static_cast<float>(state.addrV);
            SDL_PushGPUVertexUniformData(cmd_, 0, &c, sizeof(ModelConstants));
            SDL_PushGPUFragmentUniformData(cmd_, 0, &c, sizeof(ModelConstants));
            SDL_PushGPUFragmentUniformData(cmd_,1,&shadowMapConstants_,sizeof(ShadowConstants));
        } else {
            SDL_PushGPUFragmentUniformData(cmd_, 0, &list.sprite[d.constants], sizeof(SpriteConstants));
        }
        SDL_DrawGPUPrimitives(rp, d.count, 1, d.first, 0);
    }
    SDL_EndGPURenderPass(rp);
    if(list.opaqueDrawCount&&list.opaqueDrawCount>=list.draws.size())DrawAmbientOcclusion(list);
}

bool SdlGpuDevice::ReadPixels(std::vector<uint8_t>* rgba) {
    if (!cmd_ || !readback_) return false;
    if (needClear_) Submit(GpuDrawList{ width_, height_ });  // nothing drawn: still clear
    const bool effects = hudReady_ || graphics_.postAA || graphics_.sharpening || graphics_.bloom || graphics_.colorGrade || graphics_.crtMask || graphics_.crtCurvature || graphics_.crtGlow ||
        graphics_.brightness != 50 || graphics_.gamma != 50 || graphics_.contrast != 50 || graphics_.saturation != 50;
    if (effects) DrawPresent(postColor_, 0, 0, width_, height_, 0);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd_);
    SDL_GPUTextureRegion src = {};
    src.texture = effects ? postColor_ : color_;
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

bool SdlGpuDevice::ReadPresentation(int w,int h,std::vector<uint8_t>* rgba) {
    if(window_||!cmd_||!rgba||w<1||h<1||w>8192||h>8192||!EnsurePostTarget(captureColor_,w,h))return false;
    if(needClear_)Submit(GpuDrawList{width_,height_});
    DrawPresent(captureColor_.texture,0,0,w,h,present_.crt);
    SDL_GPUTransferBufferCreateInfo info{};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=static_cast<Uint32>(w)*h*4;
    auto* transfer=SDL_CreateGPUTransferBuffer(dev_,&info);if(!transfer)return false;
    auto* copy=SDL_BeginGPUCopyPass(cmd_);
    SDL_GPUTextureRegion source{};source.texture=captureColor_.texture;source.w=w;source.h=h;source.d=1;
    SDL_GPUTextureTransferInfo destination{};destination.transfer_buffer=transfer;destination.pixels_per_row=w;destination.rows_per_layer=h;
    SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_);cmd_=nullptr;
    if(!fence){SDL_ReleaseGPUTransferBuffer(dev_,transfer);return false;}
    SDL_WaitForGPUFences(dev_,true,&fence,1);SDL_ReleaseGPUFence(dev_,fence);
    const auto* pixels=static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(dev_,transfer,false));
    if(pixels){rgba->assign(pixels,pixels+static_cast<size_t>(w)*h*4);SDL_UnmapGPUTransferBuffer(dev_,transfer);}
    SDL_ReleaseGPUTransferBuffer(dev_,transfer);return pixels!=nullptr;
}

bool SdlGpuDevice::ReadLinearPresentation(int w,int h,std::vector<float>* rgba) {
    if(window_||!cmd_||!rgba||!presentHdrPipe_||w<1||h<1||w>8192||h>8192||
       !EnsurePostTarget(floatCaptureColor_,w,h,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT))return false;
    if(needClear_)Submit(GpuDrawList{width_,height_});
    DrawPresent(floatCaptureColor_.texture,0,0,w,h,present_.crt,true);
    SDL_GPUTransferBufferCreateInfo info{};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size=static_cast<Uint32>(w)*h*8;
    auto* transfer=SDL_CreateGPUTransferBuffer(dev_,&info);if(!transfer)return false;
    auto* copy=SDL_BeginGPUCopyPass(cmd_);
    SDL_GPUTextureRegion source{};source.texture=floatCaptureColor_.texture;source.w=w;source.h=h;source.d=1;
    SDL_GPUTextureTransferInfo destination{};destination.transfer_buffer=transfer;
    destination.pixels_per_row=w;destination.rows_per_layer=h;
    SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_);cmd_=nullptr;
    if(!fence){SDL_ReleaseGPUTransferBuffer(dev_,transfer);return false;}
    SDL_WaitForGPUFences(dev_,true,&fence,1);SDL_ReleaseGPUFence(dev_,fence);
    const auto* pixels=static_cast<const uint16_t*>(SDL_MapGPUTransferBuffer(dev_,transfer,false));
    if(pixels){rgba->resize(static_cast<size_t>(w)*h*4);
        for(size_t value=0;value<rgba->size();++value)(*rgba)[value]=HalfToFloat(pixels[value]);
        SDL_UnmapGPUTransferBuffer(dev_,transfer);}
    SDL_ReleaseGPUTransferBuffer(dev_,transfer);return pixels!=nullptr;
}

bool SdlGpuDevice::ReadMotionPixels(std::vector<float>* rgba) {
    if(!rgba||!motionReady_||!motionColor_.texture)return false;
    SDL_GPUCommandBuffer* command=cmd_?cmd_:SDL_AcquireGPUCommandBuffer(dev_);if(!command)return false;
    SDL_GPUTransferBufferCreateInfo info{};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=static_cast<Uint32>(width_)*height_*8;
    auto* transfer=SDL_CreateGPUTransferBuffer(dev_,&info);if(!transfer){if(!cmd_)SDL_CancelGPUCommandBuffer(command);return false;}
    auto* copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source{};source.texture=motionColor_.texture;source.w=width_;source.h=height_;source.d=1;
    SDL_GPUTextureTransferInfo destination{};destination.transfer_buffer=transfer;destination.pixels_per_row=width_;destination.rows_per_layer=height_;
    SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);if(command==cmd_)cmd_=nullptr;
    if(!fence){SDL_ReleaseGPUTransferBuffer(dev_,transfer);return false;}
    SDL_WaitForGPUFences(dev_,true,&fence,1);SDL_ReleaseGPUFence(dev_,fence);
    const auto* pixels=static_cast<const uint16_t*>(SDL_MapGPUTransferBuffer(dev_,transfer,false));
    if(pixels){rgba->resize(static_cast<size_t>(width_)*height_*4);for(size_t i=0;i<rgba->size();++i)(*rgba)[i]=HalfToFloat(pixels[i]);SDL_UnmapGPUTransferBuffer(dev_,transfer);}
    SDL_ReleaseGPUTransferBuffer(dev_,transfer);return pixels!=nullptr;
}

void SdlGpuDevice::EndFrame() {
    if (!cmd_) return;
    if (needClear_) Submit(GpuDrawList{ width_, height_ });
    if (window_) {
        // The present pass: the swap chain texture cleared black, the frame scaled into the image rectangle (the
        // viewport) with the shared present shader.
        SDL_GPUTexture* swap = acquiredSwap_;
        Uint32 sw = acquiredWidth_, sh = acquiredHeight_;
        const bool acquired = swap || SDL_WaitAndAcquireGPUSwapchainTexture(cmd_, window_, &swap, &sw, &sh);
        if (acquired && swap) {
            PresentParams p = present_;
            if (p.w <= 0 || p.h <= 0) {
                p.x = p.y = 0;
                p.w = static_cast<int>(sw);
                p.h = static_cast<int>(sh);
            }
            DrawPresent(swap, p.x, p.y, p.w, p.h, p.crt,graphics_.hdrOutput);
        }
    }
    SDL_SubmitGPUCommandBuffer(cmd_);
    cmd_ = nullptr;
    acquiredSwap_ = nullptr;
}

void SdlGpuDevice::DrawPresent(SDL_GPUTexture* target, int x, int y, int w, int h, int crt,bool hdr) {
    int sourceWidth,sourceHeight;bool fsr;
    auto* source=PostSource(w,h,&sourceWidth,&sourceHeight,&fsr);
    SDL_GPUColorTargetInfo ct = {};
    ct.texture = target;
    ct.clear_color = { 0, 0, 0, 1 };
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    if (SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd_, &ct, 1, nullptr)) {
        const SDL_GPUViewport vp = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), 0, 1 };
        SDL_SetGPUViewport(rp, &vp);
        SDL_BindGPUGraphicsPipeline(rp, hdr?presentHdrPipe_:presentPipe_);
        const SDL_GPUTextureSamplerBinding ts = { source, presentSampler_ };
        SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
        auto options=hudReady_?HudDisplayGraphics(graphics_):graphics_;if(fsr)options.sharpening=0;options.hdrOutput=hdr;
        const auto cb = MakePresentConstants(sourceWidth, sourceHeight, w, h, crt, options);
        SDL_PushGPUFragmentUniformData(cmd_, 0, &cb, sizeof(cb));
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }
}

#include "shadow_sdl.inl"
#include "scene_effects_sdl.inl"
#include "hud_output_sdl.inl"
#include "warmup_sdl.inl"

}  // namespace

std::unique_ptr<GpuDevice> CreateSdlGpuDevice(const GpuDeviceDesc& desc, std::string* error) {
    auto d = std::make_unique<SdlGpuDevice>();
    if (!d->Init(desc, error)) return nullptr;
    return d;
}

}  // namespace port
