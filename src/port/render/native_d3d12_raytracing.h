#pragma once
#include "gpu_device.h"

namespace port {
struct NativeD3D12RaytracingInfo {
    bool supported = false, compilerAvailable = false, compiler = false, enabled = false;
    uint32_t geometries = 0, triangles = 0;
    double synchronizationCpuMs = 0; // actual AS submission/fence cost; not GPU duration
    std::string status;
};
// Optional native-only DXR1.1 directional visibility. Defaults Off; unsupported
// hardware/compiler requests fail without changing the renderer's live state.
bool ConfigureNativeD3D12Raytracing(GpuDevice&, bool enabled, std::string* error = nullptr);
NativeD3D12RaytracingInfo GetNativeD3D12RaytracingInfo(const GpuDevice&);
} // namespace port
