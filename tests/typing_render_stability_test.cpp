// Advancing source-world GPU history versus fresh caches at the same snapshot,
// plus frozen-frame allocation churn for per-frame edited HUD meshes.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

#include "game/world.h"
#include "render/d3d11_device.h"
#include "scene/scene.h"

static void Require(bool ok,const char* reason) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",reason); std::exit(1); }
}
namespace {
int kWidth=320,kHeight=240;
struct Fixture {
    std::unique_ptr<port::GpuDevice> gpu;
    port::ModelRenderer renderer;
    port::AssetStore assets;
    port::Scene scene;
    std::vector<uint8_t> previousTextures;
    Fixture(const char* directory,const char* bank,bool naomi=false) {
        std::string error;
        port::GpuDeviceDesc desc;desc.width=kWidth;desc.height=kHeight;
        gpu=port::CreateD3D11Device(desc,&error);
        Require(gpu!=nullptr,error.c_str());
        Require(renderer.Init(gpu.get(),&error),error.c_str());
        Require(assets.Open(directory,&error),error.c_str());
        Require(assets.LoadTypingReference(bank,&error),error.c_str());
        if(naomi) if(const char* reference=std::getenv("HOTD2_NAOMI_REFERENCE")) {
            const bool loaded=assets.LoadNaomiVisuals(reference,&error);
            Require(loaded,error.c_str());
        }
        if (naomi) SetNaomi(true);
        port::SceneDef definition;definition.useWorld=definition.externalWorld=true;
        Require(scene.Init(assets,definition,&error),error.c_str());
    }
    void SetNaomi(bool enabled) {
        std::string error;
        renderer.ClearCache();
        const bool loaded=assets.SetNaomiGraphics(enabled, &error);
        Require(loaded, error.c_str());
        auto graphics = gpu->EffectiveGraphics();
        graphics.naomiGraphics = enabled;
        Require(gpu->ConfigureGraphics(graphics, &error), error.c_str());
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
void Capture(Fixture& fixture,const game::World& world,const std::string& name) {
    fixture.Prepare(world);
    const auto pixels=fixture.Render(world,true);
    if(name=="mission-reward") {
        auto hidden=world.Draws();bool diamond=false;
        for(auto& draw:hidden) if(draw.fn==0x0040ADA0) {
            game::Model model;std::string bank;
            Require(fixture.assets.ModelById(draw.model,model,&bank),"reward model resolves");
            Require(draw.alpha<0 && draw.light.color[0]==1 && draw.light.color[1]==1 && draw.light.color[2]==1,
                    "reward retains source transparency and neutral lighting");
            for(const auto& mesh:model.meshes)diamond|=mesh.material.DiffuseA()<1;
            draw.alpha=0;
        }
        Require(diamond,"fixture exercises the translucent diamond reward");
        Require(pixels!=fixture.Render(world,true,&hidden),"the real diamond model contributes visible facets to its reward card");
    }
    if(name=="chapter-select") {
        Require(!world.Lights().fogOn,"chapter chooser does not inherit the completed stage's blackout fog");
        Require(Visible(pixels)>290000,"the library covers the chapter-selection background");
    }
    std::printf("RENDER %s: %zu visible pixels\n",name.c_str(),Visible(pixels));
    Require(Visible(pixels)>100,"issue30 screen contains visible rendered geometry");
    const auto directory=std::filesystem::path("build/issue30");
    std::filesystem::create_directories(directory);
    std::ofstream file(directory/(name+".bmp"),std::ios::binary);
    const auto word=[&](uint32_t value,int bytes) { for(int i=0;i<bytes;++i) file.put(char(value>>(8*i))); };
    file.write("BM",2);word(54+uint32_t(pixels.size()),4);word(0,4);word(54,4);
    word(40,4);word(kWidth,4);word(uint32_t(-kHeight),4);word(1,2);word(32,2);
    word(0,4);word(uint32_t(pixels.size()),4);word(0,4);word(0,4);word(0,4);word(0,4);
    for(size_t p=0;p<pixels.size();p+=4) { file.put(char(pixels[p+2]));file.put(char(pixels[p+1]));file.put(char(pixels[p]));file.put(char(255)); }
    Require(bool(file),"save issue30 rendered screen");
    std::printf("CAPTURE %s frame %d\n",name.c_str(),world.Frame());std::fflush(stdout);
}
void Issue30(const char* directory,const char* bank) {
    kWidth=640;kHeight=480;
    game::World world;std::string error;
    Require(world.Load(directory,0,&error),error.c_str());
    game::WorldConfig cfg;cfg.portFeatures=cfg.portTyping=cfg.typingOriginal=cfg.freePlay=true;
    cfg.gameMode=6;cfg.scene=0;cfg.playerState[0]=0;cfg.seedCounter=12345;cfg.livesSetting=5;
    world.Boot(cfg);Require(world.StartStage(cfg,&error),error.c_str());
    Fixture fixture(directory,bank);
    std::set<std::string> captured;
    int countdown[3]={},instructions[2]={},firstQuota=-1;
    bool title=false,choice=false,backoff=false,quotaDecreased=false;
    const auto capture=[&](const std::string& name) {if(captured.insert(name).second)Capture(fixture,world,name);};
    for(int frame=0;frame<40000;++frame) {
        fixture.Prepare(world);
        bool instruction=false,waiting=false,missionCounter=false;
        int remaining=0;
        for(const auto& sprite:world.Hud().sprites) {
            const int raw=sprite.id-0x4000;
            if(sprite.source==0x00422760&&sprite.alpha>.5f) {title=true;if(frame>285)capture("chapter-title");}
            if(raw==0xE3F) {missionCounter=true;capture("mission-counter");}
            if(sprite.source==0x00495D50 && raw>=3554 && raw<=3563 && sprite.y==120) {
                if(sprite.x==312) remaining+=(raw-3554)*10;
                if(sprite.x==332) remaining+=raw-3554;
            }
            if(raw==0xB85||raw==0xB89) {
                instruction=true;++instructions[raw==0xB89];capture(raw==0xB85?"kuarl-instructions":"zeal-instructions");
                Require(std::all_of(world.Draws().begin(),world.Draws().end(),[](const auto& d){return d.light.color[0]<=.401f;}),
                        "instruction hold dims the actual model submissions to source brightness");
            }
            if(raw>=0xB8E&&raw<=0xB90) {waiting=true;++countdown[0xB90-raw];capture("countdown-"+std::to_string(raw-0xB8D));}
            if(raw==0xBD6 && sprite.source==0x00494280) {choice=true;capture("chapter-select");}
        }
        if(missionCounter) {
            if(firstQuota<0)firstQuota=remaining;
            quotaDecreased|=remaining<firstQuota;
        }
        if(std::any_of(world.Draws().begin(),world.Draws().end(),[](const auto& d){return d.fn==0x0040ADA0;}))capture("mission-reward");
        if(world.Hud().canShoot && std::any_of(world.Draws().begin(),world.Draws().end(),[](const auto& d){return d.fn==0x004ACB40;}))capture("original-inventory");
        for(const auto& enemy:world.Enemies(false,true)) if(enemy.kind==0x44 && enemy.behaviour==1 && enemy.sub==3)backoff=true;
        if(choice)break;
        const auto prompts=world.PortTypingPrompts();
        if(instruction||waiting) {
            const auto bosses=world.Enemies(false,true);
            world.PortTypingInput("PREMATURE INPUT");
            world.SetInput(0,{});world.SetInput(1,{});world.Step();
            for(const auto& before:bosses) if(before.kind==0x45)
                for(const auto& after:world.Enemies(false,true)) if(after.serial==before.serial)
                    Require(after.hp==before.hp,"typing during instructions/countdown cannot damage Judgment");
            continue;
        }
        const auto prompt=std::find_if(prompts.begin(),prompts.end(),[](const auto& p){return !p.waiting;});
        if(prompt!=prompts.end()) world.PortTypingInput(prompt->text.substr(prompt->progress));
        bool confirm=world.Hud().stageResults.active && world.Hud().stageResults.frames>=game::StageResults::kMinimumFrames;
        for(const auto& task:world.Tasks()) if(task.fn==0x0047DFC0)confirm=true;
        if(world.PortTypingNameActive()) {world.PortTypingInput("TEST");confirm=true;}
        if(world.Hud().player[0].state!=5)confirm=true;
        game::WorldInput input;input.pressed=input.held=confirm&&frame%8==0?8:0;
        world.SetInput(0,input);world.SetInput(1,{});world.Step();
    }
    Require(title&&choice,"chapter title and chapter selection render in a real completed chapter");
    Require(instructions[0]>=120&&instructions[1]>=120,"both Judgment instruction phases retain their full hold");
    Require(countdown[0]>=60&&countdown[1]>=60&&countdown[2]>=60,"Zeal renders all three full countdown seconds");
    Require(backoff,"Kuarl backs away after a completed phrase");
    Require(firstQuota>0 && quotaDecreased,"the visible mission kill counter decreases as enemies are defeated");
    Require(captured.count("mission-counter")&&captured.count("mission-reward")&&captured.count("original-inventory"),
            "mission HUD, reward model and Original inventory appear in actual gameplay");
    std::puts("PASS issue30 chapter-one timing, blocked pretyping, retreat, mission and chapter visuals");
    // Fail the authored first mission by leaving its enemies unanswered.
    game::World failed;Require(failed.Load(directory,0,&error),error.c_str());
    failed.Boot(cfg);Require(failed.StartStage(cfg,&error),error.c_str());
    bool mission=false,failure=false,advanced=false;int failureFrame=0,scene=0,variant=0;
    for(int frame=0;frame<18000;++frame) {
        fixture.Prepare(failed);
        for(const auto& sprite:failed.Hud().sprites) {
            if(sprite.id==0x4000+0xE3F)mission=true;
            if(sprite.id==0x4000+3052 && !failure) {
                failure=true;failureFrame=frame;scene=failed.Events().scene;variant=failed.Events().variant;
                Require(failed.LiveEnemies()==0,"failed authored mission immediately clears its surviving enemies");
                Capture(fixture,failed,"mission-failed");
            }
        }
        if(failure && frame-failureFrame>180 && (failed.Events().scene!=scene || failed.Events().variant!=variant)) {advanced=true;break;}
        if(!mission) {
            const auto prompts=failed.PortTypingPrompts();
            const auto next=std::find_if(prompts.begin(),prompts.end(),[](const auto& p){return !p.waiting;});
            if(next!=prompts.end())failed.PortTypingInput(next->text.substr(next->progress));
        }
        failed.SetInput(0,{});failed.SetInput(1,{});failed.Step();
    }
    Require(mission&&failure&&advanced,"failed mission releases the script and continues the campaign");
    std::puts("PASS authored mission failure: enemies cleared, no post-failure attacks, next scene reached");
}
}
int main(int argc,char** argv) {
    const bool issue30=argc==4 && std::strcmp(argv[3],"--issue30")==0;
    Require(argc==3 || (argc==4 && (std::strcmp(argv[3],"--naomi")==0 || issue30)),
            "usage: typing_render_stability_test <game directory> <prepared bank> [--naomi|--issue30]");
    const bool naomi = argc==4 && !issue30;
#ifdef _WIN32
    _putenv_s("HOTD2_TYPING_REFERENCE",argv[2]);
#else
    setenv("HOTD2_TYPING_REFERENCE",argv[2],1);
#endif
    if(issue30) {Issue30(argv[1],argv[2]);return 0;}
    std::string error;
    game::World world;
    Require(world.Load(argv[1],0,&error),error.c_str());
    game::WorldConfig cfg;
    cfg.portFeatures=cfg.portTyping=cfg.freePlay=true;
    cfg.gameMode=6; cfg.gameType=0; cfg.eventMode=cfg.scene=0;
    cfg.playerState[0]=0; cfg.seedCounter=12345; cfg.livesSetting=5;
    world.Boot(cfg);
    Require(world.StartStage(cfg,&error),error.c_str());
    Fixture persistent(argv[1],argv[2],naomi);
    unsigned comparisons=0;
    for(;;) {
        persistent.Prepare(world);
        const int frame=world.Frame();
        const bool checkpoint=frame==1500 || frame==2000 || frame==2800 || frame==3100;
        const auto pixels=persistent.Render(world,checkpoint);
        if(checkpoint) {
            Fixture fresh(argv[1],argv[2],naomi);fresh.Prepare(world);
            const auto reference=fresh.Render(world,true);
            Equal(pixels,reference,"persistent advancing scene/cache differs from fresh rendering of identical World",frame);
            ++comparisons;
            std::printf("PASS advancing frame %d: persistent/fresh exact across %zu visible pixels\n",frame,Visible(pixels));
            if(frame==2800) {
                AllocationChurn(persistent,world,pixels);
                if (naomi) {
                    persistent.SetNaomi(false);persistent.Prepare(world);
                    const auto pc=persistent.Render(world,true);
                    Require(pc!=pixels,"Naomi changes the actual Typing scene pixels");
                    persistent.SetNaomi(true);persistent.Prepare(world);
                    Equal(pixels,persistent.Render(world,true),"returning to Naomi changed the Typing frame",frame);
                }
            }
            std::fflush(stdout);
        }
        if(frame>=3200)break;
        world.SetInput(0,{});world.SetInput(1,{});world.Step();
    }
    Require(comparisons==4,"all advancing render checkpoints were reached");
    std::puts("PASS source intro/first-fight rendered every simulation frame through 3200; fresh-cache checkpoints exact; default D3D11, no TAA/interpolation");
}
