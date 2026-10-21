#pragma once
#include "asset_store.h"
#include <algorithm>
namespace port {
enum class DisplayCalibration { BlackLevel=0,WhiteLevel=1,Gamma=2,Geometry=3 };
// Legal generated test art; caller draws native text labels/help separately.
// For gamma, compare the fine black/white checker against the solid sRGB188
// patch at actual output resolution. Filters/CRT intentionally remain visible.
inline RgbaImage MakeDisplayCalibration(int width,int height,DisplayCalibration kind) {
    RgbaImage image;if(width<16||height<16||width>8192||height>8192)return image;
    image.width=width;image.height=height;image.pixels.resize(static_cast<size_t>(width)*height*4);
    constexpr uint8_t dark[]={0,1,2,4,8,12,16,24};
    constexpr uint8_t light[]={224,232,240,244,248,252,254,255};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){uint8_t r=0,g=0,b=0;
        const int column=std::min(7,x*8/width);
        if(kind==DisplayCalibration::BlackLevel)r=g=b=dark[column];
        else if(kind==DisplayCalibration::WhiteLevel)r=g=b=light[column];
        else if(kind==DisplayCalibration::Gamma){r=g=b=x<width/2?(((x+y)&1)?255:0):188;}
        else {
            const int cell=std::max(8,height/12);const bool grid=x%cell==0||y%cell==0;
            const bool border=x==1||y==1||x==width-2||y==height-2;
            const int cx=x-width/2,cy=y-height/2;const bool circle=std::abs(cx*cx+cy*cy-(height/3)*(height/3))<height/2;
            r=g=b=border?255:grid?80:circle?190:8;
            if((std::abs(cx)<2||std::abs(cy)<2)&&std::abs(cx)<height/4&&std::abs(cy)<height/4){r=255;g=32;b=32;}
        }
        const size_t at=(static_cast<size_t>(y)*width+x)*4;image.pixels[at]=r;image.pixels[at+1]=g;image.pixels[at+2]=b;image.pixels[at+3]=255;
    }
    return image;
}
} // namespace port
