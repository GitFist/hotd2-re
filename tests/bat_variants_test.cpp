// bat_variants_test <game directory>
// Enumerate every shipped bat-family record, then execute the real spawn/task
// paths with 1P/2P, live/dead, reflection/window and Horde group-size coverage.
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "game/world_internal.h"
#include "game/world_modes.h"
#include "game/mstack.h"
#include "render/asset_store.h"

using namespace game;
namespace fs=std::filesystem;
static void Require(bool ok,const char* why) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
static std::vector<uint8_t> Read(const fs::path& p) {
    std::ifstream f(p,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
struct Record {int mode,scene,variant;uint32_t op;SpawnRecord spawn;std::string family;};
struct Data {ExeImage exe;std::map<int,EventMemory> memory;std::vector<Record> records;int scripts=0,instructions=0;};

static Data Enumerate(const std::string& dir) {
    Data d;Require(d.exe.Load((fs::path(dir)/"Hod2.exe").string()),"original executable tables load");
    const auto common=Read(fs::path(dir)/"evt/comevtbl.bin");Require(!common.empty(),"common event table loads");
    for(int mode:{0,1,2,3,4,5,6,9,10,11}) {
        const int table=d.exe.U32(0x00579928+4*mode);
        const auto name=d.exe.CStr(d.exe.U32(0x004D1C7C+4*table));
        const auto bytes=Read(fs::path(dir)/"evt"/name);Require(!bytes.empty(),"every shipped event table loads");
        auto& mem=d.memory[mode];mem.Load(EventMemory::kCommonBase,common);mem.Load(EventMemory::kStageBase,bytes);
        // Boss/training selectors explicitly address headers beyond an earlier
        // -1 gap. Bound the entire declared scene header by its first pointer.
        const uint32_t base=EventMemory::kStageBase;uint32_t first=base+0x1000;
        for(int i=0;i<256;++i){uint32_t p=0;if(mem.Read(base+4*i,&p)&&p>base&&p<first)first=p;}
        const int scenes=(first-base)/4;Require(scenes>0&&scenes<256,"complete declared scene header is bounded");
        for(int scene=0;scene<scenes;++scene) {
            uint32_t variants=0;Require(mem.Read(base+4*scene,&variants),"declared scene list readable");
            if(variants==0xFFFFFFFFu||!variants)continue;
            for(int variant=0;variant<128;++variant) {
                uint32_t pc=0;if(!mem.Read(variants+4*variant,&pc))break; // ending table can terminate at file boundary
                if(pc==0xFFFFFFFFu)break;if(!pc)continue;++d.scripts;
                bool ended=false;
                for(int i=0;i<10000;++i) {
                    EventOp event;Require(DecodeEvent(mem,pc,event),"all reachable script instructions decode");++d.instructions;
                    uint32_t op=0;
                    if(event.opcode>=1&&event.opcode<=8)op=9+((event.opcode-1)&3);
                    else if(event.opcode>=9&&event.opcode<=12)op=event.opcode;
                    else if(event.opcode==13||event.opcode==0x4B)op=0xB;
                    if(op)for(uint32_t addr:event.args) {
                        if(addr>=0xFFFFFFFEu)continue;
                        SpawnRecord r;if(!r.Read(mem,addr))continue;
                        std::string family;
                        if(r.type==0x40&&(op!=9||r.ParamS8(1)!=2))family="leech";
                        else if(r.type==0x43)family="owl";
                        else if(r.type==0x41&&r.ParamS8(1)==11)family="komono11";
                        else if(r.type==0x24&&r.ParamS8(4)==0x53&&r.ParamS16(10)==0xD9)family="flock";
                        if(!family.empty())d.records.push_back({mode,scene,variant,op,r,family});
                    }
                    if(event.opcode==0x4E||event.opcode==0x4F){ended=true;break;}pc+=event.length;
                }
                Require(ended,"every script reaches its halt/next-variant boundary");
            }
        }
    }
    const uint32_t commonHalt=d.memory.at(0).Script(7,0,0);EventOp halt;
    Require(DecodeEvent(d.memory.at(0),commonHalt,halt)&&halt.opcode==0x4E,"common stage-end script is a halt without actor spawns");
    ++d.scripts;++d.instructions;
    Require(d.scripts==588,"complete shipped script inventory, including boss/training headers beyond sentinel gaps");
    return d;
}

static void Setup(World::Impl& w,const Data& d,const std::string& dir,const Record& r,int players) {
    w.gameDir=dir;w.exe=d.exe;w.mem=d.memory.at(r.mode);
    w.cameraBank.Load(w.exe,[&](const std::string& n){return Read(fs::path(dir)/"cam"/n);});
    w.motionFiles=ListMotionFiles(w.exe);w.enemyTables.Load(w.exe);w.cfg.eventMode=r.mode;
    w.cfg.scene=r.scene;w.cfg.gameMode=w.gameMode=6;w.cfg.freePlay=true;
    w.players=players;w.playersInGame=0;w.rank=15;w.ground=r.spawn.pos[1]-10;
    for(int p=0;p<2;++p){w.player[p].state=p<players?5:9;w.player[p].lives=99;w.player[p].ammo=6;}
    w.runner=std::make_unique<EventRunner>(w.mem,w,&w.env,&w.exe);
    w.runner->scene=r.scene;w.runner->mode=r.mode;w.runner->variant=r.variant;
    w.runner->cfg.gameMode=6;w.runner->cfg.players=players;w.runner->camera.modeCur=2;
    w.runner->flags[0x5E]=w.runner->flags[2]=1; // actual window-open branch of leech group 2
    for(int i=0;i<3;++i){w.cam.pos[i]=r.spawn.pos[i];w.playerPos[i]=r.spawn.pos[i];}
    w.cam.pos[1]+=5;w.cam.pos[2]+=20;w.cam.inverse=Mat4::Translate(w.cam.pos[0],w.cam.pos[1],w.cam.pos[2]);
    w.cam.view=mx::Inverse(w.cam.inverse);w.cam.ctx=0;
    w.env.lightIntensity=.5f;w.env.lightColor[0]=1;w.env.lightColor[1]=.9f;w.env.lightColor[2]=.77f;
    w.env.block1.lightIntensity=.7f;for(float& c:w.env.block1.lightColor)c=1;
    w.LightScene();w.residency=true;w.modelLoaded.assign(0x4000,8);
    for(uint32_t& f:w.actorType)f=0x0041EBB0;
    for(uint32_t a=0x00593358;;a+=8){const int type=static_cast<int32_t>(w.exe.U32(a));if(type<0||type>=0x70)break;w.actorType[type]=w.exe.U32(a+4);}
    w.rng.Seed(12345);
}
struct Totals {long long draws=0,leech=0,owl=0,deadLeech=0,deadOwl=0,shadow=0,reflection=0,flock=0,komono=0,fx=0;uint64_t hash=1469598103934665603ull;std::set<int> models;};
static bool BodyFn(uint32_t fn){return fn==0x0043C440||fn==0x0043DA20||fn==0x004460C0||fn==0x00448210;}
static void Validate(World::Impl& w,port::AssetStore& assets,std::set<int>& decoded,Totals& t,const Record& r) {
    std::map<std::pair<uint32_t,int>,const WorldDraw*> original;
    for(const auto& draw:w.draws) {
        auto mix=[&](uint32_t v){t.hash=(t.hash^v)*1099511628211ull;};
        auto real=[&](float f){uint32_t v;std::memcpy(&v,&f,4);mix(v);};
        mix(draw.model);mix(draw.fn);mix(draw.unitSpecular);real(draw.alpha);
        for(float f:draw.world.m)real(f);real(draw.light.intensity);
        for(float c:draw.light.color)real(c);mix(draw.light.pitch);mix(draw.light.yaw);
        if(draw.edit){real(draw.edit->diffuse);for(const auto& v:draw.edit->vertices){mix(v.index);for(float p:v.pos)real(p);}}
        ++t.draws;t.models.insert(draw.model);
        for(float v:draw.world.m)Require(std::isfinite(v),"finite bat/effect transform in every variant");
        Require(std::isfinite(draw.alpha),"finite body/effect alpha");
        if(decoded.insert(draw.model).second){Model model;std::string pol;Require(assets.ModelById(draw.model,model,&pol),"all emitted models resolve to actual shipped assets");
            for(const auto& mesh:model.meshes)for(const auto& strip:mesh.strips)for(const auto& v:strip.vertices)
                for(float n:v.normal)Require(std::isfinite(n),"all emitted asset normals finite");}
        if(BodyFn(draw.fn)) {
            const float* m=draw.world.m;const float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);
            Require(std::isfinite(det)&&det!=0,"every physical/dead bat normal transform is nonsingular");
        }
        if(draw.fn==0x0043C440||draw.fn==0x0043DA20) {
            if(draw.model==0x10D0){Require(!draw.unitSpecular,"floor shadow excluded from body specular correction");++t.shadow;}
            else {
                Require(draw.model>=0x136E&&draw.model<=0x1381&&draw.unitSpecular,"every leech skin/dead/reflection body has unit specular normals");++t.leech;
                if(draw.fn==0x0043DA20)++t.deadLeech;
                if(draw.edit){Require(std::isfinite(draw.edit->diffuse),"finite persistent material edit");for(const auto& e:draw.edit->vertices)for(float p:e.pos)Require(std::isfinite(p),"finite seam deformation");}
                if(r.mode==0&&r.scene==3&&draw.fn==0x0043C440){auto key=std::make_pair(draw.task,draw.node);auto it=original.find(key);if(it==original.end())original.emplace(key,&draw);else{Require(it->second->edit==draw.edit,"reflection shares the submitted seam/material snapshot");++t.reflection;}}
            }
        } else if(draw.fn==0x004460C0||draw.fn==0x00448210) {
            Require(draw.model!=0xC07,"all owl variants avoid the invalid face/foot midpoint");
            if(draw.model==0xBBF||draw.model==0xBC0){++t.owl;if(draw.model==0xBC0)++t.deadOwl;
                Require(std::abs(draw.light.intensity-w.env.lightIntensity)<1e-6f,"all living/dead owl bodies retain scene light");
                for(int c=0;c<3;++c)Require(std::abs(draw.light.color[c]-w.env.lightColor[c])<1e-6f,"all owl body scene-light RGB components match");}
        } else if(draw.fn==0x00483F00){Require(draw.model>=0x21F&&draw.model<=0x231,"all ambient flock poses stay in their asset family");++t.flock;}
        else if(draw.fn==0x00467C80){Require(draw.model==0x1CF||draw.model==0x1D0,"komono bat parity stays in its two-pose family");++t.komono;}
        else ++t.fx;
    }
}

static Totals Run(const Data& d,const std::string& dir,const Record& r,int players,port::AssetStore& assets,std::set<int>& decoded,int count=0,int variant=-1,bool randomized=false) {
    World::Impl w;Setup(w,d,dir,r,players);SetCurrentWorld(&w);
    if(randomized){w.cfg.portFeatures=true;w.cfg.gameType=1;ModesBoot(w);RandomizerBegin(w,true);Require(RandomizerActive(w),"randomizer is active during paired native bat dispatch");}
    if(count>0&&r.scene==0)HordeGameStart(w); // real Horde dive/strike adapters, isolated from its unrelated enemy waves
    if(count||variant>=0)w.SpawnBatGroup(r.spawn,variant<0?1:variant,count);else w.Spawn(r.op,r.spawn.addr);
    Totals total;int liveFrames=0;
    for(int frame=0;frame<450;++frame) {
        w.frame=frame;w.seedCounter=frame;
        // Kind-0 owls deliberately reject hits until camera tick 682. Their
        // authored encounter begins late in the camera; retain that guard.
        const int tick=frame+(r.family=="owl"&&r.spawn.ParamS8(1)==0?700:0);
        w.runner->camera.tick=tick;w.runner->camera.playTick=tick;
        w.draws.clear();w.targets.clear();w.targetCopy.clear();w.LightScene();
        if(frame>=280&&frame<320)for(Task* task:w.ts.Live())if(auto* a=dynamic_cast<ActorTask*>(task->data.get()))if(BodyFn(a->fn)||a->fn==0x00467C80)a->flags34|=0xA;
        w.ts.RunFrame();Validate(w,assets,decoded,total,r);
        for(Task* task:w.ts.Live())if(auto* a=dynamic_cast<WTask*>(task->data.get()))if(BodyFn(a->fn))++liveFrames;
        w.ts.FreeDead();
    }
    SetCurrentWorld(nullptr);
    const bool suppressed=r.family=="owl"&&players==1&&((r.spawn.ParamS8(1)==0&&r.spawn.state>1)||(r.spawn.ParamS8(1)!=0&&r.spawn.state>2));
    Require(suppressed?total.draws==0:total.draws>0,"per-player spawn suppression and accepted actor drawing exercised");
    if(!suppressed&&r.family=="owl")Require(total.owl>0&&total.deadOwl>0,"each accepted owl slot exercises live and dead body rendering");
    if(!suppressed&&r.family=="leech")Require(total.leech>0&&total.deadLeech>0,"each leech variant exercises live and fading corpse rendering");
    if(r.family=="flock")Require(total.flock==30*450,"all 30 ambient creatures emit one valid pose on every fixture frame");
    if(r.family=="komono11")Require(total.komono==450,"both live/shot komono bats emit a valid parity pose on every fixture frame");
    std::printf("%s%s mode%d scene%d variant%d record%08X %dP count%d: draws%lld body%lld/%lld dead%lld/%lld shadow%lld reflection%lld fx%lld\n",r.family.c_str(),randomized?" randomized":"",r.mode,r.scene,r.variant,r.spawn.addr,players,count,total.draws,total.leech,total.owl,total.deadLeech,total.deadOwl,total.shadow,total.reflection,total.fx);
    return total;
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;const std::string dir=argv[1];const auto data=Enumerate(dir);
    std::map<std::string,int> families;for(const auto& r:data.records)++families[r.family];
    Require(families["leech"]==5&&families["owl"]==14&&families["komono11"]==2&&families["flock"]==1,"all 22 shipped bat-family spawn references enumerated");
    port::AssetStore assets;std::string error;Require(assets.Open(dir,&error),error.c_str());std::set<int> decoded;
    long long draws=0;int fixtures=0;std::vector<Totals> baseline;
    for(const auto& r:data.records)for(int players:{1,2}){const auto t=Run(data,dir,r,players,assets,decoded);draws+=t.draws;++fixtures;baseline.push_back(t);}
    size_t pair=0;
    for(const auto& r:data.records)for(int players:{1,2}){const auto t=Run(data,dir,r,players,assets,decoded,0,-1,true);
        Require(t.hash==baseline[pair++].hash,"randomizer preserves exact bat/ambient transforms, materials, light and specular flags");draws+=t.draws;++fixtures;}
    const auto leech=*std::find_if(data.records.begin(),data.records.end(),[](const Record& r){return r.family=="leech";});
    auto horde=leech;horde.scene=0;horde.variant=1;horde.spawn.addr=0;
    for(int players:{1,2})for(int count=2;count<=8;++count){const auto t=Run(data,dir,horde,players,assets,decoded,count);draws+=t.draws;++fixtures;}
    for(int players:{1,2}){const auto t=Run(data,dir,leech,players,assets,decoded,0,0);draws+=t.draws;++fixtures;}
    std::printf("PASS %d fixtures, %zu emitted assets, %lld draws; all%d scripts/%d instructions/%zu record references\n",fixtures,decoded.size(),draws,data.scripts,data.instructions,data.records.size());
}
