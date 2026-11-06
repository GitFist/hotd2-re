// Original node-blood rendering: texture-alpha contracts, nonplanar native
// geometry and an independent per-ray compositing oracle. No game state changes.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/camera.h"
#include "game/mstack.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_D3D12
#include "render/d3d12_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
using Item=port::ModelRenderer::SceneItem;
struct Capture final:port::GpuDevice {
    port::GraphicsOptions graphics;port::GpuDrawList last;
    std::vector<game::ModelVertex> vertices,dynamic;
    const char* Name()const override{return "recorded";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex* v,size_t n)override{if(n)vertices.assign(v,v+n);else vertices.clear();}
    void SetDynamicVertices(const game::ModelVertex* v,size_t n)override{if(n)dynamic.assign(v,v+n);else dynamic.clear();}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float[4])override{return true;}
    void Submit(const port::GpuDrawList& v)override{last=v;}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return graphics;}
};
struct Camera {game::Mat4 world,view;};
static Camera View(bool far) {
    if(!far)return{game::Mat4::Identity(),game::Mat4::Identity()};
    const float p[]={578.0675659f,-22.5590229f,-8795.1708984f};auto world=game::Mat4::Identity();
    game::mx::Translate(world,p[0],p[1],p[2]);game::mx::RotY(world,-535);game::mx::RotX(world,-1169);
    return{world,game::ViewMatrix(p,-1169,-535,0)};
}
static Item Blood(const game::Model& m,const std::string& pol,const game::Mat4& mv,const Camera& camera) {
    Item item;item.model=&m;item.polName=pol;item.priority=8;item.directModelView=true;
    item.cullable=false;item.noMeshCull=true;item.shadowCaster=false;
    const auto world=mv*camera.world;std::memcpy(item.transform,world.m,64);std::memcpy(item.modelView,mv.m,64);return item;
}
static game::Model Plane(int shading) {
    game::Model m;m.sphere[3]=4;game::ModelMesh mesh;auto* w=mesh.material.words;
    w[0]=0x8200002c;w[1]=0x83000000;w[2]=0x94002412u|(uint32_t(shading)<<6);w[8]=0;w[9]=0xffffffffu;
    if(shading==3)w[2]|=0x100000u; // later native blood frames enable vertex/material alpha
    const float material[]={.75f,.44f,.25f,.5f,.75f};std::memcpy(w+10,material,sizeof(material));
    const float sphere[]={0,0,0,4};std::memcpy(w+4,sphere,sizeof(sphere));
    game::ModelStrip strip;strip.ctrl=0x40;
    for(int i=0;i<4;++i){game::ModelVertex v{};v.pos[0]=(i&1)?.15f:-.15f;v.pos[1]=(i&2)?.15f:-.15f;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
static std::vector<uint8_t> Draw(port::GpuDevice& gpu,const port::GpuDrawList& list,const float clear[4]) {
    Require(gpu.BeginFrame(clear),"begin blood frame");gpu.Submit(list);
    std::vector<uint8_t> p;Require(gpu.ReadPixels(&p),"read blood frame");gpu.EndFrame();return p;
}
struct Triangle {game::ModelVertex v[3];int source=0,shading=0;float alpha=1;uint64_t sequence=0;};
struct Hit {double depth=0;int source=0,shading=0;float alpha=1;uint64_t sequence=0;bool edge=false;};
static bool Intersect(const Triangle& t,double x,double y,Hit* hit) {
    const double ray[]={x,y,-1};double e1[3],e2[3],s[3];
    for(int k=0;k<3;++k){e1[k]=double(t.v[1].pos[k])-t.v[0].pos[k];e2[k]=double(t.v[2].pos[k])-t.v[0].pos[k];s[k]=-double(t.v[0].pos[k]);}
    const double p[]={ray[1]*e2[2]-ray[2]*e2[1],ray[2]*e2[0]-ray[0]*e2[2],ray[0]*e2[1]-ray[1]*e2[0]};
    const double determinant=e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2];if(std::abs(determinant)<1e-14)return false;
    const double u=(s[0]*p[0]+s[1]*p[1]+s[2]*p[2])/determinant;
    const double q[]={s[1]*e1[2]-s[2]*e1[1],s[2]*e1[0]-s[0]*e1[2],s[0]*e1[1]-s[1]*e1[0]};
    const double v=(ray[0]*q[0]+ray[1]*q[1]+ray[2]*q[2])/determinant;
    const double depth=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])/determinant;
    if(u<0||v<0||u+v>1||depth<=.8)return false;
    *hit={depth,t.source,t.shading,t.alpha,t.sequence,std::min({u,v,1-u-v})<.002};return true;
}
static void Source(const game::Model& model,const game::Mat4& mv,int source,std::vector<Triangle>* triangles) {
    for(const auto& mesh:model.meshes)for(const auto& strip:mesh.strips) {
        std::vector<game::ModelVertex> vertices;game::Triangulate(strip,vertices);
        for(size_t first=0;first<vertices.size();first+=3) {
            Triangle t;t.source=source;t.shading=int((mesh.material.Tsp()>>6)&3);
            t.alpha=std::floor(std::clamp(mesh.material.DiffuseA(),0.f,1.f)*255.f)/255.f;t.sequence=triangles->size();
            for(int k=0;k<3;++k){t.v[k]=vertices[first+k];game::mx::Transform(mv,vertices[first+k].pos,t.v[k].pos);}
            triangles->push_back(t);
        }
    }
}

