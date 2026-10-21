// Genuine state/PSO creation and first-use output, with no warmup dummy draws.
#include "render/d3d11_device.h"
#include "render/model_renderer.h"
#include "game/camera.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Card(){
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,.6f,.4f,.2f};std::memcpy(w+10,material,sizeof(material));game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-30.f,-25.f},{-30.f,25.f},{30.f,-25.f},{30.f,25.f}}){game::ModelVertex vertex{};vertex.pos[0]=xy.first;vertex.pos[1]=xy.second;vertex.pos[2]=-100;vertex.normal[2]=1;strip.vertices.push_back(vertex);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
static port::ModelRenderer::SpriteDraw Sprite(){
    port::ModelRenderer::SpriteDraw sprite;sprite.isp=0xe4000000;sprite.tsp=0x94000000;
    sprite.v[0]={24,24,0,1,0xc000ff00,0,0,0};sprite.v[1]={104,24,0,1,0xc000ff00,0,1,0};
    sprite.v[2]={24,104,0,1,0xc000ff00,0,0,1};sprite.v[3]={104,104,0,1,0xc000ff00,0,1,1};return sprite;
}
int main(int argc,char** argv){
    bool sdl=false;
#ifdef HOTD2_TEST_SDL_GPU
    sdl=argc>1&&!std::strcmp(argv[1],"sdlgpu");if(sdl)Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());
#endif
    std::string error;auto make=[&]{port::GpuDeviceDesc desc;desc.width=160;desc.height=120;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
        if(sdl)gpu=port::CreateSdlGpuDevice(desc,&error);else
#endif
        gpu=port::CreateD3D11Device(desc,&error);Require(gpu!=nullptr,error.c_str());return gpu;};
    auto card=Card();port::ModelRenderer::SceneItem opaque;opaque.model=&card;opaque.cullable=false;opaque.noMeshCull=true;opaque.motionTracked=true;opaque.motionIdentity.task=1;
    auto alpha=opaque;alpha.alpha=.5f;alpha.transform[12]=25;alpha.motionIdentity.task=2;
    const std::vector<port::ModelRenderer::SpriteDraw> sprites={Sprite()};
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    auto render=[&](port::GpuDevice& gpu){port::ModelRenderer renderer;port::AssetStore assets;Require(renderer.Init(&gpu,&error),error.c_str());renderer.SetScene(assets,{opaque,alpha});
        port::TemporalFrameInfo history;history.enabled=gpu.EffectiveGraphics().temporalAA;history.reset=true;history.frameId=1;renderer.SetTemporalFrame(history);
        const float clear[4]={0,0,0,1};Require(gpu.BeginFrame(clear),"begin warmed first-use frame");renderer.DrawGame(160,120,projection.m,nullptr,nullptr,view.m,&sprites,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu.ReadPresentation(320,240,&pixels),"read warmed first-use pixels");gpu.EndFrame();return pixels;};
    for(int variant=0;variant<7;++variant){
        const int samples=variant==6?1:(variant/2==0?1:variant/2==1?2:4);auto native=make(),warmed=make();
        port::PresentParams present;present.w=320;present.h=240;native->SetPresent(present);warmed->SetPresent(present);
        port::GraphicsOptions options;options.msaa=samples;options.outputResolutionHud=variant%2!=0;options.anisotropy=16;
        if(variant==6){options.outputResolutionHud=options.temporalAA=true;options.softParticles=options.ambientOcclusion=options.shadowMaps=options.volumetricFog=options.reflections=1;}
        Require(native->ConfigureGraphics(options,&error)&&warmed->ConfigureGraphics(options,&error),error.c_str());const auto reference=render(*native);
        Require(warmed->GetGraphicsCapabilities().pipelineWarmup,"bounded state warmup capability missing");
        port::GraphicsWarmupStats first,again,used;
        Require(warmed->WarmupGraphics(&first,&error),error.c_str());Require(first.pipelinesCreated+first.stateObjectsCreated>0&&first.samplersCreated>0,"warmup did not create real GPU objects");
        Require(first.cpuMilliseconds>=0&&first.pipelinesResident<=100&&first.stateObjectsResident<=32&&first.samplersResident<=36,"warmup exceeded bounded scope or reported invalid metrics");
        Require(warmed->WarmupGraphics(&again,&error),error.c_str());Require(!again.pipelinesCreated&&!again.stateObjectsCreated&&!again.samplersCreated,"repeated warmup recreated cached objects");
        Require(render(*warmed)==reference,"state warmup changed actual first-use rendering");
        Require(warmed->WarmupGraphics(&used,&error),error.c_str());Require(used.pipelinesResident==first.pipelinesResident&&used.stateObjectsResident==first.stateObjectsResident&&used.samplersResident==first.samplersResident,"ordinary first-use draw still created an unwarmed common state");
        // A color-only setting must preserve reusable real GPU objects. Filter
        // policy changes invalidate only samplers, not unrelated raster PSOs.
        auto changed=options;changed.brightness=55;Require(warmed->ConfigureGraphics(changed,&error),error.c_str());
        port::GraphicsWarmupStats color,filter;
        Require(warmed->WarmupGraphics(&color,&error),error.c_str());Require(!color.pipelinesCreated&&!color.stateObjectsCreated&&!color.samplersCreated&&color.pipelinesResident==used.pipelinesResident&&color.stateObjectsResident==used.stateObjectsResident&&color.samplersResident==used.samplersResident,"color-only change discarded reusable warmed objects");
        Require(native->ConfigureGraphics(changed,&error),error.c_str());const auto colorReference=render(*native);
        Require(colorReference!=reference&&render(*warmed)==colorReference,"retained caches ignored or corrupted an accepted color change");
        changed.anisotropy=4;Require(warmed->ConfigureGraphics(changed,&error),error.c_str());Require(warmed->WarmupGraphics(&filter,&error),error.c_str());
        Require(!filter.pipelinesCreated&&!filter.stateObjectsCreated&&filter.samplersCreated==36&&filter.pipelinesResident==color.pipelinesResident&&filter.stateObjectsResident==color.stateObjectsResident,"anisotropy did not invalidate exactly the sampler policy");
        Require(native->ConfigureGraphics(changed,&error),error.c_str());Require(render(*warmed)==render(*native),"sampler invalidation changed warmed output relative to a normal draw");
        const float clear[4]={0,0,0,1};Require(warmed->BeginFrame(clear),"begin warmup rejection frame");Require(!warmed->WarmupGraphics(nullptr,&error),"warmup accepted mutation in an active frame");warmed->EndFrame();
        std::printf("PASS %s MSAA%d HUD%d effects%d: %u pipelines, %u states, %u samplers warmed in %.2fms; idempotent cache and exact first-use pixels\n",warmed->Name(),samples,options.outputResolutionHud,variant==6,first.pipelinesCreated,first.stateObjectsCreated,first.samplersCreated,first.cpuMilliseconds);
    }
}
