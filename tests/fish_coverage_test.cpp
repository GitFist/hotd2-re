// fish_coverage_test <game directory>
// Decode every authored event script, then run every distinct fish spawn record
// through the real constructors/task scheduler with the shipped models/motions.
// Explicit controls exercise the two procedural Hierophant waves and the
// opaque body's separate, deliberately flattened water/escape shadows.
#include "game/world_internal.h"
#include "game/mstack.h"
#include "game/world_config_io.h"
#include "render/asset_store.h"
#include "route_planner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0, cases = 0, bodies = 0, shadows = 0, effects = 0;
std::string context;
std::set<int> poses, corpsePoses;
std::map<std::string,int> violations;
void Require(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        ++violations[why];
        if (++failures <= 16) std::fprintf(stderr, "FAIL [%s]: %s\n", context.c_str(), why);
    }
}
std::vector<uint8_t> Read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
uint32_t Word(const game::EventMemory& mem, uint32_t a) {
    uint32_t v = 0;
    Require(mem.Read(a, &v), "event word is readable");
    return v;
}
struct Record { int mode, scene; game::SpawnRecord rec; };
std::vector<Record> Inventory(const std::string& dir, const game::ExeImage& exe) {
    std::vector<Record> records;
    int scripts = 0;
    for (int mode : {0,1,2,3,4,5,6,9,10,11}) {
        context = "event inventory mode " + std::to_string(mode);
        const auto name = exe.CStr(exe.U32(0x004D1C7C + 4 * exe.U32(0x00579928 + 4 * mode)));
        game::EventMemory mem;
        mem.Load(game::EventMemory::kCommonBase, Read(std::filesystem::path(dir) / "evt" / "comevtbl.bin"));
        const auto data = Read(std::filesystem::path(dir) / "evt" / name);
        Require(!data.empty(), "every event table is present");
        mem.Load(game::EventMemory::kStageBase, data);
        const uint32_t base = game::EventMemory::kStageBase;
        uint32_t first = base + 0x1000;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t p = 0;
            if (mem.Read(base + i * 4, &p) && p > base && p < first) first = p;
        }
        const int scenes = static_cast<int>((first - base) / 4);
        Require(scenes > 0 && scenes < 256, "event scene header is bounded");
        std::set<uint32_t> seenRecords;
        for (int scene = 0; scene < scenes; ++scene) {
            const uint32_t table = Word(mem, base + 4 * scene);
            if (!table || table == UINT32_MAX) continue;
            for (int variant = 0; variant < 256; ++variant) {
                uint32_t entry=0;
                if(!mem.Read(table + variant * 4,&entry))break;
                if (entry == UINT32_MAX) break;
                if (!entry) continue;
                ++scripts;
                std::set<uint32_t> visited;
                for (uint32_t a = entry; visited.insert(a).second;) {
                    game::EventOp op;
                    if (!game::DecodeEvent(mem, a, op)) {
                        Require(false, "all declared event instructions decode");
                        break;
                    }
                    if ((op.opcode >= 1 && op.opcode <= 0xD) || op.opcode == 0x4B) {
                        for (uint32_t p : op.args) {
                            uint32_t type = 0;
                            if (!mem.Read(p, &type) || (type != 0x42 && type != 0x51) || !seenRecords.insert(p).second) continue;
                            Record r{mode, scene, {}};
                            Require(r.rec.Read(mem, p), "fish spawn record resolves");
                            records.push_back(r);
                        }
                    }
                    if (op.opcode == 0x4E || op.opcode == 0x4F) break;
                    a += op.length;
                }
            }
        }
    }
    // Mode 7's common halt is the only declared script outside these stage
    // files; mode 8 has no script table. Verify it rather than omit the mode.
    game::EventMemory common;
    common.Load(game::EventMemory::kCommonBase,Read(std::filesystem::path(dir)/"evt"/"comevtbl.bin"));
    game::EventOp halt;
    Require(game::DecodeEvent(common,common.Script(7,0,0),halt)&&halt.opcode==0x4E,"common mode 7 contains only a halt");
    ++scripts;
    Require(scripts==588,"all declared scene/variant contexts, including boss/training scenes after unused slots, are decoded");
    Require(records.size() == 31, "inventory covers the 31 distinct shipped fish/control records");
    int school = 0, control = 0, opaque = 0, fade = 0;
    for (const auto& r : records) {
        if (r.rec.type == 0x42) ++school;
        else if (r.rec.ParamS16(0xE) == 6) ++control;
        else if (r.rec.ParamS16(0xC) == 0) ++opaque;
        else if (r.rec.ParamS16(0xC) == 1) ++fade;
    }
    Require(school == 3 && control == 7 && opaque == 12 && fade == 9, "all authored constructor variants are inventoried");
    std::printf("inventory: %d script contexts, %zu records = %d school + %d water controls + %d opaque + %d fading\n",
                scripts, records.size(), school, control, opaque, fade);
    return records;
}

