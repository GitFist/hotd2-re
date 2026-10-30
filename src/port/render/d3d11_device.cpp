#include "d3d11_device.h"

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstring>
#include <chrono>
#include <climits>
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

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

namespace port {

namespace {

using Microsoft::WRL::ComPtr;

D3D11_BLEND ToD3D11Blend(int d3d7) {
    switch (d3d7) {
    case 1: return D3D11_BLEND_ZERO;
    case 2: return D3D11_BLEND_ONE;
    case 3: return D3D11_BLEND_SRC_COLOR;
    case 4: return D3D11_BLEND_INV_SRC_COLOR;
    case 5: return D3D11_BLEND_SRC_ALPHA;
    case 6: return D3D11_BLEND_INV_SRC_ALPHA;
    case 7: return D3D11_BLEND_DEST_ALPHA;
    case 8: return D3D11_BLEND_INV_DEST_ALPHA;
    case 9: return D3D11_BLEND_DEST_COLOR;
    case 10: return D3D11_BLEND_INV_DEST_COLOR;
    default: return D3D11_BLEND_ONE;
    }
}

D3D11_TEXTURE_ADDRESS_MODE ToD3D11Address(int d3d7) {
    return d3d7 == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR : d3d7 == 3 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
}

class D3D11Device final : public GpuDevice {
public:
    ~D3D11Device() override { if (queueReady_) CloseHandle(queueReady_); }
    bool Init(const GpuDeviceDesc& desc, std::string* error);
    const char* Name() const override { return "d3d11"; }
    GpuTexture CreateTexture(const RgbaImage& img, bool alphaWeighted = false) override;
    void ClearTextures() override {
        ID3D11ShaderResourceView* empty = nullptr;
        ctx_->PSSetShaderResources(0, 1, &empty);
        textures_.clear();
        motionReady_=previousDepthReady_=false;
        taaHistoryReady_=taaResolved_=false;
        ctx_->Flush();
    }
    void SetStaticVertices(const game::ModelVertex* v, size_t count) override;
    void SetDynamicVertices(const game::ModelVertex* v, size_t count) override;
    void SetSpriteVertices(const game::TLVertex* v, size_t count) override;
    bool BeginFrame(const float clear[4]) override;
    void Submit(const GpuDrawList& list) override;
    void EndFrame() override;
    bool ReadPixels(std::vector<uint8_t>* rgba) override;
    bool ReadPresentation(int width, int height, std::vector<uint8_t>* rgba) override;
    bool ReadLinearPresentation(int width,int height,std::vector<float>* rgba) override;
    bool ReadMotionPixels(std::vector<float>* rgba) override;
    bool NeedsShadowCasters() const override {return graphics_.shadowMaps>0||graphics_.volumetricFog>0;}
    void SetMenuOverlay(bool enabled) override {menuOverlay_=enabled;}
    bool RequiresSceneProcessing() const override {return graphics_.temporalAA||SceneEffectsEnabled(graphics_)||graphics_.outputResolutionHud||menuOverlay_;}
    bool ProcessScene(std::string* error=nullptr) override;
    bool SetGpuTimingEnabled(bool enabled,std::string* error=nullptr) override;
    bool PollGpuTiming(GpuTimingSample* sample) override;
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
    const GraphicsCapabilities& GetGraphicsCapabilities() const override { UpdateHdrCapabilities(); return capabilities_; }

private:
    bool CreatePipelines(std::string* error);
    bool CreatePostPipelines(std::string* error);
    bool CreateOffscreen(int width = 0, int height = 0, int samples = 0);
    bool CreateBackBuffer(int w, int h);
    void PresentFrame();
    void ResolveColor();
    void DrawPresent(ID3D11RenderTargetView*, int x, int y, int width, int height, int crt,bool hdr=false);
    void UpdateHdrCapabilities() const;
    bool SetHdrOutput(bool enabled,std::string* error);
    struct PostTarget {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
        int width = 0, height = 0;
    };
    bool EnsurePostTarget(PostTarget&, int w, int h, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
    bool EnsureDepthTargets();
    bool EnsureMotionTargets();
    void DrawMotion(const GpuDrawList&);
    bool EnsureTaaTargets();
    void DrawReactive(const GpuDrawList&);
    bool EnsureCmaa(std::string* error=nullptr);
    ID3D11ShaderResourceView* DrawCmaa();
    void CollectGpuTimings();
    void StartGpuTiming();
    void FinishGpuTiming();
    void DrawDepthPrepass(const GpuDrawList&);
    void DrawAmbientOcclusion(const GpuDrawList&);
    void DrawPost(PostTarget&, ID3D11PixelShader*, ID3D11ShaderResourceView*, ID3D11ShaderResourceView*, ID3D11ShaderResourceView*, const float cb[8]);
    ID3D11ShaderResourceView* PostSource(int outWidth, int outHeight, int* width, int* height, bool* fsr);
    ID3D11BlendState* BlendState(bool enable, int src, int dst, bool coverage = false);
    ID3D11DepthStencilState* DepthState(int zfunc, bool zwrite);
    ID3D11SamplerState* Sampler(bool linear, int addrU, int addrV, int mipmapped);
    void BindPipeline(GpuDraw::Pipeline p, int width, int height);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    HWND hwnd_ = nullptr;
    ComPtr<IDXGISwapChain> swap_;
    ComPtr<IDXGISwapChain2> queueSwap_;
    HANDLE queueReady_ = nullptr;
    UINT swapFlags_ = 0;
    bool offscreen_ = true, follow_ = false;
    int sync_ = 0;
    int width_ = 0, height_ = 0;  // the render target (internal resolution)
    // The frame is always rendered into color_ / depth_ (off-screen, width_ x height_). Headless: staging_ reads it
    // back. Window: the present pass (presentVs_ / presentPs_, hlsl_shaders.h kPresentShaderHlsl) scales it into
    // present_'s rectangle of the swap chain's back buffer (backRtv_, backW_ x backH_ = the client area).
    ComPtr<ID3D11Texture2D> color_, depth_, staging_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11ShaderResourceView> colorSrv_;
    ComPtr<ID3D11RenderTargetView> backRtv_;
    int backW_ = 0, backH_ = 0;
    PresentParams present_;
    GraphicsOptions graphics_;
    mutable GraphicsCapabilities capabilities_;
    DXGI_FORMAT swapFormat_=DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D11Texture2D> multisampleColor_, postColor_;
    ComPtr<ID3D11RenderTargetView> postRtv_;
    bool frameActive_ = false;
    PostTarget smaaEdge_, smaaWeight_, smaaColor_, fsrColor_, fsrSharp_, captureColor_,floatCaptureColor_;
    ComPtr<ID3D11ShaderResourceView> smaaArea_, smaaSearch_;
    ComPtr<ID3D11PixelShader> smaaPs_[3], fsrPs_[2];
    ComPtr<ID3D11SamplerState> postPointSampler_;
    ComPtr<ID3D11Buffer> postCb_;
    PostTarget opaqueDepthColor_;
    ComPtr<ID3D11Texture2D> opaqueDepth_;
    ComPtr<ID3D11DepthStencilView> opaqueDepthDsv_;
    ComPtr<ID3D11VertexShader> depthVs_;
    ComPtr<ID3D11PixelShader> depthPs_;
    ComPtr<ID3D11PixelShader> aoPs_;
    ComPtr<ID3D11Buffer> aoCb_;
    int opaqueWidth_=0,opaqueHeight_=0;
    bool opaqueCleared_=false,opaqueReady_=false;
    PostTarget motionColor_,previousDepthColor_;
    ComPtr<ID3D11Texture2D> motionDepth_;
    ComPtr<ID3D11DepthStencilView> motionDepthDsv_;
    ComPtr<ID3D11VertexShader> motionVs_;
    ComPtr<ID3D11PixelShader> motionPs_;
    ComPtr<ID3D11Buffer> motionCb_,motionVb_;
    UINT motionCapacity_=0;
    int motionWidth_=0,motionHeight_=0;
    bool motionReady_=false,motionCleared_=false,previousDepthReady_=false;
    uint64_t motionFrameId_=0;
    TemporalFrameInfo temporal_;
    PostTarget taaResult_,taaHistory_,reactive_,previousReactive_;
    ComPtr<ID3D11PixelShader> taaPs_,taaCopyPs_,reactiveModelPs_,reactiveSpritePs_;
    ComPtr<ID3D11Buffer> taaCb_;
    bool taaHistoryReady_=false,taaResolved_=false,reactiveCleared_=false,sceneProcessed_=false;
    struct CmaaState {
        PostTarget output;
        ComPtr<ID3D11Texture2D> edges,heads;
        ComPtr<ID3D11Buffer> buffers[5];
        ComPtr<ID3D11UnorderedAccessView> uav[8];
    } cmaa_;
    ComPtr<ID3D11ComputeShader> cmaaCs_[5];
    struct TimingQueries {
        ComPtr<ID3D11Query> begin,end,disjoint;
        bool pending=false;uint64_t serial=0;int width=0,height=0;
    } timing_[8];
    bool timingEnabled_=false;
    int activeTiming_=-1;
    uint64_t timingSerial_=0,deliveredTiming_=0;
    GpuTimingSample latestTiming_;
    ComPtr<ID3D11VertexShader> presentVs_;
    ComPtr<ID3D11PixelShader> presentPs_;
    ComPtr<ID3D11Buffer> presentCb_;
    ComPtr<ID3D11SamplerState> presentSampler_;
    ComPtr<ID3D11DepthStencilState> presentDepth_;

    ComPtr<ID3D11VertexShader> vs_, spriteVs_;
    ComPtr<ID3D11PixelShader> ps_, spritePs_;
    ComPtr<ID3D11InputLayout> layout_, spriteLayout_;
    ComPtr<ID3D11Buffer> constants_, spriteCb_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11RasterizerState> rasterCull_[3];  // D3DCULL none / CW / CCW
    ComPtr<ID3D11Buffer> staticVb_, dynVb_, spriteVb_;
    UINT dynVbCapacity_ = 0, spriteVbCapacity_ = 0;  // bytes / vertices
    std::map<int, ComPtr<ID3D11BlendState>> blends_;
    std::map<int, ComPtr<ID3D11DepthStencilState>> depths_;
    std::map<int, ComPtr<ID3D11SamplerState>> samplers_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures_;  // handle = index + 1
    // Isolated directional shadow map resources; ModelConstants stays unchanged.
    bool EnsureShadowMap(int quality);
    void DrawShadowMap(const GpuDrawList&);
    PostTarget shadowMapColor_;
    ComPtr<ID3D11Texture2D> shadowMapDepth_;
    ComPtr<ID3D11DepthStencilView> shadowMapDsv_;
    ComPtr<ID3D11VertexShader> shadowMapVs_;
    ComPtr<ID3D11PixelShader> shadowMapPs_;
    ComPtr<ID3D11Buffer> shadowMapCb_;
    ShadowConstants shadowMapConstants_;
    bool ResolveTemporal(std::string* error);
    bool ProcessSceneEffects(std::string* error);
    bool EnsureHudTargets(std::string* error=nullptr);
    bool ComposeHudScene(std::string* error=nullptr);
    PostTarget hudColor_;
    ComPtr<ID3D11Texture2D> hudDepth_;
    ComPtr<ID3D11DepthStencilView> hudDsv_;
    bool hudReady_=false,menuOverlay_=false;
    std::string hudError_;
    bool EnsureSceneEffects(std::string* error=nullptr);
    void DrawReflectionSurface(const GpuDrawList&);
    TemporalCameraInfo sceneEffectsCamera_;
    SceneEffectsFrame sceneEffectsFrame_;
    float sceneEffectsJitter_[2]={};
    bool sceneEffectsApplied_=false,reflectionSurfaceCleared_=false;
    PostTarget sceneEffectsTargets_[2],reflectionSurface_;
    ComPtr<ID3D11PixelShader> sceneEffectsPs_[3],reflectionSurfacePs_;
    ComPtr<ID3D11VertexShader> reflectionSurfaceVs_;
    ComPtr<ID3D11Buffer> sceneEffectsCb_;

};

bool D3D11Device::Init(const GpuDeviceDesc& desc, std::string* error) {
    offscreen_ = desc.offscreen || !desc.nativeWindow;
    follow_ = desc.followWindowSize;
    sync_ = desc.syncInterval;
    graphics_.syncInterval = sync_;
    width_ = desc.width;
    height_ = desc.height;
    if (offscreen_) {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &ctx_)) &&
            FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &ctx_))) {
            if (error) *error = "D3D11CreateDevice failed";
            return false;
        }
    } else {
        hwnd_ = static_cast<HWND>(desc.nativeWindow);
        RECT cr;
        GetClientRect(hwnd_, &cr);
        backW_ = cr.right > 0 ? cr.right : 1;
        backH_ = cr.bottom > 0 ? cr.bottom : 1;
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = backW_;  // the client area; the frame is scaled into it by the present pass
        sd.BufferDesc.Height = backH_;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd_;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGIFactory5> factory;
        BOOL tearing = FALSE;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        auto create = [&]() { return D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &swap_, &device_, nullptr, &ctx_); };
        HRESULT made = create();
        if (FAILED(made)) { // Older DXGI: retain the established flip-discard path.
            swap_.Reset(); device_.Reset(); ctx_.Reset();
            sd.Flags = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
            made = create();
        }
        if (FAILED(made)) {
            swap_.Reset(); device_.Reset(); ctx_.Reset(); sd.Flags = 0;
            made = create();
        }
        if (FAILED(made)) {
            if (error) *error = "D3D11CreateDeviceAndSwapChain failed";
            return false;
        }
        swapFlags_ = sd.Flags;
        capabilities_.tearing = (swapFlags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
        if ((swapFlags_ & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) && SUCCEEDED(swap_.As(&queueSwap_))) {
            // Match the normal device queue limit; opt-in Low latency changes it to one.
            if (SUCCEEDED(queueSwap_->SetMaximumFrameLatency(3))) queueReady_ = queueSwap_->GetFrameLatencyWaitableObject();
            capabilities_.lowLatency = queueReady_ != nullptr;
        }
        if (!CreateBackBuffer(backW_, backH_)) {
            if (error) *error = "back buffer view creation failed";
            return false;
        }
        if (follow_) {  // the render target follows the client area (sized in BeginFrame)
            width_ = backW_;
            height_ = backH_;
        }
    }
    if (!CreateOffscreen()) {
        if (error) *error = "render target creation failed";
        return false;
    }
    auto supports = [this](int samples) {
        UINT color = 0, depth = 0;
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &color);
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, samples, &depth);
        return color && depth;
    };
    capabilities_.msaa2 = supports(2); capabilities_.msaa4 = supports(4); capabilities_.maxAnisotropy = 16;
    capabilities_.temporalAA=true;
    capabilities_.pipelineWarmup=true;
    capabilities_.outputResolutionHud=true;
    capabilities_.smaa=capabilities_.spatialFSR1=capabilities_.softParticles=capabilities_.materialMaps=capabilities_.ambientOcclusion=capabilities_.shadowMaps=true;capabilities_.sceneEffects=true;
    UINT cmaaColor=0,cmaaEdges=0,cmaaHeads=0;
    capabilities_.cmaa2=SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,&cmaaColor))&&
        SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_R8_UINT,&cmaaEdges))&&SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_R32_UINT,&cmaaHeads))&&
        (cmaaColor&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)&&(cmaaEdges&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)&&(cmaaHeads&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);
    {ComPtr<ID3D11Query> stamp,disjoint;const D3D11_QUERY_DESC a={D3D11_QUERY_TIMESTAMP,0},b={D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        capabilities_.gpuTiming=SUCCEEDED(device_->CreateQuery(&a,&stamp))&&SUCCEEDED(device_->CreateQuery(&b,&disjoint));}
    return CreatePipelines(error);
}

