#pragma once
#include "native_d3d12_interop.h"
#include <filesystem>
#include <memory>
#ifdef _WIN32
namespace port {
class XessSuperResolution {
public:
    XessSuperResolution();
    ~XessSuperResolution();
    bool Initialize(ID3D12Device*,const std::filesystem::path& dllDirectory,std::string* error);
    bool Configure(int outputWidth,int outputHeight,int quality,int* inputWidth,int* inputHeight,std::string* error);
    bool Evaluate(const NativeD3D12FrameInterop&,NativeD3D12FrameOutput*,std::string* error);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
