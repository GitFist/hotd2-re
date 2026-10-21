#include "ngx_context.h"
#ifdef _WIN32
#include <cstdio>
#include <map>
#include <mutex>
#include <wrl/client.h>
#if defined(HOTD2_NGX_SDK)
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <nvsdk_ngx.h>
#endif

namespace port {
struct NgxContext::Impl {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    std::filesystem::path directory;
    bool initialized=false;
};
NgxContext::NgxContext():impl_(std::make_unique<Impl>()){}
NgxContext::~NgxContext(){
#if defined(HOTD2_NGX_SDK)
    if(impl_->initialized)NVSDK_NGX_D3D12_Shutdown1(impl_->device.Get());
#endif
}
ID3D12Device* NgxContext::Device()const{return impl_->device.Get();}
const std::filesystem::path& NgxContext::FeatureDirectory()const{return impl_->directory;}
namespace {
struct LiveContext {NgxContext* context=nullptr;size_t owners=0;};
std::mutex contextMutex;
std::map<ID3D12Device*,LiveContext> liveContexts;
bool Fail(const char* message,std::string* error){if(error)*error=message;return false;}
#if defined(HOTD2_NGX_SDK)
bool TrustedDll(const std::filesystem::path& path) {
    std::error_code ec;
    if(!std::filesystem::is_regular_file(path,ec)||ec)return false;
    WINTRUST_FILE_INFO file{};file.cbStruct=sizeof(file);file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;
    data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;
    data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status=WinVerifyTrust(nullptr,&action,&data);
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);
    return status==ERROR_SUCCESS;
}
#endif
}
std::shared_ptr<NgxContext> AcquireNgxContext(ID3D12Device* device,const std::filesystem::path& directory,
                                            std::initializer_list<const wchar_t*> dlls,std::string* error) {
#if defined(HOTD2_NGX_SDK)
    if(!device||dlls.size()==0){Fail("NGX requires a real D3D12 device and feature DLL",error);return {};}
    std::error_code ec;const auto absolute=std::filesystem::canonical(directory,ec);
    if(ec){Fail("NGX feature directory is unavailable",error);return {};}
    for(const auto* dll:dlls)if(!dll||std::filesystem::path(dll).filename()!=dll||!TrustedDll(absolute/dll)){
        Fail("NGX requires installed, signed NVIDIA feature DLLs",error);return {};
    }
    std::lock_guard<std::mutex> lock(contextMutex);
    auto found=liveContexts.find(device);
    NgxContext* context=nullptr;
    if(found!=liveContexts.end()){
        context=found->second.context;
        if(!std::filesystem::equivalent(context->FeatureDirectory(),absolute,ec)||ec){
            Fail("NGX device already uses a different feature directory",error);return {};
        }
        ++found->second.owners;
    }else{
        const auto logs=std::filesystem::temp_directory_path(ec)/L"hotd2-ngx";
        if(ec){Fail("NGX log directory unavailable",error);return {};}
        std::filesystem::create_directories(logs,ec);
        if(ec){Fail("NGX log directory unavailable",error);return {};}
        std::unique_ptr<NgxContext> fresh(new NgxContext);
        fresh->impl_->device=device;fresh->impl_->directory=absolute;
        const wchar_t* paths[]={fresh->impl_->directory.c_str()};NVSDK_NGX_FeatureCommonInfo common{};
        common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
        const auto status=NVSDK_NGX_D3D12_Init_with_ProjectID("ce0fe0a5-7b6d-4c74-b36e-b775848e2f0f",NVSDK_NGX_ENGINE_TYPE_CUSTOM,
            "hotd2-native-1",logs.c_str(),device,&common);
        if(NVSDK_NGX_FAILED(status)){
            if(error){char text[128];std::snprintf(text,sizeof(text),"NGX initialization failed (0x%08X)",unsigned(status));*error=text;}
            return {};
        }
        fresh->impl_->initialized=true;context=fresh.release();
        liveContexts[device]={context,1};
    }
    // Registry reference accounting is guarded through shutdown too, so a new
    // feature cannot race a final owner's Shutdown1 and reinitialize mid-flight.
    return std::shared_ptr<NgxContext>(context,[device](NgxContext* value){
        std::lock_guard<std::mutex> guard(contextMutex);
        auto it=liveContexts.find(device);
        if(it!=liveContexts.end()&&it->second.context==value&&--it->second.owners==0){
            liveContexts.erase(it);delete value;
        }
    });
#else
    (void)device;(void)directory;(void)dlls;Fail("This build has no optional NVIDIA DLSS SDK integration",error);return {};
#endif
}
} // namespace port
#endif
