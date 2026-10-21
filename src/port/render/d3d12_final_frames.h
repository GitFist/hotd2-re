#pragma once
#ifdef _WIN32
#include "native_d3d12_frame_generation.h"
#include <memory>

namespace port {
// GPU work only. The owning backend executes/fences, reads the real disable
// flag, and decides which of the actual output resources to present.
class D3D12FinalFrames {
public:
    D3D12FinalFrames();
    ~D3D12FinalFrames();
    void SetCallback(NativeD3D12FinalFrameCallback,void*);
    bool Enabled() const;
    void BeginFrame();
    bool CaptureHudless(ID3D12Device*,ID3D12GraphicsCommandList*,ID3D12Resource*,D3D12_RESOURCE_STATES,std::string*);
    bool Evaluate(const NativeD3D12FrameInterop&,ID3D12Resource* finalColor,D3D12_RESOURCE_STATES,
                  const GraphicsOptions&,int crt,bool paused,std::string*);
    bool ResolveFlag(std::string*); // only after the native direct-queue fence
    ID3D12Resource* Generated() const;
    ID3D12Resource* Retained() const;
    D3D12_RESOURCE_STATES ColorState() const;
    uint32_t DisableFlag() const;
    bool Reset() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace port
#endif
