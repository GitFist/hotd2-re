#include "game/world_prototype.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>

void Require(bool ok,const char* why) { if(!ok) { fprintf(stderr,"FAIL: %s\n",why); exit(1); } }
void Frame(game::World& w, uint32_t bits=0,float x=320,float y=240,uint32_t previous=0) {
    game::WorldInput in; in.kind=1; in.x=int(x); in.y=int(y); in.held=bits; in.pressed=bits&~previous;
    in.fired=(in.pressed&4)!=0; in.offscreen=(in.pressed&2)!=0;
    w.SetInput(0,in); w.SetInput(1,{}); w.Step();
}
void Boot(game::World& w,const char* dir,bool enabled=true) {
    Require(w.Load(dir),"load"); game::WorldConfig c; c.portFeatures=enabled; c.portModernMenu=enabled;
    c.gameOver=true; w.Boot(c); w.ForceMode(4,1,1234,0);
    for(int i=0;i<10;++i) Frame(w);
}
int main(int argc,char** argv) {
    if(argc<2) return 2;
    {
        game::World w; Boot(w,argv[1],false); Frame(w,game::kPrototypeKey);
        Require(w.InspectPrototype().encounter==-1,"exactness mode ignores hotkey");
    }
    {
        game::World w; Boot(w,argv[1]); Frame(w,game::kPrototypeKey);
        for(int i=0;i<400;++i) Frame(w);
        Frame(w,game::kPrototypeGrenade);
        Require(w.InspectPrototype().grenades==2,"grenade consumes one charge");
        Frame(w,game::kPrototypeGrenade,320,240,game::kPrototypeGrenade);
        Require(w.InspectPrototype().grenades==2,"holding grenade does not spend another");
        for(int i=0;i<4;++i) { Frame(w); Frame(w,game::kPrototypeGrenade); }
        Require(w.InspectPrototype().grenades==0,"grenades cannot go below zero");
        for(int i=0;i<12000 && w.InspectPrototype().phase!=7;++i) Frame(w);
        Require(w.InspectPrototype().phase==7,"unopposed enemies can defeat the player");
        for(int i=0;i<181;++i) Frame(w);
        Frame(w,4); Require(w.GameMode()==4,"defeat can return to menu");
    }
    {
        game::World w; Require(w.Load(argv[1]),"network world loads"); game::WorldConfig c;
        c.portFeatures=c.portModernMenu=c.portNetworkRoom=true; c.gameMode=6;
        Require(w.StartNetworkGame(c),"room loads"); Frame(w,game::kPrototypeKey);
        Require(w.InspectPrototype().encounter==-1 && w.NetworkLobby(),"hidden local mode cannot disturb online room");
    }
    for(int branch=0;branch<2;++branch) {
        game::World w; Boot(w,argv[1]); Frame(w,game::kPrototypeKey);
        Require(w.InspectPrototype().encounter==0,"hotkey starts chapter");
        // Pausing must not advance the independent encounter timer.
        for(int i=0;i<3;++i) Frame(w);
        Frame(w,8); auto age=w.InspectPrototype().age;
        for(int i=0;i<15;++i) Frame(w);
        Require(w.Hud().paused && w.InspectPrototype().age==age,"pause freezes chapter");
        Frame(w,8); Frame(w);
        std::set<int> seen; uint32_t prev=0; bool complete=false;
        std::ofstream script,keys;
        if(argc>=3) { script.open(std::string(argv[2])+std::to_string(branch)+".txt"); keys.open(std::string(argv[2])+std::to_string(branch)+".keys"); }
        // Reboot for a reproducible actual-client capture from title frame 700.
        game::WorldConfig cfg; cfg.portFeatures=cfg.portModernMenu=true; cfg.gameOver=true; w.Boot(cfg);
        for(int frame=1;frame<30000;++frame) {
            auto s=w.InspectPrototype(); uint32_t bits=0; float x=320,y=200;
            if(frame==700) bits=game::kPrototypeKey;
            if(s.encounter>=0) seen.insert(s.encounter);
            if(s.phase==3) bits=branch?0x80:0x40;
            else if(s.phase==4) bits=4;
            else if(!s.targets.empty() && s.phase<6) {
                x=s.targets[0][0]; y=s.targets[0][1];
                if(x>=0 && x<640 && y>=0 && y<480) bits=w.Hud().player[0].ammo==0?2:4;
            }
            if(script) {
                const int buttons=(bits&4?1:0)|(bits&2?2:0);
                script<<frame<<" 0 "<<x/640<<' '<<y/480<<' '<<buttons<<'\n';
                keys<<frame<<' '<<(bits&~6u)<<'\n';
            }
            Frame(w,bits,x,y,prev); prev=bits;
            if(frame%3000==0) printf("route %d frame %d encounter %d phase %d kills %d lives %d targets %zu\n",branch,frame,s.encounter,s.phase,s.kills,w.Hud().player[0].lives,s.targets.size());
            if(s.phase==7) { fprintf(stderr,"died at %d encounter %d\n",frame,s.encounter); break; }
            if(s.complete) {
                complete=true; int age=s.age;
                Frame(w,4,320,240); Require(w.InspectPrototype().complete,"results minimum hold");
                for(int i=0;i<181;++i) Frame(w);
                Frame(w,4); Require(w.InspectPrototype().encounter==-1 && w.GameMode()==4,"results returns to main menu");
                printf("PASS route %d complete at %d (%d ticks), %d kills\n",branch,frame,age,s.kills); break;
            }
        }
        Require(complete,"chapter reachable from start to boss completion");
        Require(seen.count(0)&&seen.count(1)&&seen.count(2)&&seen.count(3)&&seen.count(branch?6:4)&&seen.count(branch?7:5)&&seen.count(8)&&seen.count(9),"all route encounters visited");
        Frame(w,game::kPrototypeKey); Require(w.InspectPrototype().kills==0,"restart clears prior state");
        w.RequestTitle(); Require(w.InspectPrototype().encounter==-1,"pause return clears prototype");
    }
    puts("PASS prototype");
}