struct Fixture {
    game::World::Impl w;
    explicit Fixture(const std::string& dir, const Record& r, int players, int mode) {
        w.gameDir = dir;
        Require(w.exe.Load((std::filesystem::path(dir) / "Hod2.exe").string()), "fixture reads original data tables");
        w.runner = std::make_unique<game::EventRunner>(w.mem, w, &w.env, &w.exe);
        w.runner->scene = r.scene;
        w.runner->camera.modeCur = 2;
        w.cfg.eventMode = r.mode;
        w.gameMode = mode;
        w.players = players;
        w.playersInGame = 0;
        for (auto& p : w.player) { p.state = 5; p.lives = 99; }
        w.cam.inverse = game::Mat4::Identity();
        w.cam.pos[0] = r.rec.pos[0];
        w.cam.pos[1] = r.rec.pos[1] + 20;
        w.cam.pos[2] = r.rec.pos[2] + 45;
        std::copy(w.cam.pos, w.cam.pos + 3, w.cam.inverse.m + 12);
        w.cam.view = game::mx::Inverse(w.cam.inverse);
        w.ground = r.rec.pos[1] - 15;
        w.waterLevel = r.rec.pos[1] + 1;
        w.env.lightIntensity = .8f;
        w.env.block1.lightIntensity = .7f;
        w.LightScene();
        game::SetCurrentWorld(&w);
    }
    ~Fixture() { game::SetCurrentWorld(nullptr); }
    void Spawn(const game::SpawnRecord& r) {
        auto s = std::make_unique<game::SpawnTask>(); s->rec = r;
        w.ts.Create([](game::Task& t) {
            auto& w = *game::CurrentWorld(); auto& s = t.As<game::SpawnTask>();
            if (s.rec.type == 0x42) w.SpawnFishSchool(t, s);
            else w.SpawnFish(t, s);
        }, std::move(s));
    }
    void Step() {
        w.draws.clear(); w.targets.clear(); w.sounds.clear(); ++w.frame;
        w.ts.RunFrame();
    }
    void Hit(bool air) {
        for (auto* task : w.ts.Live()) if (auto* e = dynamic_cast<game::EnemyTask*>(task->data.get())) {
            if (air) e->pos[1] = w.waterLevel + 10;
            e->flags34 |= 8 | 2;
        }
    }
};

