// bat_face_test <game> <input script> <first frame> <last frame>
// bat_face_test <game> - 6522 6782 --ambient
// A live owl's face animation must stay in its translucent face-frame family,
// including the midpoint where the original PC code selected an opaque foot.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "game/world.h"
#include "render/asset_store.h"

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}

static int AmbientTest(port::AssetStore& assets, const char* dir, int first, int last) {
    for (int id=0x21F;id<=0x231;++id) {
        game::Model model; std::string pol;
        Require(assets.ModelById(id,model,&pol) && pol.find("zabat_hod1")!=std::string::npos,
                "all 19 ambient wing poses belong to the bird asset family");
    }
    game::Model effect; std::string pol;
    Require(assets.ModelById(0x232,effect,&pol) && pol.find("eff_1")!=std::string::npos,
            "adjacent ID is a different effect asset, outside the bird cycle");
    game::World world; std::string error;
    Require(world.Load(dir,0,&error),error.c_str());
    game::WorldConfig config; config.freePlay=true; config.gameOver=true;
    world.Boot(config);
    std::set<int> poses;
    std::map<uint32_t,int> previous, lastWrap;
    int checked=0, holds=0, cycles=0;
    for (int frame=1;frame<=last;++frame) {
        world.SetInput(0,{}); world.SetInput(1,{}); world.Step();
        if (frame<first) continue;
        std::map<uint32_t,int> bodies;
        for (const auto& draw:world.Draws()) if (draw.fn==0x00483F00) {
            Require(draw.model>=0x21F && draw.model<=0x231,"ambient body stays in its resident wing-pose family");
            Require(bodies.emplace(draw.task,draw.model).second,"each ambient creature draws one physical body");
        }
        for (const auto& task:world.Tasks()) if (task.fn==0x00483F00) {
            if (!bodies.count(task.serial)) {
                std::fprintf(stderr,"frame %d task %u: resident ambient body disappeared\n",frame,task.serial);
                return 1;
            }
            const int id=bodies.at(task.serial);
            const auto old=previous.find(task.serial);
            if (old!=previous.end()) {
                const bool held=old->second==0x231 && id==0x231;
                Require(held || id==(old->second==0x231?0x21F:old->second+1),"only the invalid pose phase is held");
                if (held) ++holds;
            }
            if (id==0x21F) {
                const auto wrap=lastWrap.find(task.serial);
                if (wrap!=lastWrap.end()) {
                    Require(frame-wrap->second==20,"original 20-frame flight/bob cycle is preserved");
                    ++cycles;
                }
                lastWrap[task.serial]=frame;
            }
            previous[task.serial]=id;
            poses.insert(id); ++checked;
        }
    }
    Require(checked>0 && poses.size()==19 && holds>0 && cycles>0,
            "real attract flock exercises all 19 poses, held boundaries and full cycles");
    std::printf("PASS %d resident ambient bodies, %d corrected missing phases, %d complete 20-frame cycles\n",checked,holds,cycles);
    return 0;
}

int main(int argc, char** argv) {
    const bool ambient=argc==6 && std::string(argv[5])=="--ambient";
    if (argc != 5 && !ambient) return 2;
    const int first = std::stoi(argv[3]), last = std::stoi(argv[4]);
    Require(first > 0 && last >= first,"valid replay window");
    port::AssetStore assets;
    std::string error;
    Require(assets.Open(argv[1],&error),error.c_str());
    if (ambient) return AmbientTest(assets,argv[1],first,last);
    for (int id=0xBF8;id<=0xC06;++id) {
        game::Model model; std::string pol;
        Require(assets.ModelById(id,model,&pol),"face asset exists");
        Require(model.meshes.size()==1,"face frame has one material");
        const auto& mesh=model.meshes.front();
        Require(mesh.material.TextureRef()==9 && (mesh.material.Tsp() & 0x180000)!=0x80000,
                "face family uses its translucent feather texture");
    }
    game::Model foot; std::string pol;
    Require(assets.ModelById(0xC07,foot,&pol),"adjacent foot asset exists");
    Require(foot.meshes.size()==1 && foot.meshes.front().material.TextureRef()==2 &&
            (foot.meshes.front().material.Tsp() & 0x180000)==0x80000,
            "adjacent asset is an opaque foot, outside the face animation family");

    struct Input { int frame; float x,y; uint8_t buttons; };
    std::vector<Input> inputs;
    std::ifstream script(argv[2]);
    for (std::string line;std::getline(script,line);) {
        std::istringstream row(line);
        int frame,player; float x,y; unsigned buttons;
        if (row>>frame>>player>>x>>y>>buttons && player==0)
            inputs.push_back({frame,x,y,static_cast<uint8_t>(buttons)});
    }
    Require(!inputs.empty(),"input script loaded");
    game::World world;
    Require(world.Load(argv[1],0,&error),error.c_str());
    game::WorldConfig config; config.freePlay=true; config.gameOver=true;
    world.Boot(config);
    size_t pos=0; float x=.5f,y=.5f; uint8_t buttons=0;
    bool phases[30]={};
    int checked=0,midpoints=0;
    for (int frame=1;frame<=last;++frame) {
        const uint8_t previous=buttons;
        while (pos<inputs.size() && inputs[pos].frame<=frame) {
            const auto in=inputs[pos++]; x=in.x; y=in.y; buttons=in.buttons;
        }
        world.SetInput(0,game::MakeWorldInput(x,y,buttons,previous));
        world.SetInput(1,{}); world.Step();
        if (frame<first) continue;
        std::set<uint32_t> alive;
        std::map<uint32_t,int> faces;
        for (const auto& draw:world.Draws()) {
            if (draw.fn!=0x004460C0) continue;
            if (draw.model==0xBBF) alive.insert(draw.task);
            if (draw.model>=0xBF8 && draw.model<=0xC07) faces[draw.task]=draw.model;
        }
        for (uint32_t task:alive) {
            Require(faces.count(task)!=0,"live bat emits a face model");
            const int id=faces.at(task);
            if (id==0xC07) {
                std::fprintf(stderr,"frame %d task %u: face animation selected foot 0xC07\n",frame,task);
                return 1;
            }
            Require(id>=0xBF8 && id<=0xC06,"live face remains in the face asset family");
            const unsigned phase=world.Hud().frameCounter % 30;
            phases[phase]=true;
            if (phase==14 || phase==15 || phase==16) {
                Require(id==0xC06,"midpoint and its neighbors retain the last valid face frame");
                if (phase==15) ++midpoints;
            }
            if (phase==13 || phase==17) Require(id==0xC05,"neighboring original animation phases changed");
            ++checked;
        }
    }
    for (bool phase:phases) Require(phase,"all 30 live animation phases exercised");
    Require(midpoints>0,"invalid original midpoint was exercised");
    std::printf("PASS %d live bat face draws, %d corrected midpoints; all 30 phases use face assets\n",checked,midpoints);
}
