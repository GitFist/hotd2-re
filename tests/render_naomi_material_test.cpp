// Actual draw-builder state plus GPU tests for all native TSP texture/alpha
// instructions. Assets are synthetic; no game files or installed settings.
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#include "game/anim.h"
#include "game/camera.h"
#ifdef HOTD2_TEST_D3D12
#include "render/d3d12_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Result(bool value,const std::string& error){Require(value,error.c_str());}
struct Recorder final:port::GpuDevice {
    port::GraphicsOptions graphics;port::GpuDrawList last;std::vector<game::ModelVertex> vertices,dynamicVertices;
    const char* Name()const override{return "material-record";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex* v,size_t n)override{if(n)vertices.assign(v,v+n);else vertices.clear();}
    void SetDynamicVertices(const game::ModelVertex* v,size_t n)override{if(n)dynamicVertices.assign(v,v+n);else dynamicVertices.clear();}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float[4])override{return true;}void EndFrame()override{}
    void Submit(const port::GpuDrawList& list)override{last=list;}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return graphics;}
};
static game::Model Plane(uint32_t tsp) {
    game::Model m;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[0]=(tsp&0x180000)==0x80000?0x8000002c:0x8200002c;
    w[1]=0x83000000;w[2]=tsp;w[8]=0xffffffff;w[9]=uint32_t(-1);
    const float material[]={1,.4f,.5f,.25f,.75f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;strip.ctrl=0x40;
    for(auto p:{std::pair<float,float>{-.01f,-.01f},{.01f,-.01f},{-.01f,.01f},{.01f,.01f}}) {
        game::ModelVertex v{};v.pos[0]=p.first;v.pos[1]=p.second;v.normal[2]=1;strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
int main(int argc,char** argv) {
    port::GpuDeviceDesc desc;std::string error;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_D3D12
    if(argc>1 && std::strcmp(argv[1],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#endif
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1 && std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Result(gpu!=nullptr,error);Recorder recorder;port::ModelRenderer renderer;Result(renderer.Init(&recorder,&error),error);
    port::AssetStore assets;auto projection=game::Mat4::Identity();projection.m[0]=projection.m[5]=100;projection.m[10]=0;projection.m[14]=.5f;
    const auto view=game::Mat4::Identity();port::ModelRenderer::Lighting light;light.enabled=true;light.naomiStandard=true;
    port::RgbaImage image;image.width=image.height=1;image.pixels={204,102,51,128};
    const auto texture=gpu->CreateTexture(image);Require(texture!=0,"create material fixture texture");
    auto draw=[&](uint32_t tsp,bool native,float factor=-1.f,uint32_t control=0) {
        recorder.graphics.naomiGraphics=native;auto model=Plane(tsp);renderer.ClearCache();
        if(control)model.meshes[0].material.words[0]=control;
        port::ModelRenderer::SceneItem item;item.model=&model;item.transform[14]=-1;item.cullable=false;item.noMeshCull=true;item.alpha=factor;
        // The texture is injected below to isolate TSP interpretation. Do not
        // replace this fixture's source face RGB with geometric light packing.
        renderer.SetScene(assets,{item});renderer.DrawGame(640,480,projection.m,&light,nullptr,nullptr,nullptr,nullptr,projection.m);
        Require(recorder.last.draws.size()==1,"material draw missing");auto list=recorder.last;
        auto& d=list.draws[0];auto& c=list.model[d.constants];
        Require(native?c.flags[3]>=16:c.flags[3]<16,"native texture interpretation leaked between modes");
        // Inject a synthetic texture; leave the actual builder's authored face
        // color/alpha and instruction constants in place. Fixed alpha blending
        // exposes the result even for a source header from the opaque list.
        c.flags[0]=1;c.flags[1]=1;c.flags[2]=-1;c.litDir[3]=0;
        d.texture=texture;d.state.blendEnable=1;d.state.srcBlend=5;d.state.dstBlend=6;
        gpu->SetStaticVertices(recorder.vertices.data(),recorder.vertices.size());
        auto options=gpu->EffectiveGraphics();options.naomiGraphics=native;Result(gpu->ConfigureGraphics(options,&error),error);
        const float clear[]={.05f,.05f,.05f,1};Require(gpu->BeginFrame(clear),"begin material frame");gpu->Submit(list);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read material frame");gpu->EndFrame();return pixels;
    };
    const uint32_t baseline=0x94100000;const auto pc=draw(baseline,false);
    size_t cases=0;
    for(int instruction=0;instruction<4;++instruction)for(bool ignore:{false,true})for(bool useAlpha:{false,true}) {
        const uint32_t tsp=0x94000000u | (uint32_t(instruction)<<6) | (ignore?0x80000u:0) | (useAlpha?0x100000u:0);
        const auto pixels=draw(tsp,true);
        const double ta=ignore?1:128./255,va=useAlpha?102./255:1;
        const double alpha=instruction<2?ta:instruction==2?va:ta*va;
        const double face[]={.5,.25,.75};const double tex[]={204./255,102./255,51./255};const size_t at=(240*640+320)*4;
        for(int k=0;k<3;++k) {
            const double color=instruction==0?tex[k]:instruction==2?face[k]*(1-ta)+tex[k]*ta:face[k]*tex[k];
            const int expected=int(std::round((color*alpha+.05*(1-alpha))*255));
            if(std::abs(int(pixels[at+k])-expected)>1) {
                std::fprintf(stderr,"mode%d ignore%d use%d channel%d: %d expected%d\n",instruction,int(ignore),int(useAlpha),k,int(pixels[at+k]),expected);Require(false,"native TSP color/alpha instruction differs");
            }
        }
        ++cases;
    }
    // Native factor<1 forces MODULATE_ALPHA+UseAlpha while retaining IgnoreTexA.
    for(bool ignore:{false,true}) {
        const uint32_t tsp=0x84000000u|(ignore?0x80000u:0);const auto pixels=draw(tsp,true,.5f);
        const double ta=ignore?1:128./255,alpha=ta*(51./255);const double face[]={.5,.25,.75};
        const size_t at=(240*640+320)*4;
        for(int k=0;k<3;++k) {
            const double color=face[k]*(image.pixels[k]/255.);
            Require(std::abs(int(pixels[at+k])-int(std::round((color*alpha+.05*(1-alpha))*255)))<=1,"native explicit alpha override differs");
        }
        const auto& state=recorder.last.draws[0].state;
        Require(state.srcBlend==5 && state.dstBlend==6,"native fading wrapper did not force fade blend");++cases;
    }
    // A full-opacity call bypasses the native override entirely, including the
    // primary muzzle's additive destination factor. The PC wrapper stays PC.
    const uint32_t primary=0x84902412;
    const auto full=draw(primary,true,1);const auto& fullState=recorder.last.draws[0].state;
    Require(fullState.srcBlend==5 && fullState.dstBlend==2,"native factor1 changed authored additive blend");
    Require(full==draw(primary,true),"native factor1 changed the authored DECAL result");
    const uint32_t opaque=0x20080040;draw(opaque,true,1);
    Require(recorder.last.opaqueDrawCount==1,"native factor1 moved an authored opaque material into the translucent list");
    draw(opaque,true,.5f);Require(recorder.last.opaqueDrawCount==0,"native fading wrapper retained the opaque list");
    // Four mapped native meshes (zndina and zslman) use this real header:
    // list2 with a TSP combination that the PC conversion classified opaque.
    draw(0x24080440,true,-1,0x8200002d);
    Require(recorder.last.opaqueDrawCount==0 && recorder.last.draws[0].state.blendEnable &&
            recorder.last.draws[0].state.srcBlend==2 && recorder.last.draws[0].state.dstBlend==2,
            "native PCW translucent list lost the real ONE/ONE material");
    Require(recorder.last.draws[0].state.zfunc==4 && !recorder.last.draws[0].state.zwrite &&
            recorder.last.model[recorder.last.draws[0].constants].flags[2]<0,
            "world-space native list2 retained source depth writes/alpha testing");

    // Autosort is a tile/list property, not a camera-space muzzle exception.
    // Exercise a real list2/ONE+ONE header through a normal world transform.
    // Later opaque geometry behind it must still pass; opaque foreground must
    // block it. This checks depth behavior independently of the alpha equation.
    {
        auto model=Plane(0x24080440);model.meshes[0].material.words[0]=0x8200002d;
        port::ModelRenderer::SceneItem item;item.model=&model;item.transform[14]=-1;
        item.cullable=false;item.noMeshCull=true;
        recorder.graphics.naomiGraphics=true;renderer.ClearCache();renderer.SetScene(assets,{item});
        const auto worldProjection=game::DeviceView()*game::DeviceProjection();
        renderer.DrawGame(640,480,worldProjection.m,&light,nullptr,view.m,nullptr,nullptr,worldProjection.m);
        Require(recorder.last.draws.size()==1 && recorder.last.opaqueDrawCount==0,"world-space native list2 missing");
        auto list=recorder.last;auto& d=list.draws[0];auto& c=list.model[d.constants];
        Require(d.state.zfunc==4 && !d.state.zwrite && c.flags[2]<0,"world-space native autosort state");
        c.flags[0]=1;d.texture=texture;
        gpu->SetStaticVertices(recorder.vertices.data(),recorder.vertices.size());
        gpu->SetDynamicVertices(recorder.dynamicVertices.data(),recorder.dynamicVertices.size());
        auto options=gpu->EffectiveGraphics();options.naomiGraphics=true;Result(gpu->ConfigureGraphics(options,&error),error);
        auto probe=list;auto& pd=probe.draws[0];auto& pc=probe.model[pd.constants];
        pd.state.blendEnable=0;pd.state.zwrite=1;pc.flags[3]=20; // raw native DECAL + IgnoreTexAlpha
        const float clear[]={.05f,.05f,.05f,1};
        for(bool foreground:{false,true}) {
            // Native untextured translucent polygons also participate in
            // autosort. Move a separate camera-space copy of the depth probe
            // so the tested source retains its own original position.
            Require(d.stream==port::GpuDraw::Stream::Dynamic,"untextured native source bypassed autosort");
            auto merged=recorder.dynamicVertices;pd.first=static_cast<uint32_t>(merged.size());
            for(uint32_t i=0;i<d.count;++i){auto v=recorder.dynamicVertices[d.first+i];for(float& p:v.pos)p*=foreground?.8f:1.2f;merged.push_back(v);}
            gpu->SetDynamicVertices(merged.data(),merged.size());
            Require(gpu->BeginFrame(clear),"begin world-space depth probe");
            if(foreground){gpu->Submit(probe);gpu->Submit(list);}else{gpu->Submit(list);gpu->Submit(probe);}
            std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read world-space depth probe");gpu->EndFrame();
            const size_t at=(240*640+320)*4;
            for(int k=0;k<3;++k)Require(pixels[at+k]==image.pixels[k],foreground?
                "world-space native translucency bypassed opaque foreground":"world-space native translucency wrote depth");
        }
    }
    // Crossing world-space smoke planes use the constant (-1) native kernel,
    // not the muzzle's direct camera-space route. Their order reverses on the
    // two sides of the intersection; one centroid ordering cannot pass both.
    {
        auto a=Plane(0x941000c0),b=Plane(0x941000c0); // MODULATE_ALPHA
        for(auto* model:{&a,&b}) {
            auto* w=model->meshes[0].material.words;w[8]=0;
            const float white[]={1,1,1};std::memcpy(w+12,white,sizeof(white));
        }
        const float aa=.4f,ba=.6f;
        std::memcpy(a.meshes[0].material.words+11,&aa,4);std::memcpy(b.meshes[0].material.words+11,&ba,4);
        for(auto pair:{std::pair<game::Model*,float>{&a,.5f},{&b,-.5f}})
            for(auto& v:pair.first->meshes[0].strips[0].vertices) {
                v.pos[0]*=20;v.pos[1]*=20;v.pos[2]=pair.second*v.pos[0];
            }
        std::vector<port::ModelRenderer::SceneItem> items(2);
        for(size_t i=0;i<items.size();++i) {
            auto& item=items[i];item.model=i?&b:&a;item.transform[14]=-1;item.cullable=false;item.noMeshCull=true;
            Require(!item.directModelView,"world crossing fixture acquired direct camera-space metadata");
        }
        recorder.graphics.naomiGraphics=true;renderer.ClearCache();renderer.SetScene(assets,items);
        const auto worldProjection=game::DeviceView()*game::DeviceProjection();
        renderer.DrawGame(640,480,worldProjection.m,&light,nullptr,view.m,nullptr,nullptr,worldProjection.m);
        Require(recorder.dynamicVertices.size()>12,"crossing world-space constant-color effects were not split");
        auto list=recorder.last;
        port::RgbaImage otherImage;otherImage.width=otherImage.height=1;otherImage.pixels={32,160,96,128};
        const auto otherTexture=gpu->CreateTexture(otherImage);Require(otherTexture!=0,"world crossing second texture");
        const auto identity=game::Mat4::Identity();
        for(auto& d:list.draws) {
            auto& c=list.model[d.constants];c.flags[0]=1;d.texture=c.diffuse[3]<.5f?texture:otherTexture;
            Require(d.stream==port::GpuDraw::Stream::Dynamic && std::memcmp(c.modelView,identity.m,sizeof(c.modelView))==0,
                    "world-space split geometry retained a second world/view transform");
            for(int k=0;k<3;++k)Require(std::abs(c.diffuse[k]-254.f/255)<1e-6f,"world-space native constant RGB packing changed");
        }
        gpu->SetStaticVertices(recorder.vertices.data(),recorder.vertices.size());
        gpu->SetDynamicVertices(recorder.dynamicVertices.data(),recorder.dynamicVertices.size());
        const float clear[]={.05f,.05f,.05f,1};Require(gpu->BeginFrame(clear),"begin world crossing");gpu->Submit(list);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read world crossing");gpu->EndFrame();
        const double alphaA=(128./255)*(102./255),alphaB=(128./255)*(153./255);
        for(int x:{288,352})for(int k=0;k<3;++k) {
            const double colorA=(image.pixels[k]/255.)*(254./255),colorB=(otherImage.pixels[k]/255.)*(254./255);
            const double back=x<320?colorA:colorB,front=x<320?colorB:colorA;
            const double backAlpha=x<320?alphaA:alphaB,frontAlpha=x<320?alphaB:alphaA;
            const double expected=(.05*(1-backAlpha)+back*backAlpha)*(1-frontAlpha)+front*frontAlpha;
            Require(std::abs(int(pixels[(240*640+x)*4+k])-int(std::round(expected*255)))<=1,
                    "crossing world-space constant effects differ from per-ray compositing");
        }
    }
    draw(0x24080440,false,-1,0x8200002d);
    Require(recorder.last.opaqueDrawCount==1 && !recorder.last.draws[0].state.blendEnable,
            "native PCW list interpretation leaked into PC conversion");
    Require(draw(baseline,false)==pc,"PC pixels changed after native material mode roundtrip");
    std::printf("PASS %zu native TSP/alpha combinations, factor1 authored blend and exact PC restoration\n",cases);
}
