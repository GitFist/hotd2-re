#pragma once
#include "graphics_options.h"
#include "shadow_map.h"
#include "temporal_camera.h"
#include <algorithm>
#include <cmath>

namespace port {
// Render-only scene metadata. Eligibility is explicit: depth of field must stop
// as soon as the player can aim, even if a camera animation is still running.
struct SceneEffectsFrame {
    float fogColor[3] = {.12f,.14f,.16f};
    float lightColor[3] = {};
    float lightDirection[3] = {};
    bool cinematic = false;
};
struct SceneEffectsConstants {
    float inverseProjection[16] = {}, projection[16] = {}, inverseView[16] = {}, worldToLightClip[16] = {};
    float target[4] = {}, fog[4] = {}, lightColor[4] = {}, lightDirection[4] = {};
    float settings[4] = {}, shadow[4] = {}, jitter[4] = {};
};
static_assert(sizeof(SceneEffectsConstants)==368,"scene effect uniform ABI");
inline bool SceneEffectsEnabled(const GraphicsOptions& g) {
    return g.volumetricFog>0 || g.reflections>0 || g.cinematicFocus>0;
}
inline bool InvertSceneMatrix(const float* source,float* inverse) {
    double a[4][8]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){if(!std::isfinite(source[r*4+c]))return false;a[r][c]=source[r*4+c];a[r][c+4]=r==c?1:0;}
    for(int c=0;c<4;++c){int pivot=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(std::abs(a[pivot][c])<1e-14)return false;
        if(pivot!=c)for(int k=0;k<8;++k)std::swap(a[pivot][k],a[c][k]);
        const double scale=a[c][c];for(int k=0;k<8;++k)a[c][k]/=scale;
        for(int r=0;r<4;++r)if(r!=c){const double factor=a[r][c];for(int k=0;k<8;++k)a[r][k]-=factor*a[c][k];}}
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){inverse[r*4+c]=static_cast<float>(a[r][c+4]);if(!std::isfinite(inverse[r*4+c]))return false;}
    return true;
}
inline bool MakeSceneEffectsConstants(int width,int height,const TemporalCameraInfo& camera,const SceneEffectsFrame& frame,
                                      const ShadowConstants& map,const GraphicsOptions& options,const float* jitter,
                                      SceneEffectsConstants* out) {
    if(!out||width<=0||height<=0||!camera.valid||!SceneEffectsEnabled(options)||std::abs(camera.projection[11])<1e-8f)return false;
    for(int k=0;k<3;++k)if(!std::isfinite(frame.fogColor[k])||!std::isfinite(frame.lightColor[k])||!std::isfinite(frame.lightDirection[k]))return false;
    if(jitter)for(int k=0;k<2;++k)if(!std::isfinite(jitter[k])||std::abs(jitter[k])>1)return false;
    SceneEffectsConstants c{};
    if(!InvertSceneMatrix(camera.projection,c.inverseProjection)||!InvertSceneMatrix(camera.view,c.inverseView))return false;
    std::copy(camera.projection,camera.projection+16,c.projection);
    std::copy(map.worldToLightClip,map.worldToLightClip+16,c.worldToLightClip);
    std::copy(map.parameters,map.parameters+4,c.shadow);
    c.target[0]=static_cast<float>(width);c.target[1]=static_cast<float>(height);c.target[2]=1.f/width;c.target[3]=1.f/height;
    for(int k=0;k<3;++k){c.fog[k]=std::clamp(frame.fogColor[k],0.f,1.f);c.lightColor[k]=std::clamp(frame.lightColor[k],0.f,2.f);c.lightDirection[k]=frame.lightDirection[k];}
    c.fog[3]=options.volumetricFog*(.0005f); // homogeneous extinction / world unit
    c.settings[0]=static_cast<float>(options.volumetricFog);c.settings[1]=static_cast<float>(options.reflections);
    c.settings[2]=frame.cinematic?static_cast<float>(options.cinematicFocus):0.f;c.settings[3]=frame.cinematic?1.f:0.f;
    if(jitter){c.jitter[0]=jitter[0];c.jitter[1]=jitter[1];}
    *out=c;return true;
}