bool D3D11Device::CreateOffscreen(int width, int height, int samples) {
    if (!width) width = width_;
    if (!height) height = height_;
    if (!samples) samples = graphics_.msaa;
    // Allocate the complete replacement first. A failed MSAA/resize must keep the playable target.
    ComPtr<ID3D11Texture2D> color, depth, staging, multisample, post;
    ComPtr<ID3D11RenderTargetView> rtv, postRtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;  // sampled by the present pass (window)
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &color))) return false;
    if (FAILED(device_->CreateShaderResourceView(color.Get(), nullptr, &srv))) return false;
    if (samples > 1) {
        td.SampleDesc.Count = samples;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &multisample))) return false;
    }
    if (FAILED(device_->CreateRenderTargetView(samples > 1 ? multisample.Get() : color.Get(), nullptr, &rtv))) return false;
    td.SampleDesc.Count = 1;
    if (offscreen_) {  // headless: the readback copy
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &staging))) return false;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.CPUAccessFlags = 0;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &post)) ||
            FAILED(device_->CreateRenderTargetView(post.Get(), nullptr, &postRtv))) return false;
    }
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    td.SampleDesc.Count = samples;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &depth)) ||
        FAILED(device_->CreateDepthStencilView(depth.Get(), nullptr, &dsv))) return false;
    color_ = color; depth_ = depth; staging_ = staging; multisampleColor_ = multisample;
    rtv_ = rtv; dsv_ = dsv; colorSrv_ = srv; postColor_ = post; postRtv_ = postRtv;
    width_ = width; height_ = height;
    return true;
}

bool D3D11Device::CreateBackBuffer(int w, int h) {
    backRtv_.Reset();
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    device_->CreateRenderTargetView(back.Get(), nullptr, &backRtv_);
    backW_ = w;
    backH_ = h;
    return backRtv_ != nullptr;
}

bool D3D11Device::ResizeTarget(int w, int h) {
    if (follow_ || w <= 0 || h <= 0 || frameActive_) return false;
    if (w == width_ && h == height_) return true;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    return CreateOffscreen(w, h);
}

void D3D11Device::UpdateHdrCapabilities() const {
    capabilities_.hdrOutput=false;if(!swap_||!device_)return;
    ComPtr<IDXGIOutput> output;ComPtr<IDXGIOutput6> advanced;
    DXGI_OUTPUT_DESC1 desc{};UINT format=0;
    if(SUCCEEDED(swap_->GetContainingOutput(&output))&&SUCCEEDED(output.As(&advanced))&&SUCCEEDED(advanced->GetDesc1(&desc))&&
       desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020&&SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT,&format)))
        capabilities_.hdrOutput=(format&D3D11_FORMAT_SUPPORT_RENDER_TARGET)&&(format&D3D11_FORMAT_SUPPORT_DISPLAY);
}

