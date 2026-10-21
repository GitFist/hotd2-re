// Contact shading must leave a flat wall exact, respond to nearby occluders,
// exclude the later HUD, and restore Original across all supported MSAA counts.
#include "game/camera.h"
#include "render/ao_shaders.h"
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
static void Require(bool ok,const char* reason){if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
static game::Model Quad(float left,float right,float z) {
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffffu;
    const float material[5]={1,1,.8f,.8f,.8f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto p:{std::pair<float,float>{left,-80},{left,80},{right,-80},{right,80}}){game::ModelVertex v{};v.pos[0]=p.first;v.pos[1]=p.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
int main(int argc,char** argv) {
    auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    float reversed[16];std::memcpy(reversed,projection.m,sizeof(reversed));for(int r=0;r<4;++r)reversed[r*4+2]=reversed[r*4+3]-reversed[r*4+2];
    port::AoConstants constants;Require(port::MakeAoConstants(160,120,reversed,3,&constants),"valid reversed projection was rejected");
    Require(!port::MakeAoConstants(160,120,view.m,3,&constants),"orthographic camera accepted invalid perspective AO radius");
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;
    auto wall=Quad(-100,100,-100),block=Quad(0,100,-90);
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    auto render=[&](bool occluder,bool hud) {
        renderer.ClearCache();std::vector<port::ModelRenderer::SceneItem> items;
        for(const game::Model* model:{&wall,occluder?&block:nullptr})if(model){port::ModelRenderer::SceneItem item;item.model=model;item.cullable=false;item.noMeshCull=true;items.push_back(item);}
        renderer.SetScene(assets,items);const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin AO frame");
        renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        if(hud){port::ModelRenderer::SpriteDraw sprite;sprite.isp=4u<<29;sprite.tsp=0x80000;sprite.v[0]={260,220,0,1,0xff0000ff,0,0,0};sprite.v[1]={300,220,0,1,0xff0000ff,0,0,0};sprite.v[2]={260,260,0,1,0xff0000ff,0,0,0};sprite.v[3]={300,260,0,1,0xff0000ff,0,0,0};renderer.DrawSprites(160,120,{sprite});}
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read AO frame");gpu->EndFrame();return pixels;
    };
    for(int samples:{1,2,4}) {
        auto options=gpu->EffectiveGraphics();options.msaa=samples;options.ambientOcclusion=0;
        if(!gpu->ConfigureGraphics(options,&error)){std::printf("SKIP MSAA%d: %s\n",samples,error.c_str());continue;}
        const auto original=render(true,false),flat=render(false,false),originalHud=render(true,true);
        options.ambientOcclusion=3;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        Require(render(false,false)==flat,"AO darkened an isolated flat wall");
        const auto shaded=render(true,false);
        const size_t near=(60*160+70)*4,far=(60*160+15)*4;
        Require(shaded[near]+2<original[near],"nearby foreground geometry did not create contact occlusion");
        Require(std::abs(int(shaded[far])-int(original[far]))<=1,"distant flat surface was darkened by unrelated geometry");
        const auto withHud=render(true,true);const size_t icon=(60*160+70)*4;
        for(int k=0;k<4;++k)Require(withHud[icon+k]==originalHud[icon+k],"AO was composed over the native HUD");
        options.ambientOcclusion=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        Require(render(true,false)==original,"AO Off did not restore Original exactly");
        std::printf("PASS %s MSAA%d: contact shading, flat/far exclusions, HUD preservation and exact Off\n",gpu->Name(),samples);
    }
}
