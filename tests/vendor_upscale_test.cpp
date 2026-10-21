// Genuine vendor GPU dispatch, generated geometry, and sharp post-upscale HUD.
#include "game/camera.h"
#include "render/d3d12_device.h"
#include "render/dlss_super_resolution.h"
#include "render/fsr_super_resolution.h"
#include "render/xess_super_resolution.h"
#include "render/model_renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Require(bool okay,const char* why){if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Run {
    port::DlssSuperResolution dlss;
    port::FsrSuperResolution fsr;
    port::XessSuperResolution xess;
    bool amd=false,intel=false;
    int calls=0;
    std::string error;
    static bool Callback(const port::NativeD3D12FrameInterop& frame,port::NativeD3D12FrameOutput* out,void* user,std::string* error){
        auto& r=*static_cast<Run*>(user);
        const bool okay=r.intel?r.xess.Evaluate(frame,out,&r.error):r.amd?r.fsr.Evaluate(frame,out,&r.error):r.dlss.Evaluate(frame,out,&r.error);
        if(okay&&out->color)++r.calls;
        if(error)*error=r.error;return okay;
    }
};
static game::Model Card(){
    game::Model model;game::ModelMesh mesh;auto* w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[]={1,1,.25f,.42f,.75f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-70,-50},{-70,50},{70,-50},{70,50}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=-100;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const bool fsr2=std::strcmp(argv[1],"fsr2")==0;
    Run run;run.amd=fsr2||std::strcmp(argv[1],"fsr")==0;run.intel=std::strcmp(argv[1],"xess")==0;
    port::GpuDeviceDesc desc;desc.offscreen=true;desc.width=640;desc.height=480;
    std::string error;auto gpu=port::CreateD3D12Device(desc,&error);Require(gpu!=nullptr,error.c_str());
    auto* device=port::GetNativeD3D12Device(*gpu);
    const bool initialized=run.intel?run.xess.Initialize(device,argv[2],&error):run.amd?run.fsr.Initialize(device,argv[2],fsr2?2:3,&error):run.dlss.Initialize(device,argv[2],&error);
    Require(initialized,error.c_str());
    port::AssetStore assets;port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());auto model=Card();
    port::ModelRenderer::SceneItem item;item.model=&model;item.cullable=false;item.noMeshCull=true;item.motionTracked=true;item.motionIdentity.task=1;
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::SpriteDraw hud{};hud.isp=0xE4000000;hud.tsp=0x94918000;hud.depth=.001f;
    const float xy[4][2]={{88,20},{88,60},{144,20},{144,60}};
    for(int i=0;i<4;++i)hud.v[i]={xy[i][0],xy[i][1],0,1,0xFF18D840,0,0,0};
    std::vector<port::ModelRenderer::SpriteDraw> sprites{hud};uint64_t sequence=0;
    Require(port::SetNativeD3D12FrameCallback(*gpu,Run::Callback,&run),"attach actual vendor dispatch");
    for(int quality:{0,3}){
        int iw=0,ih=0;
        const bool configured=run.intel?run.xess.Configure(960,720,quality,&iw,&ih,&error):run.amd?run.fsr.Configure(960,720,quality,&iw,&ih,&error):run.dlss.Configure(960,720,quality,&iw,&ih,&error);
        if(fsr2&&quality==3){Require(!configured,"FSR2 must reject unsupported NativeAA instead of silently substituting another algorithm");std::printf("PASS FSR2 NativeAA explicitly unavailable: %s\n",error.c_str());continue;}
        Require(configured,error.c_str());
        Require(gpu->ResizeTarget(iw,ih),"vendor input resolution target");
        for(int frame=0;frame<6;++frame){
            item.transform[12]=frame*2.f;renderer.SetScene(assets,{item});port::TemporalFrameInfo temporal;temporal.enabled=true;temporal.frameId=++sequence;
            temporal.reset=frame==0||frame==5;temporal.jitterPixels[0]=frame%2?.25f:-.25f;temporal.jitterPixels[1]=frame%3?.125f:-.375f;renderer.SetTemporalFrame(temporal);
            const float clear[]={.03f,.03f,.03f,1};Require(gpu->BeginFrame(clear),"begin temporal scene");
            const int before=run.calls;renderer.DrawGame(iw,ih,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
            Require(run.calls==before+1,run.error.empty()?"vendor dispatch skipped real scene":run.error.c_str());
            std::vector<uint8_t> color;Require(gpu->ReadPresentation(960,720,&color),"read actual vendor result");gpu->EndFrame();
            const size_t ui=(60*960+160)*4;
            Require(color[ui]==24&&color[ui+1]==216&&color[ui+2]==64,"HUD was reconstructed/blurred instead of drawn at output resolution");
            const size_t surface=(360*960+480)*4;
            Require(color[surface+2]>100&&color[surface+2]>color[surface],"vendor output lost real scene color");
        }
        std::printf("PASS %s mode%d real GPU evaluation %dx%d->960x720, moving geometry/jitter/cut and exact post-upscale HUD\n",argv[1],quality,iw,ih);
    }
}
