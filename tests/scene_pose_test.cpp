// Real chapter draw lists and their submitted dynamic geometry remain read-only
// to gameplay when a tick is presented several times at different phases.
#include "scene/scene.h"
#include "game/world_modes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
class Capture final:public port::GpuDevice {
public:
    std::vector<port::GpuDrawList> lists;
    std::vector<game::ModelVertex> dynamic;
    const char* Name()const override{return "scene pose recorder";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool=false)override{return ++texture_;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex*,size_t)override{}
    void SetDynamicVertices(const game::ModelVertex* vertices,size_t count)override{dynamic.assign(vertices,vertices+count);}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float*)override{lists.clear();return true;}
    void Submit(const port::GpuDrawList& value)override{lists.push_back(value);}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}
    int TargetHeight()const override{return 480;}
private:uintptr_t texture_=0;
};
static uint64_t Hash(const port::GpuDrawList& list,const std::vector<game::ModelVertex>& dynamic){
    uint64_t hash=1469598103934665603ull;
    auto add=[&](const void* data,size_t size){const auto* bytes=static_cast<const uint8_t*>(data);for(size_t k=0;k<size;++k){hash^=bytes[k];hash*=1099511628211ull;}};
    for(const auto& c:list.model){add(c.world,sizeof(c.world));add(c.viewProj,sizeof(c.viewProj));add(c.modelView,sizeof(c.modelView));add(c.projection,sizeof(c.projection));}
    for(const auto& d:list.draws){add(&d.first,sizeof(d.first));add(&d.count,sizeof(d.count));add(&d.constants,sizeof(d.constants));}
    if(!dynamic.empty())add(dynamic.data(),dynamic.size()*sizeof(dynamic[0]));
    return hash;
}
int main(int argc,char** argv){
    if(argc<2){std::fprintf(stderr,"usage: scene_pose_test <game data>\n");return 2;}
    for(int chapter=1;chapter<=6;++chapter){
        port::AssetStore assets;std::string error;Require(assets.Open(argv[1],&error),error.c_str());
        game::World world;Require(world.Load(argv[1]),"load chapter world");
        game::WorldConfig config;config.gameMode=6;config.gameOver=true;config.portFeatures=true;
        config.eventMode=chapter-1;config.scene=game::SceneSelectFirstScene(chapter-1);config.playerState[0]=0;
        Require(world.StartStage(config,&error),error.c_str());int warmup=0;
        for(;warmup<3600;++warmup){world.Step();if(warmup>=359&&!world.Draws().empty())break;}
        Require(!world.Draws().empty(),"chapter fixture did not reach actual world geometry");
        Capture gpu;port::ModelRenderer renderer;Require(renderer.Init(&gpu,&error),error.c_str());
        port::Scene scene;port::SceneDef definition;definition.useWorld=true;definition.externalWorld=true;Require(scene.Init(assets,definition,&error),error.c_str());
        auto render=[&](bool enabled,float alpha){scene.SetRenderInterpolation(enabled,alpha);const auto vp=scene.BuildFromWorld(renderer,world);const float clear[4]={0,0,0,1};gpu.BeginFrame(clear);
            renderer.DrawGame(640,480,vp.m,&scene.lighting,&scene.fog,scene.view.m,nullptr,&scene.itemLights,scene.projection.m);gpu.EndFrame();Require(!gpu.lists.empty(),"chapter pose produced no submission");return Hash(gpu.lists.front(),gpu.dynamic);};
        render(true,.5f);Require(!scene.RenderInterpolationReady(),"chapter first tick claimed ready");
        world.Step();const auto rng=world.RandState();const auto frame=world.Frame();const auto camera=world.Camera();const auto drawCount=world.Draws().size();
        render(true,.25f);const bool ready=scene.RenderInterpolationReady();
        const auto first=render(true,.5f),repeat=render(true,.5f);Require(first==repeat,"same tick/phase was not deterministic across repeated presentations");
        render(true,.75f);Require(world.Frame()==frame&&world.RandState()==rng&&world.Draws().size()==drawCount,"interpolation changed simulation clock, RNG or world draws");
        Require(std::memcmp(camera.view.m,world.Camera().view.m,64)==0&&std::memcmp(camera.inverse.m,world.Camera().inverse.m,64)==0,"interpolation mutated the gameplay camera");
        const auto native=render(false,.5f);render(true,.5f);const auto restored=render(false,.5f);Require(native==restored,"real chapter Off did not restore exact native transforms/order");
        std::printf("PASS chapter %d: %zu actual submissions, read-only chapter rendering, stable sub-tick transforms/vertices and exact Off (pair %s)\n",chapter,drawCount,ready?"ready":"camera cut");
    }
}
