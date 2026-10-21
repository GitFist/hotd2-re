#pragma once

// Optional single-sample opaque depth prepass. Both backends use this exact model
// projection path and write reversed NDC depth to an R32_FLOAT color texture.
// It deliberately avoids unsupported hardware resolves of an MSAA depth target.
namespace port {
inline const char kDepthShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
// Prefix of ModelConstants, with identical offsets. The rest of its uniform
// buffer can still be uploaded without changes to the prepass.
cbuffer Frame : CB_REG {
    row_major float4x4 viewProj;
    row_major float4x4 world;
    row_major float4x4 modelView;
    row_major float4x4 projection;
    float4 lightDir;
    float4 diffuse;
    float4 flags;
    float4 litDir;
    float4 litDiffuse;
    float4 litAmbient;
    float4 fogParams;
    float4 fogColor;
    float4 localCount;
};
Texture2D tex : TEX_REG;
SamplerState samp : SAMP_REG;
#ifndef IN0
#define IN0 POSITION
#define IN1 NORMAL
#define IN2 TEXCOORD
#endif
struct VSIn { float3 pos : IN0; float3 nrm : IN1; float2 uv : IN2; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };
VSOut vsmain(VSIn i) {
    VSOut o;
    if (localCount.z > 0.5) {
        precise float4 eye = mul(float4(i.pos, 1), modelView);
        o.pos = mul(eye, projection);
    } else {
        float4 wp = mul(float4(i.pos, 1), world);
        o.pos = mul(wp, viewProj);
    }
    o.uv = i.uv;
    return o;
}
float psmain(VSOut i) : SV_Target {
    // Current native opaque batches do not alpha-test, but preserving the rule
    // also supports future explicit cutout materials without solid silhouettes.
    if (flags.z >= 0) {
        float ta = flags.x > 0.5 ? tex.Sample(samp, i.uv).a : 1;
        float alpha = flags.x > 0.5 ? (flags.w > 1.5 ? ta : ta * diffuse.a) : diffuse.a;
        clip(alpha - flags.z);
    }
    return i.pos.z;
}
)";
} // namespace port
