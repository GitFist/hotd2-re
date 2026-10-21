#pragma once
#include "native_d3d12_interop.h"
#include <filesystem>
#include <memory>
#ifdef _WIN32
namespace port {
class FsrSuperResolution {
public:
    FsrSuperResolution();
    ~FsrSuperResolution();
    bool Initialize(ID3D12Device*,const std::filesystem::path& dllDirectory,int major,std::string* error);
    bool Configure(int outputWidth,int outputHeight,int quality,int* inputWidth,int* inputHeight,std::string* error);
    bool Evaluate(const NativeD3D12FrameInterop&,NativeD3D12FrameOutput*,std::string* error);
    bool Available() const;
    const std::string& VersionName() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
