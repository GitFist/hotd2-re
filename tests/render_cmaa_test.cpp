#include "render/d3d11_device.h"
#include "render/asset_store.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Identity(float m[16]){std::fill(m,m+16,0.f);m[0]=m[5]=m[10]=m[15]=1;}
int main(int argc,char** argv){
    std::string error;port::GpuDeviceDesc desc;desc.width=desc.height=64;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);Require(gpu!=nullptr,error.c_str());Require(gpu->GetGraphicsCapabilities().cmaa2,"CMAA2 storage formats unsupported");
    port::GpuDrawList list;list.model.emplace_back();auto& constants=list.model[0];Identity(constants.world);Identity(constants.viewProj);constants.flags[1]=1;constants.flags[2]=-1;std::fill(constants.diffuse,constants.diffuse+4,1.f);
    port::GpuDraw draw;draw.count=6;list.draws.push_back(draw);
    auto render=[&](){const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin CMAA2 frame");gpu->Submit(list);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read CMAA2 compute result");gpu->EndFrame();return pixels;};
    auto options=gpu->EffectiveGraphics();uint64_t hash=1469598103934665603ull;
    for(auto size:{std::pair<int,int>{64,64},{127,95},{256,192}}){options.postAA=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(gpu->ResizeTarget(size.first,size.second),"resize CMAA2 target");list.width=size.first;list.height=size.second;
        std::array<game::ModelVertex,6> vertices{};const float xy[6][4]={{-1,-1,0,1},{1,-1,1,1},{-1,1,0,0},{-1,1,0,0},{1,-1,1,1},{1,1,1,0}};
        for(int i=0;i<6;++i){vertices[i].pos[0]=xy[i][0];vertices[i].pos[1]=xy[i][1];vertices[i].pos[2]=.5f;vertices[i].uv[0]=xy[i][2];vertices[i].uv[1]=xy[i][3];}
        gpu->SetStaticVertices(vertices.data(),vertices.size());
        port::RgbaImage image;image.width=size.first;image.height=size.second;image.pixels.resize(static_cast<size_t>(size.first)*size.second*4);
        for(size_t i=0;i<image.pixels.size();i+=4){const int pixel=static_cast<int>(i/4);const uint8_t value=((pixel%size.first+pixel/size.first)&1)?255:0;image.pixels[i]=image.pixels[i+1]=image.pixels[i+2]=value;image.pixels[i+3]=255;}
        list.draws[0].texture=gpu->CreateTexture(image);Require(list.draws[0].texture!=0,"create dense checker fixture");constants.flags[0]=1;
        const auto native=render();options.postAA=3;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        const auto first=render();Require(first.size()==image.pixels.size(),"CMAA2 output size changed");
        for(int repeat=0;repeat<4;++repeat){const auto repeated=render();Require(repeated==first,"CMAA2 retained stale counters/heads or changed dense output across frames");}
        for(uint8_t value:first){hash^=value;hash*=1099511628211ull;}
        options.postAA=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render()==native,"CMAA2 Off failed to restore dense native image");
        constants.flags[0]=0;constants.diffuse[0]=.37f;constants.diffuse[1]=.61f;constants.diffuse[2]=.83f;const auto flat=render();
        options.postAA=3;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto processed=render();
        for(int y=4;y<size.second-4;++y)for(int x=4;x<size.first-4;++x)Require(std::memcmp(&processed[(y*size.first+x)*4],&flat[(y*size.first+x)*4],4)==0,"CMAA2 altered flat colors");
        std::fill(constants.diffuse,constants.diffuse+4,1.f);
        std::printf("PASS %s CMAA2 %dx%d: dense capacity, repeated resets, flat identity and exact Off\n",gpu->Name(),size.first,size.second);
    }
    std::printf("CMAA2 dense output hash %016llx\n",static_cast<unsigned long long>(hash));
}
