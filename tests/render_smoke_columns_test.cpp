// Real single-card car smoke, including the off-axis intro camera where view-Z
// reverses the correct plane order. Blacken RGB only: two individual renders on
// white give independent transmittances, whose product is the overlap oracle.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "game/camera.h"
#include "game/mstack.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}

class TraceGpu final:public port::GpuDevice {
public:
    explicit TraceGpu(port::GpuDevice& device):gpu(device){}
    const char* Name()const override{return gpu.Name();}
    port::GpuTexture CreateTexture(const port::RgbaImage& i,bool a=false)override{return gpu.CreateTexture(i,a);}
    void ClearTextures()override{gpu.ClearTextures();}
    void SetStaticVertices(const game::ModelVertex* v,size_t n)override{gpu.SetStaticVertices(v,n);}
    void SetDynamicVertices(const game::ModelVertex* v,size_t n)override{gpu.SetDynamicVertices(v,n);}
    void SetSpriteVertices(const game::TLVertex* v,size_t n)override{gpu.SetSpriteVertices(v,n);}
    bool BeginFrame(const float* c)override{return gpu.BeginFrame(c);}
    void Submit(const port::GpuDrawList& l)override{list=l;gpu.Submit(l);}
    void EndFrame()override{gpu.EndFrame();}
    bool ReadPixels(std::vector<uint8_t>* p)override{return gpu.ReadPixels(p);}
    int TargetWidth()const override{return gpu.TargetWidth();}
    int TargetHeight()const override{return gpu.TargetHeight();}
    port::GpuDrawList list;
private:
    port::GpuDevice& gpu;
};

static void FloatWord(uint32_t& word,float f){std::memcpy(&word,&f,4);}
static game::Model Occluder() {
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=0x83000000;w[2]=0x80000;w[8]=UINT32_MAX;
    FloatWord(w[7],100);FloatWord(w[10],1);FloatWord(w[11],1);FloatWord(w[13],1);
    game::ModelStrip strip;
    for(const auto& p:std::array<std::array<float,3>,4>{{{-8,-20,-70},{-8,20,-70},{8,-20,-70},{8,20,-70}}}) {
        game::ModelVertex v{};std::copy(p.begin(),p.end(),v.pos);v.normal[2]=1;strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}

struct Pose {
    float eye[3];int pitch,yaw;
    std::array<float,16> a,b;
};
// Original frame8544 and9750: same two cars, different camera positions.
// Both origins and yaw-only billboard transforms are preserved, not recentered.
static const Pose poses[]={
    {{-656.1633f,-.5f,-529.9203f},7645,13566,
     {-.132420853f,0,-1.49414349f,0, 0,2,0,0, .996095657f,0,-.088280566f,0, -1021.62097f,-3.03690624f,-497.514587f,1},
     {-.28077811f,0,-1.4734869f,0, 0,2,0,0, .9823246f,0,-.187185407f,0, -1021.86749f,6.18167019f,-460.210999f,1}},
    {{-904.6094f,6.8f,-497.5912f},5526,18194,
     {-.000862842717f,0,-1.49999976f,0, 0,2,0,0, .999999821f,0,-.000575228478f,0, -1021.62097f,-3.03690624f,-497.514587f,1},
     {-.455476016f,0,-1.42917514f,0, 0,2,0,0, .952783406f,0,-.303650677f,0, -1021.86749f,6.18167019f,-460.210999f,1}}
};

int main(int argc,char**argv) {
    if(argc<2){std::fprintf(stderr,"usage: render_smoke_columns_test <game directory> [sdlgpu]\n");return 2;}
    std::string error;port::AssetStore assets;
    Require(assets.Open(argv[1],&error),error.c_str());
    port::GpuDeviceDesc desc;desc.width=160;desc.height=120;
    std::unique_ptr<port::GpuDevice> backend;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>2&&std::strcmp(argv[2],"sdlgpu")==0) {
        Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());backend=port::CreateSdlGpuDevice(desc,&error);
    }else
