#pragma once
#include <string>

namespace port {
namespace detail {
#include "third_party/embedded_libraries.inc"
}

inline const char kPostprocessPrefix[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef TEX1_REG
#define TEX1_REG register(t1)
#endif
#ifndef TEX2_REG
#define TEX2_REG register(t2)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
#ifndef POINT_SAMP_REG
#define POINT_SAMP_REG register(s1)
#endif
)";

inline std::string SmaaShaderSource() {
    std::string source = kPostprocessPrefix;
    source += R"(
cbuffer SmaaConstants : CB_REG { float4 metrics; };
#define SMAA_HLSL_4 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS metrics
)";
    for (const auto* part : detail::kSmaaLibraryChunks) source += part;
    source += R"(
Texture2D sourceTex : TEX_REG;
Texture2D areaOrBlendTex : TEX1_REG;
Texture2D searchTex : TEX2_REG;
struct PostVertex { float4 pos : SV_Position; float2 uv : TEXCOORD; };
PostVertex vsmain(uint id : SV_VertexID) {
    PostVertex o;
    o.uv=float2((id << 1) & 2, id & 2);
    o.pos=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);
    return o;
}
float4 edgeps(PostVertex i) : SV_Target {
    float4 offsets[3]; SMAAEdgeDetectionVS(i.uv,offsets);
    return float4(SMAALumaEdgeDetectionPS(i.uv,offsets,sourceTex),0,0);
}
float4 weightps(PostVertex i) : SV_Target {
    float2 pixel; float4 offsets[3];
    SMAABlendingWeightCalculationVS(i.uv,pixel,offsets);
    return SMAABlendingWeightCalculationPS(i.uv,pixel,offsets,sourceTex,areaOrBlendTex,searchTex,float4(0,0,0,0));
}
float4 blendps(PostVertex i) : SV_Target {
    float4 offset; SMAANeighborhoodBlendingVS(i.uv,offset);
    return SMAANeighborhoodBlendingPS(i.uv,offset,sourceTex,areaOrBlendTex);
}
)";
    return source;
}

inline std::string FsrLibrarySource() {
    std::string source = "#define A_GPU 1\n#define A_HLSL 1\n#define FSR_EASU_F 1\n#define FSR_RCAS_F 1\n";
    for (const auto* part : detail::kFsrLibraryChunks) source += part;
    return source;
}

// Real AMD EASU and RCAS, compiled from the pinned official implementation.
// EASU runs into a destination-size intermediate before final colour/CRT/HUD filtering.
inline std::string FsrShaderSource() {
    std::string source = kPostprocessPrefix;
    source += FsrLibrarySource();
    source += R"(
cbuffer FsrConstants : CB_REG { float4 sourceSize; float4 outputSize; };
Texture2D sourceTex : TEX_REG;
SamplerState sourceSampler : SAMP_REG;
AF4 FsrEasuRF(AF2 p) { return sourceTex.GatherRed(sourceSampler,p); }
AF4 FsrEasuGF(AF2 p) { return sourceTex.GatherGreen(sourceSampler,p); }
AF4 FsrEasuBF(AF2 p) { return sourceTex.GatherBlue(sourceSampler,p); }
AF4 FsrRcasLoadF(ASU2 p) { return sourceTex.Load(int3(clamp(p,int2(0,0),(int2)sourceSize.xy-1),0)); }
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b) {}
struct PostVertex { float4 pos : SV_Position; float2 uv : TEXCOORD; };
PostVertex vsmain(uint id : SV_VertexID) {
    PostVertex o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);return o;
}
float4 easups(PostVertex i) : SV_Target {
    uint4 c0,c1,c2,c3;
    FsrEasuCon(c0,c1,c2,c3,sourceSize.x,sourceSize.y,sourceSize.x,sourceSize.y,outputSize.x,outputSize.y);
    float3 color; FsrEasuF(color,(uint2)i.pos.xy,c0,c1,c2,c3);
    return float4(saturate(color),1);
}
float4 rcasps(PostVertex i) : SV_Target {
    uint4 c; FsrRcasCon(c,outputSize.z);
    float3 color; FsrRcasF(color.r,color.g,color.b,(uint2)i.pos.xy,c);
    return float4(saturate(color),1);
}
)";
    return source;
}
} // namespace port
