// Actual optional AMD SDK interpolation through the native post-HUD boundary.
// Unsupported provider is SKIP; a successful test requires changed GPU pixels.
#include "game/camera.h"
#include "render/d3d12_device.h"
#include "render/fsr_frame_generation.h"
#include "render/model_renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static double ActorCenter(const std::vector<uint8_t>& image){double xsum=0,count=0;for(int y=150;y<330;++y)for(int x=110;x<600;++x){const int p=(y*640+x)*4;
    if(image[p]>150&&image[p+1]<90&&image[p+2]<100){xsum+=x;++count;}}Require(count>500,"generated image lost the opaque actor");return xsum/count;}
struct Run {
    port::FsrFrameGeneration fg;int calls=0;bool forceCut=false;std::string failure;
    static bool Callback(const port::NativeD3D12FinalFrame& actual,port::NativeD3D12GeneratedFrames* output,void* user,std::string* error){
        auto& run=*static_cast<Run*>(user);if(!error)error=&run.failure;
        if(!actual.color||!actual.hudless||actual.color==actual.hudless){*error="AMD FG did not receive distinct actual final/HUDless images";return false;}
        auto frame=actual;if(run.forceCut)frame.raster.resetHistory=true;
        if(!run.fg.Evaluate(frame,output,error))return false;
        if(!output->generated||!output->retained||output->generated==output->retained||!output->disableInterpolation){*error="AMD FG did not produce real output pair/eligibility buffer";return false;}
        ++run.calls;return true;
    }
};
static game::Model Card(float z,float sx,float sy,float r,float g){
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,r,g,.2f};std::memcpy(w+10,material,sizeof(material));game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-sx,-sy},{-sx,sy},{sx,-sy},{sx,sy}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
int main(int argc,char** argv){
    if(argc!=2){std::fprintf(stderr,"usage: fsr_frame_generation_test <signed-AMD-DLL-directory>\n");return 2;}
    std::string error;port::GpuDeviceDesc desc;desc.width=640;desc.height=480;auto gpu=port::CreateD3D12Device(desc,&error);Require(gpu!=nullptr,error.c_str());
    Run run;port::NativeD3D12FinalFrame empty;port::NativeD3D12GeneratedFrames noOutput;
    Require(!run.fg.Available()&&!run.fg.Evaluate(empty,&noOutput,&error)&&!noOutput.generated,"uninitialized AMD adapter claimed interpolation");
    if(!run.fg.Initialize(port::GetNativeD3D12Device(*gpu),argv[1],&error)){std::printf("SKIP actual AMD Frame Generation: %s\n",error.c_str());return 0;}
    std::printf("AMD FG provider: %s\n",run.fg.VersionName().c_str());
    Require(port::SetNativeD3D12FinalFrameCallback(*gpu,Run::Callback,&run,&error),error.c_str());
    port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;
    auto wall=Card(-100,120,90,.15f,.3f),actor=Card(-90,12,20,.9f,.2f);
    auto item=[](const game::Model* model,uint32_t id){port::ModelRenderer::SceneItem i;i.model=model;i.motionTracked=true;i.motionIdentity.task=id;i.motionIdentity.model=id;i.noMeshCull=true;i.cullable=false;return i;};
    auto W=item(&wall,1),A=item(&actor,2);const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    port::ModelRenderer::SpriteDraw hud;hud.isp=4u<<29;hud.tsp=0x80000;hud.hud=true;
    hud.v[0]={24,24,0,1,0xffc03070,0,0,0};hud.v[1]={104,24,0,1,0xffc03070,0,1,0};
    hud.v[2]={24,104,0,1,0xffc03070,0,0,1};hud.v[3]={104,104,0,1,0xffc03070,0,1,1};
    const std::vector<port::ModelRenderer::SpriteDraw> sprites={hud};bool realIntermediate=false;std::vector<uint8_t> previous;
    for(int frame=0;frame<10;++frame){A.transform[12]=-18+frame*4.f;renderer.SetScene(assets,{W,A});
        port::TemporalFrameInfo temporal;temporal.enabled=true;temporal.frameId=frame;temporal.reset=frame==0||frame==8;temporal.frameTimeMs=1000.f/60;renderer.SetTemporalFrame(temporal);run.forceCut=frame==8;
        Require(port::SetNativeD3D12FrameGenerationPaused(*gpu,frame==9),"set actual pause policy");
        const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin AMD FG frame");renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
        std::vector<uint8_t> expected;Require(gpu->ReadPresentation(640,480,&expected),"read real processed final color");gpu->EndFrame();
        Require(run.calls==frame+1,run.failure.empty()?port::GetNativeD3D12FrameGenerationStats(*gpu).status.c_str():run.failure.c_str());
        std::vector<uint8_t> generated,retained;uint32_t flag=99;int width=0,height=0;
        Require(port::ReadNativeD3D12GeneratedFrames(*gpu,&generated,&retained,&width,&height,&flag,&error),error.c_str());
        Require(width==640&&height==480&&retained==expected,"AMD retained frame differs from real final/HUD color");
        if(frame==0||frame>=8){Require(flag!=0&&generated==retained,"reset/pause exposed a stale intermediate");}
        else {Require(flag==0,"continuous AMD frame incorrectly disabled by adapter");
            const double before=ActorCenter(previous),after=ActorCenter(retained),middle=ActorCenter(generated);
            Require(middle>=std::min(before,after)-4&&middle<=std::max(before,after)+4,"generated actor lies outside its real motion interval");
            if(generated!=retained&&middle>before+2&&middle<after-2)realIntermediate=true;
            const int background=(240*640+580)*4;for(int k=0;k<3;++k)Require(std::abs(int(generated[background+k])-int(retained[background+k]))<=3,"AMD generated image changed stationary background");
            // Static explicit HUD is extracted from real matching HUDless input,
            // rather than being warped with the moving opaque actor.
            for(int y=40;y<88;++y)for(int x=40;x<88;++x)for(int k=0;k<3;++k)Require(std::abs(int(generated[(y*640+x)*4+k])-int(retained[(y*640+x)*4+k]))<=3,"AMD interpolation distorted static HUD");}
        previous=retained;
    }
    Require(realIntermediate,"AMD SDK never produced GPU pixels distinct from retained input");
    std::puts("PASS actual AMD GPU interpolation: supported 3.x provider, real depth/motion/camera, generated/retained pair, retained/HUD identity, continuous intermediate and reset/pause fallback");
}
