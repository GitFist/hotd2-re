#pragma once

// Native Direct3D 12 renderer. Unlike SDL_GPU this owns the actual device,
// command queue and swap chain, so optional native extensions can record work
// against real resources instead of guessing at SDL's private objects.
#include "gpu_device.h"

namespace port {
std::unique_ptr<GpuDevice> CreateD3D12Device(const GpuDeviceDesc&, std::string* error);
} // namespace port
