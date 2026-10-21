#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <memory>
#include "native_d3d12_raytracing.h"

namespace port {
// Internal acceleration-structure owner. BeginFrame must follow completion of
// the prior frame's queue fence; the baseline native renderer guarantees this.
class D3D12RayScene {
public:
    D3D12RayScene();
    ~D3D12RayScene();
    void Initialize(ID3D12Device*);
    bool Configure(bool enabled, std::string* error);
    void BeginFrame();
    D3D12_GPU_VIRTUAL_ADDRESS Build(ID3D12GraphicsCommandList*, const GpuDrawList&,
                                   ID3D12Resource* const vertices[3], const UINT vertexBytes[3]);
    const std::vector<uint8_t>& PixelShader() const;
    const std::vector<uint8_t>& VertexShader() const;
    const NativeD3D12RaytracingInfo& Info() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace port
#endif