inline const char kSceneEffectsShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#define TEX1_REG register(t1)
#define TEX2_REG register(t2)
#define TEX3_REG register(t3)
#define SAMP_REG register(s0)
#endif
cbuffer SceneEffects : CB_REG {
    row_major float4x4 inverseProjection,projection,inverseView,worldToLightClip;
    float4 target,fog,lightColor,lightDirection,settings,shadow,jitter;
};
Texture2D colorTexture:TEX_REG;
Texture2D<float> depthTexture:TEX1_REG;
Texture2D surfaceTexture:TEX2_REG;
Texture2D<float> shadowTexture:TEX3_REG;
SamplerState colorSampler:SAMP_REG;
struct VSOut {float4 pos:SV_Position;float2 uv:TEXCOORD;};
VSOut vsmain(uint id:SV_VertexID){VSOut o;float2 uv=float2((id<<1)&2,id&2);o.pos=float4(uv.x*2-1,1-uv.y*2,0,1);o.uv=uv;return o;}
int2 DepthPixel(float2 uv){return clamp(int2(floor(uv*target.xy+jitter.xy)),int2(0,0),int2(target.xy)-1);}
bool ViewPosition(float2 uv,out float3 p){
    p=0;int2 pixel=DepthPixel(uv);float d=depthTexture.Load(int3(pixel,0));if(d<=0)return false;
    float2 depthUv=(float2(pixel)+.5-jitter.xy)*target.zw;
    float4 h=mul(float4(depthUv.x*2-1,1-depthUv.y*2,d,1),inverseProjection);
    if(abs(h.w)<1e-12)return false;p=h.xyz/h.w;return true;
}
float Visibility(float3 worldPosition){
    if(shadow.w<.5)return 1;
    float4 clip=mul(float4(worldPosition,1),worldToLightClip);float3 ndc=clip.xyz/clip.w;
    float2 uv=float2(ndc.x*.5+.5,.5-ndc.y*.5);
    if(any(uv<=0)||any(uv>=1)||ndc.z<=0||ndc.z>=1)return 1;
    uint w=1,h=1;shadowTexture.GetDimensions(w,h);int2 pixel=clamp(int2(uv*float2(w,h)),int2(0,0),int2(w,h)-1);
    return ndc.z<=shadowTexture.Load(int3(pixel,0))+shadow.z*.15?1:0;
}
float4 fogps(VSOut i):SV_Target {
    float4 color=colorTexture.Load(int3(int2(i.pos.xy),0));float3 end;
    if(settings.x<.5||!ViewPosition(i.uv,end))return color;
    float distance=min(length(end),384);if(distance<1e-5)return color;float3 direction=normalize(end);
    float3 worldDirection=normalize(mul(float4(direction,0),inverseView).xyz);
    float3 L=lightDirection.xyz;float lengthL=length(L);L=lengthL>1e-6?L/lengthL:float3(0,1,0);
    float phase=.25+.75*pow(saturate(dot(worldDirection,L)),4);
    int steps=12+int(settings.x)*8;float stepLength=distance/steps;
    float stepTransmission=exp(-fog.a*stepLength),transmission=1;float3 scatter=0;
    [loop]for(int k=0;k<36;++k){if(k>=steps)break;
        float3 viewPoint=direction*((k+.5)*stepLength);
        float3 worldPoint=mul(float4(viewPoint,1),inverseView).xyz;
        float visible=Visibility(worldPoint);
        float3 illumination=fog.rgb*.45+lightColor.rgb*(phase*visible);
        scatter+=transmission*(1-stepTransmission)*illumination;transmission*=stepTransmission;
    }
    color.rgb=color.rgb*transmission+scatter;return color;
}
float4 reflectionps(VSOut i):SV_Target {
    float4 color=colorTexture.Load(int3(int2(i.pos.xy),0));
    float4 surface=surfaceTexture.Load(int3(DepthPixel(i.uv),0));float3 p;
    if(settings.y<.5||surface.w<=0||!ViewPosition(i.uv,p)||dot(surface.xyz,surface.xyz)<.5)return color;
    float3 n=normalize(surface.xyz),ray=normalize(reflect(normalize(p),n));
    float3 origin=p+n*.35;float stepLength=2;float previousDelta=-1e20;float2 hitUv=0;bool hit=false;
    int steps=24+int(settings.y)*16;
    [loop]for(int k=0;k<72;++k){if(k>=steps)break;float3 rayPoint=origin+ray*((k+1)*stepLength);
        float4 clip=mul(float4(rayPoint,1),projection);if(clip.w<=0)break;
        float2 uv=float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5);if(any(uv<0)||any(uv>1))break;
        float3 scene;if(!ViewPosition(uv,scene)){previousDelta=-1e20;continue;}
        float delta=abs(rayPoint.z)-abs(scene.z),thickness=1.5+abs(rayPoint.z)*.015;
        if(k>=2&&delta>=0&&delta<thickness&&previousDelta<=thickness){hit=true;hitUv=uv;break;}previousDelta=delta;
    }
    if(!hit)return color;
    float edge=saturate(min(min(hitUv.x,hitUv.y),min(1-hitUv.x,1-hitUv.y))*12);
    float fresnel=.2+.8*pow(1-saturate(dot(-normalize(p),n)),5);
    float strength=saturate(surface.w)*edge*fresnel;
    float3 reflected=colorTexture.SampleLevel(colorSampler,hitUv,0).rgb;
    color.rgb=lerp(color.rgb,reflected,strength);return color;
}
static const float2 dofRing[12]={float2(1,0),float2(.866025,.5),float2(.5,.866025),float2(0,1),float2(-.5,.866025),float2(-.866025,.5),float2(-1,0),float2(-.866025,-.5),float2(-.5,-.866025),float2(0,-1),float2(.5,-.866025),float2(.866025,-.5)};
float4 dofps(VSOut i):SV_Target {
    float4 color=colorTexture.Load(int3(int2(i.pos.xy),0));float3 center,p;
    if(settings.z<.5||settings.w<.5||!ViewPosition(float2(.5,.5),center)||!ViewPosition(i.uv,p))return color;
    float focus=max(abs(center.z),1),range=max(12,focus*.25),distance=abs(p.z);
    float coc=saturate((distance-focus-range)/range);if(coc<=.001)return color;
    float radius=coc*(1+settings.z*2),weight=1;float3 total=color.rgb;
    [unroll]for(int k=0;k<12;++k){float2 uv=i.uv+dofRing[k]*radius*target.zw;float3 samplePosition;
        if(any(uv<0)||any(uv>1)||!ViewPosition(uv,samplePosition))continue;
        // Foreground silhouettes never bleed into the focused subject.
        if(abs(samplePosition.z)<distance-max(2,distance*.03))continue;
        total+=colorTexture.SampleLevel(colorSampler,uv,0).rgb;weight+=1;
    }
    color.rgb=total/weight;return color;
}
)";

