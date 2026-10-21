#include <cstdio>
#include <cstdlib>
#include "render/d3d12_device.h"
#include "render/dlss_super_resolution.h"
#include "render/native_d3d12_interop.h"
struct Check {
    port::DlssSuperResolution dlss;
    std::filesystem::path path;
    bool attempted=false,available=false;
    std::string error;
};
static bool Probe(const port::NativeD3D12FrameInterop& frame,port::NativeD3D12FrameOutput*,void* user,std::string*) {
    auto& check=*static_cast<Check*>(user);check.attempted=true;
    check.available=check.dlss.Initialize(frame.device,check.path,&check.error);
    if(!check.available)return true;
    for(int q=0;q<4;++q){
        int width=0,height=0;
        if(!check.dlss.Configure(1920,1080,q,&width,&height,&check.error)||width<1||width>1920||height<1||height>1080)std::exit(1);
        std::printf("DLSS mode %d official input: %dx%d -> 1920x1080\n",q,width,height);
        if(q==3&&(width!=1920||height!=1080))std::exit(1);
    }
    port::NativeD3D12FrameOutput out{};
    if(check.dlss.Evaluate(frame,&out,&check.error)||out.color||check.error.find("real scene color")==std::string::npos)std::exit(1);
    std::puts("PASS: refuses evaluation without genuine motion and HUD-free scene metadata");
    return true;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    port::GpuDeviceDesc desc;desc.offscreen=true;desc.width=128;desc.height=96;
    std::string error;auto gpu=port::CreateD3D12Device(desc,&error);
    if(!gpu){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    Check check;check.path=argv[1];
    if(!port::SetNativeD3D12FrameCallback(*gpu,Probe,&check))return 1;
    const float clear[4]={0,0,0,1};if(!gpu->BeginFrame(clear)||!gpu->ProcessScene(&error))return 1;gpu->EndFrame();
    if(!check.attempted)return 1;
    if(!check.available){std::fprintf(stderr,"DLSS unavailable: %s\n",check.error.c_str());return 3;}
    return 0;
}
