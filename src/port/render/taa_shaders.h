#pragma once
#include "depth_shaders.h"
#include "hlsl_shaders.h"
#include <string>

namespace port {
struct TaaConstants {
    float target[4]; // size, inverse size
    float history[4]; // valid history, current pixel jitter xy, maximum feedback
};
static_assert(sizeof(TaaConstants)==32);

// A separate coverage pass protects transparent/animated scene effects from
// temporal ghosts. Reusing the actual vertex projection preserves silhouettes.
inline std::string ReactiveModelShaderSource(bool distinguishSceneSprites=false) {
    std::string source=kDepthShaderHlsl;
    const auto at=source.find("return i.pos.z;");
    source.replace(at,std::string("return i.pos.z;").size(),
        std::string("float4 coverage=flags.x>0.5?tex.Sample(samp,i.uv):1; "
        "clip(max(max(coverage.r,coverage.g),max(coverage.b,coverage.a))-.001); return ")+
        (distinguishSceneSprites?".5;":"1;"));
    return source;
}
inline std::string ReactiveSpriteShaderSource() {
    std::string source=kSpriteShaderHlsl;
    const auto at=source.find("return c;");
    source.replace(at,std::string("return c;").size(),
        "clip(max(max(c.r,c.g),max(c.b,c.a))-.001); return 1;");
    return source;
}
inline const char kTaaShaderHlsl[]=R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#define TEX1_REG register(t1)
#define TEX2_REG register(t2)
#define TEX3_REG register(t3)
#define TEX4_REG register(t4)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
cbuffer Taa : CB_REG { float4 target, history; };
Texture2D tex : TEX_REG;
Texture2D oldColor : TEX1_REG;
Texture2D metadata : TEX2_REG;
Texture2D reactive : TEX3_REG;
Texture2D oldReactive : TEX4_REG;
SamplerState samp : SAMP_REG;
struct VSOut { float4 pos:SV_Position; float2 uv:TEXCOORD; };
float4 psmain(VSOut i):SV_Target {
    float2 sampleUV=clamp(i.uv+history.yz*target.zw,.5*target.zw,1-.5*target.zw);
    // Screen-space scene sprites use their authored unjittered coordinates.
    // The private TAA mask encodes models=.5 and scene sprites=1; both fully
    // reject history. Dilating this distinction preserves sprite edges too.
    int2 outputPixel=clamp(int2(i.uv*target.xy),int2(0,0),int2(target.xy)-1);
    float fixedCoverage=0;
    [unroll]for(int fy=-1;fy<=1;++fy)[unroll]for(int fx=-1;fx<=1;++fx) {
        int2 p=clamp(outputPixel+int2(fx,fy),int2(0,0),int2(target.xy)-1);
        fixedCoverage=max(fixedCoverage,reactive.Load(int3(p,0)).r);
    }
    if(fixedCoverage>.75)sampleUV=i.uv;
    int2 pixel=clamp(int2(sampleUV*target.xy),int2(0,0),int2(target.xy)-1);
    float4 current=tex.SampleLevel(samp,sampleUV,0);
    float4 motion=metadata.Load(int3(pixel,0));
    float2 previousUV=i.uv+motion.xy*target.zw;
    if(history.x<.5 || motion.w<.5 || any(previousUV<0) || any(previousUV>=1))return current;
    float3 low=1,high=0;
    float coverage=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x) {
        int2 p=clamp(pixel+int2(x,y),int2(0,0),int2(target.xy)-1);
        float3 c=tex.Load(int3(p,0)).rgb;low=min(low,c);high=max(high,c);
        coverage=max(coverage,reactive.Load(int3(p,0)).r);
    }
    int2 oldPixel=clamp(int2(previousUV*target.xy),int2(0,0),int2(target.xy)-1);
    [unroll]for(int oy=-1;oy<=1;++oy)[unroll]for(int ox=-1;ox<=1;++ox) {
        int2 p=clamp(oldPixel+int2(ox,oy),int2(0,0),int2(target.xy)-1);
        coverage=max(coverage,oldReactive.Load(int3(p,0)).r);
    }
    if(coverage>.001)return current;
    float3 old=clamp(oldColor.SampleLevel(samp,previousUV,0).rgb,low,high);
    float difference=abs(dot(old-current.rgb,float3(.299,.587,.114)));
    float feedback=history.w*(1-saturate(difference*1.5));
    feedback*=1-saturate(length(motion.xy)/64)*.5;
    return float4(lerp(current.rgb,old,feedback),current.a);
}
)";
inline const char kTaaCopyShaderHlsl[]=R"(
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
Texture2D tex:TEX_REG;SamplerState samp:SAMP_REG;
struct VSOut {float4 pos:SV_Position;float2 uv:TEXCOORD;};
float4 psmain(VSOut i):SV_Target{return tex.SampleLevel(samp,i.uv,0);}
)";
} // namespace port