bool D3D11Device::SetHdrOutput(bool enabled,std::string* error) {
    if(!swap_){if(error)*error="HDR output requires a window on an HDR-enabled display";return false;}
    ComPtr<IDXGISwapChain3> advanced;if(FAILED(swap_.As(&advanced))){if(error)*error="HDR swap-chain color spaces are unavailable";return false;}
    const auto next=enabled?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
    if(next==swapFormat_)return true;
    const auto previous=swapFormat_;
    const auto colorSpace=enabled?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    ctx_->OMSetRenderTargets(0,nullptr,nullptr);backRtv_.Reset();ctx_->Flush();
    HRESULT changed=swap_->ResizeBuffers(0,backW_,backH_,next,swapFlags_);
    if(SUCCEEDED(changed))changed=advanced->SetColorSpace1(colorSpace);
    if(SUCCEEDED(changed)&&CreateBackBuffer(backW_,backH_)){swapFormat_=next;return true;}
    // Restore both buffer encoding and colorspace before reporting failure.
    backRtv_.Reset();swap_->ResizeBuffers(0,backW_,backH_,previous,swapFlags_);
    advanced->SetColorSpace1(previous==DXGI_FORMAT_R16G16B16A16_FLOAT?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    CreateBackBuffer(backW_,backH_);if(error)*error="Could not change HDR output; previous presentation was restored";return false;
}

bool D3D11Device::ConfigureGraphics(const GraphicsOptions& options, std::string* error) {
    if (!ValidateGraphicsOptions(options, error)) return false;
    if (frameActive_) { if (error) *error = "Apply graphics options between frames"; return false; }
    if(options.outputResolutionHud&&!EnsureHudTargets(error))return false;
    if((options.shadowMaps||options.volumetricFog)&&!EnsureShadowMap(options.shadowMaps?options.shadowMaps:1)){if(error)*error="Directional shadow map resources are unavailable";return false;}
    if(SceneEffectsEnabled(options)&&!EnsureSceneEffects(error))return false;
    UpdateHdrCapabilities();
    if(options.hdrOutput&&!capabilities_.hdrOutput){if(error)*error="Enable HDR in Windows on this display before selecting HDR output";return false;}
    if(options.postAA==2 && (!EnsurePostTarget(smaaEdge_,width_,height_) || !EnsurePostTarget(smaaWeight_,width_,height_) || !EnsurePostTarget(smaaColor_,width_,height_))) {
        if(error)*error="Not enough GPU memory for SMAA targets";return false;
    }
    if(options.postAA==3&&!capabilities_.cmaa2){if(error)*error="CMAA2 compute resources are unavailable";return false;}
    if(options.postAA==3&&!EnsureCmaa(error))return false;
    if((options.softParticles||options.ambientOcclusion) && !EnsureDepthTargets()){if(error)*error="Not enough GPU memory for the opaque depth prepass";return false;}
    if(options.temporalAA&&(!EnsureDepthTargets()||!EnsureMotionTargets()||!EnsureTaaTargets())){if(error)*error="Not enough GPU memory for temporal targets";return false;}
    if (options.allowTearing && !capabilities_.tearing) { if (error) *error = "VRR / tearing presentation is unavailable on this device"; return false; }
    if (options.lowLatency && !capabilities_.lowLatency) { if (error) *error = "Low latency presentation is unavailable on this device"; return false; }
    if (options.lowLatency != graphics_.lowLatency && queueSwap_ &&
        FAILED(queueSwap_->SetMaximumFrameLatency(options.lowLatency ? 1 : 3))) {
        if (error) *error = "Cannot change the presentation queue limit"; return false;
    }
    const bool hdrChanged=options.hdrOutput!=graphics_.hdrOutput;
    if(hdrChanged&&!SetHdrOutput(options.hdrOutput,error)){
        if(queueSwap_&&options.lowLatency!=graphics_.lowLatency)queueSwap_->SetMaximumFrameLatency(graphics_.lowLatency?1:3);
        return false;
    }
    if (options.msaa != graphics_.msaa) {
        UINT colorQuality = 0, depthQuality = 0;
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, options.msaa, &colorQuality);
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, options.msaa, &depthQuality);
        if (!colorQuality || !depthQuality) {
            if(hdrChanged)SetHdrOutput(graphics_.hdrOutput,nullptr);
            if (queueSwap_ && options.lowLatency != graphics_.lowLatency) queueSwap_->SetMaximumFrameLatency(graphics_.lowLatency ? 1 : 3);
            if (error) *error = "Requested MSAA sample count is unavailable on this device"; return false;
        }
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        if (!CreateOffscreen(width_, height_, options.msaa)) {
            if(hdrChanged)SetHdrOutput(graphics_.hdrOutput,nullptr);
            if (queueSwap_ && options.lowLatency != graphics_.lowLatency) queueSwap_->SetMaximumFrameLatency(graphics_.lowLatency ? 1 : 3);
            if (error) *error = "Not enough GPU memory for requested graphics target"; return false;
        }
    }
    if(graphics_.temporalAA!=options.temporalAA)taaHistoryReady_=taaResolved_=false;
    const bool samplerPolicyChanged=graphics_.anisotropy!=options.anisotropy;
    graphics_ = options;
    hudReady_=false;hudError_.clear();
    sync_ = options.syncInterval;
    if(samplerPolicyChanged)samplers_.clear();
    // Blend keys already include actual alpha-to-coverage and all blend factors.
    // Other sampler state (filter/address/mips) is also fully represented in its key.
    return true;
}

bool D3D11Device::WindowSizePixels(int* w, int* h) const {
    if (offscreen_ || !hwnd_) return false;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    *w = cr.right > 0 ? cr.right : 1;
    *h = cr.bottom > 0 ? cr.bottom : 1;
    return true;
}


