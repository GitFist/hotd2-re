// The real 30-SHOT CLEAR script must finish its results even after the last life.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/save_data.h"
#include "game/world_internal.h"

namespace {
int checks = 0;
void Check(bool ok, const char* why, const game::World* world = nullptr) {
    ++checks;
    if (ok) return;
    std::fprintf(stderr,"FAIL: %s",why);
    if(world) std::fprintf(stderr," (frame%d scene%d state%d/%d lives%d/%d running%d ip%X)",
        world->Frame(),world->Events().scene,world->Hud().player[0].state,world->Hud().player[1].state,
        world->Hud().player[0].lives,world->Hud().player[1].lives,world->Events().running,world->Events().script);
    std::fputc('\n',stderr);std::exit(1);
}
void Step(game::World& world, int player = 0, uint32_t keys = 0, int x = 639, int y = 0, bool shot = false) {
    world.SetInput(0,{});world.SetInput(1,{});
    game::WorldInput input;
    input.kind=1;input.x=x;input.y=y;input.fired=shot;
    input.pressed=input.held=keys|(shot?12u:0u);
    world.SetInput(player,input);world.Step();
}
void Wait(game::World& world,int frames=30) {while(frames--)Step(world);}
bool Task(const game::World& world,uint32_t fn) {
    for(const auto& task:world.Tasks())if(task.fn==fn)return true;
    return false;
}
void RunningRules() {
    game::World::Impl world;
    world.runner=std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
    world.gameMode=6;world.subState=2;
    for(int type:{0,1,2,3}) {
        world.cfg.gameType=world.runner->cfg.gameType=type;
        for(int slot=0;slot<2;++slot) {
            for(auto& p:world.player){p.state=9;p.lives=0;}
            world.player[slot].state=5;
            Check(world.GameRunning()==(type==2),"only Challenge scripts continue with their remaining player at zero lives");
            world.player[slot].lives=1;
            world.player[1-slot].state=5;
            Check(world.GameRunning(),"each playing slot with a life keeps its script running");
        }
        for(auto& p:world.player){p.state=5;p.lives=0;}
        Check(world.GameRunning()==(type==2),"both zero-life Challenge players keep the result script alive");
        for(auto& p:world.player)p.state=4;
        Check(!world.GameRunning(),"continue countdown does not release gameplay waits");
    }
}

void StartLesson(game::World& world,const char* directory,int players) {
    std::string error;
    Check(world.Load(directory,0,&error),error.c_str());
    game::WorldConfig config;
    config.portFeatures=config.portModernMenu=true;
    config.savePatch.push_back({game::SaveData::kTrainGrades,std::vector<uint8_t>(10,1)});
    world.Boot(config);Wait(world,650);Step(world,0,8);Wait(world,4);
    Check(world.GameMode()==4,"main menu opens",&world);
    Step(world,0,0,40,175,true);Wait(world,2); // Play
    Step(world,0,0,40,233,true);Wait(world,600); // Challenges
    Check(world.Hud().gameType==2&&world.Events().scene==0,"Challenge carousel opens",&world);
    Step(world,0,0,480,194,true);Wait(world); // 30-SHOT CLEAR
    Step(world,0,0,540,459,true);Wait(world,80);
    Check(world.Events().scene==3&&Task(world,0x00497440),"real 30-SHOT CLEAR instructions open",&world);
    const auto selectedScene=world.Events().globals.at(0x009A2BBC);

    // Retain the lesson selected through the public menu, while starting with
    // each supported local-player arrangement. StartStage replaces the VM, so
    // retain the carousel's chosen retry scene along with its selected lesson.
    config.gameMode=6;config.gameType=2;config.eventMode=6;config.scene=3;config.seedCounter=12345;
    config.playerState[0]=(players&1)?0:9;config.playerState[1]=(players&2)?0:9;
    Check(world.StartStage(config,&error),error.c_str());
    const_cast<game::EventRunner&>(world.Events()).globals[0x009A2BBC]=selectedScene;
    Wait(world,60);
    const int controller=(players&1)?0:1;
    Step(world,controller,8);
    for(int frame=0;frame<600&&!world.Hud().canShoot;++frame)Step(world);
    Check(world.Hud().canShoot,"Ready/Go reaches the playable mission",&world);
    for(int slot=0;slot<2;++slot)if(players&(1<<slot))
        Check(world.Hud().player[slot].state==5&&world.Hud().player[slot].lives==2,
              "the mission gives each active player its two lives",&world);
}

enum class Exit { Retry, Quit, Escape };
void FailedLesson(const char* directory,int players,Exit exit) {
    game::World world;StartLesson(world,directory,players);
    int failure=-1;
    for(int frame=0;frame<6000;++frame) {
        Step(world);
        if(Task(world,0x00497DB0)){failure=world.Frame();break;}
    }
    Check(failure>=0,"missing the real enemies reaches Mission Failed",&world);
    Check(((players&1)&&world.Hud().player[0].lives==0)||((players&2)&&world.Hud().player[1].lives==0),
          "failure includes a zero-life player",&world);
    Wait(world,90);
    Check(Task(world,0x00497DB0)&&world.Events().running,"failed results still execute the stage script",&world);
    int lit=0;
    for(const auto& draw:world.Draws())if(draw.fn==0x00497DB0&&draw.drawMode==8)
        if(draw.light.intensity>0&&(draw.light.color[0]>0||draw.light.color[1]>0||draw.light.color[2]>0))++lit;
    Check(lit>0,"results restore the agent lighting instead of leaving a black silhouette",&world);
    const int controller=(players&1)?0:1;
    if(exit==Exit::Escape)Step(world,controller,2); // Port Escape maps to the result's Back action.
    else Step(world,controller,0,exit==Exit::Retry?340:510,382,true);
    bool left=false;
    for(int frame=0;frame<600;++frame) {
        Step(world);
        if(exit==Exit::Retry) {
            if(world.Events().scene==3&&Task(world,0x00497440)){left=true;break;}
        } else if(world.Events().scene==0&&Task(world,0x00496DE0)){left=true;break;}
    }
    Check(left,exit==Exit::Retry?"Retry returns to the same lesson instructions":"Quit/Back returns to Challenge selection",&world);
    std::printf("PASS 30-SHOT CLEAR loss: player mask%d, %s\n",players,
        exit==Exit::Retry?"Retry":exit==Exit::Quit?"Quit":"Esc/Back");
}
}
int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: challenge_failure_test <game dir>\n");return 2;}
    RunningRules();
    for(int players:{1,2})for(Exit action:{Exit::Retry,Exit::Quit,Exit::Escape})FailedLesson(argv[1],players,action);
    std::printf("PASS Challenge failure regression: %d checks\n",checks);
}
