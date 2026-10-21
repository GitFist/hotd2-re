// Optional real vendor GPU test. Unsupported adapter/driver is an explicit SKIP,
// never evidence that interpolation ran. Uses only generated opaque geometry.
#include "game/camera.h"
#include "render/d3d12_device.h"
#include "render/dlss_frame_generation.h"
#include "render/dlss_super_resolution.h"
#include "render/model_renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);}
struct Readback {ComPtr<ID3D12Resource> resource;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;int width=0,height=0;};
static bool CopyTexture(const port::NativeD3D12FrameInterop& f,ID3D12Resource* color,D3D12_RESOURCE_STATES state,Readback* out){
    const auto desc=color->GetDesc();out->width=int(desc.Width);out->height=desc.Height;
    f.device->GetCopyableFootprints(&desc,0,1,0,&out->footprint,nullptr,nullptr,&out->size);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=out->size;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(f.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out->resource))))return false;
    Barrier(f.commands,color,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=color;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION target{};target.pResource=out->resource.Get();target.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;target.PlacedFootprint=out->footprint;
    f.commands->CopyTextureRegion(&target,0,0,0,&source,nullptr);Barrier(f.commands,color,D3D12_RESOURCE_STATE_COPY_SOURCE,state);return true;
}
static std::vector<uint8_t> Pixels(const Readback& r){
    std::vector<uint8_t> pixels(size_t(r.width)*r.height*4);void* data=nullptr;D3D12_RANGE range{0,SIZE_T(r.size)};
    Require(SUCCEEDED(r.resource->Map(0,&range,&data)),"map real FG output");
    for(int y=0;y<r.height;++y)std::memcpy(pixels.data()+size_t(y)*r.width*4,static_cast<const uint8_t*>(data)+r.footprint.Offset+size_t(y)*r.footprint.Footprint.RowPitch,size_t(r.width)*4);
    D3D12_RANGE noWrite{};r.resource->Unmap(0,&noWrite);return pixels;
}
struct Run {
    port::DlssFrameGeneration fg;
    Readback generated,retained,input;
    ComPtr<ID3D12Resource> disableReadback;
    std::string failure;
    int evaluations=0;bool forceCut=false;bool lastReset=true;
    static bool Callback(const port::NativeD3D12FrameInterop& frame,port::NativeD3D12FrameOutput*,void* user,std::string* error){
        auto& run=*static_cast<Run*>(user);if(!error)error=&run.failure;port::DlssFrameGenerationInput input;input.raster=frame;input.camera=frame.camera;input.finalColor=frame.color;input.hudless=frame.color;
        if(run.forceCut)input.raster.resetHistory=true;
        port::DlssFrameGenerationOutput output;
        if(!run.fg.Evaluate(input,&output,error))return false;
        if(!output.generated||!output.retained||output.generated==output.retained||output.generated==frame.color||output.retained==frame.color){*error="FG did not return two distinct real output textures";return false;}
        run.lastReset=output.reset;
        const auto flag=output.disableInterpolation?output.disableInterpolation->GetDesc():D3D12_RESOURCE_DESC{};
        if(flag.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||flag.Width<4){*error="FG did not return a real interpolation status buffer";return false;}
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        auto readDesc=flag;readDesc.Flags=D3D12_RESOURCE_FLAG_NONE;
        run.disableReadback.Reset();
        if(FAILED(frame.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&readDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&run.disableReadback)))){*error="FG status readback allocation failed";return false;}
        Barrier(frame.commands,output.disableInterpolation,output.flagState,D3D12_RESOURCE_STATE_COPY_SOURCE);
        frame.commands->CopyBufferRegion(run.disableReadback.Get(),0,output.disableInterpolation,0,4);
        Barrier(frame.commands,output.disableInterpolation,D3D12_RESOURCE_STATE_COPY_SOURCE,output.flagState);
        if(!CopyTexture(frame,output.generated,output.colorState,&run.generated)||!CopyTexture(frame,output.retained,output.colorState,&run.retained)||!CopyTexture(frame,frame.color,frame.colorState,&run.input)){*error="FG output readback allocation failed";return false;}
        ++run.evaluations;return true;
    }
};
static game::Model Card(float z,float sx,float sy,float red,float green){
    game::Model m;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float mat[5]={1,1,red,green,.2f};std::memcpy(w+10,mat,sizeof(mat));game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-sx,-sy},{-sx,sy},{sx,-sy},{sx,sy}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
