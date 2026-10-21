#include "dlss_super_resolution.h"
#include "ngx_context.h"
#ifdef _WIN32
#include <array>
#include <cmath>
#include <cstdio>
#include <wrl/client.h>
#if defined(HOTD2_NGX_SDK)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>
#endif

namespace port {
using Microsoft::WRL::ComPtr;
struct DlssSuperResolution::Impl {
    std::shared_ptr<NgxContext> context;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> output;
    int outputW=0, outputH=0, inputW=0, inputH=0, quality=0;
    bool available=false;
#if defined(HOTD2_NGX_SDK)
    NVSDK_NGX_Parameter* parameters=nullptr;
    NVSDK_NGX_Handle* feature=nullptr;
    void ReleaseFeature() {
        if(feature) NVSDK_NGX_D3D12_ReleaseFeature(feature);
        feature=nullptr; output.Reset();
    }
    ~Impl() {
        // Native backend completes each submitted frame before returning.
        ReleaseFeature();
        if(parameters) NVSDK_NGX_D3D12_DestroyParameters(parameters);
    }
#endif
};
DlssSuperResolution::DlssSuperResolution():impl_(std::make_unique<Impl>()){}
DlssSuperResolution::~DlssSuperResolution()=default;
bool DlssSuperResolution::Available()const{return impl_->available;}
ID3D12Device* DlssSuperResolution::NativeDevice()const{return impl_->device.Get();}
void* DlssSuperResolution::NativeParameters()const {
#if defined(HOTD2_NGX_SDK)
    return impl_->parameters;
#else
    return nullptr;
#endif
}
namespace {
bool Fail(const char* message,std::string* error){if(error)*error=message;return false;}
#if defined(HOTD2_NGX_SDK)
bool Ngx(NVSDK_NGX_Result result,const char* operation,std::string* error) {
    if(NVSDK_NGX_SUCCEED(result))return true;
    if(error){char text[128];std::snprintf(text,sizeof(text),"%s failed (NGX 0x%08X)",operation,unsigned(result));*error=text;}
    return false;
}
NVSDK_NGX_PerfQuality_Value Quality(int q) {
    return q==3?NVSDK_NGX_PerfQuality_Value_DLAA:q==2?NVSDK_NGX_PerfQuality_Value_MaxPerf:
           q==1?NVSDK_NGX_PerfQuality_Value_Balanced:NVSDK_NGX_PerfQuality_Value_MaxQuality;
}
void Transition(ID3D12GraphicsCommandList* commands,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    if(before==after)return;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands->ResourceBarrier(1,&barrier);
}
#endif
}
bool DlssSuperResolution::Initialize(ID3D12Device* device,const std::filesystem::path& directory,std::string* error) {
#if defined(HOTD2_NGX_SDK)
    if(impl_->context)return Fail("DLSS is already initialized",error);
    auto context=AcquireNgxContext(device,directory,{L"nvngx_dlss.dll"},error);
    if(!context)return false;
    NVSDK_NGX_Parameter* parameters=nullptr;
    if(!Ngx(NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters),"DLSS capability query",error))return false;
    int supported=0;
    if(!Ngx(parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&supported),"DLSS availability",error)||!supported) {
        NVSDK_NGX_D3D12_DestroyParameters(parameters);
        return Fail("DLSS Super Resolution is unavailable on this adapter/driver",error);
    }
    impl_->context=std::move(context);impl_->device=device;impl_->parameters=parameters;
    impl_->available=true;return true;
#else
    (void)device;(void)directory;
    return Fail("This build has no optional NVIDIA DLSS SDK integration",error);