bool FishModel(int model) { return (model >= 0x1156 && model <= 0x1169) || model == 0xB6F || model == 0xB70; }
bool SchoolModel(int model) { return model >= 0x85A && model <= 0x88F; }
float YScale(const game::Mat4& m) { return std::sqrt(m.m[4]*m.m[4]+m.m[5]*m.m[5]+m.m[6]*m.m[6]); }
void CheckDraws(const Fixture& f) {
    for (const auto& d : f.w.draws) {
        if ((d.fn == 0x00438730 || d.fn == 0x00439C20) && FishModel(d.model)) {
            const bool shadow = YScale(d.world) < .011f;
            Require(d.unitSpecular != shadow, shadow ? "flattened fish shadow retains native lighting" : "every physical fish body uses bounded unit specular normals");
            if (shadow) {
                ++shadows;
                Require(d.light.color[0] == .1f && d.light.color[1] == .1f && d.light.color[2] == .1f,
                        "water/escape surrogate stays dark");
            } else {
                ++bodies; poses.insert(d.model);
                if (d.model == 0xB6F || d.model == 0xB70) corpsePoses.insert(d.model);
                Require(std::abs(d.light.intensity - .7f) < 1e-6f, "physical fish body uses character light");
            }
        } else if (SchoolModel(d.model) && (d.fn == 0x0042FCA0 || d.fn == 0x00431000 || d.fn == 0x00430C80)) {
            Require(!d.unitSpecular, "school materials have no specular lobe; original lighting is retained");
            if (d.model == 0x85B) ++shadows; else { ++bodies; poses.insert(d.model); }
            if (d.fn == 0x00430C80) corpsePoses.insert(d.model);
            if (d.fn == 0x0042FCA0) Require(std::abs(d.light.intensity-.7f)<1e-6f, "living school body/shadow uses character light");
        } else {
            Require(!d.unitSpecular, "fish lighting fix does not leak into hit flashes, blood, splashes or rings");
            if (d.fn == 0x00439E00 || d.model == 0xB71 || (d.model >= 0x1339 && d.model <= 0x1356)) ++effects;
        }
    }
}

void RecordCases(const std::string& dir, const std::vector<Record>& records) {
    for (const auto& r : records) {
        if (r.rec.type == 0x51 && r.rec.ParamS16(0xE) == 6) {
            char label[96]; std::snprintf(label,sizeof(label),"water control %08X",r.rec.addr); context=label;
            Fixture f(dir,r,1,6); for (int& s : f.w.fishSlots) s=1;
            f.Spawn(r.rec); f.Step(); ++cases;
            float water = 0; std::memcpy(&water,r.rec.params,4);
            Require(f.w.waterLevel==water && f.w.draws.empty(),"kind 6 changes water level without drawing a fish");
            Require(std::all_of(std::begin(f.w.fishSlots),std::end(f.w.fishSlots),[](int n){return n==0;}),"water control frees all attack points");
            continue;
        }
        for (int players : {1,2}) for (int gameMode : {5,6}) for (int action=0;action<4;++action) {
            char label[120]; std::snprintf(label,sizeof(label),"record %08X actor %02X P%d mode%d action%d",r.rec.addr,r.rec.type,players,gameMode,action); context=label;
            Fixture f(dir,r,players,gameMode); f.Spawn(r.rec); ++cases;
            int startBodies=bodies,startShadows=shadows; bool escaped=false, dead=false;
            // 0 natural; 1 shot under water / early school split; 2 shot in air /
            // late school hit; 3 all attack points unavailable -> escape.
            for (int frame=1;frame<=650;++frame) {
                if (action==2 && frame==2 && r.rec.type==0x51)
                    for (auto* t:f.w.ts.Live()) if (auto* e=dynamic_cast<game::EnemyTask*>(t->data.get())) e->pos[1]=f.w.waterLevel+10;
                if ((action==1 && frame==3) || (action==2 && frame==(r.rec.type==0x42?180:3))) f.Hit(false);
                if (action==3 && r.rec.type==0x51) for(int& slot:f.w.fishSlots) slot=1;
                f.Step(); CheckDraws(f);
                for (auto* t:f.w.ts.Live()) if(auto* d=dynamic_cast<game::WTask*>(t->data.get())) {
                    escaped=escaped||d->fn==0x00439C20; dead=dead||d->fn==0x00430C80;
                }
                if(frame==1 && r.rec.type==0x42) {
                    const int variant=r.rec.params[1];
                    const int expected=variant==1?1:variant==0?(players==1?6:8):(players==1?10:15);
                    int actual=0;
                    for(auto* t:f.w.ts.Live()) if(auto* d=dynamic_cast<game::WTask*>(t->data.get()))
                        if(d->fn==0x0042FCA0||d->fn==0x00431000) ++actual;
                    Require(actual==expected,"school constructor uses correct one/two-player count");
                }
                f.w.ts.FreeDead();
            }
            Require(bodies>startBodies,"every physical spawn produces a body");
            if (r.rec.type==0x51 && action==3) {
                Require(escaped,"unavailable attack points exercise escape task");
                if(r.rec.ParamS16(0xC)==0) Require(shadows>startShadows,"opaque escape/water shadow is exercised");
            }
            if(r.rec.type==0x42 && r.rec.params[1]!=1 && action==1) Require(dead,"early school hit exercises split/death task");
        }
    }
}

