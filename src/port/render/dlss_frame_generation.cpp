#include "dlss_frame_generation.h"
#ifdef _WIN32
#include "ngx_context.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <wrl/client.h>
#if defined(HOTD2_NGX_SDK)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers_dlssg_d3d.h>
#endif

namespace port {
using Microsoft::WRL::ComPtr;
struct DlssFrameGeneration::Impl {
    std::shared_ptr<NgxContext> context;
    ComPtr<ID3D12Resource> generated,retained,disable;
    int width=0,height=0,renderWidth=0,renderHeight=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    uint64_t lastFrame=0;
    bool available=false,haveFrame=false;
#if defined(HOTD2_NGX_SDK)
    NVSDK_NGX_Parameter* parameters=nullptr;
    NVSDK_NGX_Handle* feature=nullptr;
    void ReleaseFeature(){if(feature)NVSDK_NGX_D3D12_ReleaseFeature(feature);feature=nullptr;generated.Reset();retained.Reset();disable.Reset();haveFrame=false;}
    ~Impl(){ReleaseFeature();if(parameters)NVSDK_NGX_D3D12_DestroyParameters(parameters);}
#endif
};
DlssFrameGeneration::DlssFrameGeneration():impl_(std::make_unique<Impl>()){}
DlssFrameGeneration::~DlssFrameGeneration()=default;
bool DlssFrameGeneration::Available()const{return impl_->available;}
namespace {
bool Fail(const char* why,std::string* error){if(error)*error=why;return false;}
#if defined(HOTD2_NGX_SDK)
bool Ngx(NVSDK_NGX_Result result,const char* operation,std::string* error){
    if(NVSDK_NGX_SUCCEED(result))return true;
    if(error){char text[128];std::snprintf(text,sizeof(text),"%s failed (NGX 0x%08X)",operation,unsigned(result));*error=text;}return false;
}
bool Inverse(const float* matrix,float* inverse){
    double a[4][8]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){if(!std::isfinite(matrix[r*4+c]))return false;a[r][c]=matrix[r*4+c];a[r][c+4]=r==c?1:0;}
    for(int c=0;c<4;++c){
        int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(std::abs(a[pivot][c])<1e-14)return false;
        if(pivot!=c)for(int k=0;k<8;++k)std::swap(a[pivot][k],a[c][k]);
        const double scale=a[c][c];for(int k=0;k<8;++k)a[c][k]/=scale;
        for(int r=0;r<4;++r)if(r!=c){const double factor=a[r][c];for(int k=0;k<8;++k)a[r][k]-=factor*a[c][k];}
    }
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){inverse[r*4+c]=static_cast<float>(a[r][c+4]);if(!std::isfinite(inverse[r*4+c]))return false;}
    return true;
}
void Multiply(const float* a,const float* b,float* out){for(int r=0;r<4;++r)for(int c=0;c<4;++c){double value=0;for(int k=0;k<4;++k)value+=double(a[r*4+k])*b[k*4+c];out[r*4+c]=static_cast<float>(value);}}
void Identity(float* matrix){for(int k=0;k<16;++k)matrix[k]=k%5==0?1.f:0.f;}
bool Unit(float* v){const double n=std::sqrt(double(v[0])*v[0]+double(v[1])*v[1]+double(v[2])*v[2]);if(!std::isfinite(n)||n<1e-8)return false;for(int k=0;k<3;++k)v[k]=static_cast<float>(v[k]/n);return true;}
bool Camera(const DlssFrameGenerationInput& input,NVSDK_NGX_DLSSG_Opt_Eval_Params* out){
    const auto& c=input.camera;if(!c.valid)return false;
    float inverseProjection[16],inverseView[16],currentVP[16],previousVP[16],inverseCurrentVP[16],inversePreviousVP[16];
    if(!Inverse(c.projection,inverseProjection)||!Inverse(c.view,inverseView))return false;
    if(std::abs(c.projection[11])<1e-6f||std::abs(c.projection[0])<1e-6f||std::abs(c.projection[5])<1e-6f)return false;
    Multiply(c.view,c.projection,currentVP);Multiply(c.previousView,c.previousProjection,previousVP);
    if(!Inverse(currentVP,inverseCurrentVP)||!Inverse(previousVP,inversePreviousVP))return false;
    std::memcpy(out->cameraViewToClip,c.projection,sizeof(c.projection));std::memcpy(out->clipToCameraView,inverseProjection,sizeof(inverseProjection));
    Multiply(inverseCurrentVP,previousVP,&out->clipToPrevClip[0][0]);Multiply(inversePreviousVP,currentVP,&out->prevClipToClip[0][0]);Identity(&out->clipToLensClip[0][0]);
    // Decode actual reversed near/far planes through the inverse projection,
    // including the game's negative-view-Z convention and off-center camera.
    const double nearZ=double(inverseProjection[10])+inverseProjection[14];
    const double nearW=double(inverseProjection[11])+inverseProjection[15];
    const double farZ=inverseProjection[14],farW=inverseProjection[15];
    if(std::abs(nearW)<1e-12)return false;
    out->cameraNear=static_cast<float>(std::abs(nearZ/nearW));
    out->cameraFar=std::abs(farW)<1e-12?std::numeric_limits<float>::max():static_cast<float>(std::abs(farZ/farW));
    if(!std::isfinite(out->cameraNear)||out->cameraNear<=0||!std::isfinite(out->cameraFar)||out->cameraFar<=out->cameraNear)return false;
    out->cameraFOV=2*std::atan(1/std::abs(c.projection[5]));out->cameraAspectRatio=std::abs(c.projection[5]/c.projection[0]);
    const float forward=c.projection[11]<0?-1.f:1.f;
    for(int k=0;k<3;++k){out->cameraPos[k]=inverseView[12+k];out->cameraRight[k]=inverseView[k];out->cameraUp[k]=inverseView[4+k];out->cameraFwd[k]=inverseView[8+k]*forward;}
    if(!Unit(out->cameraRight)||!Unit(out->cameraUp)||!Unit(out->cameraFwd))return false;
    // FG's SDK field is clip-space jitter; render motion itself is unjittered.
    out->jitterOffset[0]=2*input.raster.jitterX/input.raster.width;out->jitterOffset[1]=-2*input.raster.jitterY/input.raster.height;
    out->mvecScale[0]=out->mvecScale[1]=1;
    return true;
}
void Transition(ID3D12GraphicsCommandList* commands,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};commands->ResourceBarrier(1,&b);
}
bool SameDevice(ID3D12Resource* resource,ID3D12Device* device){ComPtr<ID3D12Device> actual;return resource&&SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(&actual)))&&actual.Get()==device;}
bool Texture(ID3D12Resource* resource,int width,int height){if(!resource)return false;const auto d=resource->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==static_cast<UINT64>(width)&&d.Height==static_cast<UINT>(height)&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&!(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);}
#endif
}
bool DlssFrameGeneration::Initialize(ID3D12Device* device,const std::filesystem::path& directory,std::string* error){
#if defined(HOTD2_NGX_SDK)
    if(impl_->context)return Fail("DLSS Frame Generation is already initialized",error);
    auto context=AcquireNgxContext(device,directory,{L"nvngx_dlssg.dll"},error);if(!context)return false;
    NVSDK_NGX_Parameter* parameters=nullptr;
    if(!Ngx(NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters),"DLSS FG capability query",error))return false;
    int supported=0;const auto status=parameters->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&supported);
    if(NVSDK_NGX_FAILED(status)||!supported){NVSDK_NGX_D3D12_DestroyParameters(parameters);return Fail("DLSS Frame Generation is unavailable on this adapter/driver",error);}
    impl_->context=std::move(context);impl_->parameters=parameters;impl_->available=true;return true;
