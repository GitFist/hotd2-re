#include "render/d3d11_device.h"
#include "render/gpu_budget.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Identity(float m[16]){std::fill(m,m+16,0.f);m[0]=m[5]=m[10]=m[15]=1;}
int main(int argc,char** argv) {
    port::GpuBudgetController budget;Require(budget.Configure(16,.5f,1),"configure GPU budget");Require(!budget.Configure(0),"invalid target budget accepted");
    uint64_t serial=0;auto observe=[&](double time){return budget.Observe({++serial,640,480,time,true},640,480);};
    for(int i=0;i<7;++i)Require(!observe(24),"resolution changed before GPU warmup");
    Require(!budget.Observe({++serial,800,600,50,true},640,480),"old-resolution query affected GPU budget");
    Require(!budget.Observe({++serial,640,480,std::numeric_limits<double>::quiet_NaN(),true},640,480),"invalid GPU sample changed resolution");
    for(int i=0;i<200;++i){observe(24);Require(budget.Scale()>=.5f&&budget.Scale()<=1,"resolution exceeded configured bounds");}
    Require(budget.Scale()==.5f,"sustained GPU pressure did not reach minimum resolution");
    const float low=budget.Scale();for(int i=0;i<30;++i)observe(5);Require(budget.Scale()==low,"GPU recovery ignored hysteresis");
    for(int i=0;i<600;++i)observe(5);Require(budget.Scale()==1,"sustained GPU headroom did not recover resolution");
    const auto same=port::GpuTimingSample{serial,640,480,500,true};for(int i=0;i<100;++i)Require(!budget.Observe(same,640,480),"duplicate GPU sample affected resolution");
    std::string error;port::GpuDeviceDesc description;description.width=description.height=64;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(description,&error);}else
#endif
    gpu=port::CreateD3D11Device(description,&error);Require(gpu!=nullptr,error.c_str());
    if(!gpu->GetGraphicsCapabilities().gpuTiming){Require(!gpu->SetGpuTimingEnabled(true,&error),"unsupported GPU timing accepted");port::GpuTimingSample sample;
        Require(!gpu->PollGpuTiming(&sample),"unsupported renderer fabricated GPU timestamps");Require(gpu->SetGpuTimingEnabled(false),"disable unsupported GPU timing");
        std::printf("PASS %s: GPU budget hysteresis/bounds/stale rejection; GPU timestamps honestly unavailable\n",gpu->Name());return 0;}
    std::array<game::ModelVertex,6> vertices{};const float xy[6][2]={{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
    for(int i=0;i<6;++i){vertices[i].pos[0]=xy[i][0];vertices[i].pos[1]=xy[i][1];vertices[i].pos[2]=.5f;}gpu->SetStaticVertices(vertices.data(),vertices.size());
    port::GpuDrawList list;list.width=list.height=64;list.model.emplace_back();auto& constants=list.model[0];Identity(constants.world);Identity(constants.viewProj);
    constants.flags[1]=1;constants.flags[2]=-1;constants.diffuse[0]=.3f;constants.diffuse[1]=.6f;constants.diffuse[2]=.8f;constants.diffuse[3]=1;
    port::GpuDraw draw;draw.count=6;list.draws.push_back(draw);
    auto render=[&](){const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin timestamp frame");gpu->Submit(list);std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels),"read timestamp frame");gpu->EndFrame();return pixels;};
    const auto native=render();Require(gpu->SetGpuTimingEnabled(true,&error),error.c_str());uint64_t delivered=0;int count=0;
    for(int frame=0;frame<32;++frame){if(frame==8)std::this_thread::sleep_for(std::chrono::milliseconds(150));Require(render()==native,"GPU timing changed native color");
        port::GpuTimingSample sample;if(gpu->PollGpuTiming(&sample)){Require(sample.valid&&sample.serial>delivered&&sample.width==64&&sample.height==64&&std::isfinite(sample.milliseconds)&&sample.milliseconds>0,"GPU timestamp sample metadata is invalid");
            Require(sample.milliseconds<100,"CPU pause was counted as GPU rendering time");delivered=sample.serial;++count;}
        if(gpu->PollGpuTiming(&sample)){Require(sample.serial>delivered,"GPU timestamp was delivered twice");delivered=sample.serial;++count;}}
    Require(count>4,"nonblocking GPU timestamp ring produced no usable samples");Require(gpu->SetGpuTimingEnabled(false,&error),error.c_str());
    port::GpuTimingSample sample;Require(!gpu->PollGpuTiming(&sample),"disabled GPU timing returned a fresh sample");Require(render()==native,"disabling GPU timing changed image");
    std::printf("PASS %s: %d actual GPU timestamp samples, CPU-pause exclusion, native identity; GPU budget warmup/hysteresis/bounds/stale rejection\n",gpu->Name(),count);
}
