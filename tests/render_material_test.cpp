// Authored tangent-space materials must work across UV orientation and preserve
// the native path when disabled. Uses only generated 1x1 maps and test geometry.
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

static void Require(bool ok,const char* reason) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1); } }
static void Identity(float* m) { for(int i=0;i<16;++i)m[i]=i%5==0?1.f:0.f; }
int main(int argc,char** argv) {
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1 && std::strcmp(argv[1],"sdlgpu")==0) {
        Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);
    } else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());
    auto options=gpu->EffectiveGraphics();options.materialMaps=true;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    std::vector<game::ModelVertex> vertices;
    // Two adjacent quads; right UV is mirrored. Winding/culling is disabled here.
    for(int side=0;side<2;++side) for(int corner:{0,1,2,2,1,3}) {
        game::ModelVertex v{};
        v.pos[0]=float(side-1)+(corner&1);v.pos[1]=(corner&2)?1.f:-1.f;v.pos[2]=.5f;
        v.normal[2]=1;v.uv[0]=float(corner&1);v.uv[1]=(corner&2)?1.f:0.f;
        if(side)v.uv[0]=1-v.uv[0];vertices.push_back(v);
    }
    gpu->SetStaticVertices(vertices.data(),vertices.size());
    auto texture=[&](uint8_t r,uint8_t g,uint8_t b,uint8_t rough) {
        port::RgbaImage image;image.width=image.height=1;image.pixels={r,g,b,rough};
        const auto handle=gpu->CreateTexture(image);Require(handle!=0,"cannot create generated material map");return handle;
    };
    const auto flat=texture(128,128,255,128),tilt=texture(255,128,128,128);
    const auto smooth=texture(128,128,255,64),rough=texture(128,128,255,255);
    port::GpuDrawList list;list.width=160;list.height=120;list.model.resize(1);
    auto& c=list.model[0];Identity(c.world);Identity(c.viewProj);Identity(c.modelView);Identity(c.projection);
    c.flags[2]=-1;c.diffuse[0]=c.diffuse[1]=c.diffuse[2]=.2f;c.diffuse[3]=1;
    c.litDir[3]=1;c.litDir[0]=1;c.litDiffuse[0]=c.litDiffuse[1]=c.litDiffuse[2]=1;
    c.lightingOptions[0]=1;c.materialParams[0]=1;
    c.eyePos[2]=100;c.localCount[1]=2;
    port::GpuDraw draw;draw.first=0;draw.count=12;draw.state.zfunc=8;draw.normalRoughnessTexture=tilt;
    list.draws.push_back(draw);
    auto render=[&] {
        const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin material frame");gpu->Submit(list);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read material frame");gpu->EndFrame();return pixels;
    };
    auto rgb=[&](const std::vector<uint8_t>& pixels,int x,int expected,const char* reason,int tolerance=2) {
        const size_t p=(60*160+x)*4;for(int k=0;k<3;++k)Require(std::abs(int(pixels[p+k])-expected)<=tolerance,reason);
    };
    const auto mirrored=render();
    rgb(mirrored,40,51,"tangent map did not face the +U light");
    rgb(mirrored,120,0,"mirrored UV island lost its handedness");
    list.draws[0].normalRoughnessTexture=flat;c.litDir[0]=0;c.litDir[2]=1;
    rgb(render(),40,51,"flat map changed the expected unit-normal lighting");
    c.materialParams[0]=0;const auto unmapped=render();
    list.draws[0].normalRoughnessTexture=tilt;Require(render()==unmapped,"disabled material map changed the image");
    c.materialParams[0]=1;c.diffuse[0]=c.diffuse[1]=c.diffuse[2]=0;
    c.matSpecular[0]=c.matSpecular[1]=c.matSpecular[2]=.2f;c.matSpecular[3]=32;
    c.litSpecular[0]=c.litSpecular[1]=c.litSpecular[2]=1;c.eyePos[0]=96;c.eyePos[2]=28.5f;
    list.draws[0].normalRoughnessTexture=smooth;const auto narrow=render();
    list.draws[0].normalRoughnessTexture=rough;const auto broad=render();
    rgb(narrow,40,0,"smooth material highlight was not narrow");
    const int broadValue=broad[(60*160+40)*4];
    Require(broadValue>=38 && broadValue<=44,"authored roughness did not broaden the bounded highlight");
    std::printf("PASS %s: generic authored normals, mirrored UVs, flat maps, map-Off exactness and roughness highlights\n",gpu->Name());
}
