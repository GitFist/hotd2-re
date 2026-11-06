#pragma once

#include "gpu_device.h"
#include <algorithm>
#include <cmath>

namespace port {

// The original NL standard/TABLE kernels pack source vertex RGB before the
// PVR interpolates it. This CPU equivalent is used only when BSP must create
// additional vertices; their colors are interpolated, never re-lit/re-packed.
inline bool NaomiVertexColors(const game::ModelVertex& v,const ModelConstants& c,float color[3],float specular[3]) {
    auto byte=[](float x){return std::floor(std::clamp(x,0.f,1.f)*255.f);};
    auto dot=[](const float* a,const float* b){return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];};
    std::fill(specular,specular+3,0.f);
    if(c.litDir[3]<=.5f)std::copy(c.diffuse,c.diffuse+3,color);
    else if(c.lightingOptions[1]>1.5f) {
        float light[3];
        for(int k=0;k<3;++k)light[k]=c.matSpecular[3]<0?1.f:c.litAmbient[k]*c.litDiffuse[3];
        if(c.matSpecular[3]>=0)for(int j=0;j<static_cast<int>(c.localCount[0]);++j) {
            const float* p0=c.local[j*5];const float* p1=c.local[j*5+1];
            const float* p2=c.local[j*5+2];const float* p3=c.local[j*5+3];const float* p4=c.local[j*5+4];
            float amount=0;
            if(p2[3]>2.5f)amount=std::max(0.f,dot(v.normal,p1));
            else {
                const float delta[]={p0[0]-v.pos[0],p0[1]-v.pos[1],p0[2]-v.pos[2]};
                const float distance2=dot(delta,delta),lambert=dot(v.normal,delta);
                if(lambert<=0 || distance2>=p0[3]*p0[3])continue;
                const float inverseDistance=1.f/std::sqrt(std::max(distance2,1e-20f));
                const float distance=distance2*inverseDistance;
                amount=lambert*inverseDistance;
                if(p2[3]>1.5f) {
                    const float rho=-dot(delta,p1)*inverseDistance;
                    if(rho<p4[0])continue;
                    amount*=p4[1]>0?(rho-p4[0])*p4[1]:0;
                }
                amount/=std::max(1.f,(p3[0]+p3[1]*distance)+p3[2]*distance2);
            }
            for(int k=0;k<3;++k)light[k]+=p2[k]*amount;
        }
        for(int k=0;k<3;++k)color[k]=byte((c.diffuse[k]*c.litDiffuse[k])*light[k])/255.f;
    } else if(c.lightingOptions[1]>.5f) {
        float normal[3];
        for(int k=0;k<3;++k)normal[k]=((v.normal[0]*c.world[k]+v.normal[1]*c.world[4+k])+v.normal[2]*c.world[8+k])*c.lightingOptions[3];
        const float intensity=c.matSpecular[3]<0?1.f:c.lightingOptions[2]*c.litDiffuse[3]+std::max(0.f,dot(normal,c.litDir));
        const float intensityByte=byte(intensity);
        for(int k=0;k<3;++k)color[k]=std::floor(byte(c.diffuse[k]*c.litDiffuse[k])*intensityByte/256.f)/255.f;
        if(c.matSpecular[3]>0) {
            const float h=dot(v.normal,c.lightDir),q=std::max(0.f,2.f*h*h-1.f);
            const float sheen=h>0?std::pow(q,c.matSpecular[3]):0;
            const float sheenByte=byte(sheen);
            for(int k=0;k<3;++k)specular[k]=std::floor(byte(c.matSpecular[k]*c.litSpecular[k])*sheenByte/256.f)/255.f;
        }
    } else return false; // Never silently substitute PC lighting for an unmapped native kernel.
    for(int k=0;k<3;++k)if(!std::isfinite(color[k]) || !std::isfinite(specular[k]))return false;
    return true;
}

} // namespace port
