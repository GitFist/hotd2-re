// Real UDP inputs through two complete final-chapter Randomizer worlds.
#include "game/world.h"
#include "game/world_modes.h"
#include "game/mstack.h"
#include "net/netplay.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>

namespace {
using port::net::FrameInput;
using port::net::Session;
void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
void Input(game::World& w, int p, unsigned buttons=0) {
    game::WorldInput input; input.held=input.pressed=buttons; w.SetInput(p,input);
}
void Start(game::World& w,const char* dir,int seed) {
    Require(w.Load(dir),"load Randomizer world");
    game::WorldConfig cfg;
    cfg.portFeatures=cfg.portModernMenu=cfg.portNetworkRoom=true;
    cfg.gameMode=6;cfg.gameOver=true;cfg.startFrame=901;cfg.seedCounter=seed;cfg.creditSetting=-1;
    Require(w.StartNetworkGame(cfg),"start shared room");
    for(int i=0;i<5;++i) { Input(w,0,0x20);w.Step();Input(w,0);w.Step(); }
    Input(w,0,4);w.Step();Input(w,0);w.Step();
    Require(w.Hud().randomizerSeed!=0,"select Randomizer");
    cfg.portNetworkRoom=false;cfg.gameType=1;cfg.eventMode=5;
    cfg.scene=game::SceneSelectFirstScene(5);cfg.startFrame=w.Frame()+1;cfg.seedCounter=cfg.startFrame;
    cfg.playerState[0]=cfg.playerState[1]=0;cfg.freePlay=true;cfg.creditSetting=-1;
    Require(w.StartStage(cfg),"load the final Randomizer chapter");
}
// The signature deliberately includes all geometry as well as persistent gameplay
// state, so equal transport input cannot conceal a later boss/render divergence.
uint32_t Hash(const game::World& w) {
    uint32_t hash=2166136261u;
    auto mix=[&](const void* data,size_t size) {
        for(const auto* b=static_cast<const unsigned char*>(data);size--;++b) { hash^=*b;hash*=16777619u; }
    };
    const auto& hud=w.Hud();
    const unsigned state[]={unsigned(w.Frame()),unsigned(w.GameMode()),w.RandState(),hud.randomizerSeed,hud.randomizerHash,
        unsigned(hud.player[0].state),unsigned(hud.player[0].lives),unsigned(hud.player[0].score),unsigned(hud.player[0].ammo),
        unsigned(hud.player[1].state),unsigned(hud.player[1].lives),unsigned(hud.player[1].score),unsigned(hud.player[1].ammo)};
    mix(state,sizeof(state));
    for(const auto& draw:w.Draws()) {
        mix(&draw.model,sizeof(draw.model));mix(draw.world.m,sizeof(draw.world.m));
        mix(&draw.node,sizeof(draw.node));mix(&draw.alpha,sizeof(draw.alpha));
    }
    return hash;
}
FrameInput Aim(game::World& w,int p,int frame,uint8_t& previous) {
    static int lastX[2]={320,320},lastY[2]={216,216};
    int sx=lastX[p],sy=lastY[p];bool found=false;
    const bool aimNow=w.Hud().canShoot&&w.Hud().player[p].state==5&&w.Hud().player[p].ammo>0&&frame%4==p*2;
    const auto targets=aimNow?w.ShotTargets():std::vector<game::World::ShotTargetInfo>{};
    // Shoot hostile projectiles first, then cycle exposed body nodes so armor
    // does not strand the driver at a boss while both actual game worlds run.
    for(int pass=0;pass<2&&!found;++pass) for(const auto& target:targets) {
        if(target.civilian || (target.flags&0x04000000u) || (target.nodes.empty()?0:1)!=pass)continue;
        // Emperor's vulnerable core is offset from the collision node center.
        // Aim at the authored point rather than repeatedly shooting its armor.
        if(target.fn==0x00426C30) for(const auto& draw:w.Draws()) {
            if(draw.fn!=target.fn||draw.node!=1)continue;
            const float offset[3]={2.3121f,.1097f,0};float at[3];game::mx::Transform(draw.world,offset,at);
            if(!draw.viewSpace) {float view[3];game::mx::Transform(w.Camera().view,at,view);std::memcpy(at,view,sizeof(at));}
            if(at[2]>=-1)continue;
            const int x=int(320+at[0]*w.Focal()/-at[2]),y=int(240-at[1]*w.Focal()/-at[2]);
            if(x<0||x>=640||y<0||y>=480)continue;
            const auto shot=w.PreviewShot(p,x,y);
            if(shot.serial!=target.serial||shot.node!=1)continue;
            sx=lastX[p]=x;sy=lastY[p]=y;found=true;break;
        }
        if(found)break;
        const size_t count=std::max<size_t>(1,target.nodes.size());
        for(size_t i=0;i<count;++i) {
            const auto at=target.nodes.empty()?std::array<float,4>{target.view[0],target.view[1],target.view[2],target.radius}:
                target.nodes[(i+size_t(frame/60))%count];
            if(at[2]>=-1)continue;
            const int x=int(320+at[0]*w.Focal()/-at[2]),y=int(240-at[1]*w.Focal()/-at[2]);
            if(x<0||x>=640||y<0||y>=480)continue;
            const auto shot=w.PreviewShot(p,x,y);
            const int weakNode=target.fn==0x0042C8D0?1:target.fn==0x0042D530?2:target.fn==0x0042CDD0?0x18:-1;
            if(shot.serial!=target.serial||(weakNode>=0&&shot.node!=weakNode))continue;
            sx=lastX[p]=x;sy=lastY[p]=y;found=true;break;
        }
        if(found)break;
    }
    unsigned buttons=0;
    if(w.Hud().player[p].state==4)buttons=frame%10==0?4:0;
    else if(w.Hud().stageResults.active)buttons=frame%60==0?4:0;
    else if(w.Hud().canShoot)buttons=w.Hud().player[p].ammo==0?2:(found?1:0);
    FrameInput input;input.present=true;input.in=game::MakeWorldInput(sx/640.f,sy/480.f,uint8_t(buttons),previous);
    previous=uint8_t(buttons);return input;
}
}
int main(int argc,char** argv) {
    Require(argc==2||argc==3,"usage: randomizer_network_test <game dir> [seed]");
    const int seed=argc==3?std::atoi(argv[2]):12345;
    game::World hostWorld,guestWorld;Start(hostWorld,argv[1],seed);Start(guestWorld,argv[1],seed);
    Session host,guest;std::string error;host.SetBuild(998878);guest.SetBuild(998878);
    Require(host.Host(0,&error),error.c_str());uint8_t ip[4];uint16_t port=0;
#ifdef _WIN32
    _putenv_s("HOTD2_NET_LOCALCODE","1");
#else
    setenv("HOTD2_NET_LOCALCODE","1",1);
#endif
    Require(host.QueryPublicEndpoint(&error),error.c_str());
    Require(port::net::DecodeCode(host.LocalCode(),ip,&port),"decode isolated loopback endpoint");
    port::net::SessionConfig cfg;cfg.startFrame=hostWorld.Frame();cfg.delay=1;cfg.seed=seed;
    host.SetConfig(cfg);host.SetDelayPolicy(1,false);
    Require(guest.Join("127.0.0.1",port,&error),error.c_str());
    auto pump=[&] {host.Poll();guest.Poll();host.Send();guest.Send();};
    auto until=[&](auto ready,const char* why) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!ready()&&std::chrono::steady_clock::now()<deadline) {pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        Require(ready(),why);
    };
    const uint32_t first=cfg.startFrame+cfg.delay;
    until([&]{return guest.Connected();},"guest connects");
    guest.PushLocal(first,{});until([&]{return host.Connected();},"host connects");
    Require(Hash(hostWorld)==Hash(guestWorld),"identical final-chapter starting state");
    uint8_t previous[2]={};std::set<uint32_t> callbacks;
    bool ended=false;int finalFrame=0;
    for(int step=0;step<30000;++step) {
        const uint32_t frame=hostWorld.Frame();
        const FrameInput desired[2]={Aim(hostWorld,0,step,previous[0]),Aim(hostWorld,1,step,previous[1])};
        host.PushLocal(frame+cfg.delay,desired[0]);guest.PushLocal(frame+cfg.delay,desired[1]);
        FrameInput inputs[2],remote[2];
        until([&]{return host.RemoteInput(frame,&remote[0])&&guest.RemoteInput(frame,&remote[1]);},"final-chapter UDP input delivery");
        const bool hostLocal=host.LocalInput(frame,&inputs[0]),guestLocal=guest.LocalInput(frame,&inputs[1]);
        Require(frame<first||(hostLocal&&guestLocal),"delayed local wire input present");
        if(inputs[0].present)hostWorld.SetInput(0,inputs[0].in);
        if(remote[0].present)hostWorld.SetInput(1,remote[0].in);
        if(remote[1].present)guestWorld.SetInput(0,remote[1].in);
        if(inputs[1].present)guestWorld.SetInput(1,inputs[1].in);
        hostWorld.Step();guestWorld.Step();
        const auto a=Hash(hostWorld),b=Hash(guestWorld);
        if(a!=b)std::fprintf(stderr,"frame %u signatures %08X/%08X RNG %08X/%08X draws %zu/%zu\n",frame,a,b,
            hostWorld.RandState(),guestWorld.RandState(),hostWorld.Draws().size(),guestWorld.Draws().size());
        Require(a==b,"final-chapter gameplay and draw state stays deterministic over UDP");
        host.ReportHash(hostWorld.Frame(),a);guest.ReportHash(guestWorld.Frame(),b);pump();
        Require(!host.Desynced(nullptr,nullptr,nullptr)&&!guest.Desynced(nullptr,nullptr,nullptr),"Session reports no divergence");
        for(const auto& target:hostWorld.ShotTargets())if(target.fn!=game::fn::kEnemy&&!target.civilian)
            if(callbacks.insert(target.fn).second) {
                std::printf("final chapter step %d target callback %08X\n",step,target.fn);std::fflush(stdout);
            }
        if(step%3000==0) {std::printf("final chapter step %d mode %d substate %d targets %zu target-types %zu hash %08X\n",step,
            hostWorld.GameMode(),hostWorld.Hud().subState,hostWorld.ShotTargets().size(),callbacks.size(),a);std::fflush(stdout);}
        finalFrame=step+1;
        if(hostWorld.GameMode()==6&&hostWorld.Hud().subState>=7) {ended=true;break;}
    }
    const unsigned creatures=unsigned(callbacks.count(0x0042C8D0)+callbacks.count(0x0042D530)+
        callbacks.count(0x0042C1A0)+callbacks.count(0x0042CDD0));
    Require(callbacks.count(0x00426C30)&&(ended||creatures>=2),"full final-chapter run reaches Emperor and its creature attacks");
    std::printf("PASS Randomizer seed %d: %d final-chapter frames, matching real UDP inputs, gameplay and geometry; %s\n",
        seed,finalFrame,ended?"ending reached":"Emperor/creature attacks covered; ending not driven within bounded run");
}