#endif
}
bool DlssSuperResolution::Configure(int ow,int oh,int quality,int* iw,int* ih,std::string* error) {
#if defined(HOTD2_NGX_SDK)
    if(!Available()||!iw||!ih||ow<32||oh<32||ow>16384||oh>16384||quality<0||quality>3)return Fail("Invalid DLSS configuration",error);
    unsigned rw=ow,rh=oh,maxW=0,maxH=0,minW=0,minH=0;float sharp=0;
    if(quality!=3&&!Ngx(NGX_DLSS_GET_OPTIMAL_SETTINGS(impl_->parameters,ow,oh,Quality(quality),&rw,&rh,&maxW,&maxH,&minW,&minH,&sharp),"DLSS optimal resolution",error))return false;
    if(!rw||!rh||rw>unsigned(ow)||rh>unsigned(oh))return Fail("DLSS returned an invalid input resolution",error);
    if(impl_->outputW!=ow||impl_->outputH!=oh||impl_->inputW!=int(rw)||impl_->inputH!=int(rh)||impl_->quality!=quality)impl_->ReleaseFeature();
    impl_->outputW=ow;impl_->outputH=oh;impl_->inputW=rw;impl_->inputH=rh;impl_->quality=quality;*iw=rw;*ih=rh;return true;
#else
    (void)ow;(void)oh;(void)quality;(void)iw;(void)ih;return Fail("DLSS SDK unavailable",error);
#endif
}
bool DlssSuperResolution::Evaluate(const NativeD3D12FrameInterop& frame,NativeD3D12FrameOutput* result,std::string* error) {
#if defined(HOTD2_NGX_SDK)
    if(!Available()||!result||frame.device!=impl_->device.Get()||!frame.commands||!frame.color||!frame.depth||!frame.motion||!frame.motionValid)
        return Fail("DLSS needs real scene color, depth and valid motion before HUD composition",error);
    if(frame.width!=impl_->inputW||frame.height!=impl_->inputH||!impl_->outputW)return Fail("DLSS render dimensions changed; reconfigure required",error);
    if(!std::isfinite(frame.frameTimeMs)||frame.frameTimeMs<=0)return Fail("DLSS requires the actual positive render interval",error);
    const auto mv=frame.motion->GetDesc();
    if(mv.Format!=DXGI_FORMAT_R16G16_FLOAT&&mv.Format!=DXGI_FORMAT_R32G32_FLOAT)return Fail("DLSS motion must be a real two-channel float texture",error);
    if(frame.reactiveMask) {
        const auto reactive=frame.reactiveMask->GetDesc();
        if(reactive.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||reactive.Format!=DXGI_FORMAT_R8_UNORM||
           reactive.Width!=unsigned(frame.width)||reactive.Height!=unsigned(frame.height)||reactive.SampleDesc.Count!=1)
            return Fail("DLSS reactive coverage does not match render dimensions",error);
    }
    const bool newFeature=!impl_->feature;
    if(newFeature){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=impl_->outputW;desc.Height=impl_->outputH;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&impl_->output))))return Fail("DLSS output allocation failed",error);
        NVSDK_NGX_DLSS_Create_Params create{};create.Feature={unsigned(frame.width),unsigned(frame.height),unsigned(impl_->outputW),unsigned(impl_->outputH),Quality(impl_->quality)};
        create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        if(!Ngx(NGX_D3D12_CREATE_DLSS_EXT(frame.commands,1,1,&impl_->feature,impl_->parameters,&create),"DLSS feature creation",error))return false;
    }
    const auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(frame.commands,frame.color,frame.colorState,read);Transition(frame.commands,frame.depth,frame.depthState,read);
    Transition(frame.commands,frame.motion,frame.motionState,read);
    if(frame.reactiveMask)Transition(frame.commands,frame.reactiveMask,frame.reactiveMaskState,read);
    Transition(frame.commands,impl_->output.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};eval.Feature.pInColor=frame.color;eval.Feature.pInOutput=impl_->output.Get();
    eval.pInDepth=frame.depth;eval.pInMotionVectors=frame.motion;eval.InJitterOffsetX=frame.jitterX;eval.InJitterOffsetY=frame.jitterY;
    eval.pInBiasCurrentColorMask=frame.reactiveMask;
    eval.InRenderSubrectDimensions={unsigned(frame.width),unsigned(frame.height)};eval.InReset=frame.resetHistory||newFeature;
    eval.InMVScaleX=eval.InMVScaleY=eval.InPreExposure=eval.InExposureScale=1;eval.InFrameTimeDeltaInMsec=frame.frameTimeMs;
    const auto status=NGX_D3D12_EVALUATE_DLSS_EXT(frame.commands,impl_->feature,impl_->parameters,&eval);
    D3D12_RESOURCE_BARRIER uav{};uav.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;uav.UAV.pResource=impl_->output.Get();frame.commands->ResourceBarrier(1,&uav);
    Transition(frame.commands,frame.color,read,frame.colorState);Transition(frame.commands,frame.depth,read,frame.depthState);
    Transition(frame.commands,frame.motion,read,frame.motionState);
    if(frame.reactiveMask)Transition(frame.commands,frame.reactiveMask,read,frame.reactiveMaskState);
    Transition(frame.commands,impl_->output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(!Ngx(status,"DLSS evaluation",error))return false;
    result->color=impl_->output.Get();result->state=D3D12_RESOURCE_STATE_RENDER_TARGET;return true;
#else
    (void)frame;(void)result;return Fail("DLSS SDK unavailable",error);
#endif
}
} // namespace port
#endif
