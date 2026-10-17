#pragma once

// HLSL source of the port's two pipelines, shared by the D3D11 backend (compiled as vs_5_0 / ps_5_0 without defines)
// and the SDL_GPU backend (DXBC vs_5_1 / ps_5_1 with the register spaces SDL_GPU expects for D3D12: vertex uniforms
// b0 space1, fragment texture / sampler t0 / s0 space2, fragment uniforms b0 space3; SDLGPU_UBYTE4 swizzles the
// sprite colour, which SDL_GPU reads as UBYTE4_NORM (bytes B, G, R, A of the ARGB dword); IN0..IN2 = TEXCOORD0..2, the vertex
// input semantics SDL_GPU's D3D12 backend assigns to attribute locations 0..2).
// Our own shaders (no game code).

namespace port {

inline const char kModelShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
cbuffer Frame : CB_REG {
    row_major float4x4 viewProj;  // uploaded row-major (C++ side uses row vectors)
    row_major float4x4 world;     // item world transform (model space -> world)
    row_major float4x4 modelView;
    row_major float4x4 projection;
    float4 lightDir;
    float4 diffuse;
    float4 flags;   // x: textured, y: unlit, z: alpha-test threshold (<0 = off), w: alpha source (1 tex*mat, 2 tex)
    float4 litDir;      // xyz: world dir to light, w: 1 = D3D7 lighting
    float4 litDiffuse;  // light diffuse (rgb), w: material ambient scale
    float4 litAmbient;  // light ambient + AMBIENT render state, premultiplied sum (rgb)
    float4 fogParams;   // x: FOGSTART, y: FOGEND, z: 1 = fog on for this material
    float4 fogColor;
    float4 localCount;  // x: number of local lights, y: normal transform mode, z: split projection
    float4 matSpecular; // rgb, w: power (0 = none)
    float4 litSpecular;
    float4 eyePos;
    float4 local[40];   // 8 lights x (pos range | dir falloff | diffuse type | att | cos half theta, cos half phi)
};
Texture2D tex : TEX_REG;
SamplerState samp : SAMP_REG;
#ifndef IN0
#define IN0 POSITION
#define IN1 NORMAL
#define IN2 TEXCOORD
#endif
struct VSIn { float3 pos : IN0; float3 nrm : IN1; float2 uv : IN2; };
struct VSOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD; float depth : DEPTH; float4 lit : COLOR0; float4 spec : COLOR1; };
VSOut vsmain(VSIn i) {
    VSOut o;
    float4 wp = mul(float4(i.pos, 1), world);
    if (localCount.z > 0.5) {
        // Project only after moving into camera space. A combined world-to-clip matrix loses
        // depth precision far from the origin (the Emperor rooftop's closely layered floor).
        // Keep this intermediate precise so the shader compiler cannot reassociate the products.
        precise float4 eye = mul(float4(i.pos, 1), modelView);
        o.pos = mul(eye, projection);
    } else {
        o.pos = mul(wp, viewProj);
    }
    o.nrm = mul(i.nrm, (float3x3)world);
    o.uv = i.uv;
    o.depth = o.pos.w;  // eye-space depth (W-friendly projection: D3D table fog uses w)
    o.lit = float4(1, 1, 1, 1);
    o.spec = float4(0, 0, 0, 0);
    if (litDir.w > 0.5) {
        // D3D7 fixed function, per vertex: colour = matAmbient * (AMBIENT + sum light ambient) + sum matDiffuse *
        // lightDiffuse * max(0, N.L) * attenuation * spot, clamped (then modulated with the texture, COLOROP MODULATE).
        float3 n = normalize(o.nrm);
        if (localCount.y > 1.5) {  // inverse transpose of WORLD (D3D7 without NORMALIZENORMALS)
            float3x3 m = (float3x3)world;
            float3 c0 = cross(m[1], m[2]), c1 = cross(m[2], m[0]), c2 = cross(m[0], m[1]);
            float det = dot(m[0], c0);
            n = (i.nrm.x * c0 + i.nrm.y * c1 + i.nrm.z * c2) / det;
        } else if (localCount.y > 0.5) {
            n = o.nrm;  // WORLD applied as is, not renormalised
        }
        float3 matAmbient = diffuse.rgb * litDiffuse.w;
        float ndl = dot(n, litDir.xyz);
        float3 c = litAmbient.rgb * matAmbient + litDiffuse.rgb * diffuse.rgb * max(0, ndl);
        // specular (SPECULARENABLE on, local viewer): matSpecular * lightSpecular * (N.H)^power, light 0 only (the
        // table lights leave their specular 0); added after the texture stage
        if (matSpecular.w > 0 && ndl > 0) {
            float3 h = normalize(normalize(eyePos.xyz - wp.xyz) + litDir.xyz);
            o.spec = float4(saturate(matSpecular.rgb * litSpecular.rgb * pow(max(0, dot(n, h)), matSpecular.w)), 0);
        }
        int count = (int)localCount.x;
        for (int k = 0; k < count; ++k) {
            float4 p0 = local[k * 5], p1 = local[k * 5 + 1], p2 = local[k * 5 + 2], p3 = local[k * 5 + 3], p4 = local[k * 5 + 4];
            float3 l = p0.xyz - wp.xyz;
            float d = length(l);
            if (d > p0.w) continue;
            l = l / max(d, 1e-6);
            float att = 1.0 / max(p3.x + p3.y * d + p3.z * d * d, 1e-6);
            float spot = 1;
            if (p2.w > 1.5) {
                // D3D7 order: inside the umbra (cos theta/2) full, else outside the penumbra (cos phi/2) none, else
                // the falloff (the game's spots leave phi 0: a hard cone of theta)
                float rho = dot(-l, normalize(p1.xyz));
                if (rho <= p4.x) spot = rho <= p4.y ? 0 : pow(saturate((rho - p4.y) / max(p4.x - p4.y, 1e-6)), p1.w);
            }
            c += p2.rgb * diffuse.rgb * max(0, dot(n, l)) * att * spot;
        }
        o.lit = float4(saturate(c), 1);
    }
    return o;
}
// D3DFOG_LINEAR table fog: f = (end - d) / (end - start), colour = f * c + (1 - f) * FOGCOLOR.
float3 Fog(float3 c, float d) {
    if (fogParams.z < 0.5) return c;
    float f = saturate((fogParams.y - d) / (fogParams.y - fogParams.x));
    return lerp(fogColor.rgb, c, f);
}
float4 psmain(VSOut i) : SV_Target {
    float4 t = flags.x > 0.5 ? tex.Sample(samp, i.uv) : float4(1, 1, 1, 1);
    float a = flags.x > 0.5 ? (flags.w > 1.5 ? t.a : t.a * diffuse.a) : diffuse.a;
    if (flags.z >= 0) clip(a - flags.z);
    if (litDir.w > 0.5) return float4(Fog(saturate(t.rgb * i.lit.rgb + i.spec.rgb), i.depth), a);  // Gouraud colour (vsmain)
    float3 c = diffuse.rgb * t.rgb;
    float l = flags.y > 0.5 ? 1.0 : 0.35 + 0.65 * abs(dot(normalize(i.nrm), lightDir.xyz));
    return float4(Fog(c * l, i.depth), a);
}
)";

