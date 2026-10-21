#include "fsr_super_resolution.h"
#ifdef _WIN32
#include <windows.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <vector>
#if defined(HOTD2_FSR_SDK)
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>
#include <ffx_upscale.h>
#include <softpub.h>
#include <wintrust.h>
#endif
namespace port {
using Microsoft::WRL::ComPtr;
struct FsrSuperResolution::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> output;
    int outputW = 0, outputH = 0, inputW = 0, inputH = 0, quality = 0;
    bool available = false, first = true;
    std::string version;
#if defined(HOTD2_FSR_SDK)
    HMODULE module = nullptr;
    ffxFunctions api{};
    ffxContext context = nullptr;
    ffxCreateContextDescUpscale create{};
    ffxCreateBackendDX12Desc backend{};
    ffxOverrideVersion overrideVersion{};
    void Release() {
        if (context) api.DestroyContext(&context, nullptr);
        context = nullptr;
        output.Reset();
        first = true;
    }
    ~Impl() {
        Release();
        if (module) FreeLibrary(module);
    }
#endif
};
FsrSuperResolution::FsrSuperResolution() : impl_(std::make_unique<Impl>()) {}
FsrSuperResolution::~FsrSuperResolution() = default;
bool FsrSuperResolution::Available() const { return impl_->available; }
const std::string& FsrSuperResolution::VersionName() const { return impl_->version; }
namespace {
bool Fail(const char* message, std::string* error) {
    if (error) *error = message;
    return false;
}
#if defined(HOTD2_FSR_SDK)
bool Api(ffxReturnCode_t status, const char* action, std::string* error) {
    if (status == FFX_API_RETURN_OK) return true;
    if (error) {
        char text[128];
        std::snprintf(text, sizeof(text), "%s failed (FidelityFX %u)", action, status);
        *error = text;
    }
    return false;
}
bool Trusted(const std::filesystem::path& path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return result == ERROR_SUCCESS;
}
void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    list->ResourceBarrier(1, &b);
}
#endif
}  // namespace
bool FsrSuperResolution::Initialize(ID3D12Device* device, const std::filesystem::path& directory, int major, std::string* error) {
#if defined(HOTD2_FSR_SDK)
    if (impl_->module || !device || major < 2 || major > 4) return Fail("Invalid FSR initialization", error);
    std::error_code ec;
    const auto dll = std::filesystem::canonical(directory / L"amd_fidelityfx_upscaler_dx12.dll", ec);
    if (ec || !Trusted(dll)) return Fail("FSR needs an installed, signed AMD upscaler DLL", error);
    impl_->module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!impl_->module) return Fail("AMD upscaler DLL could not be loaded", error);
    ffxLoadFunctions(&impl_->api, impl_->module);
    const auto& api = impl_->api;
    if (!api.CreateContext || !api.DestroyContext || !api.Query || !api.Dispatch) return Fail("AMD upscaler API is incomplete", error);
    ffxQueryDescGetVersions query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    query.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    query.device = device;
    uint64_t count = 0;
    query.outputCount = &count;
    if (!Api(api.Query(nullptr, &query.header), "FSR version query", error) || count == 0 || count > 64)
        return Fail("No supported FSR version on this device", error);
    std::vector<uint64_t> ids(count);
    std::vector<const char*> names(count);
    query.versionIds = ids.data();
    query.versionNames = names.data();
    if (!Api(api.Query(nullptr, &query.header), "FSR versions", error) || count > ids.size()) return false;
    const std::string requested = std::to_string(major) + ".";
    for (size_t i = 0; i < count; ++i) {
        if (names[i] && std::string(names[i]).find(requested) != std::string::npos) {
            impl_->version = names[i];
            impl_->overrideVersion.versionId = ids[i];
            break;
        }
    }
    if (impl_->version.empty()) return Fail("Requested FSR version is unavailable on this adapter/driver", error);
    impl_->overrideVersion.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    impl_->backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    impl_->backend.device = device;
    impl_->backend.header.pNext = &impl_->overrideVersion.header;
    impl_->device = device;
    impl_->available = true;
    return true;
#else
    (void)device;
    (void)directory;
    (void)major;
    return Fail("This build has no optional AMD FSR SDK integration", error);