bool D3D11Device::CreatePipelines(std::string* error) {
    ComPtr<ID3DBlob> vsb, psb, err;
    const D3D_SHADER_MACRO shadowDefines[]={{"RASTER_SHADOW_MAP","1"},{nullptr,nullptr}};
    if (FAILED(CompileShaderCached(kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "model", shadowDefines, nullptr, "vsmain", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(CompileShaderCached(kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "model", shadowDefines, nullptr, "psmain", "ps_5_0", 0, 0, &psb, &err))) {
        if (error) *error = err ? static_cast<const char*>(err->GetBufferPointer()) : "shader compile failed";
        return false;
    }
    device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_);
    device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    device_->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_);
    D3D11_BUFFER_DESC cb = { sizeof(ModelConstants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    device_->CreateBuffer(&cb, nullptr, &constants_);
    D3D11_BUFFER_DESC shadowCb={sizeof(ShadowConstants),D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER};
    const ShadowConstants shadowZero{};D3D11_SUBRESOURCE_DATA shadowData{};shadowData.pSysMem=&shadowZero;
    if(FAILED(device_->CreateBuffer(&shadowCb,&shadowData,&shadowMapCb_)))return false;

    ComPtr<ID3DBlob> svs, sps;
    if (FAILED(CompileShaderCached(kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "sprite", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &svs, &err)) ||
        FAILED(CompileShaderCached(kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "sprite", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &sps, &err))) {
        if (error) *error = err ? static_cast<const char*>(err->GetBufferPointer()) : "sprite shader compile failed";
        return false;
    }
    device_->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(), nullptr, &spriteVs_);
    device_->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(), nullptr, &spritePs_);
    const D3D11_INPUT_ELEMENT_DESC sil[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },  // ARGB dword
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    device_->CreateInputLayout(sil, 3, svs->GetBufferPointer(), svs->GetBufferSize(), &spriteLayout_);
    D3D11_BUFFER_DESC scb = { sizeof(SpriteConstants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    device_->CreateBuffer(&scb, nullptr, &spriteCb_);

    // D3DCULL 1 none / 2 CW / 3 CCW (screen-space winding; D3D11 front faces are clockwise, so D3DCULL_CW culls front
    // faces; checked on the name-entry room, whose walls enclose the camera)
    const D3D11_CULL_MODE modes[3] = { D3D11_CULL_NONE, D3D11_CULL_FRONT, D3D11_CULL_BACK };
    for (int i = 0; i < 3; ++i) {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = modes[i];
        desc.DepthClipEnable = TRUE;
        desc.MultisampleEnable = TRUE;
        device_->CreateRasterizerState(&desc, &rasterCull_[i]);
    }
    raster_ = rasterCull_[0];
    {  // Also compiled headless so regression captures exercise the real final effects.
        ComPtr<ID3DBlob> pvs, pps;
        if (FAILED(CompileShaderCached(kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "present", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &pvs, &err)) ||
            FAILED(CompileShaderCached(kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "present", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &pps, &err))) {
            if (error) *error = err ? static_cast<const char*>(err->GetBufferPointer()) : "present shader compile failed";
            return false;
        }
        device_->CreateVertexShader(pvs->GetBufferPointer(), pvs->GetBufferSize(), nullptr, &presentVs_);
        device_->CreatePixelShader(pps->GetBufferPointer(), pps->GetBufferSize(), nullptr, &presentPs_);
        D3D11_BUFFER_DESC pcb = { sizeof(PresentConstants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
        device_->CreateBuffer(&pcb, nullptr, &presentCb_);
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device_->CreateSamplerState(&sd, &presentSampler_);
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = FALSE;
        device_->CreateDepthStencilState(&dd, &presentDepth_);
        if (!presentVs_ || !presentPs_ || !presentCb_ || !presentSampler_ || !presentDepth_) {
            if (error) *error = "present pipeline creation failed";
            return false;
        }
    }
    return vs_ && ps_ && layout_ && constants_ && spriteVs_ && spritePs_ && spriteLayout_ && spriteCb_ && CreatePostPipelines(error);
}

bool D3D11Device::CreatePostPipelines(std::string* error) {
    const auto smaa = SmaaShaderSource(), fsr = FsrShaderSource();
    const char* entries[] = {"edgeps","weightps","blendps","easups","rcasps"};
    for (int i = 0; i < 5; ++i) {
        const auto& source = i < 3 ? smaa : fsr;
        ComPtr<ID3DBlob> blob, errors;
        if (FAILED(CompileShaderCached(source.data(),source.size(),"postprocess",nullptr,nullptr,entries[i],"ps_5_0",0,0,&blob,&errors)) ||
            FAILED(device_->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,i<3?&smaaPs_[i]:&fsrPs_[i-3]))) {
            if (error) *error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "postprocess shader creation failed";
            return false;
        }
    }
    ComPtr<ID3DBlob> depthVertex,depthPixel,depthError;
    if(FAILED(CompileShaderCached(kDepthShaderHlsl,sizeof(kDepthShaderHlsl)-1,"depth",nullptr,nullptr,"vsmain","vs_5_0",0,0,&depthVertex,&depthError)) ||
       FAILED(CompileShaderCached(kDepthShaderHlsl,sizeof(kDepthShaderHlsl)-1,"depth",nullptr,nullptr,"psmain","ps_5_0",0,0,&depthPixel,&depthError)) ||
       FAILED(device_->CreateVertexShader(depthVertex->GetBufferPointer(),depthVertex->GetBufferSize(),nullptr,&depthVs_)) ||
       FAILED(device_->CreatePixelShader(depthPixel->GetBufferPointer(),depthPixel->GetBufferSize(),nullptr,&depthPs_))) {
        if(error)*error=depthError?static_cast<const char*>(depthError->GetBufferPointer()):"Opaque-depth shader creation failed";return false;
    }
    const D3D_SHADER_MACRO aoDefines[]={{"AO_MODULATE_ONLY","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> aoBlob;
    if(FAILED(CompileShaderCached(kAoShaderHlsl,sizeof(kAoShaderHlsl)-1,"ao",aoDefines,nullptr,"psmain","ps_5_0",0,0,&aoBlob,&depthError)) ||
       FAILED(device_->CreatePixelShader(aoBlob->GetBufferPointer(),aoBlob->GetBufferSize(),nullptr,&aoPs_))) {
        if(error)*error=depthError?static_cast<const char*>(depthError->GetBufferPointer()):"AO shader creation failed";return false;
    }
    D3D11_BUFFER_DESC aoBuffer={sizeof(AoConstants),D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER};
    if(FAILED(device_->CreateBuffer(&aoBuffer,nullptr,&aoCb_)))return false;
    ComPtr<ID3DBlob> motionVertex,motionPixel;
    if(FAILED(CompileShaderCached(kMotionShaderHlsl,sizeof(kMotionShaderHlsl)-1,"motion",nullptr,nullptr,"vsmain","vs_5_0",0,0,&motionVertex,&depthError)) ||
       FAILED(CompileShaderCached(kMotionShaderHlsl,sizeof(kMotionShaderHlsl)-1,"motion",nullptr,nullptr,"psmain","ps_5_0",0,0,&motionPixel,&depthError)) ||
       FAILED(device_->CreateVertexShader(motionVertex->GetBufferPointer(),motionVertex->GetBufferSize(),nullptr,&motionVs_)) ||
       FAILED(device_->CreatePixelShader(motionPixel->GetBufferPointer(),motionPixel->GetBufferSize(),nullptr,&motionPs_))) {
        if(error)*error=depthError?static_cast<const char*>(depthError->GetBufferPointer()):"Motion shader creation failed";return false;
    }
    D3D11_BUFFER_DESC motionBuffer={sizeof(MotionConstants),D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER};
    if(FAILED(device_->CreateBuffer(&motionBuffer,nullptr,&motionCb_)))return false;
    const auto reactiveModel=ReactiveModelShaderSource(true),reactiveSprite=ReactiveSpriteShaderSource();
    const std::string taaSources[]={kTaaShaderHlsl,kTaaCopyShaderHlsl,reactiveModel,reactiveSprite};
    ID3D11PixelShader** taaShaders[]={taaPs_.ReleaseAndGetAddressOf(),taaCopyPs_.ReleaseAndGetAddressOf(),reactiveModelPs_.ReleaseAndGetAddressOf(),reactiveSpritePs_.ReleaseAndGetAddressOf()};
    for(int index=0;index<4;++index){ComPtr<ID3DBlob> blob;
        if(FAILED(CompileShaderCached(taaSources[index].data(),taaSources[index].size(),"taa",nullptr,nullptr,"psmain","ps_5_0",0,0,&blob,&depthError))||
           FAILED(device_->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,taaShaders[index]))) {
            if(error)*error=depthError?static_cast<const char*>(depthError->GetBufferPointer()):"Temporal shader creation failed";return false;}}
    D3D11_BUFFER_DESC taaBuffer={sizeof(TaaConstants),D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER};
    if(FAILED(device_->CreateBuffer(&taaBuffer,nullptr,&taaCb_)))return false;
    D3D11_BUFFER_DESC buffer = {32,D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(device_->CreateBuffer(&buffer,nullptr,&postCb_))) return false;
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device_->CreateSamplerState(&sampler,&postPointSampler_))) return false;
    auto lookup = [this](int w,int h,const uint8_t* data,int channels,ComPtr<ID3D11ShaderResourceView>& result) {
        RgbaImage image; image.width=w; image.height=h; image.pixels.resize(static_cast<size_t>(w)*h*4,0);
        for(int p=0;p<w*h;++p) {for(int c=0;c<channels;++c)image.pixels[p*4+c]=data[p*channels+c];image.pixels[p*4+3]=255;}
        const auto handle=CreateTexture(image); if(!handle)return false;
        result=textures_.back();textures_.pop_back();return true;
    };
    return lookup(AREATEX_WIDTH,AREATEX_HEIGHT,areaTexBytes,2,smaaArea_) && lookup(SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,searchTexBytes,1,smaaSearch_);
}

bool D3D11Device::EnsurePostTarget(PostTarget& target,int w,int h,DXGI_FORMAT format) {
    if(target.texture && target.width==w && target.height==h)return true;
    PostTarget next; D3D11_TEXTURE2D_DESC desc{};
    desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;desc.Format=format;
    desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(device_->CreateTexture2D(&desc,nullptr,&next.texture)) ||
       FAILED(device_->CreateRenderTargetView(next.texture.Get(),nullptr,&next.rtv)) ||
       FAILED(device_->CreateShaderResourceView(next.texture.Get(),nullptr,&next.srv)))return false;
    next.width=w;next.height=h;target=std::move(next);return true;
}

bool D3D11Device::EnsureDepthTargets() {
    if(opaqueDepthDsv_&&opaqueWidth_==width_&&opaqueHeight_==height_)return true;
    if(!EnsurePostTarget(opaqueDepthColor_,width_,height_,DXGI_FORMAT_R32_FLOAT))return false;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width_;desc.Height=height_;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_D32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11DepthStencilView> view;
    if(FAILED(device_->CreateTexture2D(&desc,nullptr,&texture))||FAILED(device_->CreateDepthStencilView(texture.Get(),nullptr,&view)))return false;
    opaqueDepth_=texture;opaqueDepthDsv_=view;opaqueWidth_=width_;opaqueHeight_=height_;opaqueCleared_=opaqueReady_=false;return true;
}

void D3D11Device::DrawDepthPrepass(const GpuDrawList& list) {
    if((!graphics_.softParticles&&!graphics_.ambientOcclusion&&!SceneEffectsEnabled(graphics_)&&!list.temporal.enabled)||!list.opaqueDrawCount||!EnsureDepthTargets())return;
    ID3D11ShaderResourceView* empty[3]={};ctx_->PSSetShaderResources(0,3,empty);
    ID3D11RenderTargetView* target=opaqueDepthColor_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,opaqueDepthDsv_.Get());
    if(!opaqueCleared_){const float clear[4]={};ctx_->ClearRenderTargetView(target,clear);ctx_->ClearDepthStencilView(opaqueDepthDsv_.Get(),D3D11_CLEAR_DEPTH,0,0);opaqueCleared_=true;}
    const D3D11_VIEWPORT viewport={0,0,static_cast<float>(list.width),static_cast<float>(list.height),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->IASetInputLayout(layout_.Get());ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(depthVs_.Get(),nullptr,0);ctx_->PSSetShader(depthPs_.Get(),nullptr,0);
    ID3D11Buffer* constants=constants_.Get();ctx_->VSSetConstantBuffers(0,1,&constants);ctx_->PSSetConstantBuffers(0,1,&constants);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);
    for(size_t i=0;i<list.draws.size()&&i<list.opaqueDrawCount;++i){
        const auto& draw=list.draws[i];const auto& state=draw.state;
        if(draw.pipeline!=GpuDraw::Pipeline::Model||state.blendEnable||!state.zwrite)continue;
        ID3D11Buffer* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_.Get():dynVb_.Get();
        const UINT stride=sizeof(game::ModelVertex),offset=0;ctx_->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        ctx_->UpdateSubresource(constants_.Get(),0,nullptr,&list.model[draw.constants],0,0);
        ctx_->RSSetState(rasterCull_[state.cull>=1&&state.cull<=3?state.cull-1:0].Get());ctx_->OMSetDepthStencilState(DepthState(state.zfunc,true),0);
        auto* sampler=Sampler(state.linear!=0,state.addrU,state.addrV,state.mipmapped);ctx_->PSSetSamplers(0,1,&sampler);
        ID3D11ShaderResourceView* texture=draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1].Get():nullptr;ctx_->PSSetShaderResources(0,1,&texture);
        ctx_->Draw(draw.count,draw.first);opaqueReady_=true;
    }
    ctx_->PSSetShaderResources(0,3,empty);target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
}

bool D3D11Device::EnsureMotionTargets() {
    if(motionDepth_&&motionWidth_==width_&&motionHeight_==height_)return true;
    if(!EnsurePostTarget(motionColor_,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT)||
       !EnsurePostTarget(previousDepthColor_,width_,height_,DXGI_FORMAT_R32_FLOAT))return false;
    D3D11_TEXTURE2D_DESC description{};description.Width=width_;description.Height=height_;
    description.MipLevels=description.ArraySize=1;description.SampleDesc.Count=1;
    description.Format=DXGI_FORMAT_D32_FLOAT;description.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11DepthStencilView> view;
    if(FAILED(device_->CreateTexture2D(&description,nullptr,&texture))||FAILED(device_->CreateDepthStencilView(texture.Get(),nullptr,&view)))return false;
    motionDepth_=texture;motionDepthDsv_=view;motionWidth_=width_;motionHeight_=height_;
    motionReady_=previousDepthReady_=false;return true;
}

void D3D11Device::DrawMotion(const GpuDrawList& list) {
    if(!list.temporal.enabled||!EnsureMotionTargets())return;
    if(list.temporal.reset||(!motionCleared_&&list.temporal.frameId!=motionFrameId_+1))previousDepthReady_=false;
    const size_t bytes=list.motionVertices.size()*sizeof(MotionVertex);
    if(bytes>UINT_MAX)return;
    if(bytes>motionCapacity_) {
        D3D11_BUFFER_DESC description={static_cast<UINT>(bytes),D3D11_USAGE_DYNAMIC,D3D11_BIND_VERTEX_BUFFER,D3D11_CPU_ACCESS_WRITE};
        ComPtr<ID3D11Buffer> next;if(FAILED(device_->CreateBuffer(&description,nullptr,&next)))return;
        motionVb_=next;motionCapacity_=static_cast<UINT>(bytes);
    }
    if(bytes){D3D11_MAPPED_SUBRESOURCE mapping{};if(FAILED(ctx_->Map(motionVb_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapping)))return;
        std::memcpy(mapping.pData,list.motionVertices.data(),bytes);ctx_->Unmap(motionVb_.Get(),0);}
    ID3D11ShaderResourceView* empty[3]={};ctx_->PSSetShaderResources(0,3,empty);
    ID3D11RenderTargetView* target=motionColor_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,motionDepthDsv_.Get());
    if(!motionCleared_){const float clear[4]={};ctx_->ClearRenderTargetView(target,clear);ctx_->ClearDepthStencilView(motionDepthDsv_.Get(),D3D11_CLEAR_DEPTH,0,0);motionCleared_=true;}
    const D3D11_VIEWPORT viewport={0,0,static_cast<float>(list.width),static_cast<float>(list.height),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->IASetInputLayout(layout_.Get());ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* vertices=motionVb_.Get();const UINT stride=sizeof(MotionVertex),offset=0;ctx_->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
    ctx_->VSSetShader(motionVs_.Get(),nullptr,0);ctx_->PSSetShader(motionPs_.Get(),nullptr,0);
    ID3D11Buffer* buffer=motionCb_.Get();ctx_->VSSetConstantBuffers(0,1,&buffer);ctx_->PSSetConstantBuffers(0,1,&buffer);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);
    for(const auto& draw:list.motionDraws) {
        if(draw.constants>=list.motionConstants.size()||static_cast<size_t>(draw.first)+draw.count>list.motionVertices.size())continue;
        auto constants=list.motionConstants[draw.constants];constants.target[3]=previousDepthReady_?1.f:0.f;
        ctx_->UpdateSubresource(motionCb_.Get(),0,nullptr,&constants,0,0);
        const auto& state=draw.state;ctx_->RSSetState(rasterCull_[state.cull>=1&&state.cull<=3?state.cull-1:0].Get());
        ctx_->OMSetDepthStencilState(DepthState(state.zfunc,true),0);
        auto* sampler=Sampler(state.linear!=0,state.addrU,state.addrV,state.mipmapped);ctx_->PSSetSamplers(0,1,&sampler);
        ID3D11ShaderResourceView* textures[]={draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1].Get():nullptr,previousDepthReady_?previousDepthColor_.srv.Get():nullptr};
        ctx_->PSSetShaderResources(0,2,textures);ctx_->Draw(draw.count,draw.first);
    }
    motionFrameId_=list.temporal.frameId;motionReady_=true;temporal_=list.temporal;
    ctx_->PSSetShaderResources(0,3,empty);target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
}