// Pre-transformed (D3DFVF_TLVERTEX) sprites. D3D7 puts pixel centres on integer coordinates, D3D11 on .5, hence
// the half-pixel shift; the game's coordinates are in a 640x480 space scaled to the target. The shift stays half a GAME
// pixel at every target size (play.exe's internal resolution setting renders at 2x..4x): the 2D layer then sits
// scale / 2 target pixels right / down of the exact D3D7 placement, invisible in practice, and the sprite textures are
// sampled away from texel boundaries (a half-target-pixel shift would put every other target pixel on a boundary tie).
// Headless renders of any size are unchanged by the setting.
inline const char kSpriteShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
cbuffer Sprite : CB_REG {
    float4 screen;  // x, y: target scale from 640x480; z: alpha-test threshold (<0 off); w: 1 = textured
};
Texture2D tex : TEX_REG;
SamplerState samp : SAMP_REG;
#ifndef IN0
#define IN0 POSITION
#define IN1 COLOR
#define IN2 TEXCOORD
#endif
struct VSIn { float4 pos : IN0; float4 col : IN1; float2 uv : IN2; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOR; float2 uv : TEXCOORD; };
VSOut vsmain(VSIn i) {
    VSOut o;
    float w = 1.0 / i.pos.w;
    float x = (i.pos.x + 0.5) / 320.0 - 1.0;
    float y = 1.0 - (i.pos.y + 0.5) / 240.0;
    o.pos = float4(x * w, y * w, (1.0 - i.pos.z) * w, w); // same reversed depth as models
#ifdef SDLGPU_UBYTE4
    o.col = i.col.zyxw;
#else
    o.col = i.col;
#endif
    o.uv = i.uv;
    return o;
}
float4 psmain(VSOut i) : SV_Target {
    float4 c = i.col;
    if (screen.w > 0.5) c *= tex.Sample(samp, i.uv);
    if (screen.z >= 0) clip(c.a - screen.z);
    return c;
}
)";

