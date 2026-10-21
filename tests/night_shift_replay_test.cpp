// Public UI inputs enter the actual Horde game. Record/replay the real targeting
// inputs and print stable per-frame state digests for x86/x64 comparison.
#include "game/world.h"
#include "play/local_leaderboard.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Mix(uint64_t& hash,uint32_t word){for(int i=0;i<4;++i){hash^=uint8_t(word>>(i*8));hash*=1099511628211ull;}}
static uint64_t Hash(const game::World& w,uint64_t hash=1469598103934665603ull){
    Mix(hash,w.Frame());Mix(hash,w.GameMode());Mix(hash,w.RandState());const auto& hud=w.Hud();
    for(int v:{hud.subState,hud.nightShiftPage,hud.nightShiftRow,hud.gameType,int(hud.canShoot),int(hud.paused)})Mix(hash,v);
    for(const auto& p:hud.player)for(int v:{p.state,p.lives,p.score,p.ammo})Mix(hash,v);
    const auto run=w.PortHordeRun();for(int v:{int(run.active),int(run.debug),run.rounds,run.score,run.players,run.difficulty})Mix(hash,v);
    for(const auto& d:w.Draws()){
        Mix(hash,d.model);Mix(hash,d.fn);Mix(hash,d.node);Mix(hash,d.task);
        for(float f:d.world.m){uint32_t bits;std::memcpy(&bits,&f,4);Mix(hash,bits);}
        uint32_t bits;std::memcpy(&bits,&d.alpha,4);Mix(hash,bits);
    }
    return hash;
}
static void Boot(game::World& w,const char* dir){
    Require(w.Load(dir),"load actual game tables");game::WorldConfig cfg;cfg.portFeatures=cfg.portModernMenu=cfg.gameOver=true;cfg.creditSetting=-1;
    w.Boot(cfg);w.ForceMode(4,0x10000,0,0);
}
using Inputs=std::array<game::WorldInput,2>;
static game::WorldInput Pointer(int x,int y,bool trigger=false){game::WorldInput in;in.kind=1;in.x=x;in.y=y;in.held=in.pressed=trigger?4:0;in.fired=trigger;return in;}
static void Run(const char* dir,int players){
    game::World live;Boot(live,dir);std::vector<Inputs> tape;std::vector<uint64_t> expected;play::LocalHordeLeaderboard records;
    uint64_t rolling=1469598103934665603ull;int active=0,combat=0,maxScore=0,maxRounds=0;bool barrels=false;
    auto step=[&](const Inputs& inputs){
        tape.push_back(inputs);live.SetInput(0,inputs[0]);live.SetInput(1,inputs[1]);live.Step();
        rolling=Hash(live,rolling);expected.push_back(rolling);const auto run=live.PortHordeRun();records.Observe(run);
        active+=run.active;combat+=run.active&&live.Hud().canShoot;maxScore=std::max(maxScore,run.score);maxRounds=std::max(maxRounds,run.rounds);
        for(const auto& draw:live.Draws())if(draw.model==0x19e8||draw.model==0x19e6)barrels=true;
    };
    for(int i=0;i<3;++i)step({Pointer(630,465),{}});
    Require(live.Hud().nightShiftPage==0,"actual Home opened");step({Pointer(100,176,true),{}});step({Pointer(100,176),{}});
    Require(live.Hud().nightShiftPage==1,"Home Play click opened Play");step({Pointer(100,291),{}});step({Pointer(100,291,true),{}});step({Pointer(100,291),{}});
    uint8_t previous[2]={};
    for(int tick=0;tick<9000;++tick){
        Inputs inputs;
        for(int p=0;p<players;++p){
            float x=.5f,y=.45f;uint8_t buttons=0;bool found=false;
            for(const auto& target:live.ShotTargets()){
                if(target.civilian)continue;
                for(const auto& node:target.nodes){
                    if(node[2]>=-1)continue;const float sx=320+node[0]*live.Focal()/-node[2],sy=240-node[1]*live.Focal()/-node[2];
                    if(!std::isfinite(sx)||!std::isfinite(sy)||sx<0||sx>=640||sy<0||sy>=480)continue;
                    if(live.PreviewShot(p,int(sx),int(sy)).serial!=target.serial)continue;
                    x=sx/640;y=sy/480;found=true;break;
                }
                if(found)break;
            }
            if(p==1&&tick==100)buttons=4; // real P2 join after the Horde game starts
            else if(found&&live.Hud().canShoot)buttons=live.Hud().player[p].ammo==0?2:(tick%4==0?1:0);
            inputs[p]=game::MakeWorldInput(x,y,buttons,previous[p]);previous[p]=buttons;
        }
        step(inputs);
        if(tick==999||tick==3999||tick==8999)std::printf("Horde %dP tick%d frame%d rng%08X digest%016llX rounds%d score%d\n",players,tick+1,live.Frame(),live.RandState(),static_cast<unsigned long long>(rolling),maxRounds,maxScore);
    }
    Require(active>5000&&combat>3000&&barrels&&maxScore>0&&maxRounds>=1,"actual Horde wave, kills, round completion and store transition were not exercised");
    records.Finish();Require(!records.Entries().empty(),"actual Horde progress did not finalize a local record");
    const auto path=std::filesystem::path("horde-"+std::to_string(players)+"p-records.txt");Require(records.Save(path.string()),"save actual Horde progress sidecar");
    play::LocalHordeLeaderboard loaded;Require(loaded.Load(path.string())&&loaded.Entries().size()==1,"reload actual Horde progress sidecar");
    Require(loaded.Entries()[0].players==players&&loaded.Entries()[0].score==maxScore&&loaded.Entries()[0].rounds==maxRounds,"saved Horde participation/progress differs");
    game::World replay;Boot(replay,dir);uint64_t restored=1469598103934665603ull;
    for(size_t i=0;i<tape.size();++i){replay.SetInput(0,tape[i][0]);replay.SetInput(1,tape[i][1]);replay.Step();restored=Hash(replay,restored);Require(restored==expected[i],"actual Horde replay diverged from recorded input history");}
    std::printf("PASS real Night Shift Home/Play/Horde %dP: %zu deterministic frames, %d active, %d combat, score%d rounds%d, digest%016llX and saved records\n",players,tape.size(),active,combat,maxScore,maxRounds,static_cast<unsigned long long>(restored));
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{Run(argv[1],1);Run(argv[1],2);}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
    return 0;
}
