#pragma once
#include "native_d3d12_interop.h"

#ifdef _WIN32
namespace port {
struct NativeD3D12FinalFrame {
    NativeD3D12FrameInterop raster; // actual render-size depth/motion/unjittered camera
    ID3D12Resource* color = nullptr; // output-size final color, including post effects and HUD
    ID3D12Resource* hudless = nullptr; // matching output-size final color pipeline before HUD
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES hudlessState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bool paused = false;
};
struct NativeD3D12GeneratedFrames {
    ID3D12Resource* generated = nullptr;
    ID3D12Resource* retained = nullptr;
    ID3D12Resource* disableInterpolation = nullptr; // actual 4-byte GPU flag, read after fence
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES flagState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool reset = true;
};
using NativeD3D12FinalFrameCallback = bool (*)(const NativeD3D12FinalFrame&, NativeD3D12GeneratedFrames*, void*, std::string*);
struct NativeD3D12FrameGenerationStats {
    uint64_t evaluated = 0, generatedPresented = 0, retainedPresented = 0;
    bool lastDisabled = true, lastReset = true;
    bool lastPresentationEligible = false; // actual reset/vendor/refresh policy, independent of the vendor disable flag
    float outputRefreshHz = 0; // 0 unavailable; VSync requires a120Hz multiple
    double lastPairIntervalMs = 0; // real successful Present-return interval; not photon/display telemetry
    std::string status;
};
// Explicit 60Hz rendered input. The renderer never steps World or consumes input
// for generated frames. Window output paces real generated then retained presents
// at half-intervals; reset/SDK-disabled interpolation presents retained only.
bool SetNativeD3D12FinalFrameCallback(GpuDevice&, NativeD3D12FinalFrameCallback, void* user, std::string* error = nullptr);
bool SetNativeD3D12FrameGenerationPaused(GpuDevice&, bool paused);
NativeD3D12FrameGenerationStats GetNativeD3D12FrameGenerationStats(const GpuDevice&);
// Headless diagnostic after EndFrame: reads the actual two SDK output textures,
// even on a reset/disable. It does not count either as a window presentation.
bool ReadNativeD3D12GeneratedFrames(GpuDevice&, std::vector<uint8_t>* generated, std::vector<uint8_t>* retained,
                                  int* width, int* height, uint32_t* disableInterpolation, std::string* error = nullptr);
} // namespace port
#endif
