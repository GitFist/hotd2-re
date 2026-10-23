#include "d3d12_device.h"
#include "native_d3d12_interop.h"

#ifdef _WIN32
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>
#include "asset_store.h"
#include "hlsl_shaders.h"
#include "texture_mips.h"
#include "motion_shaders.h"
#include "hdr_color.h"
#include "d3d12_ray_scene.h"
#include "taa_shaders.h"
#include "d3d12_final_frames.h"
#include "d3d12_frame_pacer.h"
#include "d3d12_gpu_timer.h"
#include "hud_composition.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace port {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kTextureSlots = 32768, kSrvSlots = 65536, kSamplerSlots = 1024;
constexpr UINT kTextureBase = 8;
constexpr UINT64 kConstantChunk = 4 * 1024 * 1024;
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

bool Failed(HRESULT hr, const char* operation, std::string* error) {
    if (SUCCEEDED(hr)) return false;
    if (error) { char text[32]; sprintf_s(text, " (0x%08lX)", static_cast<unsigned long>(hr)); *error = std::string(operation) + text; }
    return true;
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{}; h.Type = type; h.CreationNodeMask = h.VisibleNodeMask = 1; return h;
}
D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC r{}; r.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; r.Width = std::max<UINT64>(size, 1);
    r.Height = r.DepthOrArraySize = r.MipLevels = 1; r.SampleDesc.Count = 1; r.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; return r;
}
D3D12_RESOURCE_DESC TextureDesc(int w, int h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, UINT mips = 1) {
    D3D12_RESOURCE_DESC r{}; r.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; r.Width = w; r.Height = h;
    r.DepthOrArraySize = 1; r.MipLevels = static_cast<UINT16>(mips); r.Format = format; r.SampleDesc.Count = 1; r.Flags = flags; return r;
}
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore = before; b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}
D3D12_BLEND Blend(int v) {
    constexpr D3D12_BLEND values[] = {D3D12_BLEND_ONE,D3D12_BLEND_ZERO,D3D12_BLEND_ONE,D3D12_BLEND_SRC_COLOR,
        D3D12_BLEND_INV_SRC_COLOR,D3D12_BLEND_SRC_ALPHA,D3D12_BLEND_INV_SRC_ALPHA,D3D12_BLEND_DEST_ALPHA,
        D3D12_BLEND_INV_DEST_ALPHA,D3D12_BLEND_DEST_COLOR,D3D12_BLEND_INV_DEST_COLOR};
    return v >= 1 && v <= 10 ? values[v] : D3D12_BLEND_ONE;
}
D3D12_BLEND AlphaBlend(D3D12_BLEND b) {
    switch (b) { // Color factors are not legal in the separate alpha slots.
    case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
    case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
    case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return b;
    }
}
D3D12_TEXTURE_ADDRESS_MODE Address(int v) {
    return v == 2 ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR : v == 3 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

class D3D12Device final : public GpuDevice {
public:
    ~D3D12Device() override { Flush(); if (event_) CloseHandle(event_); if (queueReady_) CloseHandle(queueReady_); }
    bool Init(const GpuDeviceDesc&, std::string*);
    const char* Name() const override { return "d3d12"; }
    GpuTexture CreateTexture(const RgbaImage&, bool) override;
    void ClearTextures() override;
    void SetStaticVertices(const game::ModelVertex* v, size_t n) override { SetVertices(0, v, n * sizeof(*v)); }
    void SetDynamicVertices(const game::ModelVertex* v, size_t n) override { SetVertices(1, v, n * sizeof(*v)); }
    void SetSpriteVertices(const game::TLVertex* v, size_t n) override { SetVertices(2, v, n * sizeof(*v)); }
    bool BeginFrame(const float[4]) override;
    void Submit(const GpuDrawList&) override;
    void EndFrame() override;
    bool ReadPixels(std::vector<uint8_t>*) override;
    bool ReadPresentation(int, int, std::vector<uint8_t>*) override;
    bool ReadMotionPixels(std::vector<float>*) override;
    bool SetGpuTimingEnabled(bool enabled,std::string* error) override {if(active_){if(error)*error="Configure GPU timing between frames";return false;}return gpuTimer_.Enable(enabled,error);}
    bool PollGpuTiming(GpuTimingSample* sample) override {return gpuTimer_.Poll(sample);}
    void SetMenuOverlay(bool enabled) override { menuOverlay_ = enabled; }
    bool RequiresSceneProcessing() const override { return callback_ != nullptr || finalFrames_.Enabled() || menuOverlay_; }
    const std::string& HudCompositionError() const override { return sceneProcessingError_; }
    bool NeedsShadowCasters() const override { return rayScene_.Info().enabled; }
    bool ProcessScene(std::string* error = nullptr) override;
    int TargetWidth() const override { return width_; }
    int TargetHeight() const override { return height_; }
    int HudTargetWidth() const override { return menuReady_ ? menuColor_.w : width_; }
    int HudTargetHeight() const override { return menuReady_ ? menuColor_.h : height_; }
    bool ConfigureGraphics(const GraphicsOptions&, std::string*) override;
    const GraphicsOptions& EffectiveGraphics() const override { return graphics_; }
    const GraphicsCapabilities& GetGraphicsCapabilities() const override { return capabilities_; }
    void SetPresent(const PresentParams& p) override { present_ = p; }
    bool ResizeTarget(int, int) override;
    bool WindowSizePixels(int*, int*) const override;
    bool Interop(std::string*);
    ID3D12Device* NativeDevice() const { return device_.Get(); }
    bool ConfigureRaytracing(bool enabled, std::string* error);
    NativeD3D12RaytracingInfo RaytracingInfo() const {auto info=rayScene_.Info();info.synchronizationCpuMs=raySynchronizationCpuMs_;return info;}
    bool SetFinalCallback(NativeD3D12FinalFrameCallback, void*, std::string*);
    void SetGenerationPaused(bool paused) { generationPaused_ = paused; }
    const NativeD3D12FrameGenerationStats& GenerationStats() const { return generationStats_; }
    bool ReadGeneratedPair(std::vector<uint8_t>*,std::vector<uint8_t>*,int*,int*,uint32_t*,std::string*);
    NativeD3D12FrameCallback callback_ = nullptr;
    void* callbackUser_ = nullptr;
private:
    struct Target { ComPtr<ID3D12Resource> resource; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET; int w = 0, h = 0; };
    struct Upload { ComPtr<ID3D12Resource> resource; uint8_t* data = nullptr; UINT64 used = 0; };
    bool BeginCommands();
    bool Flush();
    bool Wait();
    bool CreateRoot(std::string*);
    bool Compile(std::string*);
    bool CreateTargets(int, int);
    bool CreateTarget(Target&, int, int, UINT rtv);
    bool ResizeSwap(int, int);
    ComPtr<ID3D12Resource> UploadBuffer(const void*, UINT64);
    D3D12_GPU_VIRTUAL_ADDRESS Constant(const void*, size_t);
    void SetVertices(int, const void*, size_t);
    ID3D12PipelineState* Pipeline(int, const GpuState&, bool raytracing = false);
    UINT TextureTable(UINT diffuse, UINT normal = 0, UINT previousDepth = 1);
    UINT SamplerTable(const GpuState&, bool present = false);
    void BindTarget(Target&, UINT rtv, bool depth, int x = 0, int y = 0, int w = 0, int h = 0);
    void DrawPresent(Target&, UINT rtv, int x, int y, int w, int h, int crt);
    void DrawPresentFrom(Target& source,UINT sourceSrv,Target& target,UINT rtv,int x,int y,int w,int h,int crt,const GraphicsOptions&);
    bool PresentExternal(ID3D12Resource*, D3D12_RESOURCE_STATES, int64_t waitAfterGenerated = 0,int syncInterval = -1);
    void UpdateOutputRefresh();
    NativeD3D12FrameInterop FrameInterop() const;
    bool ReadTarget(Target&, std::vector<uint8_t>*);
    bool EnsureMotionTargets();
    void DrawMotion(const GpuDrawList&);
    void DrawReactive(const GpuDrawList&);
    void SaveMotionDepth();
    bool ComposeMenuScene(std::string*);
    bool EnsureMenuTargets(std::string*);
    Target& FinalColor() { return menuReady_ ? menuColor_ : output_.resource ? output_ : color_; }
    UINT FinalRtv() const { return menuReady_ ? 8 : output_.resource ? 4 : 0; }
    void FrameBindings();
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(ID3D12DescriptorHeap* heap, UINT index, UINT stride) const {
        auto h = heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(index) * stride; return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(ID3D12DescriptorHeap* heap, UINT index, UINT stride) const {
        auto h = heap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(index) * stride; return h;
    }
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12InfoQueue> debugInfo_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr, queueReady_ = nullptr;
    UINT64 fenceValue_ = 0, frameNumber_ = 0;
    bool commandsOpen_ = false, active_ = false, broken_ = false;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGISwapChain3> swap_;
    ComPtr<IDXGISwapChain2> queueSwap_;
    HWND hwnd_ = nullptr;
    bool offscreen_ = true, follow_ = false;
    UINT swapFlags_ = 0;
    int width_ = 0, height_ = 0, backW_ = 0, backH_ = 0;
    Target color_, capture_, output_, back_[2];
    Target menuColor_;
    ComPtr<ID3D12Resource> menuDepth_;
    bool menuOverlay_ = false, menuReady_ = false;
    ComPtr<ID3D12Resource> depth_;
    ComPtr<ID3D12Resource> outputDepth_;
    Target motionMetadata_, motionXY_, reactiveMask_;
    ComPtr<ID3D12Resource> motionDepth_, previousDepth_;
    bool motionReady_ = false, reactiveReady_ = false, previousDepthReady_ = false;
    uint64_t previousTemporalFrame_ = 0;
    TemporalFrameInfo temporal_;
    TemporalCameraInfo temporalCamera_;
    D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    ComPtr<ID3D12DescriptorHeap> rtv_, dsv_, srv_, srvCpu_, sampler_;
    UINT rtvStride_ = 0, srvStride_ = 0, samplerStride_ = 0, nextSrv_ = kTextureSlots, nextSampler_ = 0;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3DBlob> vs_[6], ps_[6]; // model, sprite, present, motion MRT, reactive model/sprite
    std::map<uint64_t, ComPtr<ID3D12PipelineState>> pipelines_;
    std::map<std::tuple<UINT, UINT, UINT>, UINT> textureTables_;
    std::map<int, UINT> samplerTables_;
    ComPtr<ID3D12Resource> vertices_[3];
    UINT vertexBytes_[3] = {};
    std::vector<ComPtr<ID3D12Resource>> textures_, retained_;
    std::vector<Upload> constants_;
    PresentParams present_;
    GraphicsOptions graphics_;
    GraphicsCapabilities capabilities_;
    D3D12RayScene rayScene_;
    D3D12_GPU_VIRTUAL_ADDRESS rayAddress_ = 0;
    double raySynchronizationCpuMs_ = 0;
    D3D12FinalFrames finalFrames_;
    D3D12FramePacer framePacer_;
    D3D12GpuTimer gpuTimer_;
    NativeD3D12FrameGenerationStats generationStats_;
    bool generationPaused_ = false;
    std::string sceneProcessingError_;
};

bool D3D12Device::Init(const GpuDeviceDesc& desc, std::string* error) {
    const bool debug = GetEnvironmentVariableW(L"HOTD2_D3D12_DEBUG", nullptr, 0) != 0;
    if (debug) { ComPtr<ID3D12Debug> layer; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)))) layer->EnableDebugLayer(); }
    if (desc.width < 1 || desc.height < 1 || desc.width > 16384 || desc.height > 16384) { if (error) *error = "Invalid D3D12 target size"; return false; }
    offscreen_ = desc.offscreen || !desc.nativeWindow; follow_ = desc.followWindowSize;
    hwnd_ = offscreen_ ? nullptr : static_cast<HWND>(desc.nativeWindow);
    graphics_.syncInterval = desc.syncInterval;
    if (Failed(CreateDXGIFactory1(IID_PPV_ARGS(&factory_)), "CreateDXGIFactory", error)) return false;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 a{}; adapter->GetDesc1(&a);
        if (!(a.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) break;
        adapter.Reset();
    }
    if (!device_ && offscreen_) {
        if (SUCCEEDED(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    }
    if (!device_) { if (error) *error = "No usable Direct3D 12 device"; return false; }
    if (debug) device_.As(&debugInfo_);
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (Failed(device_->CreateCommandQueue(&q, IID_PPV_ARGS(&queue_)), "CreateCommandQueue", error) ||
        Failed(device_->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator_)), "CreateCommandAllocator", error) ||
        Failed(device_->CreateCommandList(0, q.Type, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "CreateCommandList", error) ||
        Failed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence", error)) return false;
    list_->Close(); event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    capabilities_.gpuTiming=gpuTimer_.Initialize(device_.Get(),queue_.Get());
    if (!event_) { if (error) *error = "CreateEvent failed"; return false; }
    const auto makeHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible, ComPtr<ID3D12DescriptorHeap>& heap) {
        D3D12_DESCRIPTOR_HEAP_DESC d{}; d.Type = type; d.NumDescriptors = count;
        d.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        return !Failed(device_->CreateDescriptorHeap(&d, IID_PPV_ARGS(&heap)), "CreateDescriptorHeap", error);
    };
    if (!makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 9, false, rtv_) || !makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4, false, dsv_) ||
        !makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvSlots, true, srv_) || !makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kTextureSlots, false, srvCpu_) || !makeHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerSlots, true, sampler_)) return false;
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    samplerStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{}; nullSrv.Format = kColorFormat; nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; nullSrv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(nullptr, &nullSrv, Cpu(srvCpu_.Get(), 0, srvStride_));
    nullSrv.Format = DXGI_FORMAT_R32_FLOAT; device_->CreateShaderResourceView(nullptr, &nullSrv, Cpu(srvCpu_.Get(), 1, srvStride_));
    if (!CreateRoot(error) || !Compile(error) || !CreateTargets(desc.width, desc.height)) { if (error && error->empty()) *error = "D3D12 target creation failed"; return false; }
    capabilities_.maxAnisotropy = 16;
    rayScene_.Initialize(device_.Get());
    if (!offscreen_) {
        BOOL tearing = FALSE; factory_->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
        int w, h; WindowSizePixels(&w, &h);
        DXGI_SWAP_CHAIN_DESC1 s{}; s.Width = w; s.Height = h; s.Format = kColorFormat; s.SampleDesc.Count = 1;
        s.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; s.BufferCount = 2; s.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        s.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        ComPtr<IDXGISwapChain1> swap;
        if (Failed(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd_, &s, nullptr, nullptr, &swap), "CreateSwapChainForHwnd", error) || Failed(swap.As(&swap_), "SwapChain3", error)) return false;
        factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER); swapFlags_ = s.Flags;
        capabilities_.tearing = tearing != FALSE;
        if (SUCCEEDED(swap_.As(&queueSwap_)) && SUCCEEDED(queueSwap_->SetMaximumFrameLatency(3))) { queueReady_ = queueSwap_->GetFrameLatencyWaitableObject(); capabilities_.lowLatency = queueReady_ != nullptr; }
        if (!ResizeSwap(w, h)) { if (error) *error = "D3D12 swap-chain target creation failed"; return false; }
        UpdateOutputRefresh();
    }
    return true;
}