bool D3D11Device::EnsureTaaTargets() {
    return EnsurePostTarget(taaResult_,width_,height_)&&EnsurePostTarget(taaHistory_,width_,height_)&&
        EnsurePostTarget(reactive_,width_,height_,DXGI_FORMAT_R8_UNORM)&&EnsurePostTarget(previousReactive_,width_,height_,DXGI_FORMAT_R8_UNORM);
}

void D3D11Device::DrawReactive(const GpuDrawList& list) {
    if(!graphics_.temporalAA||sceneProcessed_||!EnsureTaaTargets()||!EnsureDepthTargets())return;
    ID3D11ShaderResourceView* empty[5]={};ctx_->PSSetShaderResources(0,5,empty);
    ID3D11RenderTargetView* target=reactive_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,opaqueDepthDsv_.Get());
    if(!reactiveCleared_){const float clear[4]={};ctx_->ClearRenderTargetView(target,clear);reactiveCleared_=true;}
    const D3D11_VIEWPORT viewport={0,0,static_cast<float>(list.width),static_cast<float>(list.height),0,1};ctx_->RSSetViewports(1,&viewport);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);
    for(const auto& draw:list.draws) {
        if(!draw.reactive||draw.hud)continue;
        const bool model=draw.pipeline==GpuDraw::Pipeline::Model;
        ID3D11Buffer* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_.Get():draw.stream==GpuDraw::Stream::Dynamic?dynVb_.Get():spriteVb_.Get();
        if(!vertices)continue;
        const UINT stride=model?sizeof(game::ModelVertex):sizeof(game::TLVertex),offset=0;ctx_->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        ctx_->IASetInputLayout(model?layout_.Get():spriteLayout_.Get());ctx_->IASetPrimitiveTopology(model?D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx_->VSSetShader(model?depthVs_.Get():spriteVs_.Get(),nullptr,0);ctx_->PSSetShader(model?reactiveModelPs_.Get():reactiveSpritePs_.Get(),nullptr,0);
        ID3D11Buffer* constants=model?constants_.Get():spriteCb_.Get();ctx_->VSSetConstantBuffers(0,1,&constants);ctx_->PSSetConstantBuffers(0,1,&constants);
        ctx_->UpdateSubresource(constants,0,nullptr,model?static_cast<const void*>(&list.model[draw.constants]):static_cast<const void*>(&list.sprite[draw.constants]),0,0);
        const auto& state=draw.state;ctx_->RSSetState(rasterCull_[state.cull>=1&&state.cull<=3?state.cull-1:0].Get());ctx_->OMSetDepthStencilState(DepthState(state.zfunc,false),0);
        auto* sampler=Sampler(state.linear!=0,state.addrU,state.addrV,state.mipmapped);ctx_->PSSetSamplers(0,1,&sampler);
        ID3D11ShaderResourceView* texture=draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1].Get():nullptr;ctx_->PSSetShaderResources(0,1,&texture);ctx_->Draw(draw.count,draw.first);
    }
    ctx_->PSSetShaderResources(0,5,empty);target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
}

bool D3D11Device::ResolveTemporal(std::string* error) {
    if(!graphics_.temporalAA||sceneProcessed_)return true;
    if(!frameActive_||!motionReady_||!temporal_.enabled||!reactiveCleared_) {
        taaHistoryReady_=false;if(error)*error="Temporal AA requires current geometry motion and reactive coverage";return false;
    }
    ResolveColor();
    ID3D11RenderTargetView* target=taaResult_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,nullptr);
    const D3D11_VIEWPORT viewport={0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->IASetInputLayout(nullptr);ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx_->RSSetState(rasterCull_[0].Get());
    ctx_->VSSetShader(presentVs_.Get(),nullptr,0);ctx_->PSSetShader(taaPs_.Get(),nullptr,0);
    TaaConstants constants{{static_cast<float>(width_),static_cast<float>(height_),1.f/width_,1.f/height_},
        {taaHistoryReady_&&previousDepthReady_&&!temporal_.reset?1.f:0.f,temporal_.jitterPixels[0],temporal_.jitterPixels[1],.9f}};
    ctx_->UpdateSubresource(taaCb_.Get(),0,nullptr,&constants,0,0);ID3D11Buffer* buffer=taaCb_.Get();ctx_->PSSetConstantBuffers(0,1,&buffer);
    ID3D11ShaderResourceView* textures[]={colorSrv_.Get(),taaHistory_.srv.Get(),motionColor_.srv.Get(),reactive_.srv.Get(),previousReactive_.srv.Get()};ctx_->PSSetShaderResources(0,5,textures);
    ID3D11SamplerState* sampler=presentSampler_.Get();ctx_->PSSetSamplers(0,1,&sampler);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);ctx_->OMSetDepthStencilState(presentDepth_.Get(),0);ctx_->Draw(3,0);
    ID3D11ShaderResourceView* empty[5]={};ctx_->PSSetShaderResources(0,5,empty);
    target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,nullptr);ctx_->PSSetShader(taaCopyPs_.Get(),nullptr,0);
    ID3D11ShaderResourceView* resolved=taaResult_.srv.Get();ctx_->PSSetShaderResources(0,1,&resolved);ctx_->Draw(3,0);
    ctx_->PSSetShaderResources(0,5,empty);ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
    taaResolved_=sceneProcessed_=true;return true;
}

void D3D11Device::DrawAmbientOcclusion(const GpuDrawList& list) {
    if(!graphics_.ambientOcclusion||!opaqueReady_||!list.opaqueDrawCount)return;
    AoConstants constants{};bool projection=false;
    for(size_t i=0;i<list.draws.size()&&i<list.opaqueDrawCount;++i) {
        const auto& draw=list.draws[i];if(draw.pipeline!=GpuDraw::Pipeline::Model)continue;
        const auto& model=list.model[draw.constants];
        if(model.localCount[2]>.5f&&MakeAoConstants(width_,height_,model.projection,graphics_.ambientOcclusion,&constants)){projection=true;break;}
    }
    if(!projection)return;
    ID3D11RenderTargetView* target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,nullptr);
    const D3D11_VIEWPORT viewport={0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->RSSetState(rasterCull_[0].Get());ctx_->IASetInputLayout(nullptr);ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(presentVs_.Get(),nullptr,0);ctx_->PSSetShader(aoPs_.Get(),nullptr,0);
    ctx_->UpdateSubresource(aoCb_.Get(),0,nullptr,&constants,0,0);ID3D11Buffer* buffer=aoCb_.Get();ctx_->PSSetConstantBuffers(0,1,&buffer);
    ID3D11ShaderResourceView* textures[]={nullptr,opaqueDepthColor_.srv.Get()};ctx_->PSSetShaderResources(0,2,textures);
    const float blend[4]={};ctx_->OMSetBlendState(BlendState(true,9,1),blend,0xffffffff);ctx_->OMSetDepthStencilState(presentDepth_.Get(),0);
    ctx_->Draw(3,0);
    ID3D11ShaderResourceView* empty[3]={};ctx_->PSSetShaderResources(0,3,empty);ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
}

void D3D11Device::DrawPost(PostTarget& target,ID3D11PixelShader* shader,ID3D11ShaderResourceView* a,
                         ID3D11ShaderResourceView* b,ID3D11ShaderResourceView* c,const float cb[8]) {
    ID3D11RenderTargetView* rt=target.rtv.Get();ctx_->OMSetRenderTargets(1,&rt,nullptr);
    const float clear[4]={0,0,0,0};ctx_->ClearRenderTargetView(rt,clear);
    D3D11_VIEWPORT vp={0,0,static_cast<float>(target.width),static_cast<float>(target.height),0,1};ctx_->RSSetViewports(1,&vp);
    ctx_->RSSetState(rasterCull_[0].Get());ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(presentVs_.Get(),nullptr,0);ctx_->PSSetShader(shader,nullptr,0);
    ctx_->UpdateSubresource(postCb_.Get(),0,nullptr,cb,0,0);ID3D11Buffer* constants=postCb_.Get();ctx_->PSSetConstantBuffers(0,1,&constants);
    ID3D11SamplerState* samplers[]={presentSampler_.Get(),postPointSampler_.Get(),presentSampler_.Get()};ctx_->PSSetSamplers(0,3,samplers);
    ID3D11ShaderResourceView* textures[]={a,b,c};ctx_->PSSetShaderResources(0,3,textures);
    const float blend[4]={0,0,0,0};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);ctx_->OMSetDepthStencilState(presentDepth_.Get(),0);
    ctx_->Draw(3,0);
    ID3D11ShaderResourceView* empty[3]={};ctx_->PSSetShaderResources(0,3,empty);ctx_->OMSetRenderTargets(0,nullptr,nullptr);
}

