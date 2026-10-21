#pragma once

// Optional screen-space contact occlusion, composed after opaque geometry and
// before every translucent effect/HUD draw. No random kernel/history is used.
#include <algorithm>
#include <cmath>

namespace port {
struct AoConstants {
    float inverseProjection[16];
    float target[4];       // width, height, 1/width, 1/height
    float settings[4];     // world radius, bounded strength, angular bias, sample rings
    float projectionScale[4]; // projection X/Y scale, reserved
};

inline bool MakeAoConstants(int width,int height,const float projection[16],int quality,AoConstants* out) {
    if (!out || width<=0 || height<=0 || !projection || quality<1 || quality>3) return false;
    if(std::abs(projection[11])<1e-8f)return false; // pixel-radius policy is for the game's perspective camera
    // Invert the actual reversed projection in double precision. A generic inverse
    // preserves camera shifts and nonstandard projection signs in original scenes.
    double a[4][8]{};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
        if(!std::isfinite(projection[row*4+col]))return false;
        a[row][col]=projection[row*4+col];a[row][col+4]=row==col?1:0;
    }
    for(int col=0;col<4;++col) {
        int pivot=col;for(int row=col+1;row<4;++row)if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(std::abs(a[pivot][col])<1e-14)return false;
        if(pivot!=col)for(int k=0;k<8;++k)std::swap(a[pivot][k],a[col][k]);
        const double scale=a[col][col];for(int k=0;k<8;++k)a[col][k]/=scale;
        for(int row=0;row<4;++row)if(row!=col){const double factor=a[row][col];for(int k=0;k<8;++k)a[row][k]-=factor*a[col][k];}
    }
    *out={};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
        const float value=static_cast<float>(a[row][col+4]);if(!std::isfinite(value))return false;
        out->inverseProjection[row*4+col]=value;
    }
    out->target[0]=float(width);out->target[1]=float(height);out->target[2]=1.f/width;out->target[3]=1.f/height;
    out->settings[0]=8.f+quality*8.f;out->settings[1]=.2f+quality*.15f;out->settings[2]=.05f;out->settings[3]=float(quality);
    out->projectionScale[0]=std::abs(projection[0]);out->projectionScale[1]=std::abs(projection[5]);
    return true;
}

inline const char kAoShaderHlsl[] = R"(
#ifndef CB_REG
#define CB_REG register(b0)
#endif
#ifndef TEX_REG
#define TEX_REG register(t0)
#endif
#ifndef DEPTH_TEX_REG
#define DEPTH_TEX_REG register(t1)
#endif
cbuffer Occlusion : CB_REG {
    row_major float4x4 inverseProjection;
    float4 target;
    float4 settings;
    float4 projectionScale;
};
Texture2D colorTexture : TEX_REG;
Texture2D<float> depthTexture : DEPTH_TEX_REG;
struct VSOut {float4 pos:SV_Position;float2 uv:TEXCOORD;};
VSOut vsmain(uint id:SV_VertexID) {
    VSOut o;float2 uv=float2((id<<1)&2,id&2);
    o.pos=float4(uv.x*2-1,1-uv.y*2,0,1);o.uv=uv;return o;
}
bool Position(int2 pixel,out float3 position) {
    position=0;
    pixel=clamp(pixel,int2(0,0),int2(target.xy)-1);
    float depth=depthTexture.Load(int3(pixel,0));
    if(depth<=0)return false;
    float2 uv=(float2(pixel)+.5)*target.zw;
    float4 p=mul(float4(uv.x*2-1,1-uv.y*2,depth,1),inverseProjection);
    if(abs(p.w)<1e-12)return false;
    position=p.xyz/p.w;return true;
}
static const float2 directions[8]={float2(1,0),float2(.70710678,.70710678),float2(0,1),float2(-.70710678,.70710678),float2(-1,0),float2(-.70710678,-.70710678),float2(0,-1),float2(.70710678,-.70710678)};
float4 psmain(VSOut i):SV_Target {
    int2 pixel=clamp(int2(i.pos.xy),int2(0,0),int2(target.xy)-1);
#ifdef AO_MODULATE_ONLY
    // The backend multiplies this factor into the existing opaque render target.
    // This avoids sampling the target being written and preserves MSAA samples.
    float4 color=float4(1,1,1,1);
#else
    float4 color=colorTexture.Load(int3(pixel,0));
#endif
    float3 p,left,right,above,below;
    if(!Position(pixel,p))return color;
    bool hasLeft=Position(pixel-int2(1,0),left),hasRight=Position(pixel+int2(1,0),right);
    bool hasAbove=Position(pixel-int2(0,1),above),hasBelow=Position(pixel+int2(0,1),below);
    if((!hasLeft&&!hasRight)||(!hasAbove&&!hasBelow))return color;
    float3 dx=!hasLeft?right-p:!hasRight?p-left:abs(right.z-p.z)<abs(p.z-left.z)?right-p:p-left;
    float3 dy=!hasAbove?below-p:!hasBelow?p-above:abs(below.z-p.z)<abs(p.z-above.z)?below-p:p-above;
    float3 n=cross(dx,dy);float normalLength=dot(n,n);
    if(normalLength<1e-16)return color;
    n*=rsqrt(normalLength);if(dot(n,-p)<0)n=-n;
    float radius=max(settings.x,1e-3),radius2=radius*radius;
    float2 pixelRadius=clamp(radius*projectionScale.xy*target.xy*.5/max(abs(p.z),.8),1,256);
    int rings=clamp((int)settings.w,1,3);
    float obscurance=0;
    [unroll]for(int direction=0;direction<8;++direction) {
        float horizon=0;
        [unroll]for(int ring=1;ring<=3;++ring)if(ring<=rings) {
            float fraction=(float)ring/(rings+1);
            int2 offset=int2(round(directions[direction]*pixelRadius*fraction));
            float3 samplePosition;
            if(!Position(pixel+offset,samplePosition))continue;
            float3 v=samplePosition-p;float distance2=dot(v,v);
            if(distance2<=1e-8||distance2>=radius2)continue;
            float angle=max(0,dot(n,v*rsqrt(distance2))-settings.z);
            horizon=max(horizon,angle*(1-distance2/radius2));
        }
        obscurance+=horizon;
    }
    float factor=1-saturate(obscurance*.125)*settings.y;
    return float4(color.rgb*factor,color.a);
}
)";
} // namespace port
