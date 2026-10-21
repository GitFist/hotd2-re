// Stable render correspondence, separate from deterministic World/replay state.
#include "game/camera.h"
#include "render/model_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
class Capture final:public port::GpuDevice {
public:
    std::vector<port::GpuDrawList> lists;bool process=false,shadows=false;int boundary=0;size_t sceneCount=0;
    const char* Name()const override{return "temporal history capture";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool=false)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex*,size_t)override{}
    void SetDynamicVertices(const game::ModelVertex*,size_t)override{}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float*)override{lists.clear();boundary=0;return true;}
    void Submit(const port::GpuDrawList& value)override{lists.push_back(value);}
    bool RequiresSceneProcessing()const override{return process;}
    bool NeedsShadowCasters()const override{return shadows;}
    bool ProcessScene(std::string* =nullptr)override{++boundary;sceneCount=lists.back().draws.size();return true;}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 160;}
    int TargetHeight()const override{return 120;}
};
static game::Model Make(bool opaque=true) {
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=opaque?0x80000u:0x94100000u;w[8]=w[9]=0xffffffffu;
    const float mat[5]={1,opaque?1.f:.5f,.4f,.5f,.6f};std::memcpy(w+10,mat,sizeof(mat));
    game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-20,-20},{-20,20},{20,-20},{20,20}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=-100;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
int main() {
    Capture gpu;port::ModelRenderer renderer;port::AssetStore assets;std::string error;Require(renderer.Init(&gpu,&error),error.c_str());
    game::Model a=Make(),b=Make(),alpha=Make(false);
    auto item=[&](const game::Model* m,uint32_t task){port::ModelRenderer::SceneItem i;i.model=m;i.cullable=false;i.noMeshCull=true;i.motionTracked=true;i.motionIdentity.task=task;i.motionIdentity.model=1;return i;};
    auto A=item(&a,10),B=item(&b,20),effect=item(&alpha,30);B.dynamicGeometry=true;
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::TemporalFrameInfo frame;frame.enabled=true;frame.frameId=1;
    port::TemporalSceneStamp stamp;stamp.world=123;stamp.simulationFrame=100;renderer.SetTemporalScene(stamp);
    std::vector<port::ModelRenderer::SpriteDraw> sprites(2);sprites[1].hud=false;
    auto render=[&](std::vector<port::ModelRenderer::SceneItem> items,bool enabled=true){renderer.SetScene(assets,items);frame.enabled=enabled;renderer.SetTemporalFrame(frame);gpu.BeginFrame(nullptr);renderer.DrawGame(160,120,projection.m,nullptr,nullptr,view.m,&sprites,nullptr,projection.m);};
    render({A,B,effect});const auto first=gpu.lists.front();
    Require(first.temporal.reset&&first.motionDraws.size()==2,"first frame must reset and exclude alpha/HUD motion");
    for(const auto& c:first.motionConstants)Require(c.target[2]==0&&c.target[3]==0,"new history claimed valid prior geometry/depth");
    // Different draw order and a different fully deformed vertex stream must
    // retain each task's own prior positions, including CPU-skinned attachments.
    b.meshes[0].strips[0].vertices[0].pos[0]+=7;B.transform[12]=3;A.transform[12]=9;
    ++frame.frameId;frame.jitterPixels[0]=.25f;frame.jitterPixels[1]=-.25f;++stamp.simulationFrame;renderer.SetTemporalScene(stamp);
    render({B,effect,A});const auto second=gpu.lists.front();
    Require(!second.temporal.reset&&second.motionDraws.size()==2,"consecutive render frame reset stable actor history");
    for(const auto& c:second.motionConstants)Require(c.target[2]==1&&c.target[3]==0,"CPU correspondence/opaque depth ownership incorrect");
    const auto& mv=second.motionVertices[second.motionDraws[0].first];
    Require(mv.position[0]==-13&&mv.previous[0]==-20,"previous fully deformed positions were replaced by current positions");
    Require(second.motionConstants[0].previousModelView[12]==0&&second.motionConstants[0].modelView[12]==3,"actor history followed submission order instead of stable identity");
    Require(second.motionConstants[1].modelView[12]==9&&second.motionConstants[1].previousModelView[12]==0,"second actor lost independent transform history");
    Require(second.motionConstants[0].jitter[0]==.25f&&second.motionConstants[0].jitter[2]==0,"previous/current jitter not carried separately");
    // Replaced topology cannot inherit another triangle's previous positions.
    b.meshes[0].strips[0].ctrl^=0x40;++frame.frameId;render({B,A});
    Require(gpu.lists[0].motionConstants[0].target[2]==0&&gpu.lists[0].motionConstants[1].target[2]==1,"topology replacement did not invalidate only the affected item");
    ++frame.frameId;stamp.camera[4]++;renderer.SetTemporalScene(stamp);render({A,B});
    Require(gpu.lists[0].temporal.reset,"camera path cut did not reset history");
    ++frame.frameId;--stamp.simulationFrame;renderer.SetTemporalScene(stamp);render({A,B});
    Require(gpu.lists[0].temporal.reset,"rollback did not reset render history");
    frame.frameId+=2;render({A,B});Require(gpu.lists[0].temporal.reset,"discontinuous rendered frame ID retained stale history");
    ++frame.frameId;render({A,B},false);Require(!gpu.lists[0].temporal.enabled&&gpu.lists[0].motionDraws.empty(),"Off retained temporal motion or jitter");
    Require(gpu.lists[0].model[0].projection[8]==projection.m[8],"Off changed original projection");
    // Only an explicitly opted-in backend extracts HUD; world sprites and alpha
    // models remain before the single scene boundary, UI is submitted afterward.
    gpu.process=true;++frame.frameId;render({A,B,effect});
    Require(gpu.boundary==1&&gpu.lists.size()==2&&gpu.sceneCount==4,"preHUD scene boundary lost models/world effect or ran twice");
    Require(gpu.lists.back().draws.size()==1&&gpu.lists.back().draws[0].hud,"HUD entered temporal scene processing");
    for(const auto& d:gpu.lists.front().draws)Require(!d.hud,"HUD remained in scene submission");
    gpu.process=false;gpu.shadows=true;frame.enabled=false;renderer.SetTemporalFrame(frame);
    auto hidden=B;hidden.cullable=true;hidden.transform[12]=1000000;
    auto overlay=A;overlay.shadowCaster=false;
    renderer.SetScene(assets,{A,hidden,overlay,effect});
    port::ModelRenderer::Lighting light;light.enabled=true;
    gpu.BeginFrame(nullptr);renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
    Require(gpu.lists[0].draws.size()==3&&gpu.lists[0].shadowCasters.size()==2,"shadow caster list used view culling or included alpha/explicit overlay");
    bool offscreen=false;for(const auto& d:gpu.lists[0].shadowCasters)if(gpu.lists[0].model[d.constants].world[12]==1000000)offscreen=true;
    Require(offscreen,"offscreen geometry cannot cast a shadow into view");
    gpu.shadows=false;gpu.BeginFrame(nullptr);renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
    Require(gpu.lists[0].shadowCasters.empty()&&gpu.lists[0].model.size()==3,"default Off generated hidden caster work or changed normal draws");
    std::puts("PASS stable actor/deformation history, topology/camera/rollback/frame resets, jitter separation, Off, preHUD boundary and full unculled shadow casters");
}
