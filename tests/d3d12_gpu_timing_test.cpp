#include "render/d3d12_device.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
static void Require(bool value,const char* why){if(!value){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    port::GpuDeviceDesc desc;desc.width=256;desc.height=192;std::string error;auto gpu=port::CreateD3D12Device(desc,&error);Require(bool(gpu),error.c_str());
    Require(gpu->GetGraphicsCapabilities().gpuTiming,"native capability advertises genuine queue timestamps");port::GpuTimingSample sample;Require(!gpu->PollGpuTiming(&sample),"no fabricated startup sample");Require(gpu->SetGpuTimingEnabled(true,&error),error.c_str());
    const float clear[4]={.12f,.28f,.48f,1};uint64_t previous=0;
    for(int frame=0;frame<3;++frame){Require(gpu->BeginFrame(clear),"begin timed native frame");const auto start=std::chrono::steady_clock::now();std::this_thread::sleep_for(std::chrono::milliseconds(50));gpu->EndFrame();
        const double cpu=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();Require(gpu->PollGpuTiming(&sample)&&sample.valid,"completed native GPU sample is fresh");
        Require(sample.serial>previous&&sample.width==256&&sample.height==192&&sample.milliseconds>0&&sample.milliseconds<cpu*.8,"timestamp interval excludes CPU frame delay");previous=sample.serial;Require(!gpu->PollGpuTiming(&sample),"completed samples delivered once");printf("actual native GPU interval %.4fms; delayed CPU frame %.2fms\n",sample.milliseconds,cpu);}
    Require(gpu->ResizeTarget(128,96),"resize native timing target");Require(gpu->BeginFrame(clear),"begin resized timed frame");Require(!gpu->SetGpuTimingEnabled(false,&error),"timing state cannot change during a frame");gpu->EndFrame();Require(gpu->PollGpuTiming(&sample)&&sample.width==128&&sample.height==96&&sample.serial>previous,"timing metadata follows actual target and serial");
    Require(gpu->BeginFrame(clear),"begin diagnostic split frame");std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"actual synchronous capture splits queue interval");gpu->EndFrame();Require(!gpu->PollGpuTiming(&sample),"CPU capture gap cannot become a GPU-duration sample");
    Require(gpu->SetGpuTimingEnabled(false,&error),error.c_str());Require(gpu->BeginFrame(clear),"begin timing Off frame");gpu->EndFrame();Require(!gpu->PollGpuTiming(&sample),"timing Off produces no sample");
    puts("PASS: native hardware timestamp intervals, CPU-delay exclusion, fresh nonblocking delivery, resized metadata and synchronous-capture rejection");
}
