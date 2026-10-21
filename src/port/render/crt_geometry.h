#pragma once
#include <algorithm>
#include <cmath>
namespace port {
// Same barrel transform as the optional presentation shader. Pointer input must
// use output->image mapping before gameplay/menu aim tests, including light guns.
inline void CrtWarpUV(float* u,float* v,int curvature) {
    if(!u||!v||curvature<=0)return;
    const float k=std::clamp(curvature,0,3)*.035f;
    const float x=*u*2-1,y=*v*2-1,factor=1+k*(x*x+y*y);
    *u=(x*factor+1)*.5f;*v=(y*factor+1)*.5f;
}
// Useful for display overlays/calibration markers authored in image space.
inline void CrtUnwarpUV(float* u,float* v,int curvature) {
    if(!u||!v||curvature<=0)return;
    const float k=std::clamp(curvature,0,3)*.035f;
    const float x=*u*2-1,y=*v*2-1,radius=std::sqrt(x*x+y*y);
    if(radius<1e-8f)return;
    float r=radius;
    for(int iteration=0;iteration<6;++iteration)r-=(r+k*r*r*r-radius)/(1+3*k*r*r);
    const float factor=r/radius;*u=(x*factor+1)*.5f;*v=(y*factor+1)*.5f;
}
} // namespace port
