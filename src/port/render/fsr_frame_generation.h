#pragma once
#include "native_d3d12_frame_generation.h"
#include <filesystem>
#include <memory>
#ifdef _WIN32
namespace port {
// Direct AMD FSR 3.x frame interpolation; owns GPU work/resources only. Native
// presentation consumes the same output-pair contract used by the NGX adapter.
class FsrFrameGeneration {
public:
    FsrFrameGeneration();
    ~FsrFrameGeneration();
    bool Initialize(ID3D12Device*,const std::filesystem::path& signedDllDirectory,std::string* error);
    bool Evaluate(const NativeD3D12FinalFrame&,NativeD3D12GeneratedFrames*,std::string* error);
    bool Available()const;
    const std::string& VersionName()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace port
#endif