#endif
        backend=port::CreateD3D11Device(desc,&error);
    Require(backend!=nullptr,error.c_str());TraceGpu gpu(*backend);
    port::ModelRenderer renderer;Require(renderer.Init(&gpu,&error),error.c_str());
    std::vector<game::Model> smoke(15);std::string pol;
    for(int frame=0;frame<15;++frame) {
        Require(assets.ModelById(0x135F+frame,smoke[frame],&pol)&&pol=="eff_1.bin","all fifteen real smoke frames resolve");
        Require(smoke[frame].meshes.size()==1,"car smoke stays a single card, outside layered-plume sorting");
        auto& m=smoke[frame].meshes[0];
        Require(m.material.Isp()==0x83000000&&m.material.Tsp()==0x94002453,"real depth/blend/texture-alpha material retained");
        for(int k=12;k<=14;++k)FloatWord(m.material.words[k],0); // black smoke, original alpha/UV/geometry
    }
    auto green=Occluder();
    port::ModelRenderer::Lighting light;light.enabled=true;
    for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    const auto projection=game::DeviceView()*game::DeviceProjection();
    long long overlap=0,legacyBad=0,greenPixels=0;int cases=0;
    for(bool dynamic:{false,true})for(int frame=0;frame<15;++frame)for(int scene=0;scene<2;++scene)for(int phase=-8;phase<=8;++phase) {
        const auto& pose=poses[scene];
        const auto view=game::ViewMatrix(pose.eye,pose.pitch+phase*4,pose.yaw+phase*12,0);
        const auto vp=view*projection;
        port::ModelRenderer::SceneItem a,b,o;
        a.model=b.model=&smoke[frame];a.polName=b.polName=pol;
        a.cullable=b.cullable=o.cullable=false;a.noMeshCull=b.noMeshCull=o.noMeshCull=true;
        a.dynamicGeometry=b.dynamicGeometry=dynamic;
        std::copy(pose.a.begin(),pose.a.end(),a.transform);std::copy(pose.b.begin(),pose.b.end(),b.transform);
        o.model=&green;
        // The green plane is fixed in camera space, in front of both plumes.
        const auto inverse=game::mx::Inverse(view);std::copy(inverse.m,inverse.m+16,o.transform);
        auto render=[&](const std::vector<port::ModelRenderer::SceneItem>& items,const std::vector<port::ModelRenderer::SpriteDraw>* sprites=nullptr) {
            renderer.SetScene(assets,items);const float clear[4]={1,1,1,1};Require(gpu.BeginFrame(clear),"begin smoke oracle frame");
            renderer.DrawGame(desc.width,desc.height,vp.m,&light,nullptr,view.m,sprites,nullptr,projection.m);
            std::vector<uint8_t> pixels;Require(gpu.ReadPixels(&pixels),"read smoke oracle pixels");gpu.EndFrame();
            for(const auto& d:gpu.list.draws)if(d.pipeline==port::GpuDraw::Pipeline::Model)
                Require(d.state.zwrite&&d.state.zfunc==4,"opt-in ordering preserves depth testing and writes");
            return pixels;
        };
        const auto singleA=render({a}),singleB=render({b}),occluder=render({o});
        const auto legacy=render({a,b});
        a.sortBackToFront=b.sortBackToFront=true;
        const auto fixed=render({a,b}),blocked=render({a,b,o});
        for(size_t i=0;i<fixed.size();i+=4) {
            const int expected=(int(singleA[i])*singleB[i]+127)/255;
            const bool both=singleA[i]<250&&singleB[i]<250;
            if(both){++overlap;if(std::abs(int(legacy[i])-expected)>3)++legacyBad;}
            for(int c=0;c<3;++c) {
                if(std::abs(int(fixed[i+c])-expected)>3) {
                    std::fprintf(stderr,"%s frame%X pose%d phase%d dynamic%d pixel%zu expected%d observed%d single%d/%d\n",gpu.Name(),0x135F+frame,scene,phase,dynamic,i/4,expected,fixed[i+c],singleA[i],singleB[i]);
                    Require(false,"every real smoke overlap matches independent transmittance product");
                }
                const bool isGreen=occluder[i]<2&&occluder[i+1]>253&&occluder[i+2]<2;
                if(isGreen){Require(std::abs(int(blocked[i+c])-int(occluder[i+c]))<=1,"solid scenery still occludes smoke");if(c==0)++greenPixels;}
                else Require(std::abs(int(blocked[i+c])-expected)<=3,"smoke outside the solid occluder remains complete");
            }
        }
        if(frame==0&&phase==0) {
            // Same cached/dynamic model, hint cleared again: no stale model state.
            a.sortBackToFront=b.sortBackToFront=false;
            Require(render({a,b})==legacy,"per-instance sorting hint does not leak through model cache or later scenes");
            auto marker=a,other=b;marker.transform[12]+=1;other.priority=9;other.sortBackToFront=true;
            port::ModelRenderer::SpriteDraw sprite{};sprite.key=0x80000008;sprite.depth=-130;
            sprite.isp=(4u<<29)|0x4000000;sprite.tsp=0x94100000;
            for(auto& v:sprite.v){v.x=v.y=-10;v.z=.5f;v.rhw=1;}
            const std::vector<port::ModelRenderer::SpriteDraw> sprites{sprite};
            render({a,marker,b,other},&sprites);const auto nativeSlots=renderer.lastPass1;
            a.sortBackToFront=b.sortBackToFront=true;
            render({a,marker,b,other},&sprites);const auto sortedSlots=renderer.lastPass1;
            Require(nativeSlots.size()==sortedSlots.size(),"scoped ordering retains every mixed-list entry");
            for(size_t i=0;i<nativeSlots.size();++i)if(nativeSlots[i].item!=0&&nativeSlots[i].item!=2) {
                const auto& x=nativeSlots[i];const auto& y=sortedSlots[i];
                Require(x.item==y.item&&x.sprite==y.sprite&&x.key==y.key&&x.depth==y.depth,
                        "unflagged models, sprites and different-priority effects keep their exact slots/keys");
            }
        }
        ++cases;
    }
    Require(overlap>10000&&legacyBad>1000&&greenPixels>10000,"real overlap, legacy negative control and opaque occlusion were all exercised");
    std::printf("PASS %s: %d cases, fifteen smoke frames, two real cameras, continuous rotation, cached/dynamic; %lld overlap pixels, %lld legacy failures, %lld occluded pixels\n",gpu.Name(),cases,overlap,legacyBad,greenPixels);
}