bool D3D12Device::CreateRoot(std::string* error) {
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 3;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; ranges[1].NumDescriptors = 3;
    D3D12_ROOT_PARAMETER p[4]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (int i = 1; i < 3; ++i) { p[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[i].DescriptorTable.NumDescriptorRanges = 1; p[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1]; p[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; }
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[3].Descriptor.ShaderRegister = 3; p[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC d{}; d.NumParameters = 4; d.pParameters = p; d.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, errors;
    if (Failed(D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "SerializeRootSignature", error)) { if (error && errors) *error = static_cast<const char*>(errors->GetBufferPointer()); return false; }
    return !Failed(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)), "CreateRootSignature", error);
}
bool D3D12Device::Compile(std::string* error) {
    const auto reactiveModel = ReactiveModelShaderSource(), reactiveSprite = ReactiveSpriteShaderSource();
    const char* sources[] = {kModelShaderHlsl, kSpriteShaderHlsl, kPresentShaderHlsl, kMotionShaderHlsl, reactiveModel.c_str(), reactiveSprite.c_str()};
    const D3D_SHADER_MACRO spriteDefines[] = {{"SDLGPU_UBYTE4", "1"}, {nullptr, nullptr}};
    const D3D_SHADER_MACRO motionDefines[] = {{"MOTION_MRT", "1"}, {nullptr, nullptr}};
    for (int i = 0; i < 6; ++i) {
        ComPtr<ID3DBlob> errors;
        for (int stage = 0; stage < 2; ++stage) {
            auto& out = stage ? ps_[i] : vs_[i];
            if (Failed(D3DCompile(sources[i], strlen(sources[i]), "native-d3d12", i == 1 || i == 5 ? spriteDefines : i == 3 ? motionDefines : nullptr, nullptr,
                stage ? "psmain" : "vsmain", stage ? "ps_5_1" : "vs_5_1", D3DCOMPILE_ENABLE_STRICTNESS, 0, &out, &errors), "Compile D3D12 shader", error)) {
                if (error && errors) *error = static_cast<const char*>(errors->GetBufferPointer()); return false;
            }
        }
    }
    return true;
}

bool D3D12Device::Wait() {
    if (!queue_ || !fence_) return false;
    const UINT64 value = ++fenceValue_;
    if (FAILED(queue_->Signal(fence_.Get(), value))) { broken_ = true; return false; }
    if (fence_->GetCompletedValue() < value) {
        if (FAILED(fence_->SetEventOnCompletion(value, event_)) || WaitForSingleObject(event_, 30000) != WAIT_OBJECT_0) { broken_ = true; return false; }
    }
    if (FAILED(device_->GetDeviceRemovedReason())) {broken_=true;return false;}
    gpuTimer_.Complete();
    if (debugInfo_) {
        for (UINT64 message = 0; message < debugInfo_->GetNumStoredMessages(); ++message) {
            SIZE_T size = 0; debugInfo_->GetMessage(message, nullptr, &size); std::vector<uint8_t> bytes(size);
            auto* info = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (SUCCEEDED(debugInfo_->GetMessage(message, info, &size)) && info->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) fprintf(stderr, "D3D12: %s\n", info->pDescription);
        }
        debugInfo_->ClearStoredMessages();
    }
    return true;
}
bool D3D12Device::BeginCommands() {
    if (commandsOpen_) return true;
    if (broken_ || FAILED(allocator_->Reset()) || FAILED(list_->Reset(allocator_.Get(), nullptr))) { broken_ = true; return false; }
    commandsOpen_ = true;
    return true;
}
bool D3D12Device::Flush() {
    if (!commandsOpen_) return !broken_;
    gpuTimer_.Split();
    if (FAILED(list_->Close())) { commandsOpen_ = false; broken_ = true; return false; }
    ID3D12CommandList* lists[] = {list_.Get()}; queue_->ExecuteCommandLists(1, lists); commandsOpen_ = false;
    const bool okay = Wait();
    retained_.clear();
    return okay;
}
ComPtr<ID3D12Resource> D3D12Device::UploadBuffer(const void* data, UINT64 size) {
    ComPtr<ID3D12Resource> r;
    const auto heap = Heap(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = BufferDesc(size);
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r)))) return {};
    void* mapped = nullptr; D3D12_RANGE empty{};
    if (FAILED(r->Map(0, &empty, &mapped))) return {};
    if (data && size) memcpy(mapped, data, static_cast<size_t>(size));
    r->Unmap(0, nullptr); return r;
}
void D3D12Device::SetVertices(int stream, const void* data, size_t bytes) {
    if (bytes > UINT_MAX) return;
    if (vertices_[stream] && active_) retained_.push_back(vertices_[stream]);
    vertices_[stream] = bytes && data ? UploadBuffer(data, bytes) : ComPtr<ID3D12Resource>{};
    vertexBytes_[stream] = vertices_[stream] ? static_cast<UINT>(bytes) : 0;
}
D3D12_GPU_VIRTUAL_ADDRESS D3D12Device::Constant(const void* data, size_t bytes) {
    const UINT64 size = (bytes + 255) & ~UINT64(255);
    if (constants_.empty() || constants_.back().used + size > kConstantChunk) {
        Upload u; u.resource = UploadBuffer(nullptr, kConstantChunk);
        if (!u.resource) return 0;
        void* mapped = nullptr; D3D12_RANGE empty{}; if (FAILED(u.resource->Map(0, &empty, &mapped))) return 0;
        u.data = static_cast<uint8_t*>(mapped); constants_.push_back(std::move(u));
    }
    auto& u = constants_.back(); const UINT64 at = u.used; memcpy(u.data + at, data, bytes); u.used += size;
    return u.resource->GetGPUVirtualAddress() + at;
}