#endif
}
bool FsrSuperResolution::Configure(int width, int height, int quality, int* iw, int* ih, std::string* error) {
#if defined(HOTD2_FSR_SDK)
    if (!Available() || !iw || !ih || width < 32 || height < 32 || width > 16384 || height > 16384 || quality < 0 || quality > 3)
        return Fail("Invalid FSR configuration", error);
    uint32_t rw = 0, rh = 0;
    ffxQueryDescUpscaleGetRenderResolutionFromQualityMode query{};
    query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
    query.header.pNext = &impl_->backend.header;
    query.displayWidth = width;
    query.displayHeight = height;
    query.qualityMode = quality == 3 ? FFX_UPSCALE_QUALITY_MODE_NATIVEAA : quality + 1;
    query.pOutRenderWidth = &rw;
    query.pOutRenderHeight = &rh;
    if (!Api(impl_->api.Query(nullptr, &query.header), "FSR input resolution", error) || rw == 0 || rh == 0 || rw > unsigned(width) ||
        rh > unsigned(height))
        return false;
    if (impl_->outputW != width || impl_->outputH != height || impl_->inputW != int(rw) || impl_->inputH != int(rh) ||
        impl_->quality != quality)
        impl_->Release();
    if (!impl_->context) {
        impl_->create = {};
        impl_->create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        impl_->create.header.pNext = &impl_->backend.header;
        impl_->create.flags =
            FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        impl_->create.maxRenderSize = {rw, rh};
        impl_->create.maxUpscaleSize = {unsigned(width), unsigned(height)};
        if (!Api(impl_->api.CreateContext(&impl_->context, &impl_->create.header, nullptr), "FSR context creation", error)) return false;
    }
    impl_->outputW = width;
    impl_->outputH = height;
    impl_->inputW = rw;
    impl_->inputH = rh;
    impl_->quality = quality;
    *iw = rw;
    *ih = rh;
    return true;
#else
    (void)width;
    (void)height;
    (void)quality;
    (void)iw;
    (void)ih;
    return Fail("FSR SDK unavailable", error);
#endif
}
bool FsrSuperResolution::Evaluate(const NativeD3D12FrameInterop& f, NativeD3D12FrameOutput* out, std::string* error) {
#if defined(HOTD2_FSR_SDK)
    if (!Available() || !impl_->context || !out || f.device != impl_->device.Get() || !f.commands || !f.color || !f.depth || !f.motion ||
        !f.motionValid || !f.camera.valid)
        return Fail("FSR needs real scene color/depth/motion and a perspective camera before HUD", error);
    if (f.width != impl_->inputW || f.height != impl_->inputH) return Fail("FSR input dimensions changed; reconfigure required", error);
    if (!std::isfinite(f.frameTimeMs) || f.frameTimeMs <= 0) return Fail("FSR requires the actual positive render interval", error);
    const auto& p = f.camera.projection;
    const double a = p[10], b = p[14], c = p[11], d = p[15];
    if (std::abs(c) < 1e-8 || std::abs(c - a) < 1e-8 || std::abs(a) < 1e-12 || std::abs(p[5]) < 1e-8)
        return Fail("FSR requires a finite perspective depth range", error);
    const float cameraNear = static_cast<float>(std::abs((b - d) / (c - a))), cameraFar = static_cast<float>(std::abs(b / a));
    if (!std::isfinite(cameraNear) || !std::isfinite(cameraFar) || cameraNear <= 0 || cameraFar <= cameraNear)
        return Fail("FSR camera depth range is invalid", error);
    if (!impl_->output) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = impl_->outputW;
        desc.Height = impl_->outputH;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(f.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     IID_PPV_ARGS(&impl_->output))))
            return Fail("FSR output allocation failed", error);
    }
    const auto read = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(f.commands, f.color, f.colorState, read);
    Transition(f.commands, f.depth, f.depthState, read);
    Transition(f.commands, f.motion, f.motionState, read);
    if (f.reactiveMask) Transition(f.commands, f.reactiveMask, f.reactiveMaskState, read);
    Transition(f.commands, impl_->output.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ffxDispatchDescUpscale dispatch{};
    dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatch.commandList = f.commands;
    dispatch.color = ffxApiGetResourceDX12(f.color);
    dispatch.depth = ffxApiGetResourceDX12(f.depth);
    dispatch.motionVectors = ffxApiGetResourceDX12(f.motion);
    dispatch.reactive = ffxApiGetResourceDX12(f.reactiveMask);
    dispatch.output = ffxApiGetResourceDX12(impl_->output.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.jitterOffset = {f.jitterX, f.jitterY};
    dispatch.motionVectorScale = {1, 1};
    dispatch.renderSize = {unsigned(f.width), unsigned(f.height)};
    dispatch.upscaleSize = {unsigned(impl_->outputW), unsigned(impl_->outputH)};
    dispatch.frameTimeDelta = f.frameTimeMs;
    dispatch.preExposure = 1;
    dispatch.reset = impl_->first || f.resetHistory;
    dispatch.cameraNear = cameraNear;
    dispatch.cameraFar = cameraFar;
    dispatch.cameraFovAngleVertical = 2 * std::atan(1 / std::abs(p[5]));
    dispatch.viewSpaceToMetersFactor = 1;
    dispatch.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
    const auto status = impl_->api.Dispatch(&impl_->context, &dispatch.header);
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = impl_->output.Get();
    f.commands->ResourceBarrier(1, &uav);
    Transition(f.commands, f.color, read, f.colorState);
    Transition(f.commands, f.depth, read, f.depthState);
    Transition(f.commands, f.motion, read, f.motionState);
    if (f.reactiveMask) Transition(f.commands, f.reactiveMask, read, f.reactiveMaskState);
    Transition(f.commands, impl_->output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (!Api(status, "FSR temporal dispatch", error)) return false;
    impl_->first = false;
    out->color = impl_->output.Get();
    out->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    return true;
#else
    (void)f;
    (void)out;
    return Fail("FSR SDK unavailable", error);
#endif
}
}  // namespace port
#endif