// Present pass of the window devices (gpu_device.h PresentParams): the rendered frame (internal resolution) scaled into
// the image rectangle of the back buffer, which is the viewport of this draw (one full-viewport triangle from
// SV_VertexID, no vertex buffer). Sampling: "sharp bilinear" with a LINEAR sampler - texel coordinates snapped so that
// every source texel is sharp and only its edges are blended over one destination pixel (nearest at integer scales,
// no shimmer at fractional ones; the old DXGI / blit stretch was plain bilinear). CRT filter (dst.z > 0): scanlines
// per GAME row (480 of them whatever the internal resolution): a slight vertical blend with the neighbouring rows and a
// cosine darkening between the rows, strength 1..3 (18 / 36 / 54 % at the gap), with a small brightness lift.
inline const char kPresentShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef SAMP_REG
#define SAMP_REG register(s0)
#endif
cbuffer Present : CB_REG {
    float4 src;      // x, y: source size (texels); z, w: 1 / size
    float4 dst;      // x, y: destination rectangle size (pixels); z: crt strength (0 off); w: game rows (480)
};
Texture2D tex : TEX_REG;
SamplerState samp : SAMP_REG;
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };
VSOut vsmain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);  // (0,0) (2,0) (0,2)
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}
float3 SharpSample(float2 uv) {
    float2 texel = uv * src.xy;
    float2 scale = dst.xy / src.xy;  // destination pixels per source texel
    float2 f = frac(texel);
    float2 snapped = floor(texel) + 0.5 + clamp((f - 0.5) * scale, -0.5, 0.5);
    return tex.Sample(samp, snapped * src.zw).rgb;
}
float4 psmain(VSOut i) : SV_Target {
    float3 c = SharpSample(i.uv);
    if (dst.z > 0.5) {
        float row = i.uv.y * dst.w;               // game row coordinate (0..480)
        float rowStep = 1.0 / dst.w;              // one game row in uv
        float3 above = SharpSample(i.uv - float2(0, 0.5 * rowStep));
        float3 below = SharpSample(i.uv + float2(0, 0.5 * rowStep));
        float blend = 0.05 * dst.z;               // slight vertical blend (5 / 10 / 15 %)
        c = lerp(c, 0.5 * (above + below), blend);
        // The dark line sits in the lower part of every game row (phase -0.25: at 2 pixels per row the two pixels
        // sample the pattern at its bright and dark extremes). It fades out below 2 pixels per row, where lines
        // would only dim or alias the picture.
        float gap = 0.5 + 0.5 * cos(6.283185 * (row - 0.25));
        float amp = saturate(dst.y / dst.w - 1.0);
        c *= 1.0 - 0.18 * dst.z * gap * amp;
        c *= 1.0 + 0.06 * dst.z * amp;            // brightness lift for the darkened lines
    }
    return float4(saturate(c), 1.0);
}
)";

}  // namespace port
