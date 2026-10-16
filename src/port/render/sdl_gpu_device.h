#pragma once

// SDL_GPU GpuDevice (SDL3's GPU API: D3D12 / Vulkan / Metal). desc.nativeWindow = SDL_Window* (claimed for a swap
// chain; the frame is rendered into an off-screen target of desc.width x desc.height and scaled into the swap chain by
// the present pass shared with the D3D11 backend: gpu_device.h PresentParams, hlsl_shaders.h kPresentShaderHlsl), or
// null + desc.offscreen for headless rendering with readback.
//
// Shaders: hlsl_shaders.h compiled at run time to DXBC (shader model 5.1, D3DCompile) on Windows, so the device is
// created for the D3D12 driver. Vulkan (SPIR-V) / Metal (MSL) need offline-compiled blobs of the same HLSL
// (SDL_shadercross or DXC -spirv), not available yet: on other platforms device creation fails with a message
// (notes/agents/platform.md).

#include <memory>
#include <string>

#include "gpu_device.h"

namespace port {

std::unique_ptr<GpuDevice> CreateSdlGpuDevice(const GpuDeviceDesc& desc, std::string* error);

}  // namespace port
