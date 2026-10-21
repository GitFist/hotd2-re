#pragma once
#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include "gpu_timing.h"
namespace port {
// The native bring-up backend fences each completed rendered frame. Timestamp
// readback is therefore cached at that fence and Poll never blocks the caller.
class D3D12GpuTimer {
public:
    bool Initialize(ID3D12Device*,ID3D12CommandQueue*);
    bool Enable(bool,std::string*);
    void Begin(ID3D12GraphicsCommandList*,uint64_t serial,int width,int height);
    void Split(); // mid-frame synchronous capture would include CPU queue gaps
    void End(ID3D12GraphicsCommandList*);
    void Complete(); // only after the queue fence covering End has completed
    bool Poll(GpuTimingSample*);
    bool Supported() const {return frequency_!=0;}
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    uint64_t frequency_=0;
    bool enabled_=false,active_=false,split_=false,pending_=false,fresh_=false;
    GpuTimingSample current_,latest_;
};
}
#endif
