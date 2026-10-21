#pragma once
#include "temporal_frame.h"

namespace port {
// Default output RGBA16F = {motion.xy, previous reversed depth, history valid}.
// MOTION_MRT also writes a genuine RG16F target for temporal SDKs. The backend
// clears both targets to zero and rasterizes opaque geometry with real depth.
inline const char kMotionShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
#ifndef PREVIOUS_DEPTH_REG
#define PREVIOUS_DEPTH_REG register(t1)
#endif
cbuffer Motion : CB_REG {
    row_major float4x4 modelView, projection, previousModelView, previousProjection;
    float4 target, jitter, alpha;
};
Texture2D tex : TEX_REG;
Texture2D previousDepth : PREVIOUS_DEPTH_REG;
SamplerState samp : SAMP_REG;
#ifndef IN0
#define IN0 POSITION
#define IN1 NORMAL
#define IN2 TEXCOORD
#endif
struct VSIn { float3 current : IN0; float3 previous : IN1; float2 uv : IN2; };
struct VSOut { float4 pos : SV_Position; float4 current : TEXCOORD0; float4 previous : TEXCOORD1; float2 uv : TEXCOORD2; };
VSOut vsmain(VSIn i) {
    VSOut o;
    precise float4 eye = mul(float4(i.current,1),modelView);
    o.current = mul(eye,projection);
    precise float4 oldEye = mul(float4(i.previous,1),previousModelView);
    o.previous = mul(oldEye,previousProjection);
    o.pos = o.current; o.uv = i.uv;
    return o;
}
float4 MotionValue(VSOut i) {
    if(alpha.y>=0) {
        float ta=alpha.x>0.5?tex.Sample(samp,i.uv).a:1;
        float a=alpha.x>0.5?(alpha.w>1.5?ta:ta*alpha.z):alpha.z;
        clip(a-alpha.y);
    }
    if(target.z<0.5 || i.previous.w<=1e-6 || i.current.w<=1e-6) return 0;
    float2 now=i.current.xy/i.current.w*float2(0.5,-0.5)+0.5;
    float2 old=i.previous.xy/i.previous.w*float2(0.5,-0.5)+0.5;
    float oldZ=i.previous.z/i.previous.w;
    float2 velocity=(old-now)*target.xy - (jitter.zw-jitter.xy);
    if(any(old<0) || any(old>=1) || oldZ<=0 || oldZ>1) return float4(velocity,oldZ,0);
    // The depth snapshot is from the immediately preceding rendered frame,
    // sampled at the jittered previous location. Reject actual occlusion changes.
    float valid=0;
    if(target.w>0.5) {
        int2 p=clamp(int2(old*target.xy),int2(0,0),int2(target.xy)-1);
        float actual=previousDepth.Load(int3(p,0)).r;
        float tolerance=max(2e-6,abs(oldZ)*0.01);
        valid=actual>0 && abs(actual-oldZ)<=tolerance ? 1:0;
    }
    return float4(velocity,oldZ,valid);
}
#ifdef MOTION_MRT
struct PSOut { float4 metadata : SV_Target0; float2 motion : SV_Target1; };
PSOut psmain(VSOut i) { PSOut o; o.metadata=MotionValue(i); o.motion=o.metadata.xy; return o; }
#else
float4 psmain(VSOut i) : SV_Target { return MotionValue(i); }
#endif
)";
} // namespace port
