// Native post-HUD integration contract, independently of any vendor SDK.
// This callback only copies known images; it is not an interpolation algorithm.
#include "render/d3d12_device.h"
#include "render/native_d3d12_frame_generation.h"
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
static void Require(bool value,const char* why){if(!value){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Barrier(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);}
struct TestCallback {
    ComPtr<ID3D12Resource> generated,retained,flag,upload;
    uint32_t disable=0;int calls=0;bool alias=false;
};
static bool CopyBoundary(const port::NativeD3D12FinalFrame& f,port::NativeD3D12GeneratedFrames* out,void* context,std::string*){
    auto& t=*static_cast<TestCallback*>(context);++t.calls;
    Require(f.raster.device&&f.raster.commands&&f.color&&f.hudless&&f.color!=f.hudless,"real matching final and HUDless resources");
    auto desc=f.color->GetDesc();Require(desc.Width==64&&desc.Height==48&&f.hudless->GetDesc().Width==64,"actual frame dimensions");
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    const auto allocate=[&](ComPtr<ID3D12Resource>& destination){return SUCCEEDED(f.raster.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(destination.ReleaseAndGetAddressOf())));};
    Require(allocate(t.generated)&&allocate(t.retained),"test GPU image allocation");
    Barrier(f.raster.commands,f.color,f.colorState,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(f.raster.commands,f.hudless,f.hudlessState,D3D12_RESOURCE_STATE_COPY_SOURCE);
    f.raster.commands->CopyResource(t.retained.Get(),f.color);f.raster.commands->CopyResource(t.generated.Get(),f.hudless);
    Barrier(f.raster.commands,f.color,D3D12_RESOURCE_STATE_COPY_SOURCE,f.colorState);Barrier(f.raster.commands,f.hudless,D3D12_RESOURCE_STATE_COPY_SOURCE,f.hudlessState);
    Barrier(f.raster.commands,t.generated.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);Barrier(f.raster.commands,t.retained.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Require(SUCCEEDED(f.raster.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(t.flag.ReleaseAndGetAddressOf()))),"real GPU disable flag");
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;buffer.Flags=D3D12_RESOURCE_FLAG_NONE;
    Require(SUCCEEDED(f.raster.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(t.upload.ReleaseAndGetAddressOf()))),"test flag upload");
    void* mapped=nullptr;D3D12_RANGE empty{};Require(SUCCEEDED(t.upload->Map(0,&empty,&mapped)),"map test flag");memcpy(mapped,&t.disable,4);t.upload->Unmap(0,nullptr);
    f.raster.commands->CopyBufferRegion(t.flag.Get(),0,t.upload.Get(),0,4);Barrier(f.raster.commands,t.flag.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    out->generated=t.generated.Get();out->retained=t.alias?t.generated.Get():t.retained.Get();out->disableInterpolation=t.flag.Get();out->reset=false;return true;
}
static void Compare(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const char* why){Require(a.size()==b.size(),why);for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]){fprintf(stderr,"%s: byte %zu actual=%d expected=%d\n",why,i,a[i],b[i]);std::exit(1);}}
int main(){
    port::GpuDeviceDesc desc;desc.width=64;desc.height=48;std::string error;auto gpu=port::CreateD3D12Device(desc,&error);Require(bool(gpu),error.c_str());
    port::GraphicsOptions options;options.gamma=61;options.brightness=54;options.sharpening=2;options.crtMask=1;options.crtGlow=1;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    port::PresentParams present;present.crt=1;gpu->SetPresent(present);
    // The HUD covers the middle of a colored checker; all operations occur on GPU.
    std::array<game::TLVertex,4> vertices{};const float xy[4][2]={{220,160},{420,160},{220,320},{420,320}};
    for(int i=0;i<4;++i){vertices[i].x=xy[i][0];vertices[i].y=xy[i][1];vertices[i].z=.5f;vertices[i].rhw=1;vertices[i].diffuse=0xffc03070;}
    gpu->SetSpriteVertices(vertices.data(),vertices.size());port::GpuDrawList hud;hud.width=64;hud.height=48;hud.sprite.emplace_back();hud.sprite[0].screen[2]=-1;
    port::GpuDraw draw;draw.pipeline=port::GpuDraw::Pipeline::Sprite;draw.stream=port::GpuDraw::Stream::Sprite;draw.count=4;draw.hud=true;hud.draws.push_back(draw);
    const float clear[4]={.12f,.28f,.48f,1};std::vector<uint8_t> expectedHudless,expectedFinal;
    Require(gpu->BeginFrame(clear),"baseline begin");Require(gpu->ReadPresentation(64,48,&expectedHudless),"baseline processed HUDless");gpu->Submit(hud);Require(gpu->ReadPresentation(64,48,&expectedFinal),"baseline processed final");gpu->EndFrame();Require(expectedHudless!=expectedFinal,"HUD affects actual pixels");
    TestCallback callback;Require(port::SetNativeD3D12FinalFrameCallback(*gpu,CopyBoundary,&callback,&error),error.c_str());
    auto render=[&](){Require(gpu->BeginFrame(clear),"FG boundary begin");Require(gpu->ProcessScene(&error),error.c_str());gpu->Submit(hud);gpu->EndFrame();};
    render();std::vector<uint8_t> generated,retained;int width=0,height=0;uint32_t flag=99;
    Require(port::ReadNativeD3D12GeneratedFrames(*gpu,&generated,&retained,&width,&height,&flag,&error),error.c_str());Require(width==64&&height==48&&flag==0,"real pair and zero disable flag readback");
    Compare(generated,expectedHudless,"HUDless effects applied exactly once");Compare(retained,expectedFinal,"final gamma/sharpen/CRT and HUD applied exactly once");
    auto stats=port::GetNativeD3D12FrameGenerationStats(*gpu);Require(stats.evaluated==1&&!stats.lastDisabled&&stats.generatedPresented==0&&stats.retainedPresented==0,"headless never fabricates presentation counts");
    callback.disable=1;render();Require(port::ReadNativeD3D12GeneratedFrames(*gpu,&generated,&retained,nullptr,nullptr,&flag,&error)&&flag==1,"actual GPU disable flag reaches backend");Require(port::GetNativeD3D12FrameGenerationStats(*gpu).lastDisabled,"disable flag suppresses interpolation");
    const auto before=gpu->EffectiveGraphics();auto warped=before;warped.crtCurvature=1;Require(!gpu->ConfigureGraphics(warped,&error)&&gpu->EffectiveGraphics()==before,"curvature conflict rejected transactionally");
    Require(port::SetNativeD3D12FinalFrameCallback(*gpu,nullptr,nullptr,&error),"disable FG");Require(gpu->ConfigureGraphics(warped,&error),error.c_str());Require(!port::SetNativeD3D12FinalFrameCallback(*gpu,CopyBoundary,&callback,&error),"enabling FG over curved color rejected");Require(gpu->ConfigureGraphics(before,&error),error.c_str());
    callback.alias=true;Require(port::SetNativeD3D12FinalFrameCallback(*gpu,CopyBoundary,&callback,&error),error.c_str());render();stats=port::GetNativeD3D12FrameGenerationStats(*gpu);Require(stats.lastDisabled&&stats.status.find("distinct")!=std::string::npos,"aliased output rejected with raster fallback");
    puts("PASS: native post-HUD GPU boundary, matching HUDless/final effects, real pair/flag readback, reset-safe stats, curvature guard and alias fallback");
}
