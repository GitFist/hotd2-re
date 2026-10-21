#include <cstdio>
#include <cstdlib>
#include "render/d3d12_device.h"
#include "render/fsr_super_resolution.h"
int main(int argc,char** argv){
    if(argc!=2)return 2;
    port::GpuDeviceDesc desc;desc.offscreen=true;desc.width=128;desc.height=96;
    std::string error;auto gpu=port::CreateD3D12Device(desc,&error);
    if(!gpu){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    for(int major:{2,3,4}){
        port::FsrSuperResolution fsr;
        if(!fsr.Initialize(port::GetNativeD3D12Device(*gpu),argv[1],major,&error)){
            std::printf("FSR %d unavailable: %s\n",major,error.c_str());if(major==3)return 1;continue;
        }
        std::printf("Actual AMD provider: %s\n",fsr.VersionName().c_str());
        for(int q=0;q<4;++q){
            int width=0,height=0;
            if(!fsr.Configure(1920,1080,q,&width,&height,&error)){
                if(major==2&&q==3){std::printf("FSR2 NativeAA explicitly unavailable: %s\n",error.c_str());continue;}
                std::fprintf(stderr,"%s\n",error.c_str());return 1;
            }
            std::printf("FSR %d mode %d official input: %dx%d ->1920x1080\n",major,q,width,height);
            if(width<1||width>1920||height<1||height>1080||(q==3&&(width!=1920||height!=1080)))return 1;
        }
    }
    return 0;
}
