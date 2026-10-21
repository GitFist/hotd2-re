#include "d3d12_final_frames.h"
#ifdef _WIN32
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include "hlsl_shaders.h"

namespace port {
using Microsoft::WRL::ComPtr;
namespace {
void Transition(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};c->ResourceBarrier(1,&b);
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;return h;}
bool Fail(const char* text,std::string* error){if(error)*error=text;return false;}
}
struct D3D12FinalFrames::Impl {
    NativeD3D12FinalFrameCallback callback=nullptr;void* user=nullptr;
    ComPtr<ID3D12Resource> rawHudless,finalColor,hudless,uniforms,generated,retained,readback;
    ComPtr<ID3D12DescriptorHeap> rtv,srv,sampler;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    D3D12_RESOURCE_STATES colorState=D3D12_RESOURCE_STATE_RENDER_TARGET;
    uint32_t disable=1;bool reset=true,captured=false;
    bool Init(ID3D12Device*,std::string*);
    bool Targets(ID3D12Device*,const D3D12_RESOURCE_DESC&,std::string*);
    bool Post(const NativeD3D12FrameInterop&,ID3D12Resource*,D3D12_RESOURCE_STATES,ID3D12Resource*,UINT,const PresentConstants&,std::string*);
};
D3D12FinalFrames::D3D12FinalFrames():impl_(std::make_unique<Impl>()){}
D3D12FinalFrames::~D3D12FinalFrames()=default;
void D3D12FinalFrames::SetCallback(NativeD3D12FinalFrameCallback callback,void* user){impl_->callback=callback;impl_->user=user;}
bool D3D12FinalFrames::Enabled() const{return impl_->callback!=nullptr;}
void D3D12FinalFrames::BeginFrame(){auto& i=*impl_;i.generated.Reset();i.retained.Reset();i.readback.Reset();i.disable=1;i.reset=true;i.captured=false;}
bool D3D12FinalFrames::Impl::Init(ID3D12Device* device,std::string* error) {
    if(pipeline)return true;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=2;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtv))))return Fail("FG final RTV allocation failed",error);
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srv))))return Fail("FG final SRV allocation failed",error);
    heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;heap.NumDescriptors=1;
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&sampler))))return Fail("FG final sampler allocation failed",error);
    D3D12_SAMPLER_DESC sample{};sample.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;sample.AddressU=sample.AddressV=sample.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sample.MaxLOD=D3D12_FLOAT32_MAX;sample.MaxAnisotropy=1;sample.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    device->CreateSampler(&sample,sampler->GetCPUDescriptorHandleForHeapStart());
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=1;ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;ranges[1].NumDescriptors=1;
    D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    for(int k=1;k<3;++k){p[k].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[k].DescriptorTable.NumDescriptorRanges=1;p[k].DescriptorTable.pDescriptorRanges=&ranges[k-1];p[k].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=3;rootDesc.pParameters=p;
    ComPtr<ID3DBlob> rootBlob,errors,vs,ps;
    if(FAILED(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBlob,&errors))||FAILED(device->CreateRootSignature(0,rootBlob->GetBufferPointer(),rootBlob->GetBufferSize(),IID_PPV_ARGS(&root))))return Fail("FG final root signature failed",error);
    if(FAILED(D3DCompile(kPresentShaderHlsl,strlen(kPresentShaderHlsl),"fg-final",nullptr,nullptr,"vsmain","vs_5_1",0,0,&vs,&errors))||
       FAILED(D3DCompile(kPresentShaderHlsl,strlen(kPresentShaderHlsl),"fg-final",nullptr,nullptr,"psmain","ps_5_1",0,0,&ps,&errors)))return Fail("FG final shader compilation failed",error);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root.Get();desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()};desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    auto& blend=desc.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask=UINT_MAX;desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
    desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;desc.DepthStencilState.StencilReadMask=desc.DepthStencilState.StencilWriteMask=0xff;
    desc.DepthStencilState.FrontFace=desc.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.NumRenderTargets=1;desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    if(FAILED(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline))))return Fail("FG final presentation pipeline failed",error);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=512;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto upload=Heap(D3D12_HEAP_TYPE_UPLOAD);
    if(FAILED(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uniforms))))return Fail("FG final constant allocation failed",error);
    return true;
}
bool D3D12FinalFrames::Impl::Targets(ID3D12Device* device,const D3D12_RESOURCE_DESC& input,std::string* error) {
    if(input.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||input.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||input.DepthOrArraySize!=1||input.MipLevels!=1||input.SampleDesc.Count!=1||input.Width<1||input.Width>8192||input.Height<1||input.Height>8192)return Fail("FG final color requires a supported RGBA8 texture",error);
    if(rawHudless&&rawHudless->GetDesc().Width==input.Width&&rawHudless->GetDesc().Height==input.Height)return true;
    auto desc=input;desc.MipLevels=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;
    ComPtr<ID3D12Resource> raw,final,hud;
    const auto allocate=[&](ComPtr<ID3D12Resource>& resource){return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(resource.GetAddressOf())));};
    if(!allocate(raw)||!allocate(final)||!allocate(hud))return Fail("FG matching final/HUDless allocation failed",error);
    rawHudless=raw;finalColor=final;hudless=hud;return true;
}
bool D3D12FinalFrames::CaptureHudless(ID3D12Device* device,ID3D12GraphicsCommandList* commands,ID3D12Resource* source,D3D12_RESOURCE_STATES state,std::string* error) {
    auto& i=*impl_;if(!i.callback)return true;
    if(!source||!i.Init(device,error)||!i.Targets(device,source->GetDesc(),error))return false;
    Transition(commands,source,state,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(commands,i.rawHudless.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyResource(i.rawHudless.Get(),source);
    Transition(commands,source,D3D12_RESOURCE_STATE_COPY_SOURCE,state);Transition(commands,i.rawHudless.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);i.captured=true;return true;
}
bool D3D12FinalFrames::Impl::Post(const NativeD3D12FrameInterop& frame,ID3D12Resource* source,D3D12_RESOURCE_STATES state,ID3D12Resource* target,UINT index,const PresentConstants& constants,std::string* error) {
    const UINT srvStride=frame.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),rtvStride=frame.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto srvCpu=srv->GetCPUDescriptorHandleForHeapStart();srvCpu.ptr+=SIZE_T(index)*srvStride;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
    frame.device->CreateShaderResourceView(source,&view,srvCpu);
    auto rt=rtv->GetCPUDescriptorHandleForHeapStart();rt.ptr+=SIZE_T(index)*rtvStride;frame.device->CreateRenderTargetView(target,nullptr,rt);
    void* data=nullptr;D3D12_RANGE empty{};if(FAILED(uniforms->Map(0,&empty,&data)))return Fail("FG final constant map failed",error);
    memcpy(static_cast<uint8_t*>(data)+index*256,&constants,sizeof(constants));uniforms->Unmap(0,nullptr);
    Transition(frame.commands,source,state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    const auto desc=target->GetDesc();const D3D12_VIEWPORT vp{0,0,float(desc.Width),float(desc.Height),0,1};const D3D12_RECT scissor{0,0,LONG(desc.Width),LONG(desc.Height)};
    frame.commands->OMSetRenderTargets(1,&rt,FALSE,nullptr);frame.commands->RSSetViewports(1,&vp);frame.commands->RSSetScissorRects(1,&scissor);
    ID3D12DescriptorHeap* heaps[]={srv.Get(),sampler.Get()};frame.commands->SetDescriptorHeaps(2,heaps);frame.commands->SetGraphicsRootSignature(root.Get());frame.commands->SetPipelineState(pipeline.Get());
    auto texture=srv->GetGPUDescriptorHandleForHeapStart();texture.ptr+=UINT64(index)*srvStride;
    frame.commands->SetGraphicsRootConstantBufferView(0,uniforms->GetGPUVirtualAddress()+index*256);frame.commands->SetGraphicsRootDescriptorTable(1,texture);frame.commands->SetGraphicsRootDescriptorTable(2,sampler->GetGPUDescriptorHandleForHeapStart());
    frame.commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);frame.commands->DrawInstanced(3,1,0,0);
    Transition(frame.commands,source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,state);return true;
}
bool D3D12FinalFrames::Evaluate(const NativeD3D12FrameInterop& frame,ID3D12Resource* color,D3D12_RESOURCE_STATES state,const GraphicsOptions& options,int crt,bool paused,std::string* error) {
    auto& i=*impl_;if(!i.callback||!i.captured||!color)return Fail("No matching pre-HUD frame for interpolation",error);
    const auto desc=color->GetDesc();if(!i.rawHudless||desc.Width!=i.rawHudless->GetDesc().Width||desc.Height!=i.rawHudless->GetDesc().Height)return Fail("FG final/HUDless dimensions changed within the frame",error);
    const auto constants=MakePresentConstants(int(desc.Width),int(desc.Height),int(desc.Width),int(desc.Height),crt,options);
    if(!i.Post(frame,color,state,i.finalColor.Get(),0,constants,error)||!i.Post(frame,i.rawHudless.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,i.hudless.Get(),1,constants,error))return false;
    NativeD3D12FinalFrame input;input.raster=frame;input.color=i.finalColor.Get();input.hudless=i.hudless.Get();input.paused=paused;
    NativeD3D12GeneratedFrames output;if(!i.callback(input,&output,i.user,error))return false;
    if(!output.generated||!output.retained||output.generated==output.retained||!output.disableInterpolation)return Fail("FG callback did not return two distinct real frames and a GPU flag",error);
    for(auto* resource:{output.generated,output.retained}){const auto out=resource->GetDesc();if(out.Format!=desc.Format||out.Width!=desc.Width||out.Height!=desc.Height||out.MipLevels!=1||out.SampleDesc.Count!=1||out.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||out.DepthOrArraySize!=1)return Fail("FG callback returned incompatible final frames",error);}
    const auto flag=output.disableInterpolation->GetDesc();if(flag.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||flag.Width<4)return Fail("FG callback disable flag is not a real GPU buffer",error);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto heap=Heap(D3D12_HEAP_TYPE_READBACK);if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&i.readback))))return Fail("FG disable flag readback allocation failed",error);
    Transition(frame.commands,output.disableInterpolation,output.flagState,D3D12_RESOURCE_STATE_COPY_SOURCE);frame.commands->CopyBufferRegion(i.readback.Get(),0,output.disableInterpolation,0,4);Transition(frame.commands,output.disableInterpolation,D3D12_RESOURCE_STATE_COPY_SOURCE,output.flagState);
    i.generated=output.generated;i.retained=output.retained;i.colorState=output.colorState;i.reset=output.reset;return true;
}
bool D3D12FinalFrames::ResolveFlag(std::string* error){auto& i=*impl_;if(!i.readback)return Fail("No completed FG disable flag",error);void* value=nullptr;const D3D12_RANGE range{0,4};if(FAILED(i.readback->Map(0,&range,&value)))return Fail("FG disable flag readback failed",error);memcpy(&i.disable,value,4);D3D12_RANGE empty{};i.readback->Unmap(0,&empty);return true;}
ID3D12Resource* D3D12FinalFrames::Generated() const{return impl_->generated.Get();}
ID3D12Resource* D3D12FinalFrames::Retained() const{return impl_->retained.Get();}
D3D12_RESOURCE_STATES D3D12FinalFrames::ColorState() const{return impl_->colorState;}
uint32_t D3D12FinalFrames::DisableFlag() const{return impl_->disable;}
bool D3D12FinalFrames::Reset() const{return impl_->reset;}
} // namespace port
#endif