bool D3D11Device::EnsureCmaa(std::string* error) {
    if(!cmaaCs_[4]) {
        const auto source=CmaaShaderSource();
        const char* entries[]={"ResetCS","EdgesColor2x2CS","ComputeDispatchArgsCS","ProcessCandidatesCS","DeferredColorApply2x2CS"};
        ComPtr<ID3D11ComputeShader> shaders[5];
        for(int index=0;index<5;++index){ComPtr<ID3DBlob> blob,errors;
            if(FAILED(CompileShaderCached(source.data(),source.size(),"Intel CMAA2",nullptr,nullptr,entries[index],"cs_5_0",0,0,&blob,&errors))||
               FAILED(device_->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shaders[index]))) {
                if(error)*error=errors?static_cast<const char*>(errors->GetBufferPointer()):"CMAA2 compute shader creation failed";return false;}}
        for(int index=0;index<5;++index)cmaaCs_[index]=shaders[index];
    }
    if(cmaa_.output.texture&&cmaa_.output.width==width_&&cmaa_.output.height==height_)return true;
    const uint64_t pixels=static_cast<uint64_t>(width_)*height_;if(pixels>UINT_MAX/8){if(error)*error="CMAA2 working image is too large";return false;}
    CmaaState next;
    auto texture=[&](int width,int height,DXGI_FORMAT format,ComPtr<ID3D11Texture2D>& result,ComPtr<ID3D11UnorderedAccessView>& uav,bool sampled=false){
        D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;description.MipLevels=description.ArraySize=1;description.Format=format;description.SampleDesc.Count=1;
        description.BindFlags=D3D11_BIND_UNORDERED_ACCESS|(sampled?D3D11_BIND_SHADER_RESOURCE:0);
        return SUCCEEDED(device_->CreateTexture2D(&description,nullptr,&result))&&SUCCEEDED(device_->CreateUnorderedAccessView(result.Get(),nullptr,&uav));};
    if(!texture(width_,height_,DXGI_FORMAT_R8G8B8A8_UNORM,next.output.texture,next.uav[0],true)||
       FAILED(device_->CreateShaderResourceView(next.output.texture.Get(),nullptr,&next.output.srv))||
       !texture((width_+1)/2,height_,DXGI_FORMAT_R8_UINT,next.edges,next.uav[1])||
       !texture((width_+1)/2,(height_+1)/2,DXGI_FORMAT_R32_UINT,next.heads,next.uav[5])){if(error)*error="CMAA2 texture allocation failed";return false;}
    const UINT quads=static_cast<UINT>((width_+1)/2)*((height_+1)/2);
    const UINT bytes[]={static_cast<UINT>(pixels*4),quads*4,static_cast<UINT>(pixels*8),64,16};
    const int slots[]={2,3,4,6,7};
    for(int index=0;index<5;++index){D3D11_BUFFER_DESC description{};description.ByteWidth=bytes[index];description.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS|(index==4?D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS:0);
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=bytes[index]/4;view.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        if(FAILED(device_->CreateBuffer(&description,nullptr,&next.buffers[index]))||FAILED(device_->CreateUnorderedAccessView(next.buffers[index].Get(),&view,&next.uav[slots[index]]))){if(error)*error="CMAA2 working buffer allocation failed";return false;}}
    next.output.width=width_;next.output.height=height_;cmaa_=std::move(next);return true;
}

ID3D11ShaderResourceView* D3D11Device::DrawCmaa() {
    ctx_->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* empty[5]={};ctx_->PSSetShaderResources(0,5,empty);
    ctx_->CopyResource(cmaa_.output.texture.Get(),color_.Get());
    ID3D11UnorderedAccessView* views[8];for(int slot=0;slot<8;++slot)views[slot]=cmaa_.uav[slot].Get();
    views[0]=nullptr;ctx_->CSSetUnorderedAccessViews(0,8,views,nullptr);
    ID3D11ShaderResourceView* input=colorSrv_.Get();ctx_->CSSetShaderResources(0,1,&input);
    ctx_->CSSetShader(cmaaCs_[0].Get(),nullptr,0);ctx_->Dispatch(1,1,1);
    ctx_->CSSetShader(cmaaCs_[1].Get(),nullptr,0);ctx_->Dispatch((width_+27)/28,(height_+27)/28,1);
    ctx_->CSSetShader(cmaaCs_[2].Get(),nullptr,0);ctx_->Dispatch(2,1,1);
    ctx_->CSSetShader(cmaaCs_[3].Get(),nullptr,0);ctx_->DispatchIndirect(cmaa_.buffers[4].Get(),0);
    ctx_->CSSetShader(cmaaCs_[2].Get(),nullptr,0);ctx_->Dispatch(1,2,1);
    views[0]=cmaa_.uav[0].Get();ctx_->CSSetUnorderedAccessViews(0,8,views,nullptr);
    ctx_->CSSetShader(cmaaCs_[4].Get(),nullptr,0);ctx_->DispatchIndirect(cmaa_.buffers[4].Get(),0);
    ID3D11UnorderedAccessView* noViews[8]={};ctx_->CSSetUnorderedAccessViews(0,8,noViews,nullptr);ctx_->CSSetShaderResources(0,1,empty);ctx_->CSSetShader(nullptr,nullptr,0);
    return cmaa_.output.srv.Get();
}

ID3D11ShaderResourceView* D3D11Device::PostSource(int outW,int outH,int* w,int* h,bool* fsr) {
    if(hudReady_){*w=hudColor_.width;*h=hudColor_.height;*fsr=false;return hudColor_.srv.Get();}
    ResolveColor(); *w=width_;*h=height_;*fsr=false;
    auto* source=colorSrv_.Get();
    if(graphics_.postAA==3&&EnsureCmaa())source=DrawCmaa();
    if(graphics_.postAA==2 && EnsurePostTarget(smaaEdge_,*w,*h) && EnsurePostTarget(smaaWeight_,*w,*h) && EnsurePostTarget(smaaColor_,*w,*h)) {
        const float constants[8]={1.f / *w,1.f / *h,static_cast<float>(*w),static_cast<float>(*h),0,0,0,0};
        DrawPost(smaaEdge_,smaaPs_[0].Get(),source,smaaArea_.Get(),smaaSearch_.Get(),constants);
        DrawPost(smaaWeight_,smaaPs_[1].Get(),smaaEdge_.srv.Get(),smaaArea_.Get(),smaaSearch_.Get(),constants);
        DrawPost(smaaColor_,smaaPs_[2].Get(),source,smaaWeight_.srv.Get(),smaaSearch_.Get(),constants);
        source=smaaColor_.srv.Get();
    }
    if(graphics_.spatialUpscaler==1 && outW>=*w && outH>=*h && (outW>*w || outH>*h) && EnsurePostTarget(fsrColor_,outW,outH)) {
        float constants[8]={static_cast<float>(*w),static_cast<float>(*h),0,0,static_cast<float>(outW),static_cast<float>(outH),0,0};
        DrawPost(fsrColor_,fsrPs_[0].Get(),source,smaaArea_.Get(),smaaSearch_.Get(),constants);
        source=fsrColor_.srv.Get();*w=outW;*h=outH;*fsr=true;
        if(graphics_.sharpening && EnsurePostTarget(fsrSharp_,outW,outH)) {
            constants[0]=static_cast<float>(outW);constants[1]=static_cast<float>(outH);constants[6]=2.5f-graphics_.sharpening*.75f;
            DrawPost(fsrSharp_,fsrPs_[1].Get(),source,smaaArea_.Get(),smaaSearch_.Get(),constants);source=fsrSharp_.srv.Get();
        }
    }
    return source;
}

GpuTexture D3D11Device::CreateTexture(const RgbaImage& img, bool alphaWeighted) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.size() != static_cast<size_t>(img.width) * img.height * 4) return 0;
    const auto mips = BuildTextureMips(img, alphaWeighted);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = img.width;
    td.Height = img.height;
    td.MipLevels = static_cast<UINT>(mips.size() + 1);
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    init.push_back({img.pixels.data(), static_cast<UINT>(img.width * 4), 0});
    for (const auto& mip : mips) init.push_back({mip.pixels.data(), static_cast<UINT>(mip.width * 4), 0});
    ComPtr<ID3D11Texture2D> t;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateTexture2D(&td, init.data(), &t)) || FAILED(device_->CreateShaderResourceView(t.Get(), nullptr, &srv))) return 0;
    textures_.push_back(srv);
    return textures_.size();
}

