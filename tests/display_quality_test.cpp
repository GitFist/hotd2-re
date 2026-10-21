#include "render/output_resolution.h"
#include "render/crt_geometry.h"
#include "render/display_calibration.h"
#include "render/hdr_color.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){using port::OutputResolution;using port::ChooseOutputResolution;using port::RenderSize;
    Require(ChooseOutputResolution(OutputResolution::Original,1280,960,1440,1080)==RenderSize{1280,960},"Original resolution changed");
    Require(ChooseOutputResolution(OutputResolution::Match,1280,960,1440,1080)==RenderSize{1440,1080},"visible output matching includes bars/wrong size");
    Require(ChooseOutputResolution(OutputResolution::Ssaa150,640,480,1440,1080)==RenderSize{2160,1620},"1.5x output-relative SSAA size");
    Require(ChooseOutputResolution(OutputResolution::Ssaa200,640,480,1440,1080)==RenderSize{2880,2160},"2x output-relative SSAA size");
    Require(ChooseOutputResolution(OutputResolution::Ssaa200,640,480,1440,1080,.5f)==RenderSize{1440,1080},"GPU budget scale is not applied to SSAA dimensions");
    const auto wide=ChooseOutputResolution(OutputResolution::Ssaa200,1280,360,7680,2160);
    Require(wide.width==8192&&wide.height==2304,"resolution cap changed ultrawide aspect");
    for(int strength=0;strength<=3;++strength)for(int yi=0;yi<=20;++yi)for(int xi=0;xi<=20;++xi){const float originalU=xi/20.f,originalV=yi/20.f;float u=originalU,v=originalV;
        port::CrtWarpUV(&u,&v,strength);if(strength==0)Require(u==originalU&&v==originalV,"CRT Off changed aim coordinates");
        port::CrtUnwarpUV(&u,&v,strength);Require(std::abs(u-originalU)<.00001f&&std::abs(v-originalV)<.00001f,"CRT image/aim mapping do not invert");}
    const auto black=port::MakeDisplayCalibration(640,480,port::DisplayCalibration::BlackLevel);
    const int expected[]={0,1,2,4,8,12,16,24};for(int step=0;step<8;++step)Require(black.pixels[((240*640)+step*80+40)*4]==expected[step],"black-level calibration patch is incorrect");
    const auto gamma=port::MakeDisplayCalibration(640,480,port::DisplayCalibration::Gamma);
    Require(gamma.pixels[(240*640+500)*4]==188,"gamma reference is not sRGB 50% linear luminance");
    Require(std::abs(port::SrgbToLinear(188/255.f)-.5f)<.004f,"sRGB half-luminance reference is wrong");
    Require(port::MakeDisplayCalibration(0,480,port::DisplayCalibration::Geometry).pixels.empty(),"invalid calibration size allocated");
    std::puts("PASS output-relative match/SSAA/budget sizing, ultrawide caps, CRT image/aim inversion and calibrated black/gamma patterns");
}
