#include "xess_super_resolution.h"
#ifdef _WIN32
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#if defined(HOTD2_XESS_SDK)
#include <wintrust.h>
#include <softpub.h>
#include <xess/xess_d3d12.h>
#endif
namespace port {
using Microsoft::WRL::ComPtr;
struct XessSuperResolution::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> linearInput,linearOutput,output;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> transfer;
    ComPtr<ID3D12DescriptorHeap> heap;
    int iw=0,ih=0,ow=0,oh=0,quality=-1;
    bool first=true;
#if defined(HOTD2_XESS_SDK)
    HMODULE module=nullptr;xess_context_handle_t context=nullptr;
    decltype(&xessD3D12CreateContext) create=nullptr;
    decltype(&xessD3D12Init) init=nullptr;
    decltype(&xessGetInputResolution) resolution=nullptr;
    decltype(&xessD3D12Execute) execute=nullptr;
    decltype(&xessDestroyContext) destroy=nullptr;
    ~Impl(){if(context&&destroy)destroy(context);if(module)FreeLibrary(module);}
    bool Transfers(std::string*);
    bool Targets(std::string*);
    void Convert(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*,int,int,bool,int);
#endif
};
XessSuperResolution::XessSuperResolution():impl_(std::make_unique<Impl>()){}
XessSuperResolution::~XessSuperResolution()=default;
namespace {
bool Fail(const char* message,std::string* error){if(error)*error=message;return false;}
#if defined(HOTD2_XESS_SDK)
bool Api(xess_result_t status,const char* action,std::string* error){if(int(status)>=0)return true;if(error){char text[128];std::snprintf(text,sizeof(text),"%s failed (XeSS %d)",action,int(status));*error=text;}return false;}
bool Trusted(const std::filesystem::path& path){WINTRUST_FILE_INFO file{};file.cbStruct=sizeof(file);file.pcwszFilePath=path.c_str();WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;const auto result=WinVerifyTrust(nullptr,&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result==ERROR_SUCCESS;}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&barrier);}
void Uav(ID3D12GraphicsCommandList* list,ID3D12Resource* r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;list->ResourceBarrier(1,&b);}
xess_quality_settings_t Quality(int q){return q==3?XESS_QUALITY_SETTING_AA:q==2?XESS_QUALITY_SETTING_PERFORMANCE:q==1?XESS_QUALITY_SETTING_BALANCED:XESS_QUALITY_SETTING_QUALITY;}
#endif
}
#if defined(HOTD2_XESS_SDK)
bool XessSuperResolution::Impl::Transfers(std::string* error){
    // XeSS requires linear color. Legacy raster is perceptual SDR: decode to
    // FP16 before reconstruction, then encode back before output-resolution HUD.
    static constexpr char shader[]=R"(
Texture2D<float4> src:register(t0);RWTexture2D<float4> dst:register(u0);
cbuffer Transfer:register(b0){uint width,height,decode,pad;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=width||id.y>=height)return;float3 v=max(src.Load(int3(id.xy,0)).rgb,0);
 if(decode){float3 lo=v/12.92;float3 hi=pow((v+.055)/1.055,2.4);v=float3(v.r<=.04045?lo.r:hi.r,v.g<=.04045?lo.g:hi.g,v.b<=.04045?lo.b:hi.b);}
 else{float3 lo=v*12.92;float3 hi=1.055*pow(v,1/2.4)-.055;v=float3(v.r<=.0031308?lo.r:hi.r,v.g<=.0031308?lo.g:hi.g,v.b<=.0031308?lo.b:hi.b);}
 dst[id.xy]=float4(v,1);
})";
    ComPtr<ID3DBlob> code,log,signature;
    if(FAILED(D3DCompile(shader,sizeof(shader)-1,"xess-transfer",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&log)))return Fail("XeSS color transfer shader compilation failed",error);
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0}};
    D3D12_ROOT_PARAMETER parameters[3]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,4};
    for(int i=0;i<2;++i){parameters[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i+1].DescriptorTable={1,&ranges[i]};}
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=parameters;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&log))||FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root))))return Fail("XeSS transfer root signature failed",error);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root.Get();pipeline.CS={code->GetBufferPointer(),code->GetBufferSize()};
    if(FAILED(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&transfer))))return Fail("XeSS transfer pipeline failed",error);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heapDesc.NumDescriptors=4;heapDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    return SUCCEEDED(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&heap)))||Fail("XeSS transfer descriptor heap failed",error);
}
bool XessSuperResolution::Impl::Targets(std::string* error){
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;properties.CreationNodeMask=properties.VisibleNodeMask=1;
    auto make=[&](int width,int height,DXGI_FORMAT format,D3D12_RESOURCE_STATES state,bool rt,ComPtr<ID3D12Resource>& resource){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;if(rt)d.Flags|=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;return SUCCEEDED(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&resource)));};
    return (make(iw,ih,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,false,linearInput)&&
            make(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,false,linearOutput)&&
            make(ow,oh,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_RENDER_TARGET,true,output))||Fail("XeSS linear/output texture allocation failed",error);
}
void XessSuperResolution::Impl::Convert(ID3D12GraphicsCommandList* list,ID3D12Resource* source,ID3D12Resource* target,int width,int height,bool decode,int offset){
    const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=offset*stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=source->GetDesc().Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(source,&srv,cpu);cpu.ptr+=stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=target->GetDesc().Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(target,nullptr,&uav,cpu);
    ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());list->SetPipelineState(transfer.Get());
    const UINT constants[]={UINT(width),UINT(height),decode?1u:0u,0};list->SetComputeRoot32BitConstants(0,4,constants,0);
    auto gpu=heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=offset*stride;list->SetComputeRootDescriptorTable(1,gpu);gpu.ptr+=stride;list->SetComputeRootDescriptorTable(2,gpu);list->Dispatch((width+7)/8,(height+7)/8,1);Uav(list,target);
}
#endif
bool XessSuperResolution::Initialize(ID3D12Device* device,const std::filesystem::path& directory,std::string* error){
#if defined(HOTD2_XESS_SDK)
    if(impl_->module||!device)return Fail("Invalid XeSS initialization",error);
    std::error_code ec;const auto dll=std::filesystem::canonical(directory/L"libxess.dll",ec);if(ec||!Trusted(dll))return Fail("XeSS requires a signed Intel feature DLL",error);
    impl_->module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!impl_->module)return Fail("Intel XeSS DLL could not be loaded",error);
    auto& i=*impl_;i.create=reinterpret_cast<decltype(i.create)>(GetProcAddress(i.module,"xessD3D12CreateContext"));i.init=reinterpret_cast<decltype(i.init)>(GetProcAddress(i.module,"xessD3D12Init"));i.resolution=reinterpret_cast<decltype(i.resolution)>(GetProcAddress(i.module,"xessGetInputResolution"));i.execute=reinterpret_cast<decltype(i.execute)>(GetProcAddress(i.module,"xessD3D12Execute"));i.destroy=reinterpret_cast<decltype(i.destroy)>(GetProcAddress(i.module,"xessDestroyContext"));
    if(!i.create||!i.init||!i.resolution||!i.execute||!i.destroy)return Fail("Intel XeSS API is incomplete",error);
    i.device=device;return Api(i.create(device,&i.context),"XeSS device context",error)&&i.Transfers(error);
