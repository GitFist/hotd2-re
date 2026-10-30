#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
}
int main(int argc, char** argv) {
    const char* path = argc >= 2 ? argv[1] : std::getenv("HOTD2_TYPING_REFERENCE");
    Require(path && *path,"supply the prepared PC reference bank");
    game::World::Impl world;
    std::string error;
    Require(world.typingReference.Load(path,&error),error.c_str());
    world.cfg.portTyping = true;
    world.typingReferenceActive = true;
    world.residency = true;
    world.modelLoaded.assign(0x8000,0);
    const int sourceBody = world.typingReference.SourceModel(0x471);
    const int file = world.typingReference.ModelFile(sourceBody);
    Require(file >= 0,"Strength body has its authored prepared resource");
    world.ProcessRequest({0x52,uint32_t(file)});
    Require(world.modelLoaded[sourceBody] != 0 && world.modelLoaded[0x440] == 0,
            "source pol load marks its actual model identities resident");
    {
        game::World::Impl::NativeModelDrawScope scope(world,0x4A,1);
        const auto* draw = world.Draw(0x440,game::Mat4::Identity());
        Require(draw && draw->model == sourceBody && draw->skinModel == 0x440,
                "native Strength body draw binds to resident source geometry and retains its deformation identity");
        const auto* prepared = world.Draw(sourceBody,game::Mat4::Identity());
        Require(prepared && prepared->model == sourceBody,"already prepared model ids remain unchanged");
    }
    world.modelLoaded[0x440] = 4;
    const auto* unscoped = world.Draw(0x440,game::Mat4::Identity());
    Require(unscoped && unscoped->model == 0x440,"event and HUD draws outside the native enemy scope keep their authored id");
    for (const auto& row : {std::array<int,3>{0x39,0x14B3,0x152F}, {0x3A,0x14C3,0x153F},
                           {0x3B,0x14DA,0x1556}, {0x39,0x14BB,0x1537},
                           {0x3A,0x14CB,0x1547}, {0x3B,0x14D3,0x154F}}) {
        for (int frame=0;frame<=6;++frame) {
            const int face=world.typingReference.SourceModel(row[2]+frame);
            Require(face>0,"authored talking and blinking frame resolves");
            const int faceFile=world.typingReference.ModelFile(face);
            Require(faceFile>=0,"protagonist face has its prepared resource");
            world.ProcessRequest({0x52,uint32_t(faceFile)});
            game::World::Impl::NativeModelDrawScope scope(world,row[0],2);
            const auto* draw=world.Draw(row[1]+frame,game::Mat4::Identity());
            Require(draw && draw->model==face,"talking and blinking callback draws the resident source frame");
        }
    }
    {
        world.runner=std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
        game::EnemyTask enemy;
        enemy.anim.kind=1; enemy.anim.nodes[2].model=0x1B93; enemy.anim.nodes[2].hit[1]=10;
        const int head=world.typingReference.NativeEnemyModel(1,2,0x1B93);
        Require(head>=0x4000,"detached-head fixture uses a changed source model");
        world.ProcessRequest({0x52,uint32_t(world.typingReference.ModelFile(head))});
        Require(!world.modelLoaded[0x1B93],"raw native head remains nonresident");
        world.draws.clear();
        game::SetCurrentWorld(&world);
        world.SpawnFlyingHead(enemy);
        world.ts.RunFrame();
        game::SetCurrentWorld(nullptr);
        Require(std::any_of(world.draws.begin(),world.draws.end(),[&](const auto& draw) {
                    return draw.fn==0x0040A230 && draw.model==head;
                }),"flying head retains the originating skeleton binding after detachment");
        world.ts.Reset();
    }
    world.cfg.portTyping = false;
    {
        game::World::Impl::NativeModelDrawScope scope(world,0x4A,1);
        const auto* draw = world.Draw(0x440,game::Mat4::Identity());
        Require(draw && draw->model == 0x440,"native gun mode keeps its original skeleton geometry");
    }
    if (argc>=3) {
        Require(world.exe.Load((std::filesystem::path(argv[2])/"Hod2.exe").string()),"load native model file tables");
        Require(world.typingReference.SourceModel(0x34)==0x31 && world.typingReference.SourceModel(0x35)==0x32 &&
                !world.typingReference.PolModels(0x47),"identical car geometry aliases retain the native file loader");
        for (int id:{0x31,0x32,0x34,0x35}) world.modelLoaded[id]=0;
        world.ProcessRequest({0x52,0x47}); // supplied source car_1 file4D maps to native file47
        for (int id:{0x31,0x32,0x34,0x35})
            Require(world.modelLoaded[id]!=0,"native car file load retains both matched and manually drawn duplicate slots");
        Require(world.cameraBank.Load(world.exe,[&](const std::string& name) {
                    std::ifstream file(std::filesystem::path(argv[2])/"cam"/name,std::ios::binary);
                    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),{});
                }),"load native intro paths for the player placement comparison");
        Require(world.typingReference.ApplyStage(0,world.mem,world.cameraBank,&error),error.c_str());
        Require(world.typingReference.SourceModel(0x33)==0x33 && world.modelLoaded[0x33]!=0,
                "the Judgment vehicle body retains its resident canonical car model");
        for (int index=0;index<2;++index) {
            const int nativePath=world.exe.S16(0x00589AE0+index*4);
            const int start=world.exe.S16(0x00589AE2+index*4);
            const int end=int(world.exe.U32(0x00576D38+nativePath*4));
            for (bool typing:{false,true}) {
                world.ts.Reset();
                world.cfg.portTyping=typing;
                world.typingReferenceActive=typing;
                world.runner->camera.camId=typing?0x403F:0x2F;
                world.runner->camera.tick=100;
                auto* spawn=world.NewTask<game::SpawnTask>([](game::Task& task) {
                    auto& current=*game::CurrentWorld();
                    current.SpawnSt1b(task,task.As<game::SpawnTask>());
                },0);
                spawn->rec.type=0x28;
                spawn->rec.state=uint16_t(index);
                game::SetCurrentWorld(&world);
                const auto checkCar=[&](int tick) {
                    world.draws.clear();
                    world.ts.RunFrame();
                    const auto car=std::find_if(world.draws.begin(),world.draws.end(),[](const auto& draw) {
                        return draw.fn==0x00432610 && draw.model==0x33;
                    });
                    Require(car!=world.draws.end(),"both Judgment cars submit their resident bodies before removal");
                    float position[3]={},rotation[3]={};int roll=0;
                    Require(world.cameraBank.Evaluate(nativePath,float(tick),position,rotation,&roll,false),
                            "native Judgment vehicle path is available");
                    game::Mat4 expected=game::Mat4::Identity();
                    game::mx::Translate(expected,position[0],position[1],position[2]);
                    game::mx::RotZ(expected,int(rotation[2]));
                    game::mx::RotY(expected,int(rotation[1]));
                    game::mx::RotX(expected,int(rotation[0]));
                    for (int element=0;element<16;++element)
                        Require(std::fabs(car->world.m[element]-expected.m[element])<0.0001f,
                                "prepared Judgment cars retain their authored native position and orientation");
                };
                checkCar(start);
                world.runner->camera.tick=start+20;
                checkCar(start+20);
                world.runner->camera.tick=end;
                world.draws.clear();
                world.ts.RunFrame();
                Require(world.ts.Live().empty() && world.draws.empty(),
                        "Judgment vehicles and their flames end before the car body resource is unloaded");
                game::SetCurrentWorld(nullptr);
            }
        }
        world.players=2;
        for (int p=0;p<2;++p) {
            world.PlayerBodyInit(p);
            game::Task playerTask;
            auto data=std::make_unique<game::PlayerTask>();
            data->index=p;
            playerTask.data=std::move(data);
            auto& body=*world.playerBody[p];
            for (const auto& phase:{std::array<int,2>{0x20,100},{0x21,100},{0x21,300},{0x22,12}}) {
                const auto place=[&](bool typing) {
                    world.cfg.portTyping=typing;
                    world.typingReferenceActive=typing;
                    world.runner->camera.camId=phase[0]+(typing?0x4010:0);
                    world.runner->camera.tick=phase[1];
                    world.player[p].cb80=0x00415BD0;
                    body.pos[0]=9876.0f;body.pos[1]=5.0f;body.pos[2]=-9876.0f;
                    body.rot[0]=body.rot[1]=body.rot[2]=0;
                    world.AnimSetMotion(body,p==0?0x322:0x34A);
                    world.PlayerCb80(playerTask);
                };
                place(false);
                const std::array<float,3> nativePosition{body.pos[0],body.pos[1],body.pos[2]};
                const std::array<int,3> nativeRotation{body.rot[0],body.rot[1],body.rot[2]};
                const auto nativeCallback=world.player[p].cb80;
                const int nativeMotion=body.anim.motion[0];
                place(true);
                for (int axis=0;axis<3;++axis) {
                    Require(std::fabs(body.pos[axis]-nativePosition[axis])<0.0001f,
                            "prepared intro places both player bodies on the matching native car paths");
                    Require(body.rot[axis]==nativeRotation[axis],"prepared intro retains the authored player body orientation");
                }
                Require(world.player[p].cb80==nativeCallback && body.anim.motion[0]==nativeMotion,
                        "prepared intro camera32 transitions both players to their final held motion");
            }
            const int torso=world.typingReference.SourceModel(p==0?0x1616:0x162A);
            const int face=world.typingReference.SourceModel(p==0?0x1536:0x1546);
            world.ProcessRequest({0x52,uint32_t(world.typingReference.ModelFile(torso))});
            world.player[p].cb7C=0x00415120;
            world.player[p].flags12C|=1;
            world.draws.clear();
            world.PlayerCb7C(playerTask);
            for (int model:{torso,face})
                Require(std::any_of(world.draws.begin(),world.draws.end(),[&](const auto& draw) {return draw.model==model;}),
                        "both player body callbacks submit their resident prepared torso and face");
        }
