#include "render/d3d11_device.h"
#include "render/asset_store.h"
#include "render/crt_geometry.h"
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
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Identity(float m[16]){std::fill(m,m+16,0.f);m[0]=m[5]=m[10]=m[15]=1;}
int main(int argc,char** argv){std::string error;port::GpuDeviceDesc description;description.width=64;description.height=48;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(description,&error);}else
#endif
    gpu=port::CreateD3D11Device(description,&error);Require(gpu!=nullptr,error.c_str());
    std::array<game::ModelVertex,6> vertices{};const float xy[6][4]={{-1,-1,0,1},{1,-1,1,1},{-1,1,0,0},{-1,1,0,0},{1,-1,1,1},{1,1,1,0}};
    for(int i=0;i<6;++i){vertices[i].pos[0]=xy[i][0];vertices[i].pos[1]=xy[i][1];vertices[i].pos[2]=.5f;vertices[i].uv[0]=xy[i][2];vertices[i].uv[1]=xy[i][3];}gpu->SetStaticVertices(vertices.data(),vertices.size());
    port::GpuDrawList list;list.width=64;list.height=48;list.model.emplace_back();auto& c=list.model[0];Identity(c.world);Identity(c.viewProj);c.flags[0]=c.flags[1]=1;c.flags[2]=-1;std::fill(c.diffuse,c.diffuse+4,1.f);
    port::GpuDraw draw;draw.count=6;list.draws.push_back(draw);port::RgbaImage image;image.width=64;image.height=48;image.pixels.resize(64*48*4);
    for(int y=0;y<48;++y)for(int x=0;x<64;++x){const int at=(y*64+x)*4;image.pixels[at]=static_cast<uint8_t>(std::lround(x*255.f/63));image.pixels[at+1]=static_cast<uint8_t>(std::lround(y*255.f/47));image.pixels[at+2]=0;image.pixels[at+3]=255;}
    list.draws[0].texture=gpu->CreateTexture(image);Require(list.draws[0].texture!=0,"create CRT gradient");
    auto render=[&](int w=64,int h=48){const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin CRT frame");gpu->Submit(list);std::vector<uint8_t> pixels;
        Require(gpu->ReadPresentation(w,h,&pixels),"read actual CRT presentation");gpu->EndFrame();return pixels;};
    auto options=gpu->EffectiveGraphics();const auto native=render();
    for(int strength=1;strength<=3;++strength){options.crtCurvature=strength;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto curved=render();
        for(int y=0;y<48;y+=5)for(int x=0;x<64;x+=7){float u=(x+.5f)/64,v=(y+.5f)/48;port::CrtWarpUV(&u,&v,strength);const int at=(y*64+x)*4;
            if(u<0||u>1||v<0||v>1)Require(curved[at]==0&&curved[at+1]==0,"CRT corners stretched edge pixels instead of black");
            else{const float red=std::clamp(u*64-.5f,0.f,63.f)*255/63,green=std::clamp(v*48-.5f,0.f,47.f)*255/47;
                Require(std::abs(int(curved[at])-red)<2.1f&&std::abs(int(curved[at+1])-green)<2.1f,"GPU curvature does not match gameplay aim transform");}}
    }
    options.crtCurvature=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render()==native,"CRT curvature Off changed native output");
    std::fill(image.pixels.begin(),image.pixels.end(),128);for(size_t i=3;i<image.pixels.size();i+=4)image.pixels[i]=255;
    list.draws[0].texture=gpu->CreateTexture(image);const auto flat=render(128,96);std::vector<uint8_t> previous;
    for(int preset=1;preset<=3;++preset){options.crtMask=preset;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto mask=render(128,96);Require(mask!=flat&&mask!=previous,"CRT phosphor mask presets do not change output");
        bool colorSeparation=false;for(size_t i=0;i<mask.size();i+=4)colorSeparation|=mask[i]!=mask[i+1]||mask[i+1]!=mask[i+2];Require(colorSeparation,"CRT phosphor mask has no channel structure");previous=mask;}
    options.crtMask=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render(128,96)==flat,"mask Off changed source sampling");
    std::fill(image.pixels.begin(),image.pixels.end(),0);for(int y=0;y<48;++y)for(int x=0;x<64;++x){const int at=(y*64+x)*4;image.pixels[at+3]=255;if(std::abs(x-32)<=3&&std::abs(y-24)<=3)image.pixels[at]=image.pixels[at+1]=image.pixels[at+2]=255;}
    list.draws[0].texture=gpu->CreateTexture(image);const auto impulse=render();options.crtGlow=3;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto glow=render();
    Require(glow[(24*64+37)*4]>impulse[(24*64+37)*4],"phosphor glow did not spread bright source light");
    options.crtGlow=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render()==impulse,"glow Off did not restore exact source");
    std::printf("PASS %s: GPU curvature/aim agreement, black corners, independent phosphor masks/glow and exact Off\n",gpu->Name());
}
