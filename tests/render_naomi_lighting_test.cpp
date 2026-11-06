// Original Naomi standard lighting, isolated from assets and gameplay. Expected
// values use the verified scalar/dispatch and the PVR byte-color multiplication.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "game/anim.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_D3D12
#include "render/d3d12_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Plane(float dot,int exponent,float color=1.f,float ambient=.75f,float specular=0.f) {
    game::Model m;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=0xffffffffu;w[9]=static_cast<uint32_t>(exponent);
    const float material[5]={ambient,1,color,color,color};std::memcpy(w+10,material,sizeof(material));
    const float sheen[3]={specular,specular,specular};std::memcpy(w+16,sheen,sizeof(sheen));
    game::ModelStrip strip;strip.ctrl=0x40;
    for(auto p:{std::pair<float,float>{-.01f,-.01f},{.01f,-.01f},{-.01f,.01f},{.01f,.01f}}){
        game::ModelVertex v{};v.pos[0]=p.first;v.pos[1]=p.second;
        v.normal[0]=std::sqrt(std::max(0.f,1-dot*dot));v.normal[2]=dot;strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
static int Pvr(float face,float intensity) {
    const int f=static_cast<int>(std::clamp(face,0.f,1.f)*255.f);
    const int i=static_cast<int>(std::clamp(intensity,0.f,1.f)*255.f);
    return f*i/256;
}
// Independent reference: pack the native density register from raw IEEE bits,
// build its 128 byte-pair table, then address the logarithmic PVR interpolation.
static float NativeFog(float near,float far,float depth) {
    uint32_t bits=0;std::memcpy(&bits,&far,sizeof(bits));
    const unsigned mantissa=128u+((bits>>16)&127u);
    const int exponent=int((bits>>23)&255u)-126;
    const float density=std::ldexp(float(mantissa)/128.f,exponent);
    int table[128];
    for(int k=0;k<128;++k)table[k]=int(std::clamp((far*std::exp2(-float(k)/16)-near)/(far-near),0.f,1.f)*255);
    const float z=std::clamp(density/depth,1.f,255.9999f);
    const int e=int(std::floor(std::log2(z)));
    const float m=z*16/std::ldexp(1.f,e)-16;
    const int i=int(m)+16*e;const float f=m-std::floor(m);
    return (table[i]*(1-f)+table[std::min(i+1,127)]*f)/255;
}
// Records the actual draw builder's sampler and mode constants without requiring
// a source texture file. GPU color checks below independently execute the shader.
struct Recorder final:port::GpuDevice {
    port::GraphicsOptions graphics;port::GpuDrawList last;
    const char* Name()const override{return "recorded";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex*,size_t)override{}
    void SetDynamicVertices(const game::ModelVertex*,size_t)override{}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float[4])override{return true;}
    void Submit(const port::GpuDrawList& v)override{last=v;}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return graphics;}
};

