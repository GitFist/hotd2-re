#include "fsr_frame_generation.h"
#ifdef _WIN32
#include <windows.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#if defined(HOTD2_FSR_SDK)
#include <wintrust.h>
#include <softpub.h>
#include <ffx_api_loader.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#endif
namespace port {
using Microsoft::WRL::ComPtr;
struct FsrFrameGeneration::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> generated,retained,disable,flagValues;
    int width=0,height=0,renderWidth=0,renderHeight=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    uint64_t lastFrame=0;bool haveFrame=false,available=false;
    std::string version;
#if defined(HOTD2_FSR_SDK)
    HMODULE module=nullptr;ffxFunctions api{};ffxContext context=nullptr;
    // API creation descriptors must remain live for the context's lifetime.
    ffxCreateContextDescFrameGeneration create{};
    ffxCreateBackendDX12Desc backend{};
    ffxOverrideVersion provider{};
    ffxCreateContextDescFrameGenerationVersion apiVersion{};
    void Release(){if(context)api.DestroyContext(&context,nullptr);context=nullptr;generated.Reset();retained.Reset();haveFrame=false;}
    ~Impl(){Release();if(module)FreeLibrary(module);}
#endif
};
FsrFrameGeneration::FsrFrameGeneration():impl_(std::make_unique<Impl>()){}
FsrFrameGeneration::~FsrFrameGeneration()=default;
bool FsrFrameGeneration::Available()const{return impl_->available;}
const std::string& FsrFrameGeneration::VersionName()const{return impl_->version;}
namespace {
bool Fail(const char* reason,std::string* error){if(error)*error=reason;return false;}
#if defined(HOTD2_FSR_SDK)
bool Api(ffxReturnCode_t result,const char* operation,std::string* error){if(result==FFX_API_RETURN_OK)return true;
    if(error){char message[160];std::snprintf(message,sizeof(message),"%s failed (FidelityFX %u)",operation,result);*error=message;}return false;}
bool Trusted(const std::filesystem::path& path){
    WINTRUST_FILE_INFO file{};file.cbStruct=sizeof(file);file.pcwszFilePath=path.c_str();
    WINTRUST_DATA trust{};trust.cbStruct=sizeof(trust);trust.dwUIChoice=WTD_UI_NONE;trust.dwUnionChoice=WTD_CHOICE_FILE;trust.pFile=&file;
    trust.dwStateAction=WTD_STATEACTION_VERIFY;trust.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;const LONG result=WinVerifyTrust(nullptr,&action,&trust);
    trust.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&trust);return result==ERROR_SUCCESS;
}
void Transition(ID3D12GraphicsCommandList* commands,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){if(before==after)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands->ResourceBarrier(1,&b);}
void Uav(ID3D12GraphicsCommandList* commands,ID3D12Resource* resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;commands->ResourceBarrier(1,&b);}
bool SameDevice(ID3D12Resource* resource,ID3D12Device* device){ComPtr<ID3D12Device> actual;return resource&&SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&actual)))&&actual.Get()==device;}
bool Texture(ID3D12Resource* resource,int width,int height){if(!resource)return false;const auto d=resource->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==UINT64(width)&&d.Height==UINT(height)&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);}
bool Camera(const NativeD3D12FrameInterop& frame,ffxDispatchDescFrameGenerationPrepareV2* p){
    if(!frame.camera.valid||!std::isfinite(frame.frameTimeMs)||frame.frameTimeMs<=0)return false;
    const auto& projection=frame.camera.projection;float inverseView[16],inverseProjection[16];
    if(std::abs(projection[11])<1e-8f||std::abs(projection[5])<1e-8f||!InvertSceneMatrix(frame.camera.view,inverseView)||!InvertSceneMatrix(projection,inverseProjection))return false;
    const double nearZ=double(inverseProjection[10])+inverseProjection[14],nearW=double(inverseProjection[11])+inverseProjection[15];
    if(std::abs(nearW)<1e-12||std::abs(inverseProjection[15])<1e-12)return false;
    p->cameraNear=static_cast<float>(std::abs(nearZ/nearW));p->cameraFar=static_cast<float>(std::abs(double(inverseProjection[14])/inverseProjection[15]));
    if(!std::isfinite(p->cameraNear)||!std::isfinite(p->cameraFar)||p->cameraNear<=0||p->cameraFar<=p->cameraNear)return false;
    p->cameraFovAngleVertical=2*std::atan(1/std::abs(projection[5]));p->viewSpaceToMetersFactor=1;
    const float forward=projection[11]<0?-1.f:1.f;
    for(int k=0;k<3;++k){p->cameraPosition[k]=inverseView[12+k];p->cameraRight[k]=inverseView[k];p->cameraUp[k]=inverseView[4+k];p->cameraForward[k]=inverseView[8+k]*forward;}
    for(float* v:{p->cameraRight,p->cameraUp,p->cameraForward}){const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(!std::isfinite(length)||length<1e-8f)return false;for(int k=0;k<3;++k)v[k]/=length;}
    p->frameTimeDelta=frame.frameTimeMs;p->jitterOffset={frame.jitterX,frame.jitterY};p->motionVectorScale={1,1};return true;
}
#endif
}
bool FsrFrameGeneration::Initialize(ID3D12Device* device,const std::filesystem::path& directory,std::string* error){
#if defined(HOTD2_FSR_SDK)
    if(!device||impl_->module)return Fail("Invalid FSR frame-generation initialization",error);
    std::error_code ec;const auto dll=std::filesystem::canonical(directory/L"amd_fidelityfx_framegeneration_dx12.dll",ec);
    if(ec||!Trusted(dll))return Fail("FSR Frame Generation needs the installed signed AMD frame-generation DLL",error);
    impl_->module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!impl_->module)return Fail("AMD frame-generation DLL could not be loaded",error);ffxLoadFunctions(&impl_->api,impl_->module);
    auto& api=impl_->api;if(!api.CreateContext||!api.DestroyContext||!api.Configure||!api.Query||!api.Dispatch)return Fail("AMD frame-generation API is incomplete",error);
    ffxQueryDescGetVersions query{};query.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;query.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;query.device=device;
    uint64_t count=0;query.outputCount=&count;if(!Api(api.Query(nullptr,&query.header),"FSR FG version query",error)||!count||count>64)return Fail("No supported FSR Frame Generation provider",error);
    std::vector<uint64_t> ids(count);std::vector<const char*> names(count);query.versionIds=ids.data();query.versionNames=names.data();
    if(!Api(api.Query(nullptr,&query.header),"FSR FG providers",error)||count>ids.size())return false;
    for(size_t i=0;i<count;++i)if(names[i]&&std::string(names[i]).find("3.")!=std::string::npos){impl_->provider.versionId=ids[i];impl_->version=names[i];break;}
    if(impl_->version.empty())return Fail("FSR 3.x frame interpolation is unavailable on this device",error);
    impl_->backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;impl_->backend.device=device;
    impl_->provider.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;impl_->backend.header.pNext=&impl_->provider.header;
    impl_->apiVersion.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;impl_->apiVersion.version=FFX_FRAMEGENERATION_VERSION;
    impl_->provider.header.pNext=&impl_->apiVersion.header;impl_->device=device;impl_->available=true;return true;
