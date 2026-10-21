#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include "native_d3d12_interop.h"

#ifdef _WIN32
namespace port {
// Optional vendor integration. It is never selected without an explicit request.
// SDK binaries are external build dependencies, not repository assets.
class DlssSuperResolution {
public:
    DlssSuperResolution();
    ~DlssSuperResolution();
    bool Initialize(ID3D12Device*, const std::filesystem::path& featureDirectory, std::string* error);
    // 0 Quality, 1 Balanced, 2 Performance, 3 native-resolution DLAA.
    bool Configure(int outputWidth, int outputHeight, int quality, int* inputWidth, int* inputHeight, std::string* error);
    bool Evaluate(const NativeD3D12FrameInterop&, NativeD3D12FrameOutput*, std::string* error);
    bool Available() const;
    ID3D12Device* NativeDevice() const;
    void* NativeParameters() const; // SDK-owned NGX capability context; render-thread use only
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace port
#endif
