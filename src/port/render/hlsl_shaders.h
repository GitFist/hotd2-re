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
#ifndef DEPTH_TEX_REG
#define DEPTH_TEX_REG register(t1)
#endif
#ifndef DEPTH_SAMP_REG
#define DEPTH_SAMP_REG register(s1)
#endif
#ifndef MATERIAL_TEX_REG
#define MATERIAL_TEX_REG register(t2)
#endif
#ifndef MATERIAL_SAMP_REG
#define MATERIAL_SAMP_REG register(s2)
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
    float4 localCount;  // x: local lights, y: normal mode, z: split projection, w: unit specular normal
    float4 matSpecular; // rgb, w: power (0 = none)
    float4 litSpecular;
    float4 eyePos;
    float4 local[40];   // 8 lights x (pos range | dir falloff | diffuse type | att | cos half theta, cos half phi)
    float4 lightingOptions; // x: optional opaque per-pixel lighting (zero retains native Gouraud)
    float4 sampling;        // x: retain authored point magnification with AF; yz: U/V address modes
    float4 depthEffects;    // fade distance, 1/width, 1/height, current opaque-depth snapshot ready
    float4 depthProjection; // reversed projection A/B/C/D, computed before rasterization
    float4 materialParams;  // x: manifest-opted tangent normal / roughness map
};
Texture2D tex : TEX_REG;
SamplerState samp : SAMP_REG;
Texture2D<float> opaqueDepth : DEPTH_TEX_REG;
SamplerState depthSampler : DEPTH_SAMP_REG;
Texture2D normalRoughness : MATERIAL_TEX_REG;
SamplerState materialSampler : MATERIAL_SAMP_REG;
#ifndef IN0
#define IN0 POSITION
#define IN1 NORMAL
#define IN2 TEXCOORD
#endif
struct VSIn { float3 pos : IN0; float3 nrm : IN1; float2 uv : IN2; };
struct VSOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD; float depth : DEPTH; float4 lit : COLOR0; float4 spec : COLOR1; float3 worldPos : TEXCOORD1; float3 surfaceNormal : TEXCOORD2; };
float3 SafeUnit(float3 v) { return v * rsqrt(max(dot(v, v), 1e-20)); }
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
    o.worldPos = wp.xyz;
    o.surfaceNormal = o.nrm;
    if (lightingOptions.x > 0.5 && litDir.w > 0.5) {
        // WORLD can include mirrored and nonuniform scales. Transform normals with its
        // inverse transpose and normalize for modern illumination. Degenerate transforms
        // use the finite WORLD normal; they must never introduce a full-screen NaN flash.
        float3x3 m = (float3x3)world;
        float3 c0 = cross(m[1], m[2]), c1 = cross(m[2], m[0]), c2 = cross(m[0], m[1]);
        float det = dot(m[0], c0);
        if (abs(det) > 1e-12) o.surfaceNormal = (i.nrm.x * c0 + i.nrm.y * c1 + i.nrm.z * c2) / det;
    } else if (litDir.w > 0.5) {
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
            // Scaled PC fish/leech bodies are drawn without NORMALIZENORMALS.
            // Their longer normals turn a narrow highlight into a white plateau.
            // Correct only opted-in bodies; retain their original diffuse lighting.
            float3 specNormal = localCount.w > 0.5 ? normalize(n) : n;
            // D3D7 clamps the normal/halfway dot before raising it to the material's power.
            // Unnormalized inverse-transpose normals can exceed unit length on scaled actors;
            // clamping only the final colour turns their highlights into solid white flashes.
            o.spec = float4(saturate(matSpecular.rgb * litSpecular.rgb * pow(saturate(dot(specNormal, h)), matSpecular.w)), 0);
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
// Preserve the game's material colors and light table, changing where illumination is
// evaluated. Only the opaque opt-in reaches this function: sprites and blended effects
// remain on their original path. Unit normals and bounded dots prevent scaled fish/body
// highlights from growing into the white plateaus fixed in the native path.
float3 MaterialNormal(VSOut i, float3 n, float3 mapNormal) {
    // Reconstruct a local UV basis from the actual deformed surface. No stored
    // tangent layout or hardcoded actor is required. Mirrored UV islands keep
    // their handedness; zero-area UVs/surfaces retain the geometric normal.
    float3 px = ddx(i.worldPos), py = ddy(i.worldPos);
    float2 ux = ddx(i.uv), uy = ddy(i.uv);
    float determinant = ux.x * uy.y - ux.y * uy.x;
    if (abs(determinant) < 1e-12) return n;
    float3 t = (px * uy.y - py * ux.y) / determinant;
    float3 b = (py * ux.x - px * uy.x) / determinant;
    t -= n * dot(n, t);
    if (dot(t,t) < 1e-12 || dot(b,b) < 1e-12) return n;
    t = SafeUnit(t);
    float3 axisB = cross(n,t);
    if (dot(axisB,b) < 0) axisB = -axisB;
    return SafeUnit(t * mapNormal.x + axisB * mapNormal.y + n * mapNormal.z);
}
#ifdef RASTER_SHADOW_MAP
#ifndef SHADOW_CB_REG
#define SHADOW_CB_REG register(b1)
#endif
#ifndef SHADOW_TEX_REG
#define SHADOW_TEX_REG register(t3)
#endif
#ifndef SHADOW_SAMP_REG
#define SHADOW_SAMP_REG register(s3)
#endif
cbuffer RasterShadow : SHADOW_CB_REG {
    row_major float4x4 rasterShadowProjection;
    float4 rasterShadowParameters;
    float4 rasterShadowDirection;
};
Texture2D<float> rasterShadowTexture : SHADOW_TEX_REG;
SamplerState rasterShadowSampler : SHADOW_SAMP_REG;
float RasterDirectionalVisibility(float3 position,float3 normal,float3 direction) {
    if(rasterShadowParameters.w<0.5 || dot(direction,rasterShadowDirection.xyz)<0.999) return 1;
    float4 clipPosition=mul(float4(position+normal*.15,1),rasterShadowProjection);
    float3 shadow=clipPosition.xyz/clipPosition.w;
    float2 uv=shadow.xy*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>1)||shadow.z<0||shadow.z>1)return 1;
    float ndl=saturate(dot(normal,direction));
    // A PCF neighbor samples another point on a sloping receiver. Scale bias
    // by map texel footprint, with a finite grazing-angle bound.
    float slope=min(4,sqrt(max(0,1-ndl*ndl))/max(ndl,.1));
    float bias=(.08+1.5*1024*rasterShadowParameters.y*slope)*rasterShadowParameters.z;
    float visibility=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x) {
        float depth=rasterShadowTexture.SampleLevel(rasterShadowSampler,uv+float2(x,y)*rasterShadowParameters.y,0);
        visibility+=shadow.z-bias<=depth?1:0;
    }
    return lerp(1,visibility/9,rasterShadowParameters.x);
}
#ifndef DIRECT_LIGHT_VISIBILITY
#define DIRECT_LIGHT_VISIBILITY(P,N,L) RasterDirectionalVisibility(P,N,L)
#endif
#endif
#ifndef DIRECT_LIGHT_VISIBILITY
#define DIRECT_LIGHT_VISIBILITY(P,N,L) 1.0
#endif
float3 PixelLighting(VSOut i, float3 textureColor) {
    float3 n = SafeUnit(i.surfaceNormal);
    float specPower = matSpecular.w;
    if (materialParams.x > 0.5) {
        float4 detail = normalRoughness.Sample(materialSampler,i.uv);
        n = MaterialNormal(i,n,detail.rgb * 2 - 1);
        // An authored roughness controls highlight width within the existing
        // game's SDR light/material model. This is not a claim of a full PBR pipeline.
        float roughness = clamp(detail.a,0.25,1);
        if (specPower > 0) specPower = clamp(2 / pow(roughness,4) - 2,1,256);
    }
    float3 l0 = SafeUnit(litDir.xyz);
    float ndl = saturate(dot(n, l0));
    float directVisibility = saturate(DIRECT_LIGHT_VISIBILITY(i.worldPos,n,l0));
    float3 c = litAmbient.rgb * diffuse.rgb * litDiffuse.w + litDiffuse.rgb * diffuse.rgb * ndl * directVisibility;
    float3 spec = 0;
    if (specPower > 0 && ndl > 0) {
        float3 h = SafeUnit(SafeUnit(eyePos.xyz - i.worldPos) + l0);
        spec = saturate(matSpecular.rgb * litSpecular.rgb * pow(saturate(dot(n, h)), specPower)) * directVisibility;
    }
    int count = min((int)localCount.x, 8);
    for (int k = 0; k < count; ++k) {
        float4 p0 = local[k * 5], p1 = local[k * 5 + 1], p2 = local[k * 5 + 2], p3 = local[k * 5 + 3], p4 = local[k * 5 + 4];
        float3 l = p0.xyz - i.worldPos;
        float d = length(l);
        if (d > p0.w) continue;
        l /= max(d, 1e-6);
        float att = 1.0 / max(p3.x + p3.y * d + p3.z * d * d, 1e-6);
        float spot = 1;
        if (p2.w > 1.5) {
            float rho = dot(-l, SafeUnit(p1.xyz));
            if (rho <= p4.x) spot = rho <= p4.y ? 0 : pow(saturate((rho - p4.y) / max(p4.x - p4.y, 1e-6)), p1.w);
        }
        c += p2.rgb * diffuse.rgb * saturate(dot(n, l)) * att * spot;
    }
    return saturate(textureColor * saturate(c) + spec);
}
// D3DFOG_LINEAR table fog: f = (end - d) / (end - start), colour = f * c + (1 - f) * FOGCOLOR.
float3 Fog(float3 c, float d) {
    if (fogParams.z < 0.5) return c;
    float f = saturate((fogParams.y - d) / (fogParams.y - fogParams.x));
    return lerp(fogColor.rgb, c, f);
}
int PointAddress(int p, int size, int mode) {
    if (mode == 3) return clamp(p, 0, size - 1);
    if (mode == 2) {
        int period = size * 2;
        int q = ((p % period) + period) % period;
        return q < size ? q : period - 1 - q;
    }
    return ((p % size) + size) % size;
}
float4 ModelSample(float2 uv) {
    // D3D11's anisotropic sampler always uses linear magnification. An authored
    // point-filtered material must retain its exact texel when magnified; AF is
    // used for minification only. The zero option takes the native sampler path.
    if (sampling.x > 0.5) {
        uint w = 1, h = 1;
        tex.GetDimensions(w, h);
        w = max(w, 1u); h = max(h, 1u);
        float2 dimensions = float2(w, h);
        float rho = max(length(ddx(uv) * dimensions), length(ddy(uv) * dimensions));
        if (rho <= 1) {
            int2 p = int2(floor(uv * dimensions));
            p.x = PointAddress(p.x, (int)w, (int)sampling.y);
            p.y = PointAddress(p.y, (int)h, (int)sampling.z);
            return tex.Load(int3(p, 0));
        }
    }
    return tex.Sample(samp, uv);
}
float SoftOpacity(VSOut i, float alpha) {
    if (depthEffects.x <= 0 || depthEffects.w < 0.5) return alpha;
    float reversedDepth = opaqueDepth.SampleLevel(depthSampler,i.pos.xy * depthEffects.yz,0);
    if (reversedDepth <= 0) return alpha; // clear/far background has no solid occluder
    // Reconstruct directly from reversed coefficients. Subtracting depth from 1
    // here would lose the distant-surface precision retained by the renderer.
    precise float denominator = reversedDepth * depthProjection.z - depthProjection.x;
    if (abs(denominator) < 1e-8) return alpha;
    float eyeZ = (depthProjection.y - reversedDepth * depthProjection.w) / denominator;
    float sceneW = eyeZ * depthProjection.z + depthProjection.w;
    float fade = saturate((sceneW - i.depth) / depthEffects.x);
    return alpha * fade;
}
float4 psmain(VSOut i) : SV_Target {
    float4 t = flags.x > 0.5 ? ModelSample(i.uv) : float4(1, 1, 1, 1);
    float a = flags.x > 0.5 ? (flags.w > 1.5 ? t.a : t.a * diffuse.a) : diffuse.a;
    a = SoftOpacity(i,a);
    if (flags.z >= 0) clip(a - flags.z);
    if (lightingOptions.x > 0.5 && litDir.w > 0.5) return float4(Fog(PixelLighting(i, t.rgb), i.depth), a);
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
    float4 effects;  // FXAA, sharpening, quality downsample, SDR bloom
    float4 color;    // brightness offset, gamma exponent, contrast, saturation
    float4 grade;    // 0 original, 1 warm, 2 cool, 3 monochrome
    float4 hdr;      // linear scRGB output, paper-white/80
    float4 crt;      // phosphor-mask preset, curvature coefficient, glow strength
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
float3 ImageSample(float2 uv) {
    if (effects.z > 0.5 && (src.x > dst.x || src.y > dst.y)) {
        // Integrate an output pixel's source footprint, rather than taking one bilinear
        // lookup. Up to 8x8 taps cover the supported render scales and small windows.
        int2 taps = (int2)clamp(ceil(src.xy / dst.xy), 1.0, 8.0);
        float3 sum = 0;
        for (int y = 0; y < taps.y; ++y) for (int x = 0; x < taps.x; ++x) {
            float2 offset = (float2(x + 0.5, y + 0.5) / taps - 0.5) / dst.xy;
            sum += tex.SampleLevel(samp, uv + offset, 0).rgb;
        }
        return sum / (taps.x * taps.y);
    }
    return SharpSample(uv);
}
float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float3 FxaaSample(float2 uv) {
    float3 center = ImageSample(uv);
    if (effects.x < 0.5) return center;
    // FXAA's directional contrast filter. Work in final-image pixels so downsampling
    // and a high internal resolution do not shrink the edge search to subpixels.
    float2 pixel = 1.0 / dst.xy;
    float3 nw = ImageSample(uv + float2(-1, -1) * pixel);
    float3 ne = ImageSample(uv + float2( 1, -1) * pixel);
    float3 sw = ImageSample(uv + float2(-1,  1) * pixel);
    float3 se = ImageSample(uv + float2( 1,  1) * pixel);
    float lm = Luma(center), lnw = Luma(nw), lne = Luma(ne), lsw = Luma(sw), lse = Luma(se);
    float lo = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float hi = max(lm, max(max(lnw, lne), max(lsw, lse)));
    if (hi - lo < max(0.0312, hi * 0.125)) return center;
    float2 direction = float2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float reduction = max((lnw + lne + lsw + lse) * (0.25 * 0.125), 1.0 / 128.0);
    float reciprocal = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduction);
    direction = clamp(direction * reciprocal, -8.0, 8.0) * pixel;
    float3 a = 0.5 * (ImageSample(uv + direction * (1.0 / 3.0 - 0.5)) +
                       ImageSample(uv + direction * (2.0 / 3.0 - 0.5)));
    float3 b = a * 0.5 + 0.25 * (ImageSample(uv - direction * 0.5) + ImageSample(uv + direction * 0.5));
    float lb = Luma(b);
    return lb < lo || lb > hi ? a : b;
}
float4 psmain(VSOut i) : SV_Target {
    float2 uv=i.uv;
    if(crt.y>0){float2 p=uv*2-1;uv=(p*(1+crt.y*dot(p,p))+1)*.5;
        if(any(uv<0)||any(uv>1))return float4(0,0,0,1);}
    float3 c = FxaaSample(uv);
    if (effects.y > 0) {
        float2 p = 1.0 / dst.xy;
        float3 n = ImageSample(uv - float2(0, p.y));
        float3 s = ImageSample(uv + float2(0, p.y));
        float3 e = ImageSample(uv + float2(p.x, 0));
        float3 w = ImageSample(uv - float2(p.x, 0));
        float3 minimum = min(c, min(min(n, s), min(e, w)));
        float3 maximum = max(c, max(max(n, s), max(e, w)));
        c = clamp(c + effects.y * (c - (n + s + e + w) * 0.25), minimum, maximum);
    }
    if (effects.w > 0) {
        // A conservative SDR glow. Original lighting and colour are unchanged when Off.
        float3 glow = 0;
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            float3 tap = ImageSample(uv + float2(x, y) * 6.0 / dst.xy);
            glow += max(tap - 0.7, 0.0);
        }
        c += glow * (effects.w / 9.0);
    }
    if (dst.z > 0.5) {
        float row = uv.y * dst.w;               // game row coordinate (0..480)
        float rowStep = 1.0 / dst.w;              // one game row in uv
        float3 above = SharpSample(uv - float2(0, 0.5 * rowStep));
        float3 below = SharpSample(uv + float2(0, 0.5 * rowStep));
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
    if(crt.z>0){float3 glow=0;
        [unroll]for(int gy=-1;gy<=1;++gy)[unroll]for(int gx=-1;gx<=1;++gx){
            float weight=(gx==0?2:1)*(gy==0?2:1);
            glow+=max(ImageSample(uv+float2(gx,gy)*3/dst.xy)-.5,0)*weight;}
        c+=glow*(crt.z/16);}
    if(crt.x>.5){int row=(int)floor(i.uv.y*dst.y);int phase=(int)floor(i.uv.x*dst.x);
        if(crt.x>1.5)phase+=(row/2)%2;phase%=3;
        float dim=crt.x>2.5?.72:.82;
        float3 phosphor=float3(phase==0?1:dim,phase==1?1:dim,phase==2?1:dim);
        if(crt.x>1.5&&crt.x<2.5&&row%4==3)phosphor*=.82;
        if(crt.x>2.5&&row%3==2)phosphor*=.88;
        c*=phosphor*1.06;}
    // Branch around every neutral adjustment to retain the original Off path exactly.
    if (grade.x > 0.5 && grade.x < 1.5) c *= float3(1.06, 1.0, 0.94);
    else if (grade.x >= 1.5 && grade.x < 2.5) c *= float3(0.94, 1.0, 1.06);
    else if (grade.x >= 2.5) c = Luma(c).xxx;
    if (color.w != 1.0) c = lerp(Luma(c).xxx, c, color.w);
    if (color.z != 1.0) c = (c - 0.5) * color.z + 0.5;
    if (color.x != 0.0) c += color.x;
    if (color.y != 1.0) c = pow(saturate(c), color.y);
    if (hdr.x > 0.5) {
        float3 encoded=saturate(c);
        float3 linearColor=lerp(pow((encoded+0.055)/1.055,2.4),encoded/12.92,step(encoded,0.04045));
        return float4(linearColor*hdr.y,1.0);
    }
    return float4(saturate(c), 1.0);
}
)";

}  // namespace port