void D3D11Device::SetStaticVertices(const game::ModelVertex* v, size_t count) {
    staticVb_.Reset();
    if (!count) return;
    D3D11_BUFFER_DESC bd = { static_cast<UINT>(count * sizeof(game::ModelVertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA init = { v };
    device_->CreateBuffer(&bd, &init, &staticVb_);
}

void D3D11Device::SetDynamicVertices(const game::ModelVertex* v, size_t count) {
    if (!count) return;
    const UINT bytes = static_cast<UINT>(count * sizeof(game::ModelVertex));
    if (bytes > dynVbCapacity_) {
        dynVb_.Reset();
        D3D11_BUFFER_DESC bd = { bytes * 2, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
        if (SUCCEEDED(device_->CreateBuffer(&bd, nullptr, &dynVb_))) dynVbCapacity_ = bytes * 2;
    }
    if (!dynVb_) return;
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(dynVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, v, bytes);
        ctx_->Unmap(dynVb_.Get(), 0);
    }
}

void D3D11Device::SetSpriteVertices(const game::TLVertex* v, size_t count) {
    if (!count) return;
    const UINT need = static_cast<UINT>(count);
    if (need > spriteVbCapacity_) {
        D3D11_BUFFER_DESC bd = { need * static_cast<UINT>(sizeof(game::TLVertex)), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER,
                                 D3D11_CPU_ACCESS_WRITE };
        spriteVb_.Reset();
        spriteVbCapacity_ = 0;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &spriteVb_))) return;
        spriteVbCapacity_ = need;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(spriteVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, v, count * sizeof(game::TLVertex));
    ctx_->Unmap(spriteVb_.Get(), 0);
}

ID3D11BlendState* D3D11Device::BlendState(bool enable, int src, int dst, bool coverage) {
    const int key = (enable ? 1 : 0) | (src << 1) | (dst << 8) | (coverage ? 1 << 16 : 0);
    auto& s = blends_[key];
    if (!s) {
        D3D11_BLEND_DESC bd = {};
        bd.AlphaToCoverageEnable = coverage;
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable = enable;
        rt.SrcBlend = rt.SrcBlendAlpha = ToD3D11Blend(src);
        rt.DestBlend = rt.DestBlendAlpha = ToD3D11Blend(dst);
        // Alpha factors may not use colour blends in D3D11; map them to their alpha counterparts.
        auto alphaOnly = [](D3D11_BLEND b) {
            if (b == D3D11_BLEND_SRC_COLOR) return D3D11_BLEND_SRC_ALPHA;
            if (b == D3D11_BLEND_INV_SRC_COLOR) return D3D11_BLEND_INV_SRC_ALPHA;
            if (b == D3D11_BLEND_DEST_COLOR) return D3D11_BLEND_DEST_ALPHA;
            if (b == D3D11_BLEND_INV_DEST_COLOR) return D3D11_BLEND_INV_DEST_ALPHA;
            return b;
        };
        rt.SrcBlendAlpha = alphaOnly(rt.SrcBlendAlpha);
        rt.DestBlendAlpha = alphaOnly(rt.DestBlendAlpha);
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device_->CreateBlendState(&bd, &s);
    }
    return s.Get();
}

ID3D11DepthStencilState* D3D11Device::DepthState(int zfunc, bool zwrite) {
    const int key = zfunc | (zwrite ? 16 : 0);
    auto& s = depths_[key];
    if (!s) {
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = zwrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(ReversedDepthCompare(zfunc));
        device_->CreateDepthStencilState(&dd, &s);
    }
    return s.Get();
}

ID3D11SamplerState* D3D11Device::Sampler(bool linear, int addrU, int addrV, int mipmapped) {
    const int key = (linear ? 1 : 0) | (addrU << 1) | (addrV << 4) | (mipmapped << 8);
    auto& s = samplers_[key];
    if (!s) {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = mipmapped ? (linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : mipmapped==2 ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR)
                              : (linear ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT);
        if (mipmapped && graphics_.anisotropy > 1) {
            sd.Filter = D3D11_FILTER_ANISOTROPIC;
            sd.MaxAnisotropy = graphics_.anisotropy;
        }
        sd.AddressU = ToD3D11Address(addrU);
        sd.AddressV = ToD3D11Address(addrV);
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxLOD = mipmapped ? D3D11_FLOAT32_MAX : 0;
        device_->CreateSamplerState(&sd, &s);
    }
    return s.Get();
}

void D3D11Device::BindPipeline(GpuDraw::Pipeline p, int width, int height) {
    D3D11_VIEWPORT vp = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(raster_.Get());
    const bool model = p == GpuDraw::Pipeline::Model;
    ctx_->IASetInputLayout(model ? layout_.Get() : spriteLayout_.Get());
    ctx_->IASetPrimitiveTopology(model ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx_->VSSetShader(model ? vs_.Get() : spriteVs_.Get(), nullptr, 0);
    ctx_->PSSetShader(model ? ps_.Get() : spritePs_.Get(), nullptr, 0);
    ID3D11Buffer* cb = model ? constants_.Get() : spriteCb_.Get();
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetConstantBuffers(0, 1, &cb);
    if(model){ID3D11Buffer* shadow=shadowMapCb_.Get();ctx_->PSSetConstantBuffers(1,1,&shadow);}
}

void D3D11Device::Submit(const GpuDrawList& list) {
    StartGpuTiming();
    if(!hudReady_){
    DrawShadowMap(list);
    DrawDepthPrepass(list);
    if(!sceneEffectsApplied_&&list.temporalCamera.valid){sceneEffectsCamera_=list.temporalCamera;sceneEffectsFrame_=list.sceneEffects;for(int k=0;k<2;++k)sceneEffectsJitter_[k]=list.temporal.enabled?list.temporal.jitterPixels[k]:0.f;}
    DrawReflectionSurface(list);
    DrawMotion(list);
    DrawReactive(list);
    }
    if(hudReady_){ID3D11RenderTargetView* target=hudColor_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,hudDsv_.Get());}
    const float blendFactor[4] = { 0, 0, 0, 0 };
    bool bound = false;
    GpuDraw::Pipeline pipeline = GpuDraw::Pipeline::Model;
    bool streamBound = false;
    GpuDraw::Stream stream = GpuDraw::Stream::Static;
    size_t drawIndex=0;
    for (const GpuDraw& d : list.draws) {
        if(drawIndex++==list.opaqueDrawCount&&list.opaqueDrawCount){DrawAmbientOcclusion(list);bound=false;streamBound=false;}
        if (!bound || d.pipeline != pipeline) {
            BindPipeline(d.pipeline, list.width, list.height);
            bound = true;
            pipeline = d.pipeline;
            streamBound = false;
        }
        if (!streamBound || d.stream != stream) {
            ID3D11Buffer* vb = d.stream == GpuDraw::Stream::Static ? staticVb_.Get() : d.stream == GpuDraw::Stream::Dynamic ? dynVb_.Get() : spriteVb_.Get();
            const UINT stride = d.stream == GpuDraw::Stream::Sprite ? sizeof(game::TLVertex) : sizeof(game::ModelVertex), offset = 0;
            ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            streamBound = true;
            stream = d.stream;
        }
        GpuState s = d.state;
        bool coverage = false;
        if (d.pipeline == GpuDraw::Pipeline::Model) {
            if (graphics_.textureFilter) s.linear = graphics_.textureFilter == 2 ? 1 : 0;
            ModelConstants c = list.model[d.constants];
            c.depthEffects[3] = opaqueReady_ ? 1.0f : 0.0f;
            if(!d.normalRoughnessTexture || d.normalRoughnessTexture>textures_.size())c.materialParams[0]=0;
            c.sampling[0] = s.mipmapped && graphics_.anisotropy > 1 && !s.linear ? 1.0f : 0.0f;
            c.sampling[1] = static_cast<float>(s.addrU); c.sampling[2] = static_cast<float>(s.addrV);
            coverage = graphics_.alphaToCoverage && graphics_.msaa > 1 && !s.blendEnable && c.flags[2] >= 0;
            ctx_->UpdateSubresource(constants_.Get(), 0, nullptr, &c, 0, 0);
        } else ctx_->UpdateSubresource(spriteCb_.Get(), 0, nullptr, &list.sprite[d.constants], 0, 0);
        ctx_->OMSetBlendState(BlendState(s.blendEnable != 0, s.srcBlend, s.dstBlend, coverage), blendFactor, 0xFFFFFFFF);
        ctx_->RSSetState(rasterCull_[(s.cull >= 1 && s.cull <= 3) ? s.cull - 1 : 0].Get());
        ctx_->OMSetDepthStencilState(DepthState(s.zfunc, s.zwrite != 0), 0);
        ID3D11SamplerState* smp = Sampler(s.linear != 0, s.addrU, s.addrV, s.mipmapped);
        ID3D11SamplerState* materialSamplers[]={smp,postPointSampler_.Get(),smp,postPointSampler_.Get()};
        ctx_->PSSetSamplers(0, 4, materialSamplers);
        ID3D11ShaderResourceView* srv = d.texture && d.texture <= textures_.size() ? textures_[d.texture - 1].Get() : nullptr;
        ID3D11ShaderResourceView* materialTextures[]={srv,opaqueReady_?opaqueDepthColor_.srv.Get():nullptr,d.normalRoughnessTexture && d.normalRoughnessTexture<=textures_.size()?textures_[d.normalRoughnessTexture-1].Get():nullptr,shadowMapConstants_.parameters[3]>.5f?shadowMapColor_.srv.Get():nullptr};
        ctx_->PSSetShaderResources(0, 4, materialTextures);
        ctx_->Draw(d.count, d.first);
    }
    if(list.opaqueDrawCount&&list.opaqueDrawCount>=list.draws.size())DrawAmbientOcclusion(list);
    ctx_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

bool D3D11Device::BeginFrame(const float clear[4]) {
    hudReady_=false;hudError_.clear();
    if(graphics_.hdrOutput){UpdateHdrCapabilities();if(!capabilities_.hdrOutput&&SetHdrOutput(false,nullptr))graphics_.hdrOutput=false;}
    if (graphics_.lowLatency && queueReady_ && WaitForSingleObjectEx(queueReady_, 0, FALSE) != WAIT_OBJECT_0) return false;
    CollectGpuTimings();
    if (!offscreen_ && follow_) {  // the render target follows the client area
        int w, h;
        WindowSizePixels(&w, &h);
        if (w != width_ || h != height_) {
            ctx_->OMSetRenderTargets(0, nullptr, nullptr);
            if (!CreateOffscreen(w, h)) return false;
        }
    }
    ID3D11RenderTargetView* rt = rtv_.Get();
    ctx_->OMSetRenderTargets(1, &rt, dsv_.Get());
    ctx_->ClearRenderTargetView(rt, clear);
    ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    // Preserve the immediately preceding rendered opaque depth before its clear.
    previousDepthReady_=motionReady_&&opaqueReady_&&opaqueWidth_==width_&&opaqueHeight_==height_&&
        motionWidth_==width_&&motionHeight_==height_&&previousDepthColor_.texture;
    if(previousDepthReady_)std::swap(opaqueDepthColor_,previousDepthColor_);
    taaHistoryReady_=graphics_.temporalAA&&taaResolved_&&taaResult_.width==width_&&taaResult_.height==height_;
    if(taaHistoryReady_){std::swap(taaResult_,taaHistory_);std::swap(reactive_,previousReactive_);}
    taaResolved_=reactiveCleared_=sceneProcessed_=false;
    sceneEffectsApplied_=reflectionSurfaceCleared_=false;sceneEffectsCamera_={};sceneEffectsFrame_={};
    motionReady_=motionCleared_=false;opaqueCleared_=opaqueReady_=false;
    frameActive_ = true;
    return rtv_ && dsv_;
}

// The present pass: the swap chain's back buffer (resized with the window) cleared black, the frame scaled into the
// image rectangle with the shared present shader.
void D3D11Device::PresentFrame() {
    int w, h;
    WindowSizePixels(&w, &h);
    if (w != backW_ || h != backH_) {
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        backRtv_.Reset();
        swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, swapFlags_);
        CreateBackBuffer(w, h);
    }
    if (!backRtv_) return;
    ResolveColor();
    ID3D11RenderTargetView* rt = backRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rt, nullptr);
    const float black[4] = { 0, 0, 0, 1 };
    ctx_->ClearRenderTargetView(rt, black);
    PresentParams p = present_;
    if (p.w <= 0 || p.h <= 0) {
        p.x = p.y = 0;
        p.w = backW_;
        p.h = backH_;
    }
    DrawPresent(backRtv_.Get(), p.x, p.y, p.w, p.h, p.crt,graphics_.hdrOutput);
}

