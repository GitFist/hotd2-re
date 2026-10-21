#pragma once
#ifdef _WIN32
#include <d3d12.h>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>

namespace port {
// Device-scoped shared NGX initialization. Feature handles/parameter maps remain
// instance-owned; releasing SR cannot shut down a simultaneously active FG.
class NgxContext {
public:
    ~NgxContext();
    ID3D12Device* Device() const;
    const std::filesystem::path& FeatureDirectory() const;
private:
    NgxContext();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend std::shared_ptr<NgxContext> AcquireNgxContext(ID3D12Device*,const std::filesystem::path&,
                                                       std::initializer_list<const wchar_t*>,std::string*);
};
// Every requested feature DLL must be a signed local file. The initialized
// device cannot change feature search directory while another feature is live.
std::shared_ptr<NgxContext> AcquireNgxContext(ID3D12Device*,const std::filesystem::path&,
                                           std::initializer_list<const wchar_t*> dlls,std::string* error);
} // namespace port
#endif