void BossCases(const std::string& dir) {
    Record r{1,32,{}}; r.rec.pos[1]=-24; r.rec.pos[2]=-50;
    for(int kind:{1,2}) for(int players:{1,2}) for(int mode:{5,6}) for(bool hit:{false,true}) {
        context="Hierophant kind "+std::to_string(kind)+" P"+std::to_string(players)+" mode"+std::to_string(mode)+(hit?" hit":" natural");
        Fixture f(dir,r,players,mode); ++cases;
        f.w.SpawnFishJump(0,-25,-50,kind==1?100:80,kind);
        Require(f.w.liveEnemies==1 && f.w.attackers==1,"both boss wave branches create a counted physical fish");
        for(int frame=0;frame<450;++frame) { if(hit&&frame==2)f.Hit(false);f.Step();CheckDraws(f);f.w.ts.FreeDead(); }
    }
    context="boss spawn guards/default duration";
    Fixture f(dir,r,0,6);f.w.SpawnFishJump(0,-25,-50,0,1);
    Require(f.w.ts.Live().empty(),"no boss fish with no players");
    f.w.players=1;for(int& s:f.w.fishSlots)s=1;f.w.SpawnFishJump(0,-25,-50,0,2);
    Require(f.w.ts.Live().empty(),"no boss fish when all four attack points occupied");
    f.w.fishSlots[0]=0;f.w.SpawnFishJump(0,-25,-50,0,2);f.Step();CheckDraws(f);
    Require(f.w.liveEnemies==1,"zero duration uses the documented 80-frame default");
}

void AssetCases(port::AssetStore& assets) {
    int school=0, fish=0, specularMeshes=0;
    for(int id=0;id<0x116A;++id) {
        if(!SchoolModel(id)&&!FishModel(id))continue;
        context="asset model "+std::to_string(id);
        game::Model model;std::string pol;
        Require(assets.ModelById(id,model,&pol),"every school/live/dead fish pose resolves from shipped assets");
        const bool isSchool=SchoolModel(id);
        Require(pol==(isSchool?"buyo.bin":"fish.bin"),"body/death poses belong to the correct asset family");
        if(isSchool)++school;else++fish;
        for(const auto& mesh:model.meshes) {
            const int exponent=mesh.material.SpecularExponent();
            if(isSchool)Require(exponent<=0,"every school material disables specular (no unrelated normalization needed)");
            else if(exponent>0 && exponent<31)++specularMeshes;
        }
    }
    Require(school==54 && fish==22 && specularMeshes>0,"all 76 physical fish/death poses audited, including specular materials");
    std::printf("assets: %d school + %d live/dead fish models; %d specular fish meshes\n",school,fish,specularMeshes);
}

