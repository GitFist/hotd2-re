// Advancing source-world GPU history versus fresh caches at the same snapshot,
// plus frozen-frame allocation churn for per-frame edited HUD meshes.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/world.h"
#include "render/d3d11_device.h"
#include "scene/scene.h"

static void Require(bool ok,const char* reason) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); }
}
namespace {
constexpr int kWidth=320,kHeight=240;
struct Fixture {
    std::unique_ptr<port::GpuDevice> gpu;
    port::ModelRenderer renderer;
    port::AssetStore assets;
    port::Scene scene;
    std::vector<uint8_t> previousTextures;
    Fixture(const char* directory,const char* bank) {
        std::string error;
        port::GpuDeviceDesc desc;desc.width=kWidth;desc.height=kHeight;
        gpu=port::CreateD3D11Device(desc,&error);
        Require(gpu!=nullptr,error.c_str());
        Require(renderer.Init(gpu.get(),&error),error.c_str());
        Require(assets.Open(directory,&error),error.c_str());
        Require(assets.LoadTypingReference(bank,&error),error.c_str());
        port::SceneDef definition;definition.useWorld=definition.externalWorld=true;
        Require(scene.Init(assets,definition,&error),error.c_str());
    }
    void Prepare(const game::World& world) {
        // Same frame/epoch, texture-state and pol-load prefetch flow as play.cpp.
        if(scene.PrepareWorld(renderer,world))previousTextures.clear();
        const auto& states=world.Hud().texFiles;
        for(size_t file=0;file<states.size();++file)
            if(states[file] && (file>=previousTextures.size() || !previousTextures[file]))
                assets.PrefetchFile(int(file));
        previousTextures=states;
        for(int file:world.Hud().polFileLoads) {
            const auto name=assets.PolFileName(uint32_t(file));
            if(!name.empty())assets.PrefetchPolFile(name);
        }
        for(const auto* image:assets.DrainPrefetched(4))renderer.WarmTexture(image);
    }
    std::vector<uint8_t> Render(const game::World& world,bool read,
                               const std::vector<game::WorldDraw>* draws=nullptr) {
        const auto vp=scene.BuildFromWorld(renderer,world,draws);
        const auto sprites=scene.MakeSpriteDraws(world.Hud().sprites,.8f,&world.Hud().texFiles);
        const float clear[]={0,0,0,1};
        Require(gpu->BeginFrame(clear),"begin source GPU frame");
        renderer.DrawGame(kWidth,kHeight,vp.m,&scene.lighting,&scene.fog,scene.view.m,&sprites,&scene.itemLights,scene.projection.m);
        std::vector<uint8_t> pixels;
        if(read)Require(gpu->ReadPixels(&pixels),"read source GPU frame");
        gpu->EndFrame();return pixels;
    }
};
void Equal(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const char* reason,int frame,int count=0) {
    Require(a.size()==b.size(),"source image dimensions remain stable");
    size_t changed=0;
    for(size_t p=0;p<a.size();p+=4)changed+=std::memcmp(a.data()+p,b.data()+p,3)!=0;
    if(changed)std::fprintf(stderr,"frame %d copies %d: %zu differing pixels\n",frame,count,changed);
    Require(changed==0,reason);
}
size_t Visible(const std::vector<uint8_t>& pixels) {
    size_t result=0;
    for(size_t p=0;p<pixels.size();p+=4)result+=(pixels[p]||pixels[p+1]||pixels[p+2]);
    return result;
}
void AllocationChurn(Fixture& fixture,const game::World& world,const std::vector<uint8_t>& baseline) {
    const auto fixed=world.Draws();
    const auto edited=std::find_if(fixed.begin(),fixed.end(),[](const auto& draw) {
        return draw.edit && draw.skinCount==0 && draw.viewSpace;
    });
    Require(edited!=fixed.end(),"source poster contributes a real per-frame edited mesh");
    Require(Visible(baseline)>5000,"source frame contains visible world and actors");
    for(int extras:{1,16,3,48,0,25,2,64}) {
        auto draws=fixed;
        for(int i=0;i<extras;++i) {
            auto draw=*edited;
            draw.task=0xF0000000u+unsigned(i);draw.world.m[12]+=100000;
            auto edit=std::make_shared<game::ModelEdit>(*draw.edit);edit->diffuse=float(i%7)/6;
            draw.edit=std::move(edit);draws.push_back(std::move(draw));
        }
        Equal(baseline,fixture.Render(world,true,&draws),"edited HUD allocation churn changed frozen world / actor pixels",world.Frame(),extras);
    }
    std::printf("PASS frozen typing frame %d: %zu visible pixels stable across edited HUD allocations 1..64\n",world.Frame(),Visible(baseline));
}
}
int main(int argc,char** argv) {
    Require(argc==3,"usage: typing_render_stability_test <game directory> <prepared bank>");
#ifdef _WIN32
    _putenv_s("HOTD2_TYPING_REFERENCE",argv[2]);
#else
    setenv("HOTD2_TYPING_REFERENCE",argv[2],1);
#endif
    std::string error;
    game::World world;
    Require(world.Load(argv[1],0,&error),error.c_str());
    game::WorldConfig cfg;
    cfg.portFeatures=cfg.portTyping=cfg.freePlay=true;
    cfg.gameMode=6; cfg.gameType=0; cfg.eventMode=cfg.scene=0;
    cfg.playerState[0]=0; cfg.seedCounter=12345; cfg.livesSetting=5;
    world.Boot(cfg);
    Require(world.StartStage(cfg,&error),error.c_str());
    Fixture persistent(argv[1],argv[2]);
    unsigned comparisons=0;
    for(;;) {
        persistent.Prepare(world);
        const int frame=world.Frame();
        const bool checkpoint=frame==1500 || frame==2000 || frame==2800 || frame==3100;
        const auto pixels=persistent.Render(world,checkpoint);
        if(checkpoint) {
            Fixture fresh(argv[1],argv[2]);fresh.Prepare(world);
            const auto reference=fresh.Render(world,true);
            Equal(pixels,reference,"persistent advancing scene/cache differs from fresh rendering of identical World",frame);
            ++comparisons;
            std::printf("PASS advancing frame %d: persistent/fresh exact across %zu visible pixels\n",frame,Visible(pixels));
            if(frame==2800)AllocationChurn(persistent,world,pixels);
            std::fflush(stdout);
        }
        if(frame>=3200)break;
        world.SetInput(0,{});world.SetInput(1,{});world.Step();
    }
    Require(comparisons==4,"all advancing render checkpoints were reached");
    std::puts("PASS source intro/first-fight rendered every simulation frame through 3200; fresh-cache checkpoints exact; default D3D11, no TAA/interpolation");
}