#else
    (void)device;(void)directory;return Fail("This build has no optional Intel XeSS SDK integration",error);
#endif
}
bool XessSuperResolution::Configure(int ow,int oh,int quality,int* iw,int* ih,std::string* error){
#if defined(HOTD2_XESS_SDK)
    auto& i=*impl_;if(!i.context||!iw||!ih||ow<32||oh<32||ow>16384||oh>16384||quality<0||quality>3)return Fail("Invalid XeSS configuration",error);
    xess_2d_t output={unsigned(ow),unsigned(oh)},input{};if(!Api(i.resolution(i.context,&output,Quality(quality),&input),"XeSS input resolution",error))return false;
    if(!input.x||!input.y||input.x>unsigned(ow)||input.y>unsigned(oh))return Fail("XeSS returned invalid input dimensions",error);
    if(quality!=i.quality||ow!=i.ow||oh!=i.oh){
        xess_d3d12_init_params_t init{};init.outputResolution=output;init.qualitySetting=Quality(quality);init.creationNodeMask=init.visibleNodeMask=1;
        init.initFlags=XESS_INIT_FLAG_INVERTED_DEPTH|XESS_INIT_FLAG_LDR_INPUT_COLOR|XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
        if(!Api(i.init(i.context,&init),"XeSS pipeline initialization",error))return false;
        i.ow=ow;i.oh=oh;i.iw=input.x;i.ih=input.y;i.quality=quality;i.first=true;
        i.linearInput.Reset();i.linearOutput.Reset();i.output.Reset();if(!i.Targets(error))return false;
    }
    *iw=input.x;*ih=input.y;return true;
#else
    (void)ow;(void)oh;(void)quality;(void)iw;(void)ih;return Fail("XeSS SDK unavailable",error);
#endif
}
bool XessSuperResolution::Evaluate(const NativeD3D12FrameInterop& f,NativeD3D12FrameOutput* out,std::string* error){
#if defined(HOTD2_XESS_SDK)
    auto& i=*impl_;if(!i.context||!i.output||!out||f.device!=i.device.Get()||!f.commands||!f.color||!f.depth||!f.motionValid||!f.motion||!f.reactiveMask||f.width!=i.iw||f.height!=i.ih)return Fail("XeSS needs actual matching scene depth/motion/reactive resources before HUD",error);
    auto* list=f.commands;const auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Transition(list,f.color,f.colorState,read);Transition(list,f.depth,f.depthState,read);Transition(list,f.motion,f.motionState,read);Transition(list,f.reactiveMask,f.reactiveMaskState,read);
    Transition(list,i.linearInput.Get(),read,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);i.Convert(list,f.color,i.linearInput.Get(),i.iw,i.ih,true,0);Transition(list,i.linearInput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,read);
    xess_d3d12_execute_params_t params{};params.pColorTexture=i.linearInput.Get();params.pVelocityTexture=f.motion;params.pDepthTexture=f.depth;params.pResponsivePixelMaskTexture=f.reactiveMask;params.pOutputTexture=i.linearOutput.Get();params.jitterOffsetX=f.jitterX;params.jitterOffsetY=f.jitterY;params.exposureScale=1;params.resetHistory=i.first||f.resetHistory;params.inputWidth=f.width;params.inputHeight=f.height;
    const auto status=i.execute(i.context,list,&params);Uav(list,i.linearOutput.Get());
    if(int(status)>=0){Transition(list,i.linearOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,read);Transition(list,i.output.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);i.Convert(list,i.linearOutput.Get(),i.output.Get(),i.ow,i.oh,false,2);Transition(list,i.output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);Transition(list,i.linearOutput.Get(),read,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
    Transition(list,f.color,read,f.colorState);Transition(list,f.depth,read,f.depthState);Transition(list,f.motion,read,f.motionState);Transition(list,f.reactiveMask,read,f.reactiveMaskState);
    if(!Api(status,"XeSS temporal evaluation",error))return false;i.first=false;out->color=i.output.Get();out->state=D3D12_RESOURCE_STATE_RENDER_TARGET;return true;
#else
    (void)f;(void)out;return Fail("XeSS SDK unavailable",error);
#endif
}
}
#endif