void D3D11Device::ResolveColor() {
    if (!multisampleColor_) return;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    ctx_->ResolveSubresource(color_.Get(), 0, multisampleColor_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
}

void D3D11Device::DrawPresent(ID3D11RenderTargetView* target, int x, int y, int w, int h, int crt,bool hdr) {
    int sourceWidth,sourceHeight;bool fsr;
    auto* source=PostSource(w,h,&sourceWidth,&sourceHeight,&fsr);
    ctx_->OMSetRenderTargets(1, &target, nullptr);
    D3D11_VIEWPORT vp = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), 0, 1 };
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(rasterCull_[0].Get());
    auto options=hudReady_?HudDisplayGraphics(graphics_):graphics_;if(fsr)options.sharpening=0;options.hdrOutput=hdr;
    const auto cb = MakePresentConstants(sourceWidth, sourceHeight, w, h, crt, options);
    ctx_->UpdateSubresource(presentCb_.Get(), 0, nullptr, &cb, 0, 0);
    ctx_->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVb = nullptr;
    const UINT zero = 0;
    ctx_->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(presentVs_.Get(), nullptr, 0);
    ctx_->PSSetShader(presentPs_.Get(), nullptr, 0);
    ID3D11Buffer* pcb = presentCb_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &pcb);
    ID3D11SamplerState* smp = presentSampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11ShaderResourceView* srv = source;
    ctx_->PSSetShaderResources(0, 1, &srv);
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    ctx_->OMSetDepthStencilState(presentDepth_.Get(), 0);
    ctx_->Draw(3, 0);
    srv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &srv);  // the render target is bound again by the next BeginFrame
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3D11Device::EndFrame() {
    if (swap_) {
        PresentFrame();
        FinishGpuTiming();
        swap_->Present(sync_, graphics_.allowTearing && sync_ == 0 ? DXGI_PRESENT_ALLOW_TEARING : 0);
    }else {FinishGpuTiming();if(timingEnabled_)ctx_->Flush();}
    frameActive_ = false;
}

bool D3D11Device::SetGpuTimingEnabled(bool enabled,std::string* error) {
    if(frameActive_){if(error)*error="Change GPU timing between frames";return false;}
    if(enabled==timingEnabled_)return true;
    if(enabled&&!capabilities_.gpuTiming){if(error)*error="GPU timestamps are unavailable on this device";return false;}
    if(enabled&&!timing_[0].begin){TimingQueries queries[8];
        const D3D11_QUERY_DESC stamp={D3D11_QUERY_TIMESTAMP,0},disjoint={D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        for(auto& query:queries)if(FAILED(device_->CreateQuery(&stamp,&query.begin))||FAILED(device_->CreateQuery(&stamp,&query.end))||FAILED(device_->CreateQuery(&disjoint,&query.disjoint))){
            capabilities_.gpuTiming=false;if(error)*error="GPU timestamp query allocation failed";return false;}
        for(int index=0;index<8;++index)timing_[index]=std::move(queries[index]);}
    timingEnabled_=enabled;deliveredTiming_=timingSerial_;return true;
}

void D3D11Device::CollectGpuTimings() {
    if(!timingEnabled_)return;
    constexpr UINT flags=D3D11_ASYNC_GETDATA_DONOTFLUSH;
    for(auto& query:timing_)if(query.pending){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};UINT64 begin=0,end=0;
        const HRESULT clockStatus=ctx_->GetData(query.disjoint.Get(),&clock,sizeof(clock),flags);
        if(clockStatus==S_FALSE)continue;
        if(FAILED(clockStatus)){query.pending=false;continue;}
        const HRESULT first=ctx_->GetData(query.begin.Get(),&begin,sizeof(begin),flags),last=ctx_->GetData(query.end.Get(),&end,sizeof(end),flags);
        if(first==S_FALSE||last==S_FALSE)continue;query.pending=false;
        if(FAILED(first)||FAILED(last)||clock.Disjoint||!clock.Frequency||end<=begin)continue;
        const double ms=1000.*static_cast<double>(end-begin)/clock.Frequency;
        if(query.serial>latestTiming_.serial&&std::isfinite(ms)&&ms>0&&ms<10000)
            latestTiming_={query.serial,query.width,query.height,ms,true};
    }
}

bool D3D11Device::PollGpuTiming(GpuTimingSample* sample) {
    CollectGpuTimings();if(!sample||!timingEnabled_||latestTiming_.serial<=deliveredTiming_||!latestTiming_.valid)return false;
    *sample=latestTiming_;deliveredTiming_=latestTiming_.serial;return true;
}

void D3D11Device::StartGpuTiming() {
    if(!timingEnabled_||activeTiming_>=0||!frameActive_)return;
    for(int index=0;index<8;++index)if(!timing_[index].pending){auto& query=timing_[index];query.serial=++timingSerial_;query.width=width_;query.height=height_;
        ctx_->Begin(query.disjoint.Get());ctx_->End(query.begin.Get());activeTiming_=index;return;}
}

void D3D11Device::FinishGpuTiming() {
    if(activeTiming_<0)return;auto& query=timing_[activeTiming_];ctx_->End(query.end.Get());ctx_->End(query.disjoint.Get());query.pending=true;activeTiming_=-1;
}

bool D3D11Device::ReadPixels(std::vector<uint8_t>* rgba) {
    if (!staging_) return false;
    ResolveColor();
    const bool effects = hudReady_ || graphics_.postAA || graphics_.sharpening || graphics_.bloom || graphics_.colorGrade || graphics_.crtMask || graphics_.crtCurvature || graphics_.crtGlow ||
        graphics_.brightness != 50 || graphics_.gamma != 50 || graphics_.contrast != 50 || graphics_.saturation != 50;
    if (effects) DrawPresent(postRtv_.Get(), 0, 0, width_, height_, 0);
    ctx_->CopyResource(staging_.Get(), effects ? postColor_.Get() : color_.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    rgba->resize(static_cast<size_t>(width_) * height_ * 4);
    for (int y = 0; y < height_; ++y)
        memcpy(&(*rgba)[static_cast<size_t>(y) * width_ * 4], static_cast<uint8_t*>(map.pData) + y * map.RowPitch, width_ * 4);
    ctx_->Unmap(staging_.Get(), 0);
    return true;
}

bool D3D11Device::ReadPresentation(int w,int h,std::vector<uint8_t>* rgba) {
    if(!offscreen_ || !frameActive_ || !rgba || w<1 || h<1 || w>8192 || h>8192 || !EnsurePostTarget(captureColor_,w,h))return false;
    DrawPresent(captureColor_.rtv.Get(),0,0,w,h,present_.crt);
    D3D11_TEXTURE2D_DESC desc{};captureColor_.texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device_->CreateTexture2D(&desc,nullptr,&staging)))return false;
    ctx_->CopyResource(staging.Get(),captureColor_.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map;if(FAILED(ctx_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return false;
    rgba->resize(static_cast<size_t>(w)*h*4);
    for(int row=0;row<h;++row)memcpy(rgba->data()+static_cast<size_t>(row)*w*4,static_cast<const uint8_t*>(map.pData)+row*map.RowPitch,w*4);
    ctx_->Unmap(staging.Get(),0);return true;
}

bool D3D11Device::ReadLinearPresentation(int w,int h,std::vector<float>* rgba) {
    if(!offscreen_||!frameActive_||!rgba||w<1||h<1||w>8192||h>8192||!EnsurePostTarget(floatCaptureColor_,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
    DrawPresent(floatCaptureColor_.rtv.Get(),0,0,w,h,present_.crt,true);
    D3D11_TEXTURE2D_DESC desc{};floatCaptureColor_.texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device_->CreateTexture2D(&desc,nullptr,&staging)))return false;
    ctx_->CopyResource(staging.Get(),floatCaptureColor_.texture.Get());D3D11_MAPPED_SUBRESOURCE map;
    if(FAILED(ctx_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return false;
    rgba->resize(static_cast<size_t>(w)*h*4);
    for(int row=0;row<h;++row){const auto* source=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(map.pData)+row*map.RowPitch);
        for(int value=0;value<w*4;++value)(*rgba)[static_cast<size_t>(row)*w*4+value]=HalfToFloat(source[value]);}
    ctx_->Unmap(staging.Get(),0);return true;
}

bool D3D11Device::ReadMotionPixels(std::vector<float>* rgba) {
    if(!rgba||!motionReady_||!motionColor_.texture)return false;
    ctx_->OMSetRenderTargets(0,nullptr,nullptr);
    D3D11_TEXTURE2D_DESC description{};motionColor_.texture->GetDesc(&description);
    description.Usage=D3D11_USAGE_STAGING;description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device_->CreateTexture2D(&description,nullptr,&staging)))return false;
    ctx_->CopyResource(staging.Get(),motionColor_.texture.Get());D3D11_MAPPED_SUBRESOURCE mapping{};
    if(FAILED(ctx_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapping)))return false;
    rgba->resize(static_cast<size_t>(width_)*height_*4);
    for(int row=0;row<height_;++row){const auto* source=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapping.pData)+row*mapping.RowPitch);
        for(int value=0;value<width_*4;++value)(*rgba)[static_cast<size_t>(row)*width_*4+value]=HalfToFloat(source[value]);}
    ctx_->Unmap(staging.Get(),0);return true;
}

#include "shadow_d3d11.inl"
#include "scene_effects_d3d11.inl"
#include "hud_output_d3d11.inl"
#include "warmup_d3d11.inl"

}  // namespace

std::unique_ptr<GpuDevice> CreateD3D11Device(const GpuDeviceDesc& desc, std::string* error) {
    auto d = std::make_unique<D3D11Device>();
    if (!d->Init(desc, error)) return nullptr;
    return d;
}

}  // namespace port
