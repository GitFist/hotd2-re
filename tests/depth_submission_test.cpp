#include "game/camera.h"
#include "render/model_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool ok,const char* reason){if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
class Capture final:public port::GpuDevice {
public:
    port::GraphicsOptions options;port::GpuDrawList list;
    const char* Name()const override{return "depth submission capture";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool=false)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex*,size_t)override{}
    void SetDynamicVertices(const game::ModelVertex*,size_t)override{}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float*)override{return true;}
    void Submit(const port::GpuDrawList& value)override{list=value;}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}
    int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return options;}
};
static game::ModelMesh Card(float depth,bool opaque,float red) {
    game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=opaque?0x80000u:0x94100000u;w[8]=w[9]=0xffffffffu;
    const float material[5]={1,opaque?1.f:.5f,red,.25f,.25f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto p:{std::pair<float,float>{-40,-40},{-40,40},{40,-40},{40,40}}){game::ModelVertex v{};v.pos[0]=p.first;v.pos[1]=p.second;v.pos[2]=depth;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);return mesh;
}
int main() {
    Capture gpu;port::ModelRenderer renderer;port::AssetStore assets;std::string error;Require(renderer.Init(&gpu,&error),error.c_str());
    game::Model solid,layered,ordinary,car;
    solid.meshes={Card(-100,true,.1f)};layered.meshes={Card(-99,false,.8f),Card(-98,false,.8f)};
    ordinary.meshes={Card(-97,false,.3f)};car.meshes={Card(-96,false,.6f)};
    std::vector<port::ModelRenderer::SceneItem> items;
    for(auto model:{&ordinary,&solid,&car,&layered}){port::ModelRenderer::SceneItem item;item.model=model;item.cullable=false;item.noMeshCull=true;item.sortBackToFront=model==&car;items.push_back(item);}
    renderer.SetScene(assets,items);
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;
    for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    for(int softness:{0,1,2,3}) {
        gpu.options.softParticles=softness;
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        Require(gpu.list.opaqueDrawCount==1,"opaque depth prefix includes alpha geometry or omits opaque models");
        Require(gpu.list.draws.size()==5,"depth metadata changed native draw count");
        int eligible=0;
        for(size_t i=0;i<gpu.list.draws.size();++i) {
            const auto& d=gpu.list.draws[i];const auto& c=gpu.list.model[d.constants];
            Require((i<gpu.list.opaqueDrawCount)==!d.state.blendEnable,"opaque prefix does not match native pass order");
            const bool smoke=std::abs(c.diffuse[0]-.8f)<1e-5f||std::abs(c.diffuse[0]-.6f)<1e-5f;
            const float expected=smoke&&softness?float(16<<(softness-1)):0.f;
            Require(c.depthEffects[0]==expected,"soft-particle eligibility leaked to ordinary alpha or opaque geometry");
            Require(c.depthEffects[3]==0,"renderer claimed an opaque snapshot before the backend produced it");
            Require(c.depthEffects[1]==1.f/640&&c.depthEffects[2]==1.f/480,"depth snapshot coordinates use the wrong target size");
            Require(c.depthProjection[0]==projection.m[11]-projection.m[10]&&c.depthProjection[1]==projection.m[15]-projection.m[14],"reversed depth reconstruction lost projection precision");
            if(c.depthEffects[0]>0)++eligible;
        }
        Require(eligible==(softness?3:0),"known smoke paths did not opt in independently");
    }
    // Orthographic W is constant, so it must never be mistaken for eye distance.
    renderer.DrawGame(640,480,view.m,&light,nullptr,view.m,nullptr,nullptr,view.m);
    for(const auto& c:gpu.list.model)Require(c.depthEffects[0]==0,"orthographic projection enabled invalid W-based fading");
    std::vector<port::ModelRenderer::SpriteDraw> sprites(2);sprites[1].linear=true;
    renderer.DrawSprites(640,480,sprites);
    Require(gpu.list.opaqueDrawCount==0,"HUD sprites entered the opaque model prepass");
    Require(gpu.list.draws.size()==2&&!gpu.list.draws[0].state.linear&&gpu.list.draws[1].state.linear,"UI atlas filtering altered original sprite state");
    for(const auto& d:gpu.list.draws)Require(!d.state.mipmapped,"UI atlas enabled mipmaps on the native HUD");
    game::Model nearMesh;nearMesh.meshes={Card(-.75f,true,1)};
    const float sphere[]={10,0,-.75f,.1f};
    std::memcpy(nearMesh.meshes[0].material.words+4,sphere,sizeof(sphere));
    for(auto& strip:nearMesh.meshes[0].strips)for(auto& vertex:strip.vertices){vertex.pos[0]=10+vertex.pos[0]*.00025f;vertex.pos[1]*=.00025f;}
    port::ModelRenderer::SceneItem nearItem;nearItem.model=&nearMesh;nearItem.cullable=false;
    renderer.SetScene(assets,{nearItem});
    renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
    Require(gpu.list.draws.size()==1,"native .8 near-intersection culling behavior changed");
    auto typingProjection=game::DeviceProjection();typingProjection.m[10]=8000.f/7999.5f;typingProjection.m[14]=-.5f*typingProjection.m[10];
    typingProjection=game::DeviceView()*typingProjection;
    renderer.DrawGame(640,480,typingProjection.m,&light,nullptr,view.m,nullptr,nullptr,typingProjection.m);
    Require(gpu.list.draws.empty(),"typing .5 near plane incorrectly retained the native .8 side-culling exemption");
    std::puts("PASS opaque prefix, known smoke eligibility, Off/reset readiness, precise depth metadata, orthographic exclusion and UI-only sprite filtering");
}