#else
    (void)device;(void)directory;return Fail("This build has no optional AMD Frame Generation SDK integration",error);
#endif
}
bool FsrFrameGeneration::Evaluate(const NativeD3D12FinalFrame& input,NativeD3D12GeneratedFrames* output,std::string* error){
    if(output)*output={};
#if defined(HOTD2_FSR_SDK)
    const auto& frame=input.raster;
    if(!Available()||!output||frame.device!=impl_->device.Get()||!frame.commands||!frame.motionValid||frame.width<32||frame.height<32||frame.width>8192||frame.height>8192)
        return Fail("FSR FG needs a supported native device and actual scene motion",error);
    if(!SameDevice(input.color,frame.device)||!SameDevice(input.hudless,frame.device)||!SameDevice(frame.depth,frame.device)||!SameDevice(frame.motion,frame.device)||
       !Texture(frame.depth,frame.width,frame.height)||!Texture(frame.motion,frame.width,frame.height))return Fail("FSR FG scene inputs do not match the native device/render dimensions",error);
    const auto desc=input.color->GetDesc(),motion=frame.motion->GetDesc();
    if(desc.Width<32||desc.Width>8192||desc.Height<32||desc.Height>8192||!Texture(input.color,int(desc.Width),int(desc.Height))||
       !Texture(input.hudless,int(desc.Width),int(desc.Height))||input.hudless->GetDesc().Format!=desc.Format||desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)
        return Fail("FSR FG requires matching SDR final/HUDless textures",error);
    if(motion.Format!=DXGI_FORMAT_R16G16_FLOAT&&motion.Format!=DXGI_FORMAT_R32G32_FLOAT)return Fail("FSR FG requires actual two-channel float motion",error);
    ffxDispatchDescFrameGenerationPrepareV2 prepare{};if(!Camera(frame,&prepare))return Fail("FSR FG needs actual finite perspective camera and render interval",error);
    if(impl_->width!=int(desc.Width)||impl_->height!=int(desc.Height)||impl_->renderWidth!=frame.width||impl_->renderHeight!=frame.height||impl_->format!=desc.Format)impl_->Release();
    const bool create=!impl_->context;
    if(create){
        impl_->create={};impl_->create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;impl_->create.header.pNext=&impl_->backend.header;
        impl_->create.flags=FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;impl_->create.displaySize={unsigned(desc.Width),desc.Height};impl_->create.maxRenderSize={unsigned(frame.width),unsigned(frame.height)};
        impl_->create.backBufferFormat=ffxApiGetSurfaceFormatDX12(desc.Format);
        if(!Api(impl_->api.CreateContext(&impl_->context,&impl_->create.header,nullptr),"FSR FG context creation",error))return false;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        auto target=desc;target.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;target.Alignment=0;
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&impl_->generated)))||
           FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&impl_->retained))))return Fail("FSR FG output allocation failed",error);
        impl_->width=int(desc.Width);impl_->height=int(desc.Height);impl_->renderWidth=frame.width;impl_->renderHeight=frame.height;impl_->format=desc.Format;
    }
    if(!impl_->disable||!impl_->flagValues){impl_->disable.Reset();impl_->flagValues.Reset();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=4;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&impl_->disable))))return Fail("FSR FG eligibility flag allocation failed",error);
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;buffer.Width=8;buffer.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&impl_->flagValues))))return Fail("FSR FG flag upload allocation failed",error);
        void* data=nullptr;const D3D12_RANGE noRead{};if(FAILED(impl_->flagValues->Map(0,&noRead,&data)))return Fail("FSR FG flag map failed",error);const uint32_t values[2]={0,1};std::memcpy(data,values,sizeof(values));impl_->flagValues->Unmap(0,nullptr);
    }
    const bool reset=create||input.paused||frame.resetHistory||frame.frameTimeMs>250.f||!impl_->haveFrame||frame.frame!=impl_->lastFrame+1;
    const auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(frame.commands,input.color,input.colorState,read);Transition(frame.commands,frame.depth,frame.depthState,read);Transition(frame.commands,frame.motion,frame.motionState,read);
    const bool separateHudless=input.hudless!=input.color;if(separateHudless)Transition(frame.commands,input.hudless,input.hudlessState,read);
    Transition(frame.commands,impl_->generated.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ffxConfigureDescFrameGeneration configuration{};configuration.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configuration.frameGenerationEnabled=true;configuration.allowAsyncWorkloads=false;configuration.HUDLessColor=ffxApiGetResourceDX12(input.hudless);
    configuration.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;configuration.generationRect={0,0,int32_t(desc.Width),int32_t(desc.Height)};configuration.frameID=frame.frame;
    auto status=impl_->api.Configure(&impl_->context,&configuration.header);
    if(status==FFX_API_RETURN_OK){prepare.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;prepare.commandList=frame.commands;prepare.frameID=frame.frame;prepare.reset=reset;
        prepare.flags=FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;prepare.renderSize={unsigned(frame.width),unsigned(frame.height)};
        prepare.depth=ffxApiGetResourceDX12(frame.depth);prepare.motionVectors=ffxApiGetResourceDX12(frame.motion);status=impl_->api.Dispatch(&impl_->context,&prepare.header);}
    if(status==FFX_API_RETURN_OK){ffxDispatchDescFrameGeneration dispatch{};dispatch.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;dispatch.commandList=frame.commands;
        dispatch.presentColor=ffxApiGetResourceDX12(input.color);dispatch.outputs[0]=ffxApiGetResourceDX12(impl_->generated.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);dispatch.numGeneratedFrames=1;
        dispatch.reset=reset;dispatch.backbufferTransferFunction=FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;dispatch.minMaxLuminance[0]=0;dispatch.minMaxLuminance[1]=100;
        dispatch.generationRect=configuration.generationRect;dispatch.frameID=frame.frame;status=impl_->api.Dispatch(&impl_->context,&dispatch.header);}
    Uav(frame.commands,impl_->generated.Get());Transition(frame.commands,impl_->generated.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(frame.commands,input.color,read,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(frame.commands,impl_->retained.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);frame.commands->CopyResource(impl_->retained.Get(),input.color);
    Transition(frame.commands,impl_->retained.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(reset){Transition(frame.commands,impl_->generated.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);frame.commands->CopyResource(impl_->generated.Get(),input.color);Transition(frame.commands,impl_->generated.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);}
    Transition(frame.commands,input.color,D3D12_RESOURCE_STATE_COPY_SOURCE,input.colorState);Transition(frame.commands,frame.depth,read,frame.depthState);Transition(frame.commands,frame.motion,read,frame.motionState);
    if(separateHudless)Transition(frame.commands,input.hudless,read,input.hudlessState);
    // AMD's direct API has no vendor-disable resource. This real GPU flag
    // describes our known reset/pause/gap eligibility, not vendor telemetry.
    Transition(frame.commands,impl_->disable.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
    frame.commands->CopyBufferRegion(impl_->disable.Get(),0,impl_->flagValues.Get(),reset?4:0,4);
    Transition(frame.commands,impl_->disable.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(!Api(status,"FSR Frame Generation dispatch",error)){impl_->haveFrame=false;return false;}
    output->generated=impl_->generated.Get();output->retained=impl_->retained.Get();output->disableInterpolation=impl_->disable.Get();output->reset=reset;
    impl_->haveFrame=true;impl_->lastFrame=frame.frame;return true;
#else
    (void)input;return Fail("AMD Frame Generation SDK unavailable",error);
#endif
}
} // namespace port
#endif
