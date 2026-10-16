#pragma once

// Direct3D 11 GpuDevice (Windows): the reference backend of the port renderer. The frame is always rendered into an
// off-screen target of desc.width x desc.height (the internal resolution; followWindowSize: the client area).
// desc.nativeWindow = HWND for a swap chain sized to the client area, into which the present pass (gpu_device.h
// PresentParams, hlsl_shaders.h kPresentShaderHlsl) scales the frame; null with desc.offscreen for readback (headless
// screenshots; falls back to WARP when no hardware device is available).

#include <memory>
#include <string>

#include "gpu_device.h"

namespace port {

std::unique_ptr<GpuDevice> CreateD3D11Device(const GpuDeviceDesc& desc, std::string* error);

}  // namespace port
