#include <cstdio>
#include <cstdlib>
#include "render/d3d12_device.h"
#include "render/fsr_super_resolution.h"
#include "render/fsr_depth.h"
static void DepthRanges(){
    auto require=[](bool okay,const char* why){if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}};
    port::FsrDepthRange depth;
    // Retain the existing PC direct-projection and inverse-projection formulas,
    // including their float rounding; neither finite result may select infinity.
    const double a=double(-1.f-(-1.0001f)),b=.80008f,c=-1;
    require(port::DecodeFsrDepthRange(b,c-a,b,a,&depth)&&!depth.infinite&&
        depth.nearPlane==float(std::abs(b/(c-a)))&&depth.farPlane==float(std::abs(b/a)),"finite SR depth metadata changed");
    require(port::DecodeFsrDepthRange(-1,1.25,-1,.000125,&depth)&&!depth.infinite&&
        depth.nearPlane==.8f&&depth.farPlane==8000,"finite FG depth metadata changed");
    require(port::DecodeFsrDepthRange(1e-5f,-1,1e-5f,0,&depth)&&depth.infinite&&
        depth.nearPlane==1e-5f&&depth.farPlane==(std::numeric_limits<float>::max)(),"native SR infinite endpoint rejected");
    require(port::DecodeFsrDepthRange(-1,100000,-1,0,&depth)&&depth.infinite&&
        depth.nearPlane==1e-5f,"native FG inverse infinite endpoint rejected");
    require(port::DecodeFsrDepthRange(-1,1,-1,1e-13,&depth)&&!depth.infinite&&
        std::isfinite(depth.farPlane),"distant finite depth silently treated as infinite");
    require(!port::DecodeFsrDepthRange(-1,0,-1,0,&depth)&&
        !port::DecodeFsrDepthRange(-1,1,0,0,&depth)&&
        !port::DecodeFsrDepthRange(-1,1,-1,2,&depth)&&
        !port::DecodeFsrDepthRange(std::numeric_limits<double>::quiet_NaN(),1,-1,0,&depth),"invalid depth endpoints accepted");
    std::puts("PASS finite/infinite reversed-depth metadata and invalid endpoints");
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    DepthRanges();
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
