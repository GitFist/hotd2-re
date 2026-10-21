#pragma once

#include "gpu_device.h"
#include "temporal_camera.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>

namespace port {
struct NativeD3D12FrameInterop {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12GraphicsCommandList* commands = nullptr;
    ID3D12Resource* color = nullptr; // RGBA8 at render dimensions, render-target state
    ID3D12Resource* depth = nullptr; // R32 typeless/D32 view, reversed depth, depth-write state
    ID3D12Resource* motion = nullptr; // RG16_FLOAT XY in input pixels, null until real pass produced
    ID3D12Resource* reactiveMask = nullptr; // R8_UNORM actual non-HUD scene alpha/animated coverage
    int width = 0, height = 0;
    uint64_t frame = 0;
    float jitterX = 0, jitterY = 0; // input-resolution pixels
    float frameTimeMs = 1000.f / 60.f;
    bool resetHistory = true;
    bool motionValid = false;
    TemporalCameraInfo camera;
    // This is the raster scene at the caller's explicit invocation point. No
    // motion vectors, jitter or HUD separation are implied. Extensions restore
    // these states before returning and retain resources until this queue's
    // submitted frame completes. The backend currently waits at EndFrame.
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    D3D12_RESOURCE_STATES motionState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES reactiveMaskState = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
struct NativeD3D12FrameOutput {
    // Optional actual extension-owned RGBA8 Texture2D. On success the backend
    // retains a COM reference for this frame, renders HUD into it, then presents
    // it. Null keeps the original scene. The callback treats input color/depth
    // as read-only and restores their states even on failure.
    ID3D12Resource* color = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
using NativeD3D12FrameCallback = bool (*)(const NativeD3D12FrameInterop&, NativeD3D12FrameOutput*, void*, std::string*);
// Fail on another backend. Explicit invocation allows the caller to place an
// extension between world geometry and HUD; nothing runs automatically.
bool SetNativeD3D12FrameCallback(GpuDevice&, NativeD3D12FrameCallback, void* user);
bool InvokeNativeD3D12FrameCallback(GpuDevice&, std::string* error = nullptr);
ID3D12Device* GetNativeD3D12Device(GpuDevice&); // borrowed; null on another backend
} // namespace port
#endif