// Exercise complete event/camera/resource/player paths too. Configs are optional
// QA artifacts, accepted directly by play.exe --config for matching screenshots.
void SceneCases(const std::string& dir,const char* output) {
    // Other authored records are covered above using their exact constructors.
    // These five scenes also support an independent fresh stage start, without
    // inheriting route gates/flags from the previous scene.
    for(int mode:{1,2}) for(int scene:(mode==1?std::vector<int>{21,28}:std::vector<int>{1,7,9})) {
        context="complete stage "+std::to_string(mode+1)+" scene "+std::to_string(scene);
        game::World world;std::string error;
        Require(world.Load(dir,mode,&error),"complete scene loads stage/camera/event assets");
        game::WorldConfig cfg;cfg.eventMode=mode;cfg.scene=scene;cfg.gameMode=6;cfg.playerState[0]=0;
        cfg.seedCounter=902;cfg.livesSetting=9;cfg.gameOver=true;
        world.SetInput(0,game::MakeWorldInput(.5f,.5f,0,0));
        Require(world.StartStage(cfg,&error),"complete scene starts");
        if(output) {
            const auto path=std::filesystem::path(output)/("stage"+std::to_string(mode+1)+"-scene"+std::to_string(scene)+"-fish.cfg");
            Require(game::SaveWorldConfig(cfg,path.string()),"QA scene config is written");
        }
        int first=-1,last=-1,physical=0;std::set<uint32_t> fishTasks;
        plan::Planner planner;planner.Init();planner.Emit(0,.5f,.5f,0);
        float x=.5f,y=.5f;uint8_t buttons=0;
        for(int frame=1;frame<=8000;++frame) {
            const uint8_t previous=buttons;
            plan::Line line{};
            if(first<0 && planner.Decide(world,frame,&line)) {x=line.x;y=line.y;buttons=static_cast<uint8_t>(line.buttons);}
            if(first>=0 && buttons) {buttons=0;planner.Emit(frame,x,y,0);}
            world.SetInput(0,game::MakeWorldInput(x,y,buttons,previous));world.SetInput(1,{});world.Step();
            for(const auto& d:world.Draws()) {
                const bool fish=(d.fn==0x00438730||d.fn==0x00439C20)&&FishModel(d.model)&&YScale(d.world)>.011f;
                const bool school=(d.fn==0x0042FCA0||d.fn==0x00431000||d.fn==0x00430C80)&&SchoolModel(d.model)&&d.model!=0x85B;
                if(!fish&&!school)continue;
                if(first<0)first=frame;last=frame;++physical;fishTasks.insert(d.task);
                Require(d.unitSpecular==fish,"complete event route selects the correct physical-fish lighting path");
            }
            if(first>=0&&frame>=first+360)break;
        }
        if(output) {
            const auto path=std::filesystem::path(output)/("stage"+std::to_string(mode+1)+"-scene"+std::to_string(scene)+"-fish.txt");
            std::ofstream script(path);for(const auto& line:planner.lines)script<<line<<'\n';
        }
        Require(physical>0,"every authored fish scene actually reaches its fish drawing tasks");
        std::printf("scene stage%d/%d: first=%d last=%d, %zu fish tasks, %d physical draws\n",mode+1,scene,first,last,fishTasks.size(),physical);
    }
    context="stage2 attract entry";
    game::World world;std::string error;
    Require(world.Load(dir,1,&error),"attract loads stage2 data");
    game::WorldConfig cfg;cfg.eventMode=1;cfg.scene=28;cfg.gameMode=5;cfg.demoIndex=1;
    Require(world.StartStage(cfg,&error),"actual attract configuration starts");
    int count=0;
    for(int frame=1;frame<=1744;++frame) {
        world.Step();
        for(const auto& d:world.Draws()) if(d.fn==0x00438730&&FishModel(d.model)&&YScale(d.world)>.011f) {
            ++count;Require(d.unitSpecular,"actual attract physical fish uses the fix");
        }
    }
    Require(count>0,"stage2 attract reaches its authored fish before the timer expires");
    std::printf("attract: %d physical fish draws within 1744 frames\n",count);
}
} // namespace

int main(int argc,char**argv) {
    if(argc<2||argc>3){std::fprintf(stderr,"usage: fish_coverage_test <game directory> [QA config directory]\n");return 2;}
    port::AssetStore assets;std::string error;
    Require(assets.Open(argv[1],&error),error.c_str());if(failures)return 2;
    AssetCases(assets);
    const auto records=Inventory(argv[1],assets.Exe());
    RecordCases(argv[1],records);BossCases(argv[1]);SceneCases(argv[1],argc==3?argv[2]:nullptr);
    context="coverage totals";
    Require(bodies>0 && shadows>0 && effects>0 && corpsePoses.size()>2,"physical bodies, water/escape shadows, corpses and original effects exercised");
    for(const auto& [why,count]:violations)std::printf("violations %d: %s\n",count,why.c_str());
    std::printf("%s: %d cases, %d checks, %d physical draws, %d shadow draws, %d effect draws, %zu runtime poses, %zu corpse poses; %d failures\n",
                failures?"FAIL":"PASS",cases,checks,bodies,shadows,effects,poses.size(),corpsePoses.size(),failures);
    return failures?1:0;
}