// Material-gated screen-space reflection receivers. Actual specular metadata
// controls eligibility; an optional authored normal/roughness map refines it.
inline const char kReflectionSurfaceShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#define TEX1_REG register(t1)
#define SAMP_REG register(s0)
#endif
cbuffer Model : CB_REG {
    row_major float4x4 viewProj,world,modelView,projection;
    float4 lightDir,diffuse,flags,litDir,litDiffuse,litAmbient,fogParams,fogColor,localCount;
    float4 matSpecular,litSpecular,eyePos;float4 local[40];
    float4 lightingOptions,sampling,depthEffects,depthProjection,materialParams;
};
Texture2D baseTexture:TEX_REG;Texture2D normalRoughness:TEX1_REG;SamplerState materialSampler:SAMP_REG;
#ifndef IN0
#define IN0 POSITION
#define IN1 NORMAL
#define IN2 TEXCOORD
#endif
struct VSIn{float3 p:IN0;float3 n:IN1;float2 uv:IN2;};
struct VSOut{float4 pos:SV_Position;float3 eye:TEXCOORD0;float2 uv:TEXCOORD1;};
VSOut vsmain(VSIn i){VSOut o;precise float4 eye=mul(float4(i.p,1),modelView);o.eye=eye.xyz;o.pos=mul(eye,projection);o.uv=i.uv;return o;}
float4 psmain(VSOut i):SV_Target {
    if(flags.z>=0){float a=flags.x>.5?baseTexture.Sample(materialSampler,i.uv).a:1;clip(a*diffuse.a-flags.z);}
    float3 dpdx=ddx(i.eye),dpdy=ddy(i.eye);float3 n=normalize(cross(dpdx,dpdy));if(dot(n,-i.eye)<0)n=-n;
    float strength=matSpecular.w>0?saturate(max(matSpecular.r,max(matSpecular.g,matSpecular.b)))*.65:0;
    if(materialParams.x>.5){float4 map=normalRoughness.Sample(materialSampler,i.uv);float2 du=ddx(i.uv),dv=ddy(i.uv);float determinant=du.x*dv.y-du.y*dv.x;
        if(abs(determinant)>1e-8){float3 t=(dpdx*dv.y-dpdy*du.y)/determinant;t=normalize(t-n*dot(n,t));float3 b=normalize(cross(n,t))*sign(determinant);float3 authored=map.xyz*2-1;
            n=normalize(t*authored.x+b*authored.y+n*authored.z);}
        strength*=pow(1-map.a,2);
    }
    return float4(n,strength);
}
)";
} // namespace port