#ifdef _WIN32
        _putenv_s("HOTD2_TYPING_REFERENCE",path);
#else
        setenv("HOTD2_TYPING_REFERENCE",path,1);
#endif
        auto campaign=std::make_unique<game::World>();
        Require(campaign->Load(argv[2],0,&error),error.c_str());
        game::WorldConfig cfg;
        cfg.portTyping=true; cfg.portFeatures=true; cfg.eventMode=0; cfg.scene=0; cfg.gameMode=6;
        cfg.playerState[0]=0; cfg.playerState[1]=9; cfg.seedCounter=12345; cfg.livesSetting=5;
        cfg.startFrame=901; // the direct scene launcher uses these capture frame labels
        campaign->Boot(cfg);
        Require(campaign->StartStage(cfg,&error),error.c_str());
        bool sawCar=false,sawActor=false;
        while (campaign->Frame()<2100) {
            campaign->Step();
            if (campaign->Frame()<1400) continue;
            bool sawJamesBody=false,sawJamesFace=false;
            for (const auto& draw:campaign->Draws()) {
                sawCar |= draw.fn==0x0048E600 && draw.model==world.typingReference.SourceModel(0x1603);
                sawActor |= draw.fn==0x0049B260;
                sawJamesBody |= draw.model==world.typingReference.SourceModel(0x1616);
                sawJamesFace |= draw.model==world.typingReference.SourceModel(0x1536);
            }
            if (campaign->Frame()==1500)
                Require(sawJamesBody && sawJamesFace,"actual opening submits James's prepared body and face while seated in the car");
            if (campaign->Frame()==1850)
                Require(sawJamesBody && sawJamesFace,"actual opening submits James's prepared body and face after leaving the car");
        }
        Require(sawCar,"source opening submits the resident car under cameras4030..4032");
        Require(sawActor,"source opening actor remains visible on camera4031/4032");
    }
    std::puts("typing_model_bindings_test: OK");
}
