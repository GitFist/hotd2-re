// Real depth intersection fading, generated geometry only. Off must remain exact,
// ordinary alpha geometry must be excluded, and all MSAA counts must use valid depth.
#include "game/camera.h"
#include "render/d3d11_device.h"
#include "render/model_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok,const char* reason) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);} }
static game::ModelMesh Card(float z,float r,float g,float b,bool opaque) {
    game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=opaque?0x80000u:0x94100000u;w[8]=w[9]=0xffffffffu;
    const float material[5]={1,opaque?1.f:.5f,r,g,b};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-80,-60},{-80,60},{80,-60},{80,60}}) {
        game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;
        strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);return mesh;
}
int main(int argc,char** argv) {
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1 && std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
    port::AssetStore assets;game::Model wall,smoke,ordinary;
    wall.meshes={Card(-100,0,1,0,true)};
    smoke.meshes={Card(-99,1,1,1,false),Card(-98,1,1,1,false)};
    ordinary.meshes={Card(-99,1,1,1,false)};
    port::ModelRenderer::Lighting light;light.enabled=true;
    for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    auto render=[&](const game::Model& effect,bool includeWall) {
        renderer.ClearCache();std::vector<port::ModelRenderer::SceneItem> items;
        auto add=[&](const game::Model& model){port::ModelRenderer::SceneItem item;item.model=&model;item.cullable=false;item.noMeshCull=true;items.push_back(item);};
        if(includeWall)add(wall);add(effect);renderer.SetScene(assets,items);
        const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin soft-particle frame");
        renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read soft-particle frame");gpu->EndFrame();return pixels;
    };
    auto rgb=[](const std::vector<uint8_t>& pixels,int expected,const char* reason,int tolerance=2){const size_t at=(60*160+80)*4;Require(std::abs(int(pixels[at])-expected)<=tolerance,reason);Require(pixels[at+1]>=253,"opaque wall color changed");};
    for(int samples:{1,2,4}) {
        auto options=gpu->EffectiveGraphics();options.msaa=samples;options.softParticles=0;
        if(!gpu->ConfigureGraphics(options,&error)){std::printf("SKIP unsupported MSAA%d: %s\n",samples,error.c_str());continue;}
        const auto original=render(smoke,true);rgb(original,191,"native layered smoke changed");
        const auto originalOrdinary=render(ordinary,true),originalBackground=render(smoke,false);
        options.softParticles=1;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        const auto softened=render(smoke,true);rgb(softened,23,"smoke intersection did not fade using opaque depth",3);
        Require(render(ordinary,true)==originalOrdinary,"ordinary alpha geometry was softened");
        Require(render(smoke,false)==originalBackground,"clear background incorrectly faded smoke");
        options.softParticles=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        Require(render(smoke,true)==original,"turning soft particles Off did not restore exact original image");
        std::printf("PASS %s MSAA%d: depth fade, opaque color, alpha exclusions, clear background and Off restoration\n",gpu->Name(),samples);
    }
}