int main(int argc,char** argv) {
    if(argc<3){std::fprintf(stderr,"usage: render_naomi_blood_test <game directory> <naomi bank> [d3d12|sdlgpu]\n");return 2;}
    std::string error;port::GpuDeviceDesc desc;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_D3D12
    if(argc>3&&std::strcmp(argv[3],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#endif
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>3&&std::strcmp(argv[3],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());auto options=gpu->EffectiveGraphics();options.naomiGraphics=true;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Capture capture;capture.graphics.naomiGraphics=true;port::ModelRenderer recorder,renderer;
    Require(recorder.Init(&capture,&error)&&renderer.Init(gpu.get(),&error),error.c_str());
    const auto projection=game::DeviceView()*game::DeviceProjection();const auto near=View(false),far=View(true);
    port::ModelRenderer::Lighting light;light.enabled=true;light.naomiStandard=true;light.naomiIntensity=.7f;
    const float clear[]={20.f/255,20.f/255,20.f/255,1};
    auto record=[&](port::AssetStore& assets,const std::vector<Item>& items,const Camera& camera) {
        // The synthetic per-mode models reuse a stack address; retire the
        // immutable-model cache before recording their different materials.
        recorder.ClearCache();
        recorder.SetScene(assets,items);const auto vp=camera.view*projection;
        recorder.DrawGame(640,480,vp.m,&light,nullptr,camera.view.m,nullptr,nullptr,projection.m);
        Require(!capture.last.draws.empty(),"native blood draws missing");
        for(const auto& d:capture.last.draws) {
            Require(d.state.zfunc==4&&!d.state.zwrite,"native blood used raw ISP depth writes instead of active autosort");
            Require(capture.last.model[d.constants].flags[2]<0,"native blood retained PC punch-through alpha test");
            Require(d.stream==port::GpuDraw::Stream::Dynamic&&d.first<=capture.dynamic.size()&&d.count<=capture.dynamic.size()-d.first,"blood split stream range");
        }
    };

    // Known-color independent equations distinguish native texture-alpha-only
    // mode1 from mode3's texture-alpha * quantized material-alpha contract.
    port::AssetStore empty;port::RgbaImage tex;tex.width=tex.height=1;tex.pixels={120,64,32,128};
    const auto texture=gpu->CreateTexture(tex);Require(texture!=0,"blood contract texture");
    for(int shading:{1,3}) {
        const auto model=Plane(shading);auto mv=game::Mat4::Translate(0,0,-1);
        record(empty,{Blood(model,"",mv,near)},near);
        auto list=capture.last;for(auto& d:list.draws){d.texture=texture;list.model[d.constants].flags[0]=1;}
        gpu->SetStaticVertices(capture.vertices.data(),capture.vertices.size());gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
        const auto pixels=Draw(*gpu,list,clear);const size_t at=(240*640+320)*4;
        const float alpha=(128.f/255)*(shading==1?1.f:112.f/255);
        const int face[]={62,126,190};
        for(int k=0;k<3;++k) {
            const int expected=int(std::round(tex.pixels[k]*(face[k]/255.f)*alpha+20*(1-alpha)));
            if(std::abs(int(pixels[at+k])-expected)>1) {
                const auto& c=list.model[list.draws[0].constants];
                std::fprintf(stderr,"blood contract shading%d channel%d got%d expected%d alpha%.9f diffuse[%.9f %.9f %.9f %.9f] flags[%.9f %.9f %.9f %.9f] light[%.9f %.9f %.9f %.9f] options[%.9f %.9f %.9f %.9f]\n",
                    shading,k,int(pixels[at+k]),expected,alpha,c.diffuse[0],c.diffuse[1],c.diffuse[2],c.diffuse[3],
                    c.flags[0],c.flags[1],c.flags[2],c.flags[3],c.litDir[0],c.litDir[1],c.litDir[2],c.litDir[3],
                    c.lightingOptions[0],c.lightingOptions[1],c.lightingOptions[2],c.lightingOptions[3]);
                Require(false,"blood texture/material color or alpha contract");
            }
        }
        // An opaque probe behind the blood is visible afterwards only if the
        // translucent draw left depth unchanged; a nearer probe blocks it.
        auto probe=list;probe.draws={list.draws[0]};auto& d=probe.draws[0];auto& c=probe.model[d.constants];
        d.state.blendEnable=0;d.state.zwrite=1;c.flags[3]=20;c.litDir[3]=0;c.flags[1]=1; // native DECAL, IgnoreTexAlpha
        auto merged=capture.dynamic;const uint32_t first=uint32_t(merged.size());
        for(uint32_t i=0;i<d.count;++i)merged.push_back(capture.dynamic[d.first+i]);d.first=first;
        auto probeAt=[&](float depth){for(uint32_t i=0;i<d.count;++i)for(int k=0;k<3;++k)merged[first+i].pos[k]=capture.dynamic[list.draws[0].first+i].pos[k]*depth;gpu->SetDynamicVertices(merged.data(),merged.size());};
        probeAt(1.2f);Require(gpu->BeginFrame(clear),"begin blood depth probe");gpu->Submit(list);gpu->Submit(probe);
        std::vector<uint8_t> p;Require(gpu->ReadPixels(&p),"read blood depth probe");gpu->EndFrame();
        for(int k=0;k<3;++k)Require(p[at+k]==tex.pixels[k],"blood wrote depth and rejected a later opaque probe");
        probeAt(.81f);Require(gpu->BeginFrame(clear),"begin blood opaque occlusion");gpu->Submit(probe);gpu->Submit(list);
        Require(gpu->ReadPixels(&p),"read blood opaque occlusion");gpu->EndFrame();
        for(int k=0;k<3;++k)Require(p[at+k]==tex.pixels[k],"blood bypassed foreground opaque depth");
    }

    renderer.ClearCache();recorder.ClearCache();port::AssetStore assets;
    Require(assets.Open(argv[1],&error)&&assets.LoadNaomiVisuals(argv[2],&error)&&assets.SetNaomiGraphics(true,&error),error.c_str());
    assets.SetBloodColor(port::BloodColor::Red);
    std::array<game::Model,25> models;std::array<std::string,25> pol;
    for(int i=0;i<25;++i)Require(assets.NaomiModelById(0x3A+i,models[i],&pol[i]),"native node blood model mapping");
    auto render=[&](const std::vector<Item>& items,const Camera& camera) {
        renderer.SetScene(assets,items);const auto vp=camera.view*projection;Require(gpu->BeginFrame(clear),"begin real blood frame");
        renderer.DrawGame(640,480,vp.m,&light,nullptr,camera.view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read real blood frame");gpu->EndFrame();return pixels;
    };
    for(int frame:{0,1,4,12,24}) {
        auto mv=game::Mat4::Translate(1,0,-40);game::mx::Scale(mv,.3f,.3f,.3f);
        const auto a=render({Blood(models[frame],pol[frame],mv,near)},near);
        const auto b=render({Blood(models[frame],pol[frame],mv,far)},far);
        Require(a==b,"far camera changed native three-dimensional blood pixels");
    }

    // Two real nonplanar source frames, given controlled texels. Intersect rays
    // with original unsplit triangles, sort each pixel's hits independently,
    // and compare the GPU result: source-card submission order is not the oracle.
    auto a=game::Mat4::Translate(-.3f,0,-14),b=game::Mat4::Translate(.4f,.25f,-14.2f);
    game::mx::Scale(a,1.3f,1.3f,1.3f);game::mx::Scale(b,1.3f,1.3f,1.3f);
    record(assets,{Blood(models[0],pol[0],a,near),Blood(models[1],pol[1],b,near)},near);
    std::vector<Triangle> triangles;Source(models[0],a,0,&triangles);Source(models[1],b,1,&triangles);
    port::RgbaImage red,green;red.width=red.height=green.width=green.height=1;
    red.pixels={160,32,16,128};green.pixels={32,128,24,160};
    const auto redTexture=gpu->CreateTexture(red),greenTexture=gpu->CreateTexture(green);
    Require(redTexture&&greenTexture,"real-geometry oracle textures");auto list=capture.last;
    for(auto& d:list.draws){d.texture=list.model[d.constants].diffuse[3]>.9999f?redTexture:greenTexture;list.model[d.constants].flags[0]=1;}
    gpu->SetStaticVertices(capture.vertices.data(),capture.vertices.size());gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
    const auto pixels=Draw(*gpu,list,clear);size_t checked=0,multiple=0;
    for(int py=65;py<416;py+=11)for(int px=65;px<576;px+=11) {
        const double x=((px+.5)/320-1)/projection.m[0],y=(1-(py+.5)/240)/projection.m[5];
        std::vector<Hit> hits;Hit h;bool edge=false;
        for(const auto& t:triangles)if(Intersect(t,x,y,&h)){edge|=h.edge;hits.push_back(h);}
        if(hits.empty()||edge)continue;
        std::stable_sort(hits.begin(),hits.end(),[](const Hit& l,const Hit& r){return l.depth>r.depth;});
        bool tie=false;for(size_t i=1;i<hits.size();++i)if(hits[i].source!=hits[i-1].source&&std::abs(hits[i].depth-hits[i-1].depth)<1e-7)tie=true;
        if(tie)continue;
        std::array<double,3> expected={20,20,20};
        for(const auto& hit:hits) {
            const auto& color=hit.source?green.pixels:red.pixels;
            const double alpha=(color[3]/255.)*(hit.shading==1?1.:hit.alpha);
            for(int k=0;k<3;++k)expected[k]=std::round(color[k]*(254./255)*alpha+expected[k]*(1-alpha));
        }
        for(int k=0;k<3;++k)if(std::abs(int(pixels[(py*640+px)*4+k])-expected[k])>2) {
            std::fprintf(stderr,"blood oracle at%d,%d hits%zu channel%d got%d expected%.0f\n",px,py,hits.size(),k,int(pixels[(py*640+px)*4+k]),expected[k]);
            Require(false,"native nonplanar blood differs from independent per-ray alpha compositing");
        }
        ++checked;if(hits.size()>1)++multiple;
    }
    Require(checked>20&&multiple>5,"real nonplanar blood oracle did not exercise overlapping sheets");

    renderer.ClearCache();recorder.ClearCache();Require(assets.SetNaomiGraphics(false,&error),error.c_str());
    options.naomiGraphics=false;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    game::Model pc;std::string pcPol;Require(assets.ModelById(0x3B,pc,&pcPol),"PC blood model");
    auto item=Blood(pc,pcPol,a,far);const auto original=render({item},far);item.directModelView=false;
    Require(render({item},far)==original,"native blood sorting metadata changed PC pixels");
    std::printf("PASS %s: blood alpha modes, opaque depth, five real frames/camera invariance, %zu per-ray samples/%zu overlaps, PC isolation\n",gpu->Name(),checked,multiple);
}
