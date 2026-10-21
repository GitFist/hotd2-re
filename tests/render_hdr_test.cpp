// Validate actual GPU scRGB values without enabling HDR or changing a monitor.
#include "render/d3d11_device.h"
#include "render/hdr_color.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* reason){if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
static void Identity(float m[16]){std::fill(m,m+16,0.f);m[0]=m[5]=m[10]=m[15]=1;}
int main(int argc,char** argv) {
    std::string error;port::GpuDeviceDesc desc;desc.width=desc.height=64;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());
    Require(!gpu->GetGraphicsCapabilities().hdrOutput,"headless device advertised a physical HDR display");
    auto options=gpu->EffectiveGraphics(),invalid=options;invalid.hdrOutput=true;
    Require(!gpu->ConfigureGraphics(invalid,&error)&&gpu->EffectiveGraphics()==options,"headless HDR output was accepted or changed configuration");
    invalid=options;invalid.hdrPaperWhite=79;Require(!gpu->ConfigureGraphics(invalid,&error),"invalid paper white was accepted");
    std::array<game::ModelVertex,6> vertices{};
    const float xy[6][2]={{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
    for(int i=0;i<6;++i){vertices[i].pos[0]=xy[i][0];vertices[i].pos[1]=xy[i][1];vertices[i].pos[2]=.5f;vertices[i].normal[2]=1;}
    gpu->SetStaticVertices(vertices.data(),vertices.size());
    port::GpuDrawList list;list.width=list.height=64;list.model.emplace_back();
    auto& c=list.model[0];Identity(c.world);Identity(c.viewProj);c.flags[1]=1;c.flags[2]=-1;c.diffuse[3]=1;
    port::GpuDraw draw;draw.count=6;list.draws.push_back(draw);
    const float black[4]={0,0,0,1};
    for(int white:{80,200,400}) {
        options.hdrPaperWhite=white;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        for(float encoded:{0.f,.02f,.04045f,.1f,.5f,1.f}) {
            c.diffuse[0]=c.diffuse[1]=c.diffuse[2]=encoded;
            Require(gpu->BeginFrame(black),"begin SDR reference");gpu->Submit(list);
            std::vector<uint8_t> reference;Require(gpu->ReadPixels(&reference),"read native SDR reference");gpu->EndFrame();
            Require(gpu->BeginFrame(black),"begin FP16 capture");gpu->Submit(list);
            std::vector<float> linear;Require(gpu->ReadLinearPresentation(64,64,&linear),"read actual FP16 scRGB capture");gpu->EndFrame();
            Require(linear.size()==64*64*4,"FP16 capture size mismatch");
            const size_t at=(32*64+32)*4;
            const float expected=port::SrgbToLinear(reference[at]/255.f)*(white/80.f);
            for(int k=0;k<3;++k)Require(std::isfinite(linear[at+k])&&std::abs(linear[at+k]-expected)<.003f,"GPU scRGB transfer/reference white is incorrect");
            Require(linear[at+3]==1,"scRGB alpha changed");
            if(encoded==1&&white>80)Require(linear[at]>1,"HDR capture clipped to the SDR numeric range");
            Require(gpu->BeginFrame(black),"begin SDR restoration");gpu->Submit(list);
            std::vector<uint8_t> after;Require(gpu->ReadPixels(&after),"read SDR restoration");gpu->EndFrame();
            Require(after==reference,"diagnostic HDR capture changed native rendering");
        }
    }
    std::printf("PASS %s: GPU FP16 scRGB EOTF, black/midtones/white at 80/200/400 nits, values above 1, physical-output rejection and exact SDR restoration\n",gpu->Name());
}