bool D3D12Device::CreateTarget(Target& target, int w, int h, UINT rtv) {
    Target made; auto desc = TextureDesc(w, h, kColorFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT); D3D12_CLEAR_VALUE clear{}; clear.Format = kColorFormat;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&made.resource)))) return false;
    made.w = w; made.h = h; device_->CreateRenderTargetView(made.resource.Get(), nullptr, Cpu(rtv_.Get(), rtv, rtvStride_));
    target = std::move(made); return true;
}
bool D3D12Device::CreateTargets(int w, int h) {
    Target color; ComPtr<ID3D12Resource> depth;
    auto desc = TextureDesc(w, h, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT); D3D12_CLEAR_VALUE clear{}; clear.Format = DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth = 0;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth)))) return false;
    if (!CreateTarget(color, w, h, 0)) return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth.Get(), &dsv, dsv_->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = kColorFormat; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(color.resource.Get(), &srv, Cpu(srvCpu_.Get(), 2, srvStride_));
    srv.Format = DXGI_FORMAT_R32_FLOAT; device_->CreateShaderResourceView(depth.Get(), &srv, Cpu(srvCpu_.Get(), 3, srvStride_));
    color_ = std::move(color); depth_ = depth; depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    width_ = w; height_ = h; capture_ = {}; motionMetadata_ = {}; motionXY_ = {}; reactiveMask_ = {}; motionDepth_.Reset(); previousDepth_.Reset(); previousDepthReady_ = false; return true;
}
bool D3D12Device::ResizeSwap(int w, int h) {
    if (!swap_ || active_ || !Flush()) return false;
    if (back_[0].resource) {
        back_[0] = {}; back_[1] = {};
        if (FAILED(swap_->ResizeBuffers(2, w, h, kColorFormat, swapFlags_))) return false;
    }
    for (UINT i = 0; i < 2; ++i) {
        if (FAILED(swap_->GetBuffer(i, IID_PPV_ARGS(&back_[i].resource)))) return false;
        back_[i].state = D3D12_RESOURCE_STATE_PRESENT; back_[i].w = w; back_[i].h = h;
        device_->CreateRenderTargetView(back_[i].resource.Get(), nullptr, Cpu(rtv_.Get(), i + 2, rtvStride_));
    }
    backW_ = w; backH_ = h; return true;
}
bool D3D12Device::ResizeTarget(int w, int h) {
    if (active_ || follow_ || w < 1 || h < 1 || w > 16384 || h > 16384 || !Flush()) return false;
    return (w == width_ && h == height_) || CreateTargets(w, h);
}
bool D3D12Device::WindowSizePixels(int* w, int* h) const {
    if (offscreen_ || !hwnd_ || !w || !h) return false;
    RECT rect{}; if (!GetClientRect(hwnd_, &rect)) return false;
    *w = std::max(1L, rect.right); *h = std::max(1L, rect.bottom); return true;
}