int main(int argc,char**argv) {
    std::string error;port::GpuDeviceDesc desc;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_D3D12
    if(argc>1&&std::strcmp(argv[1],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#endif
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
    port::AssetStore assets;auto view=game::Mat4::Identity();auto projection=game::Mat4::Identity();
    projection.m[0]=projection.m[5]=100;projection.m[10]=0;projection.m[14]=.5f;
    port::ModelRenderer::Lighting light;light.enabled=true;light.dirToLight[1]=0;light.dirToLight[2]=1;
    light.naomiStandard=true;light.naomiIntensity=.7f;
    for(int k=0;k<3;++k){light.diffuse[k]=.98f;light.ambient[k]=.3f;light.ambientRS[k]=178.f/255;}
    auto render=[&](game::Model& model,float scale=1.f,const port::ModelRenderer::Fog* fog=nullptr,float depth=1.f){
        port::ModelRenderer::SceneItem item;item.model=&model;item.cullable=false;item.noMeshCull=true;
        item.transform[0]=item.transform[5]=item.transform[10]=scale;item.transform[14]=-depth;
        renderer.ClearCache();renderer.SetScene(assets,{item});const float clear[4]={0,0,0,1};
        Require(gpu->BeginFrame(clear),"begin frame");renderer.DrawGame(640,480,projection.m,&light,fog,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read frame");gpu->EndFrame();return pixels;
    };
    auto rgb=[&](const std::vector<uint8_t>& p,int expected,const char* why){
        const size_t at=(240*640+320)*4;for(int k=0;k<3;++k)Require(std::abs(int(p[at+k])-expected)<=1,why);
    };
    auto base=Plane(0,0);const auto pc=render(base);rgb(pc,191,"PC ambient baseline changed");
    auto options=gpu->EffectiveGraphics();options.naomiGraphics=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    for(float n:{-.5f,0.f,.25f,.5f,1.f})for(float scale:{1.f,.3f,2.f}){
        auto model=Plane(n,0);const int expected=Pvr(1,.7f*.75f+std::max(0.f,n));
        rgb(render(model,scale),expected,"Naomi scalar/color or uniform scale cancellation differs");
    }
    // Full directional saturation must not clamp a dark face up to white. The
    // face and scalar have separate PVR clamps and alpha remains independent.
    auto dark=Plane(1,0,.2f);rgb(render(dark),Pvr(.2f,1),"Naomi clamped after face multiplication");
    auto constant=Plane(-.5f,-1);rgb(render(constant),Pvr(1,1),"native -1 material was directionally dimmed");
    for(auto pair:{std::pair<int,int>{1,2},{2,4},{3,6},{4,8},{5,10},{6,10},{7,14},{8,16},{16,32},{32,64}}){
        auto model=Plane(.99f,pair.first,0,.75f,.8f);const float q=2*.99f*.99f-1;
        rgb(render(model),Pvr(.8f,std::pow(q,float(pair.second))),"native specular dispatch/half-vector differs");
    }
    // A parallel viewer's half-vector is prepared per draw, not per vertex:
    // changing vertex positions must not flatten its centered highlight.
    auto wide=Plane(1,5,0,.75f,.8f);
    for(auto& v:wide.meshes[0].strips[0].vertices){v.pos[0]*=100;v.pos[1]*=100;}
    projection.m[0]=projection.m[5]=1;rgb(render(wide),Pvr(.8f,1),"Naomi specular used a local vertex viewer");
    // W-friendly depth follows the same convention as game cameras. The source
    // SDK packs ~2*FAR in FOG_DENSITY; the port's stored near/far are doubled.
    projection.m[10]=-.5f;projection.m[11]=-1;projection.m[14]=projection.m[15]=0;
    auto fogPlane=Plane(0,-1,0);
    for(auto& v:fogPlane.meshes[0].strips[0].vertices){v.pos[0]*=1000;v.pos[1]*=1000;}
    port::ModelRenderer::Fog fog;fog.enabled=true;fog.start=12.75f;fog.end=246.9f;
    fog.color[0]=fog.color[1]=fog.color[2]=1;
    for(float d:{1.f,7.f,12.75f,14.f,19.f,30.f,41.f,63.f,91.f,123.45f,180.f,246.9f,400.f}) {
        const int expected=int(std::round(NativeFog(fog.start*.5f,fog.end*.5f,d)*255));
        rgb(render(fogPlane,1,&fog,d),expected,"native fog register/LUT/interpolation differs");
    }
    for(float invalid:{fog.start,0.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto bad=fog;bad.end=invalid;rgb(render(fogPlane,1,&bad,30),0,"degenerate native fog produced a color");
    }
    // Ordinary PC fog remains its exact analytic linear interpolation.
    options.naomiGraphics=false;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    rgb(render(fogPlane,1,&fog,91),int(std::round((91-fog.start)/(fog.end-fog.start)*255)),"PC fog changed with native LUT addition");
    projection.m[10]=0;projection.m[11]=0;projection.m[14]=.5f;projection.m[15]=1;
    projection.m[0]=projection.m[5]=100;
    Require(render(base)==pc,"PC image changed after Naomi mode roundtrip");

    Recorder recorder;port::ModelRenderer recordRenderer;Require(recordRenderer.Init(&recorder,&error),error.c_str());
    port::ModelRenderer::SceneItem item;item.model=&base;item.cullable=false;item.noMeshCull=true;item.transform[14]=-1;
    for(bool native:{false,true,false}){
        recorder.graphics.naomiGraphics=native;recordRenderer.SetScene(assets,{item});
        recordRenderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        Require(!recorder.last.draws.empty(),"recorded draw missing");
        for(const auto& draw:recorder.last.draws)Require(draw.state.mipmapped==(native?0:1),"Naomi sampling retained generated mips or leaked into PC");
        Require((recorder.last.model[0].lightingOptions[1]>.5f)==native,"standard mode constant leaked");
    }
    recorder.graphics.naomiGraphics=true;light.naomiStandard=false;
    recordRenderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
    Require(recorder.last.model[0].lightingOptions[1]==0,"unverified light-table semantics entered standard kernel");
    // Native full-alpha model draws submit DECAL unchanged. The PC conversion
    // interprets the same muzzle header as texture*material; Naomi replaces the
    // vertex/material RGBA with texture RGBA. Supply a synthetic texture to the
    // actual draw builder's constants so this needs no original asset files.
    port::RgbaImage image;image.width=image.height=1;image.pixels={160,96,32,128};
    const auto texture=gpu->CreateTexture(image);Require(texture!=0,"create DECAL texture");
    auto muzzle=Plane(0,-1,.25f);auto& mw=muzzle.meshes[0].material.words;
    std::vector<game::ModelVertex> vertices;game::Triangulate(muzzle.meshes[0].strips[0],vertices);
    gpu->SetStaticVertices(vertices.data(),vertices.size());
    item.model=&muzzle;item.priority=12;
    for(uint32_t tsp:{0x84902412u,0x94902424u,0x94982424u})for(bool native:{false,true,false}) {
        const float faceAlpha=tsp==0x84902412u?.92f:.44f;
        mw[0]=0x8200002c;mw[2]=tsp;std::memcpy(mw+11,&faceAlpha,sizeof(faceAlpha));
        recorder.graphics.naomiGraphics=native;recordRenderer.ClearCache();recordRenderer.SetScene(assets,{item});
        recordRenderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        auto list=recorder.last;Require(list.draws.size()==1,"one muzzle layer draw");
        auto& draw=list.draws[0];auto& c=list.model[draw.constants];
        const bool additive=tsp==0x84902412u,ignoreAlpha=(tsp&0x80000u)!=0;
        Require(draw.state.blendEnable && draw.state.srcBlend==5 && draw.state.dstBlend==(additive?2:6),"muzzle source blend changed");
        Require(draw.state.zfunc==4 && draw.state.zwrite==(native?0:1),"native autosort or PC authored depth state changed");
        Require(native?c.flags[2]<0:c.flags[2]>=0,"native translucent alpha testing or PC punch-through changed");
        Require(native?c.flags[3]>=16:c.flags[3]==1,"native material interpretation leaked into PC or missed native muzzle");
        draw.texture=texture;c.flags[0]=1;c.flags[1]=1;c.litDir[3]=0;
        options.naomiGraphics=native;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        const float clear[4]={20.f/255,20.f/255,20.f/255,1};Require(gpu->BeginFrame(clear),"begin muzzle frame");
        gpu->Submit(list);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read muzzle frame");gpu->EndFrame();
        const float alpha=native?(ignoreAlpha?1.f:128.f/255):faceAlpha*128.f/255;
        const size_t at=(240*640+320)*4;
        for(int k=0;k<3;++k) {
            const float source=image.pixels[k]*(native?1.f:.25f);
            const int expected=int(std::round(source*alpha+20*(additive?1.f:1-alpha)));
            Require(std::abs(int(pixels[at+k])-expected)<=1,"muzzle DECAL RGBA or legacy modulation differs");
        }
    }
    // An explicit-alpha wrapper changes the native TSP to its separate fading
    // path. It must not accidentally inherit DECAL from a preceding full draw.
    item.alpha=.5f;recorder.graphics.naomiGraphics=true;
    recordRenderer.SetScene(assets,{item});recordRenderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
    Require(recorder.last.model[0].flags[3]>=16,"explicit native alpha lost its native material interpretation");
    std::puts("PASS Naomi lighting/fog/sampling, muzzle DECAL RGBA with authored blend/depth, explicit alpha and PC restoration");
}
