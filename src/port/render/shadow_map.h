#pragma once
#include <algorithm>
#include <cmath>

namespace port {
struct ShadowConstants {
    float worldToLightClip[16] = {};
    float parameters[4] = {}; // strength,1/map size,1/depth span,snapshot ready
    float direction[4] = {};  // normalized world-space direction toward light
};
struct ShadowMapFrame {
    ShadowConstants constants;
    int resolution = 0;
    bool valid = false;
};
static_assert(sizeof(ShadowConstants)==96,"shadow uniform ABI");

// A stable directional near-field map, fitted in light XY to a 1024-world-unit
// receiver region ahead of the camera and in Z to ALL unculled caster bounds.
// Snapping XY to map texels avoids a continuously swimming projection. Outside
// this finite region receivers remain lit rather than sampling a clamped edge.
inline bool MakeShadowMapFrame(const float* view,const float* projection,const float* light,
                               const float bounds[6],int quality,ShadowMapFrame* out) {
    if(!view||!projection||!light||!bounds||!out||quality<1||quality>3)return false;
    *out={};
    for(int k=0;k<16;++k)if(!std::isfinite(view[k])||!std::isfinite(projection[k]))return false;
    for(int k=0;k<6;++k)if(!std::isfinite(bounds[k]))return false;
    for(int k=0;k<3;++k)if(bounds[k]>bounds[k+3])return false;
    float l[3]={light[0],light[1],light[2]};
    auto unit=[](float* a){const float length=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);if(!std::isfinite(length)||length<1e-6f)return false;for(int k=0;k<3;++k)a[k]/=length;return true;};
    if(!unit(l))return false;
    float forward[3]={-l[0],-l[1],-l[2]},up[3]={0,1,0};if(std::abs(forward[1])>.95f){up[0]=1;up[1]=0;}
    auto cross=[](const float* a,const float* b,float* c){c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];};
    auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    float right[3];cross(up,forward,right);if(!unit(right))return false;cross(forward,right,up);
    float eye[3],focus[3];const float cameraSign=projection[11]<0?-1.f:1.f;
    for(int k=0;k<3;++k){eye[k]=-(view[12]*view[k*4]+view[13]*view[k*4+1]+view[14]*view[k*4+2]);focus[k]=eye[k]+view[k*4+2]*cameraSign*256.f;}
    const int size=1024<<(quality-1);constexpr float extent=512.f;
    const float texel=extent*2/size;
    const float cx=std::floor(dot(focus,right)/texel+.5f)*texel,cy=std::floor(dot(focus,up)/texel+.5f)*texel;
    float nearZ=1e30f,farZ=-1e30f;
    for(int corner=0;corner<8;++corner){const float p[3]={bounds[(corner&1)?3:0],bounds[(corner&2)?4:1],bounds[(corner&4)?5:2]};const float z=dot(p,forward);nearZ=std::min(nearZ,z);farZ=std::max(farZ,z);}
    nearZ-=16;farZ+=16;const float span=farZ-nearZ;if(!std::isfinite(span)||span<1e-6f)return false;
    auto& c=out->constants;
    for(int row=0;row<3;++row){c.worldToLightClip[row*4]=right[row]/extent;c.worldToLightClip[row*4+1]=up[row]/extent;c.worldToLightClip[row*4+2]=forward[row]/span;c.direction[row]=l[row];}
    c.worldToLightClip[12]=-cx/extent;c.worldToLightClip[13]=-cy/extent;c.worldToLightClip[14]=-nearZ/span;c.worldToLightClip[15]=1;
    c.parameters[0]=1;c.parameters[1]=1.f/size;c.parameters[2]=1.f/span;c.parameters[3]=0; // backend owns readiness
    out->resolution=size;out->valid=true;return true;
}

inline const char kShadowMapShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef SHADOW_CB_REG
#define SHADOW_CB_REG register(b1)
#endif
cbuffer Model : CB_REG {row_major float4x4 unusedViewProjection;row_major float4x4 world;};
cbuffer Shadow : SHADOW_CB_REG {row_major float4x4 worldToLightClip;float4 parameters;float4 direction;};
#ifndef IN0
#define IN0 POSITION
#define IN1 NORMAL
#define IN2 TEXCOORD
#endif
struct VSIn {float3 position:IN0;float3 normal:IN1;float2 uv:IN2;};
struct VSOut {float4 position:SV_Position;};
VSOut vsmain(VSIn i){VSOut o;precise float4 p=mul(float4(i.position,1),world);o.position=mul(p,worldToLightClip);return o;}
float psmain(VSOut i):SV_Target{return i.position.z;}
)";
} // namespace port