GpuTexture D3D12Device::CreateTexture(const RgbaImage& img, bool alphaWeighted) {
    if (img.width < 1 || img.height < 1 || img.width > 16384 || img.height > 16384 || img.pixels.size() != size_t(img.width) * img.height * 4 || textures_.size() + kTextureBase >= kTextureSlots) return 0;
    const auto mips = BuildTextureMips(img, alphaWeighted); const UINT levels = static_cast<UINT>(mips.size() + 1);
    auto desc = TextureDesc(img.width, img.height, kColorFormat, D3D12_RESOURCE_FLAG_NONE, levels); auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> texture;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)))) return 0;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levels); std::vector<UINT> rows(levels); std::vector<UINT64> rowSizes(levels); UINT64 total = 0;
    device_->GetCopyableFootprints(&desc, 0, levels, 0, footprints.data(), rows.data(), rowSizes.data(), &total);
    auto upload = UploadBuffer(nullptr, total); if (!upload) return 0;
    void* mapped = nullptr; D3D12_RANGE empty{}; if (FAILED(upload->Map(0, &empty, &mapped))) return 0;
    for (UINT mip = 0; mip < levels; ++mip) {
        const auto& source = mip ? mips[mip - 1] : img;
        for (int y = 0; y < source.height; ++y) memcpy(static_cast<uint8_t*>(mapped) + footprints[mip].Offset + size_t(y) * footprints[mip].Footprint.RowPitch, source.pixels.data() + size_t(y) * source.width * 4, size_t(source.width) * 4);
    }
    upload->Unmap(0, nullptr); if (!BeginCommands()) return 0;
    for (UINT mip = 0; mip < levels; ++mip) {
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = mip;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[mip];
        list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    Barrier(list_.Get(), texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    retained_.push_back(upload); textures_.push_back(texture); const UINT index = static_cast<UINT>(textures_.size() + kTextureBase - 1);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = kColorFormat; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = levels;
    device_->CreateShaderResourceView(texture.Get(), &srv, Cpu(srvCpu_.Get(), index, srvStride_));
    if (!active_ && !Flush()) return 0;
    return textures_.size();
}
void D3D12Device::ClearTextures() {
    if (active_) return; // contract requires between frames; do not invalidate recorded references
    Flush(); textures_.clear(); textureTables_.clear(); nextSrv_ = kTextureSlots;
}

ID3D12PipelineState* D3D12Device::Pipeline(int pipeline, const GpuState& s, bool raytracing) {
    const uint64_t key = uint64_t(pipeline) | uint64_t(s.blendEnable != 0) << 3 | uint64_t(s.srcBlend) << 4 | uint64_t(s.dstBlend) << 8 |
        uint64_t(s.zfunc) << 12 | uint64_t(s.zwrite != 0) << 16 | uint64_t(s.cull) << 17 | uint64_t(raytracing) << 32;
    auto found = pipelines_.find(key); if (found != pipelines_.end()) return found->second.Get();
    const D3D12_INPUT_ELEMENT_DESC model[] = {{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    const D3D12_INPUT_ELEMENT_DESC sprite[] = {{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{}; d.pRootSignature = root_.Get();
    d.VS = {vs_[pipeline]->GetBufferPointer(), vs_[pipeline]->GetBufferSize()}; d.PS = {ps_[pipeline]->GetBufferPointer(), ps_[pipeline]->GetBufferSize()};
    if (raytracing) { const auto& shader = rayScene_.PixelShader(); const auto& vertex = rayScene_.VertexShader(); if (shader.empty() || vertex.empty()) return nullptr; d.PS = {shader.data(), shader.size()}; d.VS = {vertex.data(),vertex.size()}; }
    if (pipeline != 2) d.InputLayout = {pipeline == 1 || pipeline == 5 ? sprite : model, 3};
    auto& blend = d.BlendState.RenderTarget[0]; blend.BlendEnable = s.blendEnable != 0;
    blend.SrcBlend = Blend(s.srcBlend); blend.DestBlend = Blend(s.dstBlend); blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = AlphaBlend(blend.SrcBlend); blend.DestBlendAlpha = AlphaBlend(blend.DestBlend); blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX; d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = s.cull == 2 ? D3D12_CULL_MODE_FRONT : s.cull == 3 ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = pipeline != 2; d.DepthStencilState.DepthWriteMask = s.zwrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(ReversedDepthCompare(std::clamp<int>(s.zfunc, 1, 8)));
    d.DepthStencilState.StencilReadMask = d.DepthStencilState.StencilWriteMask = 0xff;
    d.DepthStencilState.FrontFace = d.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; d.NumRenderTargets = 1; d.RTVFormats[0] = kColorFormat;
    d.DSVFormat = pipeline == 2 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_D32_FLOAT; d.SampleDesc.Count = 1;
    if (pipeline == 3) { d.NumRenderTargets = 2; d.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; d.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT; }
    if (pipeline == 4 || pipeline == 5) d.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
    ComPtr<ID3D12PipelineState> p;
    const HRESULT pipelineResult = device_->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p));
    if (FAILED(pipelineResult)) { if (!raytracing) broken_ = true; else fprintf(stderr,"Native DXR pipeline creation failed 0x%08lX\n",static_cast<unsigned long>(pipelineResult)); return nullptr; }
    auto* result = p.Get(); pipelines_.emplace(key, std::move(p)); return result;
}
UINT D3D12Device::TextureTable(UINT diffuse, UINT normal, UINT previousDepth) {
    const auto key = std::make_tuple(diffuse, normal, previousDepth); auto found = textureTables_.find(key); if (found != textureTables_.end()) return found->second;
    if (nextSrv_ + 3 > kSrvSlots) { broken_ = true; return 0; }
    const UINT at = nextSrv_; nextSrv_ += 3;
    const UINT indices[] = {diffuse, previousDepth, normal};
    for (UINT i = 0; i < 3; ++i) device_->CopyDescriptorsSimple(1, Cpu(srv_.Get(), at + i, srvStride_), Cpu(srvCpu_.Get(), indices[i], srvStride_), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    textureTables_[key] = at; return at;
}
UINT D3D12Device::SamplerTable(const GpuState& s, bool present) {
    const int key = (present ? 1 << 15 : 0) | s.linear | s.mipmapped << 1 | s.addrU << 2 | s.addrV << 4;
    auto found = samplerTables_.find(key); if (found != samplerTables_.end()) return found->second;
    if (nextSampler_ + 3 > kSamplerSlots) { broken_ = true; return 0; }
    const UINT at = nextSampler_; nextSampler_ += 3;
    D3D12_SAMPLER_DESC d{};
    d.Filter = s.mipmapped ? (graphics_.anisotropy > 1 ? D3D12_FILTER_ANISOTROPIC : s.linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR) : s.linear ? D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
    if (present) d.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    d.AddressU = present ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : Address(s.addrU); d.AddressV = present ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : Address(s.addrV); d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    d.MaxAnisotropy = graphics_.anisotropy; d.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS; d.MaxLOD = s.mipmapped || present ? D3D12_FLOAT32_MAX : 0.f;
    device_->CreateSampler(&d, Cpu(sampler_.Get(), at, samplerStride_)); device_->CreateSampler(&d, Cpu(sampler_.Get(), at + 2, samplerStride_));
    d.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; d.AddressU = d.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    device_->CreateSampler(&d, Cpu(sampler_.Get(), at + 1, samplerStride_));
    samplerTables_[key] = at; return at;
}

bool D3D12Device::ConfigureGraphics(const GraphicsOptions& o, std::string* error) {
    if (!ValidateGraphicsOptions(o, error)) return false;
    if (active_) { if (error) *error = "Apply graphics options between frames"; return false; }
    if (finalFrames_.Enabled() && o.crtCurvature) { if(error)*error="CRT curvature cannot be combined with frame generation because depth and motion are unwarped";return false; }
    if (o.msaa != 1 || o.alphaToCoverage || o.postAA > 1 || o.spatialUpscaler || o.softParticles || o.materialMaps || o.ambientOcclusion || o.hdrOutput || o.temporalAA || o.shadowMaps || o.volumetricFog || o.reflections || o.cinematicFocus || o.outputResolutionHud) {
        if (error) *error = "Native D3D12 currently supports single-sample raster, FXAA, filtering and color effects; the selected effect is unavailable"; return false;
    }
    if (o.allowTearing && !capabilities_.tearing) { if (error) *error = "Tearing presentation is unavailable"; return false; }
    if (o.lowLatency && !capabilities_.lowLatency) { if (error) *error = "Low-latency presentation is unavailable"; return false; }
    if (queueSwap_ && o.lowLatency != graphics_.lowLatency && FAILED(queueSwap_->SetMaximumFrameLatency(o.lowLatency ? 1 : 3))) { if (error) *error = "Cannot set presentation queue limit"; return false; }
    if (!Flush()) { if (error) *error = "D3D12 device is unavailable"; return false; }
    graphics_ = o; samplerTables_.clear(); nextSampler_ = 0; return true;
}
void D3D12Device::FrameBindings() {
    ID3D12DescriptorHeap* heaps[] = {srv_.Get(), sampler_.Get()}; list_->SetDescriptorHeaps(2, heaps); list_->SetGraphicsRootSignature(root_.Get());
}
void D3D12Device::BindTarget(Target& target, UINT rtv, bool depth, int x, int y, int w, int h) {
    Barrier(list_.Get(), target.resource.Get(), target.state, D3D12_RESOURCE_STATE_RENDER_TARGET); target.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    const auto rt = Cpu(rtv_.Get(), rtv, rtvStride_);
    const auto ds = Cpu(dsv_.Get(), &target == &menuColor_ ? 3 : &target == &output_ ? 1 : 0, device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    list_->OMSetRenderTargets(1, &rt, FALSE, depth ? &ds : nullptr);
    const D3D12_VIEWPORT vp{float(x),float(y),float(w ? w : target.w),float(h ? h : target.h),0,1};
    const D3D12_RECT scissor{0,0,target.w,target.h}; list_->RSSetViewports(1, &vp); list_->RSSetScissorRects(1, &scissor);
}
bool D3D12Device::BeginFrame(const float clear[4]) {
    if (active_ || broken_) return false;
    if (!offscreen_) {
        if (graphics_.lowLatency && queueReady_ && WaitForSingleObjectEx(queueReady_, 0, FALSE) != WAIT_OBJECT_0) return false;
        int w, h; if (!WindowSizePixels(&w, &h)) return false;
        if ((w != backW_ || h != backH_) && !ResizeSwap(w, h)) return false;
        if (follow_ && (w != width_ || h != height_) && !CreateTargets(w, h)) return false;
    }
    if (!BeginCommands()) return false;
    for (auto& c : constants_) c.resource->Unmap(0, nullptr); constants_.clear();
    textureTables_.clear(); nextSrv_ = kTextureSlots; output_ = {}; outputDepth_.Reset(); menuReady_ = false; motionReady_ = reactiveReady_ = false; temporal_ = {}; active_ = true; ++frameNumber_;
    rayScene_.BeginFrame(); rayAddress_ = 0;
    raySynchronizationCpuMs_=0;
    finalFrames_.BeginFrame();
    sceneProcessingError_.clear();
    gpuTimer_.Begin(list_.Get(),frameNumber_,width_,height_);
    FrameBindings(); auto& target = FinalColor(); BindTarget(target, FinalRtv(), true);
    list_->ClearRenderTargetView(Cpu(rtv_.Get(), 0, rtvStride_), clear, 0, nullptr);
    list_->ClearDepthStencilView(dsv_->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 0.f, 0, 0, nullptr);
    return true;
}
void D3D12Device::Submit(const GpuDrawList& draws) {
    if (!active_ || broken_) return;
    if (rayScene_.Info().enabled && (draws.opaqueDrawCount || !draws.shadowCasters.empty())) {
        ID3D12Resource* vertices[] = {vertices_[0].Get(), vertices_[1].Get(), vertices_[2].Get()};
        rayAddress_ = rayScene_.Build(list_.Get(), draws, vertices, vertexBytes_);
        if(rayAddress_){
            // Keep a separately completed AS submission as the correctness baseline.
            // Native GPU timing discards this split interval rather than counting
            // its CPU queue/fence gap as shader execution time.
            const auto submitted=framePacer_.Now();
            if(!Flush()||!BeginCommands()){broken_=true;return;}
            raySynchronizationCpuMs_+=framePacer_.ElapsedMs(submitted);
        }
    }
    if (draws.temporal.enabled) DrawMotion(draws);
    FrameBindings(); auto& target = FinalColor(); BindTarget(target, FinalRtv(), true);
    size_t drawIndex = 0;
    for (const auto& draw : draws.draws) {
        const size_t currentDraw = drawIndex++;
        const int stream = static_cast<int>(draw.stream), pipeline = static_cast<int>(draw.pipeline);
        if (stream < 0 || stream > 2 || pipeline < 0 || pipeline > 1 || !vertices_[stream] || !draw.count) continue;
        const UINT stride = stream == 2 ? sizeof(game::TLVertex) : sizeof(game::ModelVertex);
        if (uint64_t(draw.first) + draw.count > vertexBytes_[stream] / stride) continue;
        GpuState state = draw.state; D3D12_GPU_VIRTUAL_ADDRESS cb = 0; bool rayDraw = false;
        if (pipeline == 0) {
            if (draw.constants >= draws.model.size()) continue;
            if (graphics_.textureFilter) state.linear = graphics_.textureFilter == 2;
            auto constants = draws.model[draw.constants]; constants.depthEffects[3] = 0;
            rayDraw = rayAddress_ && rayScene_.Info().enabled && currentDraw < draws.opaqueDrawCount && !state.blendEnable && state.zwrite && constants.flags[2] < 0 && constants.litDir[3] > .5f;
            if (rayDraw) constants.lightingOptions[0] = 1;
            if (!draw.normalRoughnessTexture || draw.normalRoughnessTexture > textures_.size()) constants.materialParams[0] = 0;
            constants.sampling[0] = state.mipmapped && graphics_.anisotropy > 1 && !state.linear ? 1.f : 0.f;
            constants.sampling[1] = state.addrU; constants.sampling[2] = state.addrV;
            cb = Constant(&constants, sizeof(constants));
        } else { if (draw.constants >= draws.sprite.size()) continue; cb = Constant(&draws.sprite[draw.constants], sizeof(SpriteConstants)); }
        auto* pso = Pipeline(pipeline, state, rayDraw); if (!pso || !cb) { broken_ = true; break; }
        const UINT diffuse = draw.texture && draw.texture <= textures_.size() ? static_cast<UINT>(draw.texture + kTextureBase - 1) : 0;
        const UINT normal = draw.normalRoughnessTexture && draw.normalRoughnessTexture <= textures_.size() ? static_cast<UINT>(draw.normalRoughnessTexture + kTextureBase - 1) : 0;
        const UINT table = TextureTable(diffuse, normal), samplers = SamplerTable(state);
        if (broken_) break;
        list_->SetPipelineState(pso); list_->SetGraphicsRootConstantBufferView(0, cb);
        if (rayDraw) list_->SetGraphicsRootShaderResourceView(3, rayAddress_);
        list_->SetGraphicsRootDescriptorTable(1, Gpu(srv_.Get(), table, srvStride_)); list_->SetGraphicsRootDescriptorTable(2, Gpu(sampler_.Get(), samplers, samplerStride_));
        const D3D12_VERTEX_BUFFER_VIEW vb{vertices_[stream]->GetGPUVirtualAddress(), vertexBytes_[stream], stride}; list_->IASetVertexBuffers(0, 1, &vb);
        list_->IASetPrimitiveTopology(pipeline == 1 ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list_->DrawInstanced(draw.count, 1, draw.first, 0);
    }
    if (draws.temporal.enabled && motionReady_) DrawReactive(draws);
}
void D3D12Device::DrawPresent(Target& target, UINT rtv, int x, int y, int w, int h, int crt) {
    auto& source = FinalColor();
    DrawPresentFrom(source, menuReady_ ? 7 : output_.resource ? 4 : 2, target, rtv, x, y, w, h, crt, menuReady_ ? HudDisplayGraphics(graphics_) : graphics_);
}
void D3D12Device::DrawPresentFrom(Target& source,UINT sourceSrv,Target& target,UINT rtv,int x,int y,int w,int h,int crt,const GraphicsOptions& graphics) {
    Barrier(list_.Get(), source.resource.Get(), source.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); source.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    FrameBindings(); BindTarget(target, rtv, false, x, y, w, h);
    const auto constants = MakePresentConstants(source.w, source.h, w, h, crt, graphics);
    GpuState s{}; s.zwrite = 0; s.zfunc = 8; auto* pipeline = Pipeline(2, s); if (!pipeline) return;
    const auto cb = Constant(&constants, sizeof(constants)); if (!cb) { broken_ = true; return; }
    const UINT table = TextureTable(sourceSrv), samplers = SamplerTable(s, true);
    list_->SetPipelineState(pipeline); list_->SetGraphicsRootConstantBufferView(0, cb);
    list_->SetGraphicsRootDescriptorTable(1, Gpu(srv_.Get(), table, srvStride_)); list_->SetGraphicsRootDescriptorTable(2, Gpu(sampler_.Get(), samplers, samplerStride_));
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list_->DrawInstanced(3, 1, 0, 0);
}
void D3D12Device::EndFrame() {
    if (!active_) return;
    if (finalFrames_.Enabled()) {
        generationStats_.lastPairIntervalMs=0;
        generationStats_.lastPresentationEligible=false;
        std::string error; const auto raster = FrameInterop(); auto& finalColor = FinalColor();
        const bool evaluated = finalFrames_.Evaluate(raster,finalColor.resource.Get(),finalColor.state,menuReady_ ? HudDisplayGraphics(graphics_) : graphics_,present_.crt,generationPaused_,&error);
        SaveMotionDepth();
        if(evaluated)gpuTimer_.End(list_.Get());
        if (evaluated && Flush() && finalFrames_.ResolveFlag(&error)) {
            ++generationStats_.evaluated; generationStats_.lastDisabled = finalFrames_.DisableFlag() != 0;
            generationStats_.lastReset = finalFrames_.Reset() || raster.resetHistory || generationPaused_;
            bool interpolate = !generationStats_.lastDisabled && !generationStats_.lastReset;
            generationStats_.status = interpolate ? "Generated and retained GPU frames ready" : "Interpolation reset or disabled; retained frame only";
            if (swap_) {
                UpdateOutputRefresh();
                const int pairSync=graphics_.syncInterval?FrameGenerationVsyncInterval(generationStats_.outputRefreshHz):0;
                if (graphics_.syncInterval && !pairSync) { interpolate = false; generationStats_.status = "VSync frame generation requires 120/240/360/480 Hz; retained frames only"; }
                generationStats_.lastPresentationEligible=interpolate;
                int64_t generatedAt = 0;
                if (interpolate && PresentExternal(finalFrames_.Generated(),finalFrames_.ColorState(),0,pairSync)) { ++generationStats_.generatedPresented; generatedAt = framePacer_.Now(); }
                if (PresentExternal(finalFrames_.Retained(),finalFrames_.ColorState(),graphics_.syncInterval ? 0 : generatedAt,interpolate?pairSync:graphics_.syncInterval)) {++generationStats_.retainedPresented;if(generatedAt)generationStats_.lastPairIntervalMs=framePacer_.ElapsedMs(generatedAt);}
            }
            active_ = false; return;
        }
        generationStats_.lastDisabled = true; generationStats_.lastReset = true;
        generationStats_.status = error.empty() ? "Frame generation unavailable; original frame retained" : error;
        if (!commandsOpen_ && !BeginCommands()) { active_ = false; return; }
    } else SaveMotionDepth();
    if (swap_) {
        const UINT index = swap_->GetCurrentBackBufferIndex(); auto& target = back_[index];
        BindTarget(target, index + 2, false); const float black[4] = {0,0,0,1}; list_->ClearRenderTargetView(Cpu(rtv_.Get(), index + 2, rtvStride_), black, 0, nullptr);
        auto p = present_; if (p.w <= 0 || p.h <= 0) { p.x = p.y = 0; p.w = backW_; p.h = backH_; }
        DrawPresent(target, index + 2, p.x, p.y, p.w, p.h, p.crt);
        Barrier(list_.Get(), target.resource.Get(), target.state, D3D12_RESOURCE_STATE_PRESENT); target.state = D3D12_RESOURCE_STATE_PRESENT;
    }
    gpuTimer_.End(list_.Get());
    Flush();
    if (swap_ && !broken_) swap_->Present(graphics_.syncInterval, graphics_.allowTearing && graphics_.syncInterval == 0 ? DXGI_PRESENT_ALLOW_TEARING : 0);
    active_ = false;
}
bool D3D12Device::ReadTarget(Target& target, std::vector<uint8_t>* rgba) {
    if (!rgba || !target.resource || !commandsOpen_) return false;
    auto desc = target.resource->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 total = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    auto readDesc = BufferDesc(total); auto heap = Heap(D3D12_HEAP_TYPE_READBACK); ComPtr<ID3D12Resource> readback;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
    const auto previous = target.state; Barrier(list_.Get(), target.resource.Get(), previous, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = target.resource.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); Barrier(list_.Get(), target.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, previous);
    if (!Flush()) return false;
    void* mapped = nullptr; const D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    if (FAILED(readback->Map(0, &range, &mapped))) return false;
    rgba->resize(size_t(target.w) * target.h * 4);
    for (int row = 0; row < target.h; ++row) memcpy(rgba->data() + size_t(row) * target.w * 4, static_cast<uint8_t*>(mapped) + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch, size_t(target.w) * 4);
    const D3D12_RANGE noWrite{}; readback->Unmap(0, &noWrite);
    return BeginCommands(); // callers may append HUD or ask for another capture before EndFrame
}
bool D3D12Device::ReadPixels(std::vector<uint8_t>* rgba) {
    if (!offscreen_ || !active_) return false;
    const bool effects = graphics_.postAA || graphics_.sharpening || graphics_.bloom || graphics_.colorGrade || graphics_.crtMask || graphics_.crtCurvature || graphics_.crtGlow || graphics_.brightness != 50 || graphics_.gamma != 50 || graphics_.contrast != 50 || graphics_.saturation != 50;
    if (effects || output_.resource || menuReady_) return ReadPresentation(width_, height_, rgba);
    return ReadTarget(color_, rgba);
}
bool D3D12Device::ReadPresentation(int w, int h, std::vector<uint8_t>* rgba) {
    if (!offscreen_ || !active_ || !rgba || w < 1 || h < 1 || w > 8192 || h > 8192) return false;
    if ((capture_.w != w || capture_.h != h) && !CreateTarget(capture_, w, h, 1)) return false;
    DrawPresent(capture_, 1, 0, 0, w, h, present_.crt); return ReadTarget(capture_, rgba);
}
bool D3D12Device::EnsureMotionTargets() {
    if (motionDepth_ && motionMetadata_.w == width_ && motionMetadata_.h == height_) return true;
    const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
    Target metadata, xy, reactive; ComPtr<ID3D12Resource> depth, previous;
    const auto makeColor = [&](Target& target, DXGI_FORMAT format) {
        auto desc = TextureDesc(width_, height_, format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear{}; clear.Format = format;
        target.w = width_; target.h = height_;
        return SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&target.resource)));
    };
    if (!makeColor(metadata, DXGI_FORMAT_R16G16B16A16_FLOAT) || !makeColor(xy, DXGI_FORMAT_R16G16_FLOAT) || !makeColor(reactive, DXGI_FORMAT_R8_UNORM)) return false;
    auto desc = TextureDesc(width_, height_, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{}; clear.Format = DXGI_FORMAT_D32_FLOAT;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth)))) return false;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&previous)))) return false;
    device_->CreateRenderTargetView(metadata.resource.Get(), nullptr, Cpu(rtv_.Get(), 5, rtvStride_));
    device_->CreateRenderTargetView(xy.resource.Get(), nullptr, Cpu(rtv_.Get(), 6, rtvStride_));
    device_->CreateRenderTargetView(reactive.resource.Get(), nullptr, Cpu(rtv_.Get(), 7, rtvStride_));
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth.Get(), &dsv, Cpu(dsv_.Get(), 2, device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32_FLOAT; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(previous.Get(), &srv, Cpu(srvCpu_.Get(), 5, srvStride_));
    motionMetadata_ = std::move(metadata); motionXY_ = std::move(xy); reactiveMask_ = std::move(reactive); motionDepth_ = depth; previousDepth_ = previous; previousDepthReady_ = false;
    return true;
}
void D3D12Device::DrawMotion(const GpuDrawList& draws) {
    if (!EnsureMotionTargets()) return; // raster frame remains usable without a temporal extension
    FrameBindings();
    Barrier(list_.Get(), motionMetadata_.resource.Get(), motionMetadata_.state, D3D12_RESOURCE_STATE_RENDER_TARGET); motionMetadata_.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier(list_.Get(), motionXY_.resource.Get(), motionXY_.state, D3D12_RESOURCE_STATE_RENDER_TARGET); motionXY_.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    const D3D12_CPU_DESCRIPTOR_HANDLE targets[] = {Cpu(rtv_.Get(), 5, rtvStride_), Cpu(rtv_.Get(), 6, rtvStride_)};
    const auto ds = Cpu(dsv_.Get(), 2, device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    list_->OMSetRenderTargets(2, targets, FALSE, &ds);
    const D3D12_VIEWPORT vp{0,0,float(width_),float(height_),0,1}; const D3D12_RECT scissor{0,0,width_,height_};
    list_->RSSetViewports(1, &vp); list_->RSSetScissorRects(1, &scissor);
    if (!motionReady_) {
        const float zero[4] = {}; for (const auto rtv : targets) list_->ClearRenderTargetView(rtv, zero, 0, nullptr);
        list_->ClearDepthStencilView(ds, D3D12_CLEAR_FLAG_DEPTH, 0.f, 0, 0, nullptr); temporal_ = draws.temporal; temporalCamera_ = draws.temporalCamera;
    }
    const bool previousValid = previousDepthReady_ && !draws.temporal.reset && draws.temporal.frameId == previousTemporalFrame_ + 1;
    auto vertices = draws.motionVertices.empty() ? ComPtr<ID3D12Resource>{} : UploadBuffer(draws.motionVertices.data(), draws.motionVertices.size() * sizeof(MotionVertex));
    if (vertices) {
        retained_.push_back(vertices);
        const D3D12_VERTEX_BUFFER_VIEW vb{vertices->GetGPUVirtualAddress(), static_cast<UINT>(draws.motionVertices.size() * sizeof(MotionVertex)), sizeof(MotionVertex)};
        list_->IASetVertexBuffers(0, 1, &vb); list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (const auto& draw : draws.motionDraws) {
            if (draw.constants >= draws.motionConstants.size() || uint64_t(draw.first) + draw.count > draws.motionVertices.size() || !draw.count) continue;
            auto constants = draws.motionConstants[draw.constants]; constants.target[0] = float(width_); constants.target[1] = float(height_); constants.target[3] = previousValid ? 1.f : 0.f;
            if (draws.temporal.reset) constants.target[2] = 0;
            GpuState state = draw.state; state.blendEnable = 0; state.zwrite = 1;
            if (graphics_.textureFilter) state.linear = graphics_.textureFilter == 2;
            auto* pso = Pipeline(3, state); const auto cb = Constant(&constants, sizeof(constants)); if (!pso || !cb) return;
            const UINT diffuse = draw.texture && draw.texture <= textures_.size() ? static_cast<UINT>(draw.texture + kTextureBase - 1) : 0;
            const auto table = TextureTable(diffuse, 0, 5), sampler = SamplerTable(state); if (broken_) return;
            list_->SetPipelineState(pso); list_->SetGraphicsRootConstantBufferView(0, cb);
            list_->SetGraphicsRootDescriptorTable(1, Gpu(srv_.Get(), table, srvStride_)); list_->SetGraphicsRootDescriptorTable(2, Gpu(sampler_.Get(), sampler, samplerStride_));
            list_->DrawInstanced(draw.count, 1, draw.first, 0);
        }
    }
    motionReady_ = true;
}
void D3D12Device::SaveMotionDepth() {
    if (!motionReady_ || !previousDepth_) { previousDepthReady_ = false; return; }
    Barrier(list_.Get(), motionDepth_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(list_.Get(), previousDepth_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyResource(previousDepth_.Get(), motionDepth_.Get());
    Barrier(list_.Get(), motionDepth_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    Barrier(list_.Get(), previousDepth_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    previousDepthReady_ = true; previousTemporalFrame_ = temporal_.frameId;
}
void D3D12Device::DrawReactive(const GpuDrawList& draws) {
    if (!reactiveMask_.resource || !motionDepth_) return;
    FrameBindings(); Barrier(list_.Get(), reactiveMask_.resource.Get(), reactiveMask_.state, D3D12_RESOURCE_STATE_RENDER_TARGET); reactiveMask_.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    const auto rt = Cpu(rtv_.Get(), 7, rtvStride_), ds = Cpu(dsv_.Get(), 2, device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    list_->OMSetRenderTargets(1, &rt, FALSE, &ds);
    const D3D12_VIEWPORT vp{0,0,float(width_),float(height_),0,1}; const D3D12_RECT scissor{0,0,width_,height_};
    list_->RSSetViewports(1, &vp); list_->RSSetScissorRects(1, &scissor);
    if (!reactiveReady_) { const float zero[4] = {}; list_->ClearRenderTargetView(rt, zero, 0, nullptr); }
    for (const auto& draw : draws.draws) {
        if (!draw.reactive || draw.hud) continue;
        const int stream = static_cast<int>(draw.stream); if (stream < 0 || stream > 2 || !vertices_[stream] || !draw.count) continue;
        const bool sprite = draw.pipeline == GpuDraw::Pipeline::Sprite; const UINT stride = stream == 2 ? sizeof(game::TLVertex) : sizeof(game::ModelVertex);
        if (uint64_t(draw.first) + draw.count > vertexBytes_[stream] / stride) continue;
        GpuState state = draw.state; state.blendEnable = 0; state.zwrite = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cb = 0;
        if (sprite) { if (draw.constants >= draws.sprite.size()) continue; cb = Constant(&draws.sprite[draw.constants], sizeof(SpriteConstants)); }
        else { if (draw.constants >= draws.model.size()) continue; if (graphics_.textureFilter) state.linear = graphics_.textureFilter == 2; cb = Constant(&draws.model[draw.constants], sizeof(ModelConstants)); }
        auto* pso = Pipeline(sprite ? 5 : 4, state); if (!pso || !cb) return;
        const UINT diffuse = draw.texture && draw.texture <= textures_.size() ? static_cast<UINT>(draw.texture + kTextureBase - 1) : 0;
        const auto table = TextureTable(diffuse), sampler = SamplerTable(state); if (broken_) return;
        list_->SetPipelineState(pso); list_->SetGraphicsRootConstantBufferView(0, cb);
        list_->SetGraphicsRootDescriptorTable(1, Gpu(srv_.Get(), table, srvStride_)); list_->SetGraphicsRootDescriptorTable(2, Gpu(sampler_.Get(), sampler, samplerStride_));
        const D3D12_VERTEX_BUFFER_VIEW vb{vertices_[stream]->GetGPUVirtualAddress(), vertexBytes_[stream], stride}; list_->IASetVertexBuffers(0, 1, &vb);
        list_->IASetPrimitiveTopology(sprite ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list_->DrawInstanced(draw.count, 1, draw.first, 0);
    }
    reactiveReady_ = true;
}
bool D3D12Device::ReadMotionPixels(std::vector<float>* rgba) {
    if (!active_ || !motionReady_ || !rgba) return false;
    auto& target = motionMetadata_; const auto desc = target.resource->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 total = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    auto buffer = BufferDesc(total); const auto heap = Heap(D3D12_HEAP_TYPE_READBACK); ComPtr<ID3D12Resource> readback;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
    Barrier(list_.Get(), target.resource.Get(), target.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = target.resource.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); Barrier(list_.Get(), target.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, target.state);
    if (!Flush()) return false; void* mapped = nullptr; const D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    if (FAILED(readback->Map(0, &range, &mapped))) return false;
    rgba->resize(size_t(width_) * height_ * 4);
    for (int row = 0; row < height_; ++row) {
        const auto* source = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped) + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch);
        for (int value = 0; value < width_ * 4; ++value) (*rgba)[size_t(row) * width_ * 4 + value] = HalfToFloat(source[value]);
    }
    const D3D12_RANGE noWrite{}; readback->Unmap(0, &noWrite); return BeginCommands();
}
bool D3D12Device::Interop(std::string* error) {
    if (!active_ || !commandsOpen_ || broken_) { if (error) *error = "No active native D3D12 frame"; return false; }
    if (!callback_) { if (error) *error = "No native D3D12 extension is configured"; return false; }
    Barrier(list_.Get(), color_.resource.Get(), color_.state, D3D12_RESOURCE_STATE_RENDER_TARGET); color_.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    const auto frame = FrameInterop();
    NativeD3D12FrameOutput result;
    bool okay = callback_(frame, &result, callbackUser_, error);
    if (okay && result.color && result.color != color_.resource.Get()) {
        const auto desc = result.color->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Format != kColorFormat || desc.Width < 1 || desc.Width > 8192 || desc.Height < 1 || desc.Height > 8192 ||
            desc.DepthOrArraySize != 1 || desc.SampleDesc.Count != 1 || !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) || (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
            if (error) *error = "Native extension returned an unsupported output texture"; okay = false;
        } else {
            auto depthDesc = TextureDesc(static_cast<int>(desc.Width), static_cast<int>(desc.Height), DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT); D3D12_CLEAR_VALUE clear{}; clear.Format = DXGI_FORMAT_D32_FLOAT;
            ComPtr<ID3D12Resource> depth;
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth)))) {
                if (error) *error = "Native extension output HUD depth allocation failed"; okay = false;
            } else {
                output_.resource = result.color; output_.state = result.state; output_.w = static_cast<int>(desc.Width); output_.h = static_cast<int>(desc.Height); outputDepth_ = depth;
                device_->CreateRenderTargetView(result.color, nullptr, Cpu(rtv_.Get(), 4, rtvStride_));
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = kColorFormat; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
                device_->CreateShaderResourceView(result.color, &srv, Cpu(srvCpu_.Get(), 4, srvStride_));
                const auto dsv = Cpu(dsv_.Get(), 1, device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
                device_->CreateDepthStencilView(depth.Get(), nullptr, dsv);
                list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.f, 0, 0, nullptr);
            }
        }
    }
    FrameBindings(); auto& target = FinalColor(); BindTarget(target, FinalRtv(), true); return okay;
}
NativeD3D12FrameInterop D3D12Device::FrameInterop() const {
    NativeD3D12FrameInterop frame;frame.device=device_.Get();frame.queue=queue_.Get();frame.commands=list_.Get();frame.color=color_.resource.Get();frame.depth=depth_.Get();frame.width=width_;frame.height=height_;frame.frame=frameNumber_;frame.colorState=color_.state;
    frame.frameTimeMs=temporal_.frameTimeMs;
    if(motionReady_){frame.motion=motionXY_.resource.Get();frame.motionState=motionXY_.state;frame.motionValid=true;frame.depth=motionDepth_.Get();frame.resetHistory=temporal_.reset||!previousDepthReady_||temporal_.frameId!=previousTemporalFrame_+1;frame.jitterX=temporal_.jitterPixels[0];frame.jitterY=temporal_.jitterPixels[1];frame.camera=temporalCamera_;if(reactiveReady_){frame.reactiveMask=reactiveMask_.resource.Get();frame.reactiveMaskState=reactiveMask_.state;}}
    return frame;
}
bool D3D12Device::EnsureMenuTargets(std::string* error) {
    int width=present_.w,height=present_.h;
    if(width<=0||height<=0){width=width_;height=height_;WindowSizePixels(&width,&height);}
    if(!ValidHudOutputSize(width,height)){if(error)*error="HUD output requires dimensions up to 8192 and at most 16 million pixels";return false;}
    if(menuColor_.resource&&menuDepth_&&menuColor_.w==width&&menuColor_.h==height)return true;
    auto desc=TextureDesc(width,height,DXGI_FORMAT_D32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    const auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;
    ComPtr<ID3D12Resource> depth;Target color;
    if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)))||
       !CreateTarget(color,width,height,8)){
        if(error)*error="Output-resolution menu allocation failed; internal UI retained";return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=kColorFormat;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device_->CreateShaderResourceView(color.resource.Get(),&srv,Cpu(srvCpu_.Get(),7,srvStride_));
    device_->CreateDepthStencilView(depth.Get(),nullptr,Cpu(dsv_.Get(),3,device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)));
    menuColor_=std::move(color);menuDepth_=depth;return true;
}
bool D3D12Device::ComposeMenuScene(std::string* error) {
    if(menuReady_)return true;
    if(!active_||!commandsOpen_||!EnsureMenuTargets(error))return false;
    auto& source=FinalColor();
    DrawPresentFrom(source,output_.resource?4:2,menuColor_,8,0,0,menuColor_.w,menuColor_.h,0,HudSceneGraphics(graphics_,false));
    if(broken_){if(error)*error="Output-resolution menu composition failed; internal UI retained";return false;}
    menuReady_=true;
    const auto dsv=Cpu(dsv_.Get(),3,device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    list_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,0.f,0,0,nullptr);
    BindTarget(menuColor_,8,true);return true;
}
bool D3D12Device::ProcessScene(std::string* error) {
    if(menuReady_)return true;
    sceneProcessingError_.clear();
    bool okay=callback_?Interop(&sceneProcessingError_):true;
    if(okay&&menuOverlay_)okay=ComposeMenuScene(&sceneProcessingError_);
    if(!okay&&error)*error=sceneProcessingError_;
    if(finalFrames_.Enabled()){
        auto& source=FinalColor();std::string captureError;
        if(!finalFrames_.CaptureHudless(device_.Get(),list_.Get(),source.resource.Get(),source.state,&captureError)){generationStats_.status=captureError;if(error)*error=captureError;okay=false;}
    }
    return okay;
}
bool D3D12Device::SetFinalCallback(NativeD3D12FinalFrameCallback callback,void* user,std::string* error) {
    if(active_){if(error)*error="Configure frame generation between frames";return false;}
    if(callback && graphics_.crtCurvature){if(error)*error="CRT curvature cannot be combined with frame generation because depth and motion are unwarped";return false;}
    finalFrames_.SetCallback(callback,user);generationStats_.lastDisabled=generationStats_.lastReset=true;
    generationStats_.lastPresentationEligible=false;
    generationStats_.status=callback?"Frame generation configured; waiting for matching scene and HUD":"Frame generation Off";return true;
}
void D3D12Device::UpdateOutputRefresh(){
    generationStats_.outputRefreshHz=0;if(!swap_)return;ComPtr<IDXGIOutput> output;DXGI_OUTPUT_DESC desc{};DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(SUCCEEDED(swap_->GetContainingOutput(&output))&&SUCCEEDED(output->GetDesc(&desc))&&EnumDisplaySettingsW(desc.DeviceName,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1)generationStats_.outputRefreshHz=float(mode.dmDisplayFrequency);
}
bool D3D12Device::PresentExternal(ID3D12Resource* resource,D3D12_RESOURCE_STATES state,int64_t waitAfterGenerated,int syncInterval) {
    if(!swap_||!resource||!BeginCommands())return false;
    Target source;source.resource=resource;source.state=state;const auto desc=resource->GetDesc();source.w=int(desc.Width);source.h=int(desc.Height);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=kColorFormat;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device_->CreateShaderResourceView(resource,&srv,Cpu(srvCpu_.Get(),6,srvStride_));textureTables_.clear();
    const UINT index=swap_->GetCurrentBackBufferIndex();auto& target=back_[index];BindTarget(target,index+2,false);const float black[4]={0,0,0,1};list_->ClearRenderTargetView(Cpu(rtv_.Get(),index+2,rtvStride_),black,0,nullptr);
    auto p=present_;if(p.w<=0||p.h<=0){p.x=p.y=0;p.w=backW_;p.h=backH_;}
    DrawPresentFrom(source,6,target,index+2,p.x,p.y,p.w,p.h,0,GraphicsOptions{});
    Barrier(list_.Get(),source.resource.Get(),source.state,state);
    Barrier(list_.Get(),target.resource.Get(),target.state,D3D12_RESOURCE_STATE_PRESENT);target.state=D3D12_RESOURCE_STATE_PRESENT;
    if(!Flush())return false;if(waitAfterGenerated)framePacer_.WaitHalfFrame(waitAfterGenerated);
    const int sync=syncInterval<0?graphics_.syncInterval:syncInterval;
    return swap_->Present(sync,graphics_.allowTearing&&sync==0?DXGI_PRESENT_ALLOW_TEARING:0)==S_OK;
}
bool D3D12Device::ReadGeneratedPair(std::vector<uint8_t>* generated,std::vector<uint8_t>* retained,int* w,int* h,uint32_t* flag,std::string* error) {
    if(!offscreen_||active_||!generated||!retained||generated==retained||!finalFrames_.Generated()||!finalFrames_.Retained()){if(error)*error="No completed headless generated frame pair";return false;}
    const auto desc=finalFrames_.Generated()->GetDesc();if(w)*w=int(desc.Width);if(h)*h=int(desc.Height);if(flag)*flag=finalFrames_.DisableFlag();
    Target a,b;a.resource=finalFrames_.Generated();b.resource=finalFrames_.Retained();a.w=b.w=int(desc.Width);a.h=b.h=int(desc.Height);a.state=b.state=finalFrames_.ColorState();
    return BeginCommands()&&ReadTarget(a,generated)&&ReadTarget(b,retained)&&Flush();
}
bool D3D12Device::ConfigureRaytracing(bool enabled, std::string* error) {
    if (active_) { if (error) *error = "Apply ray shadows between frames"; return false; }
    if (!Flush()) { if (error) *error = "Native D3D12 device is unavailable"; return false; }
    const bool old = rayScene_.Info().enabled;
    if (!rayScene_.Configure(enabled, error)) return false;
    if (enabled && !Pipeline(0, GpuState{}, true)) { rayScene_.Configure(old, nullptr); if (error) *error = "DXR model pipeline is unavailable"; return false; }
    return true;
}
} // namespace

