// Verified native TABLE RGB kernel (0x0C0C4E00/5160), independent of assets.
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#include "game/anim.h"
#ifdef HOTD2_TEST_D3D12
#include "render/d3d12_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Result(bool value,const std::string& error){Require(value,error.c_str());}
static game::Model Plane(int index=0,float face=1,float ambient=1,float normal=.5f) {
    game::Model m;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=0xffffffff;w[9]=uint32_t(index);
    const float values[]={ambient,1,face,face,face};std::memcpy(w+10,values,sizeof(values));
    const float spec[]={1,1,1};std::memcpy(w+16,spec,sizeof(spec));
    game::ModelStrip strip;strip.ctrl=0x40;
    for(auto xy:{std::pair<float,float>{-1e-5f,-1e-5f},{1e-5f,-1e-5f},{-1e-5f,1e-5f},{1e-5f,1e-5f}}) {
        game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.normal[2]=normal;strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
static int Packed(double rgb){return int(std::floor(std::clamp(rgb,0.0,1.0)*255));}
int main(int argc,char** argv) {
    port::GpuDeviceDesc desc;std::string error;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_D3D12
    if(argc>1 && std::strcmp(argv[1],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#endif
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1 && std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Result(gpu!=nullptr,error);port::ModelRenderer renderer;Result(renderer.Init(gpu.get(),&error),error);
    port::AssetStore assets;auto projection=game::Mat4::Identity();projection.m[0]=projection.m[5]=100000;projection.m[10]=0;projection.m[14]=.5f;
    const auto view=game::Mat4::Identity();port::ModelRenderer::Lighting light;
    light.enabled=true;light.naomiTable=true;light.localCount=1;light.local[0].pos[2]=0;
    light.local[0].att[0]=.5f;light.local[0].range=100;light.local[0].type=1;
    auto transform=game::Mat4::Translate(0,0,-1);
    auto render=[&](game::Model& model) {
        renderer.ClearCache();port::ModelRenderer::SceneItem item;item.model=&model;item.cullable=false;item.noMeshCull=true;
        std::memcpy(item.transform,transform.m,sizeof(item.transform));renderer.SetScene(assets,{item});
        const float clear[]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin table frame");
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read table frame");gpu->EndFrame();return pixels;
    };
    auto rgb=[&](const std::vector<uint8_t>& pixels,std::array<int,3> expected,const char* why) {
        const size_t at=(240*640+320)*4;
        for(int k=0;k<3;++k)if(std::abs(int(pixels[at+k])-expected[k])>1) {
            std::fprintf(stderr,"RGB %u,%u,%u expected %d,%d,%d\n",pixels[at],pixels[at+1],pixels[at+2],expected[0],expected[1],expected[2]);Require(false,why);
        }
    };
    auto scalar=[&](game::Model& model,int expected,const char* why){rgb(render(model),{expected,expected,expected},why);};
    auto model=Plane();const auto pc=render(model);rgb(pc,{255,255,255},"PC point attenuation baseline changed");
    auto graphics=gpu->EffectiveGraphics();graphics.naomiGraphics=true;Result(gpu->ConfigureGraphics(graphics,&error),error);
    scalar(model,127,"native attenuation below1 incorrectly amplified TABLE light");
    light.naomiIntensity=.01f;scalar(model,127,"standard intensity leaked into native TABLE RGB");
    model=Plane(0,.8f,.75f);light.localCount=2;light.local[1]=light.local[0];
    for(int k=0;k<3;++k)light.naomiTableAmbient[k]=.1f;
    const float color0[]={.2f,.4f,.7f},color1[]={.7f,.1f,.3f};
    std::copy(color0,color0+3,light.local[0].diffuse);std::copy(color1,color1+3,light.local[1].diffuse);
    rgb(render(model),{107,66,117},"native TABLE colored lights were packed before their RGB sum");
    model=Plane();light.localCount=1;
    for(int k=0;k<3;++k){light.local[0].diffuse[k]=1;light.naomiTableAmbient[k]=0;}
    light.local[0].att[0]=2;scalar(model,63,"native TABLE attenuation denominator differs");
    light.local[0].att[0]=.25f;light.local[0].att[1]=.125f;light.local[0].att[2]=.25f;light.local[0].pos[2]=1;
    scalar(model,85,"native linear/quadratic distance attenuation differs");
    // Model scale is removed from the inverse used to prepare native lights.
    for(float scale:{.3f,1.f,2.f}) {
        auto scaling=game::Mat4::Identity();scaling.m[0]=scaling.m[5]=scaling.m[10]=scale;
        transform=scaling*game::Mat4::RotateY(0x2000)*game::Mat4::Translate(0,0,-1);
        const auto rotation=game::Mat4::RotateY(0x2000);light.local[0].pos[0]=2*rotation.m[8];light.local[0].pos[2]=2*rotation.m[10]-1;
        scalar(model,85,"native TABLE rotation-only inverse retained model scale");
    }
    transform=game::Mat4::Translate(0,0,-1);light.local[0].pos[0]=0;light.local[0].pos[2]=0;
    light.local[0].att[0]=.5f;light.local[0].att[1]=light.local[0].att[2]=0;
    light.local[0].range=1;scalar(model,0,"native TABLE range boundary must reject distance >= range");
    light.local[0].range=1.001f;scalar(model,127,"native TABLE range rejected an interior point");
    light.local[0].range=100;light.local[0].type=2;light.local[0].theta=light.local[0].phi=.3926991f;
    const double angle10=10*3.14159265358979323846/180;
    light.local[0].dir[0]=float(std::sin(angle10));light.local[0].dir[2]=-float(std::cos(angle10));
    const double cutoff=std::cos(.3926991*.5);
    scalar(model,Packed(.5*(std::cos(angle10)-cutoff)/(1-cutoff)),"native flashlight must have a linear angular ramp");
    for(float& component:light.local[0].dir)component*=3;
    scalar(model,Packed(.5*(std::cos(angle10)-cutoff)/(1-cutoff)),"native light direction setter did not normalize before the model inverse");
    // Actor TABLE spots stored the source half-angle in PC theta while phi
    // stayed0. Their native 0x1000 cone is wider than PC's theta/2 hard cutoff.
    light.local[0].phi=0;const double angle20=20*3.14159265358979323846/180;
    light.local[0].dir[0]=float(std::sin(angle20));light.local[0].dir[2]=-float(std::cos(angle20));
    scalar(model,Packed(.5*(std::cos(angle20)-std::cos(.3926991))/(1-std::cos(.3926991))),"native actor spotlight half-angle mapping differs");
    light.localCount=0;for(int k=0;k<3;++k)light.naomiTableAmbient[k]=.339f;
    model=Plane(0,.9f,.9f);scalar(model,70,"native TABLE ambient was quantized before material multiplication");
    light.naomiColor[0]=.5f;light.naomiColor[1]=.75f;
    rgb(render(model),{35,52,70},"native TABLE lost global light RGB modulation");
    model=Plane(-1,.8f,.2f);rgb(render(model),{102,153,204},"native TABLE -1 material was lit or used intensity-color /256");
    // Standard TABLE kernels have no material-specular contribution even if
    // word9 is positive and the ordinary standard path would be highlighted.
    model=Plane(5);for(int k=0;k<3;++k){light.naomiColor[k]=1;light.naomiTableAmbient[k]=0;}
    light.localCount=1;light.local[0].type=1;light.local[0].pos[2]=0;light.local[0].att[0]=.5f;
    scalar(model,127,"standard material specular leaked into native TABLE kernel");
    model=Plane();graphics.naomiGraphics=false;Result(gpu->ConfigureGraphics(graphics,&error),error);
    Require(render(model)==pc,"PC TABLE pixels changed after a native mode roundtrip");
    std::puts("PASS native TABLE RGB/attenuation/cones/range/scale/constant/specular and exact PC restoration");
}