#else
    (void)device;(void)directory;return Fail("This build has no optional NVIDIA DLSS Frame Generation SDK integration",error);
#endif
}
bool DlssFrameGeneration::Evaluate(const DlssFrameGenerationInput& input,DlssFrameGenerationOutput* output,std::string* error){
    if(output)*output={};
#if defined(HOTD2_NGX_SDK)
    const auto& frame=input.raster;
    if(!Available()||!output||frame.device!=impl_->context->Device()||!frame.commands||!frame.motionValid||frame.width<32||frame.height<32||frame.width>8192||frame.height>8192)
        return Fail("DLSS FG needs a supported native device and actual scene motion metadata",error);
    if(!std::isfinite(frame.frameTimeMs)||frame.frameTimeMs<=0)return Fail("DLSS FG requires the actual positive render interval",error);
    if(!SameDevice(input.finalColor,frame.device)||!SameDevice(frame.depth,frame.device)||!SameDevice(frame.motion,frame.device)||
       !Texture(frame.depth,frame.width,frame.height)||!Texture(frame.motion,frame.width,frame.height))return Fail("DLSS FG input textures do not match the native device/render dimensions",error);
    const auto desc=input.finalColor->GetDesc(),motionDesc=frame.motion->GetDesc();
    if(desc.Width<32||desc.Width>8192||desc.Height<32||desc.Height>8192||!Texture(input.finalColor,static_cast<int>(desc.Width),static_cast<int>(desc.Height))||
       (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM))return Fail("DLSS FG final color format/dimensions unsupported",error);
    if(motionDesc.Format!=DXGI_FORMAT_R16G16_FLOAT&&motionDesc.Format!=DXGI_FORMAT_R32G32_FLOAT)return Fail("DLSS FG requires real two-channel float motion, not a metadata alias",error);
    if(input.hudless&&(!SameDevice(input.hudless,frame.device)||!Texture(input.hudless,static_cast<int>(desc.Width),static_cast<int>(desc.Height))||input.hudless->GetDesc().Format!=desc.Format))return Fail("DLSS FG HUDless texture must match final color",error);
    NVSDK_NGX_DLSSG_Opt_Eval_Params options{};if(!Camera(input,&options))return Fail("DLSS FG requires finite actual current/previous perspective camera matrices",error);
    if(impl_->width!=int(desc.Width)||impl_->height!=int(desc.Height)||impl_->renderWidth!=frame.width||impl_->renderHeight!=frame.height||impl_->format!=desc.Format)impl_->ReleaseFeature();
    const bool create=!impl_->feature;
    if(create){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        auto target=desc;target.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;target.Alignment=0;
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&impl_->generated)))||
           FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&impl_->retained))))return Fail("DLSS FG output allocation failed",error);
        D3D12_RESOURCE_DESC flag{};flag.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;flag.Width=4;flag.Height=flag.DepthOrArraySize=flag.MipLevels=1;flag.SampleDesc.Count=1;flag.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;flag.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&flag,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&impl_->disable))))return Fail("DLSS FG interpolation-status allocation failed",error);
        NVSDK_NGX_DLSSG_Create_Params config{};config.Width=static_cast<unsigned>(desc.Width);config.Height=desc.Height;config.NativeBackbufferFormat=desc.Format;config.RenderWidth=frame.width;config.RenderHeight=frame.height;
        if(!Ngx(NGX_D3D12_CREATE_DLSSG(frame.commands,1,1,&impl_->feature,impl_->parameters,&config),"DLSS FG feature creation",error))return false;
        impl_->width=static_cast<int>(desc.Width);impl_->height=desc.Height;impl_->renderWidth=frame.width;impl_->renderHeight=frame.height;impl_->format=desc.Format;
    }
    // Direct NGX FG has no frame-time input parameter. A long render gap must
    // still reset interpolation instead of generating a stale intermediate.
    options.reset=frame.resetHistory||frame.frameTimeMs>250.f||create||!impl_->haveFrame||frame.frame!=impl_->lastFrame+1;
    options.depthInverted=true;options.cameraMotionIncluded=true;options.motionVectorsDilated=false;
    options.motionVectorsInvalidValue=std::numeric_limits<float>::max(); // static zero motion remains valid
    options.notRenderingGameFrames=input.paused;options.menuDetectionEnabled=true;
    options.colorBuffersHDR=input.hdr;
    options.multiFrameCount=options.multiFrameIndex=1;
    options.mvecsSubrectSize={unsigned(frame.width),unsigned(frame.height)};options.depthSubrectSize=options.mvecsSubrectSize;
    options.backbufferSubrectSize={unsigned(desc.Width),desc.Height};options.outputInterpSubrectSize=options.backbufferSubrectSize;options.outputRealSubrectSize=options.backbufferSubrectSize;
    if(input.hudless)options.hudLessSubrectSize=options.backbufferSubrectSize;
    const auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(frame.commands,input.finalColor,input.finalColorState,read);Transition(frame.commands,frame.depth,frame.depthState,read);Transition(frame.commands,frame.motion,frame.motionState,read);
    const bool separateHudless=input.hudless&&input.hudless!=input.finalColor;
    if(separateHudless)Transition(frame.commands,input.hudless,input.hudlessState,read);
    Transition(frame.commands,impl_->generated.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(frame.commands,impl_->retained.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    NVSDK_NGX_D3D12_DLSSG_Eval_Params eval{};eval.pBackbuffer=input.finalColor;eval.pDepth=frame.depth;eval.pMVecs=frame.motion;eval.pHudless=input.hudless;
    eval.pOutputInterpFrame=impl_->generated.Get();eval.pOutputRealFrame=impl_->retained.Get();eval.pOutputDisableInterpolation=impl_->disable.Get();
    const auto status=NGX_D3D12_EVALUATE_DLSSG(frame.commands,impl_->feature,impl_->parameters,&eval,&options);
    for(auto* resource:{impl_->generated.Get(),impl_->retained.Get(),impl_->disable.Get()}){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;frame.commands->ResourceBarrier(1,&b);}
    Transition(frame.commands,input.finalColor,read,input.finalColorState);Transition(frame.commands,frame.depth,read,frame.depthState);Transition(frame.commands,frame.motion,read,frame.motionState);
    if(separateHudless)Transition(frame.commands,input.hudless,read,input.hudlessState);
    Transition(frame.commands,impl_->generated.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(frame.commands,impl_->retained.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(!Ngx(status,"DLSS Frame Generation evaluation",error)){impl_->haveFrame=false;return false;}
    output->generated=impl_->generated.Get();output->retained=impl_->retained.Get();output->disableInterpolation=impl_->disable.Get();output->reset=options.reset;
    impl_->lastFrame=frame.frame;impl_->haveFrame=true;return true;
#else
    (void)input;return Fail("DLSS Frame Generation SDK unavailable",error);
#endif
}
} // namespace port
#endif