int main(int argc,char** argv){
    if(argc!=2){std::fprintf(stderr,"usage: dlss_frame_generation_test <signed-feature-directory>\n");return 2;}
    std::string error;port::GpuDeviceDesc desc;desc.width=640;desc.height=480;auto gpu=port::CreateD3D12Device(desc,&error);Require(gpu!=nullptr,error.c_str());
    Run run;port::DlssFrameGenerationInput empty;port::DlssFrameGenerationOutput noOutput;
    Require(!run.fg.Available()&&!run.fg.Evaluate(empty,&noOutput,&error)&&!noOutput.generated,"uninitialized/invalid input must not claim interpolation");
    auto* device=port::GetNativeD3D12Device(*gpu);Require(device!=nullptr,"native device pointer");
    if(!run.fg.Initialize(device,argv[1],&error)){std::printf("SKIP actual DLSS Frame Generation: %s\n",error.c_str());return 0;}
    {
        port::DlssSuperResolution companion;
        Require(companion.Initialize(device,argv[1],&error),error.c_str());
        // Destroying SR must release only its feature/map ownership, retaining
        // the shared NGX device context while the FG instance still uses it.
    }
    Require(port::SetNativeD3D12FrameCallback(*gpu,Run::Callback,&run),"install explicit GPU test callback");
    port::AssetStore assets;port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
    auto wall=Card(-100,120,90,.15f,.3f),object=Card(-90,12,20,.9f,.2f);
    auto item=[](const game::Model* m,uint32_t id){port::ModelRenderer::SceneItem i;i.model=m;i.motionTracked=true;i.motionIdentity.task=id;i.motionIdentity.model=id;i.noMeshCull=true;i.cullable=false;return i;};
    auto W=item(&wall,1),O=item(&object,2);const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    bool interpolatedDifference=false;
    for(int frame=0;frame<5;++frame){
        O.transform[12]=-12+frame*6.f;renderer.SetScene(assets,{W,O});port::TemporalFrameInfo temporal;temporal.enabled=true;temporal.frameId=frame;temporal.reset=frame==0||frame==4;renderer.SetTemporalFrame(temporal);run.forceCut=frame==4;
        const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin actual FG GPU frame");
        // This test draws no HUD; therefore scene color is exactly final color.
        // Runtime integration must use its separate post-HUD callback instead.
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> color;Require(gpu->ReadPixels(&color),"complete GPU FG work");gpu->EndFrame();
        Require(run.evaluations==frame+1,run.failure.c_str());const auto generated=Pixels(run.generated),retained=Pixels(run.retained),input=Pixels(run.input);
        Require(retained==input,"retained FG resource is not the real rendered frame");
        if(frame==0||frame==4){Require(run.lastReset,"scene cut did not reset FG");Require(generated==retained,"reset FG frame is not a real-frame copy");}
        else {
            void* flag=nullptr;D3D12_RANGE range{0,4};Require(SUCCEEDED(run.disableReadback->Map(0,&range,&flag)),"map vendor interpolation-disable status");
            const bool disabled=*static_cast<const uint8_t*>(flag)!=0;D3D12_RANGE noWrite{};run.disableReadback->Unmap(0,&noWrite);
            if(!disabled&&generated!=retained)interpolatedDifference=true;
        }
    }
    Require(interpolatedDifference,"vendor evaluation never produced an intermediate image distinct from the retained frame");
    std::puts("PASS actual NGX GPU interpolation: shared SR/FG lifetime, distinct generated/retained, retained pixel identity, enabled moving intermediate, reset copy and native states");
}