std::unique_ptr<GpuDevice> CreateD3D12Device(const GpuDeviceDesc& desc, std::string* error) {
    auto device = std::make_unique<D3D12Device>(); if (!device->Init(desc, error)) return {}; return device;
}
bool SetNativeD3D12FrameCallback(GpuDevice& gpu, NativeD3D12FrameCallback callback, void* user) {
    auto* native = dynamic_cast<D3D12Device*>(&gpu); if (!native) return false;
    native->callback_ = callback; native->callbackUser_ = user; return true;
}
bool InvokeNativeD3D12FrameCallback(GpuDevice& gpu, std::string* error) {
    auto* native = dynamic_cast<D3D12Device*>(&gpu); if (!native) { if (error) *error = "Native D3D12 backend is required"; return false; }
    return native->Interop(error);
}
ID3D12Device* GetNativeD3D12Device(GpuDevice& gpu) {
    auto* native = dynamic_cast<D3D12Device*>(&gpu); return native ? native->NativeDevice() : nullptr;
}
bool ConfigureNativeD3D12Raytracing(GpuDevice& gpu, bool enabled, std::string* error) {
    auto* native = dynamic_cast<D3D12Device*>(&gpu); if (!native) { if (error) *error = "Native D3D12 is required for DXR"; return false; }
    return native->ConfigureRaytracing(enabled, error);
}
NativeD3D12RaytracingInfo GetNativeD3D12RaytracingInfo(const GpuDevice& gpu) {
    const auto* native = dynamic_cast<const D3D12Device*>(&gpu); if (native) return native->RaytracingInfo();
    NativeD3D12RaytracingInfo info; info.status = "Native D3D12 is required for DXR"; return info;
}
bool SetNativeD3D12FinalFrameCallback(GpuDevice& gpu,NativeD3D12FinalFrameCallback callback,void* user,std::string* error){auto* native=dynamic_cast<D3D12Device*>(&gpu);if(!native){if(error)*error="Native D3D12 is required for generated frames";return false;}return native->SetFinalCallback(callback,user,error);}
bool SetNativeD3D12FrameGenerationPaused(GpuDevice& gpu,bool paused){auto* native=dynamic_cast<D3D12Device*>(&gpu);if(!native)return false;native->SetGenerationPaused(paused);return true;}
NativeD3D12FrameGenerationStats GetNativeD3D12FrameGenerationStats(const GpuDevice& gpu){const auto* native=dynamic_cast<const D3D12Device*>(&gpu);if(native)return native->GenerationStats();NativeD3D12FrameGenerationStats stats;stats.status="Native D3D12 is required for generated frames";return stats;}
bool ReadNativeD3D12GeneratedFrames(GpuDevice& gpu,std::vector<uint8_t>* generated,std::vector<uint8_t>* retained,int* w,int* h,uint32_t* flag,std::string* error){auto* native=dynamic_cast<D3D12Device*>(&gpu);if(!native){if(error)*error="Native D3D12 is required for generated frames";return false;}return native->ReadGeneratedPair(generated,retained,w,h,flag,error);}
} // namespace port
#endif
