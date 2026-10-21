#pragma once
#ifdef _WIN32
#include "native_d3d12_interop.h"
#include "temporal_camera.h"
#include <filesystem>
#include <memory>

namespace port {
struct DlssFrameGenerationInput {
    NativeD3D12FrameInterop raster; // original render dimensions, actual depth + RG float motion
    TemporalCameraInfo camera;
    ID3D12Resource* finalColor = nullptr; // final post-processing + HUD, at output dimensions
    ID3D12Resource* hudless = nullptr;    // optional same color pipeline/size/format, without HUD
    D3D12_RESOURCE_STATES finalColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES hudlessState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bool paused = false;
    bool hdr = false; // explicit color pipeline metadata; FP16 storage alone does not imply HDR
};
struct DlssFrameGenerationOutput {
    ID3D12Resource* generated = nullptr;
    ID3D12Resource* retained = nullptr;
    ID3D12Resource* disableInterpolation = nullptr; // 4-byte GPU flag buffer; read after queue completion
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES flagState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool reset = true; // a reset's generated frame is just the vendor's real-frame copy
};
// Direct optional NGX FG. Evaluate records real vendor work and two distinct
// output resources, restores input states, and never presents or advances World.
// Caller executes/waits before releasing or reconfiguring resources, then paces
// generated first, retained second. No extra frame is claimed by this class alone.
class DlssFrameGeneration {
public:
    DlssFrameGeneration();
    ~DlssFrameGeneration();
    bool Initialize(ID3D12Device*,const std::filesystem::path& featureDirectory,std::string* error);
    bool Available() const;
    bool Evaluate(const DlssFrameGenerationInput&,DlssFrameGenerationOutput*,std::string* error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace port
#endif
