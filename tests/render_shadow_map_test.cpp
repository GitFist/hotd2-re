// Actual light-space raster shadows, including a caster outside the view.
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
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Card(float z,float sx,float sy){
    game::Model m;m.sphere[2]=z;m.sphere[3]=std::sqrt(sx*sx+sy*sy);game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float mat[5]={1,1,.7f,.7f,.7f};std::memcpy(w+10,mat,sizeof(mat));game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-sx,-sy},{-sx,sy},{sx,-sy},{sx,sy}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
int main(int argc,char** argv){
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;
    auto wall=Card(-100,100,80),object=Card(-50,8,12);
    port::ModelRenderer::SceneItem W,O;W.model=&wall;W.cullable=false;W.noMeshCull=true;O.model=&object;O.transform[12]=50;O.cullable=true;O.noMeshCull=true;
    port::ModelRenderer::Lighting light;light.enabled=true;light.dirToLight[0]=light.dirToLight[2]=std::sqrt(.5f);light.dirToLight[1]=0;
    for(int k=0;k<3;++k){light.ambient[k]=.1f;light.diffuse[k]=.8f;}
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    auto render=[&]{renderer.SetScene(assets,{W,O});const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin raster shadow frame");renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read raster shadow frame");gpu->EndFrame();return pixels;};
    auto red=[](const std::vector<uint8_t>& p,int x){return int(p[(60*160+x)*4]);};
    for(int samples:{1,2,4}){
        auto options=gpu->EffectiveGraphics();options.msaa=samples;options.perPixelLighting=true;options.shadowMaps=0;
        if(!gpu->ConfigureGraphics(options,&error)){std::printf("SKIP unsupported MSAA%d: %s\n",samples,error.c_str());continue;}
        O.transform[12]=50;O.shadowCaster=true;const auto original=render();
        options.shadowMaps=1;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto shadow=render();
        Require(red(original,80)-red(shadow,80)>30,"offscreen opaque caster did not shadow the visible receiver");
        Require(std::abs(red(original,20)-red(shadow,20))<=2,"directional shadow darkened unrelated visible wall");
        O.transform[12]=90;const auto moved=render();Require(std::abs(red(original,80)-red(moved,80))<=2,"moving caster retained stale shadow depth");
        O.transform[12]=50;O.shadowCaster=false;const auto excluded=render();Require(std::abs(red(original,80)-red(excluded,80))<=2,"explicit sky/overlay caster exclusion ignored");
        O.shadowCaster=true;options.shadowMaps=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render()==original,"turning shadow maps Off did not restore exact original image");
        std::printf("PASS %s MSAA%d: real raster shadow, unculled offscreen caster, directional-only locality, moving/excluded caster and exact Off\n",gpu->Name(),samples);
    }
}
