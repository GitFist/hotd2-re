// CPU-only provenance diagnostic for the six existing chapter replay frames.
// Uses Scene's actual native binding/deformation guard, without rendering.
#include "render/asset_store.h"
#include "render/naomi_deformation.h"
#include "assets/naomi_visuals.h"
#include "game/world.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {
void Require(bool ok,const char* why) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1); }
}
struct Script {
    struct Line {int frame,player;float x,y;uint8_t buttons;};
    std::vector<Line> lines;size_t pos=0;
    bool joined[2]={};float x[2]={.5f,.5f},y[2]={.5f,.5f};uint8_t held[2]={};
    explicit Script(const char* path) {
        std::ifstream in(path);
        for (std::string text;std::getline(in,text);) {
            std::istringstream row(text);Line line{};int buttons=0;
            if ((row>>line.frame>>line.player>>line.x>>line.y>>buttons) && line.player>=0 && line.player<2) {
                line.buttons=static_cast<uint8_t>(buttons);lines.push_back(line);
            }
        }
        Require(!lines.empty(),"input replay missing");
    }
    std::array<game::WorldInput,2> Frame(int frame) {
        uint8_t edges[2]={};
        while(pos<lines.size() && lines[pos].frame<=frame) {
            const auto& l=lines[pos++];joined[l.player]=true;x[l.player]=l.x;y[l.player]=l.y;
            edges[l.player]|=l.buttons&~held[l.player];held[l.player]=l.buttons;
        }
        std::array<game::WorldInput,2> out{};
        for(int p=0;p<2;++p)if(joined[p])out[p]=game::MakeWorldInput(x[p],y[p],held[p],held[p]&~edges[p]);
        if(joined[1])out[1].deviceMode=6;
        return out;
    }
};
int Part(const game::ExeImage& exe,int id) {
    uint16_t file=0,count=0;
    if(!exe.Read(0x004E83B4+id*2,&file,2) || !exe.Read(0x004E803C+file*2,&count,2))return -1;
    const auto list=exe.U32(0x004E794C+file*4);
    for(int i=0;i<count;++i)if(static_cast<uint16_t>(exe.S16(list+i*2))==id)return i;
    return -1;
}
const char* GuardFailure(const game::ModelMesh& pc,const game::ModelMesh& native) {
    if(pc.strips.size()!=native.strips.size())return "strip-count";
    for(size_t s=0;s<pc.strips.size();++s) {
        const auto& p=pc.strips[s];const auto& n=native.strips[s];
        if(p.triangleList!=n.triangleList || p.vertices.size()!=n.vertices.size())return "topology";
        for(size_t v=0;v<p.vertices.size();++v) {
            for(int k=0;k<3;++k)if(!std::isfinite(p.vertices[v].pos[k]) || !std::isfinite(n.vertices[v].pos[k]) ||
                std::abs(p.vertices[v].pos[k]-n.vertices[v].pos[k])>.001f)return "position";
            for(int k=0;k<2;++k)if(!std::isfinite(p.vertices[v].uv[k]) || !std::isfinite(n.vertices[v].uv[k]) ||
                std::abs(p.vertices[v].uv[k]-n.vertices[v].uv[k])>.001f)return "UV";
        }
    }
    return "unknown";
}
struct CameraUseAudit {
    struct Use {int count=0,first=0,last=0;float minTime=0,maxTime=0;};
    std::map<std::pair<int,int>,Use> uses;
    void Record(const game::World& world) {
        for(const auto& sample:world.Events().camera.evals) {
            auto& u=uses[{world.Events().mode,sample.first}];
            if(!u.count) {u.first=world.Frame();u.minTime=u.maxTime=sample.second;}
            ++u.count;u.last=world.Frame();
            u.minTime=std::min(u.minTime,sample.second);u.maxTime=std::max(u.maxTime,sample.second);
        }
    }
    void Print(const game::ExeImage& exe) const {
        std::map<int,std::pair<std::string,int>> names;
        for(const auto& file:game::ListCameraFiles(exe))
            for(size_t i=0;i<file.cameraIds.size();++i)
                names[file.cameraIds[i]]={file.name,static_cast<int>(i)};
        // CameraDirector evaluations only: boss/object callbacks which directly
        // read CameraBank are deliberately not claimed as covered by this log.
        for(const auto& entry:uses) {
            const auto& key=entry.first;const auto& u=entry.second;const auto n=names.find(key.second);
            std::printf("CAMERA-USE stage=%d id=%X bank=%s part=%d count=%d frames=%d..%d time=%.9g..%.9g\n",
                key.first,key.second,n==names.end()?"unknown":n->second.first.c_str(),
                n==names.end()?-1:n->second.second,u.count,u.first,u.last,u.minTime,u.maxTime);
        }
    }
};
void Audit(const game::World& world,port::AssetStore& assets,port::NaomiVisuals& bank) {
    int rigid=0,bound=0,sourceUv=0,gaps=0,gate=0,alias=0,untextured=0;
    std::set<std::tuple<int,int,int>> seen;
    std::printf("FRAME %d mode=%d scene=%d variant=%d draws=%zu native-effects=%zu\n",world.Frame(),world.GameMode(),
        world.Events().scene,world.Events().variant,world.Draws().size(),world.NativeVisualEffects().size());
    const auto& camera=world.Events().camera;
    const auto& context=camera.ctx[camera.activeCtx];
    std::printf("CAMERA-FRAME stage=%d id=%X tick=%d play=%d rail=%.9g flags=%X roll=%d context=%d pos=%.9g,%.9g,%.9g angles=%d,%d,%d\n",
        world.Events().mode,camera.camId,camera.tick,camera.playTick,camera.railTickF,camera.railFlags,
        camera.rollEnable,camera.activeCtx,context.pos[0],context.pos[1],context.pos[2],
        context.pitch,context.yaw,context.roll);
    for(const auto& draw:world.Draws()) {
        const int deformation=(draw.skinCount>0?1:0)|(draw.edit?2:0)|(draw.wavePhase>=0?4:0)|
                              (draw.uvScroll>=0?8:0)|(draw.envMap?16:0);
        const bool mountRole=draw.fn==0x0048fe90 && !deformation && draw.node>=0;
        if(!seen.emplace(draw.model,deformation,mountRole?draw.node:-1).second)continue;
        game::Model pc,native;std::string name,nativeName;
        if(!assets.ModelById(draw.model,pc,&name)) {
            std::printf("MISSING-PC model=%d fn=%08X\n",draw.model,draw.fn);continue;
        }
        if(mountRole && assets.NaomiNodeModelById(draw.model,static_cast<uint32_t>(draw.node),native,&nativeName)) {++rigid;continue;}
        if(!deformation && assets.NaomiModelById(draw.model,native,&nativeName)) {++rigid;continue;}
        for(size_t mesh=0;mesh<pc.meshes.size();++mesh) {
            game::ModelMesh source,prepared;uint32_t evidence=0;
            const bool mapped=assets.NaomiMeshById(draw.model,static_cast<uint32_t>(mesh),source,&nativeName,&evidence);
            if(mapped && evidence==12 && !deformation && port::TransferNaomiSourceUv(pc.meshes[mesh],source,&prepared)) {
                ++sourceUv;
                std::printf("APPEARANCE model=%d bank=%s part=%d mesh=%zu fn=%08X geometry=PC source-uv=%s:%u\n",
                    draw.model,name.c_str(),Part(assets.Exe(),draw.model),mesh,draw.fn,
                    prepared.textureBank.c_str(),prepared.material.TextureRef());
                continue;
            }
            const bool accepted=mapped && evidence!=12 && (!deformation || port::PrepareNaomiDeformationMesh(pc.meshes[mesh],source,&prepared,evidence==16));
            if(accepted) {++bound;continue;}
            ++gaps;if(mapped)++gate;
            const auto& material=pc.meshes[mesh].material;
            std::string texture="untextured";
            if(material.Textured()) {
                const auto texBank=assets.TextureFileFor(name);
                const auto* mapping=bank.TextureMapping(texBank,material.TextureRef());
                if(mapping && bank.CanDecodeTexture(mapping->bank,mapping->index)) {
                    texture="naomi:"+mapping->bank+":"+std::to_string(mapping->index);++alias;
                } else texture="PC:"+texBank+":"+std::to_string(material.TextureRef());
            } else ++untextured;
            std::printf("GAP model=%d bank=%s part=%d mesh=%zu fn=%08X deform=%X reason=%s texture=%s\n",
                draw.model,name.c_str(),Part(assets.Exe(),draw.model),mesh,draw.fn,deformation,
                !mapped?"no-binding":evidence==12?(deformation?"source-UV-requires-rigid":"source-UV-correspondence"):
                    GuardFailure(pc.meshes[mesh],source),texture.c_str());
        }
    }
    std::printf("SUMMARY frame=%d unique=%zu rigid-native=%d native-meshes=%d PC-shape-native-UV=%d fallback-meshes=%d guard-failures=%d native-texture-alias=%d untextured=%d\n",
        world.Frame(),seen.size(),rigid,bound,sourceUv,gaps,gate,alias,untextured);
    std::fflush(stdout);
}
}
int main(int argc,char**argv) {
    Require(argc==4,"usage: naomi_draw_audit GAME BANK ORIGINAL_INPUT_SCRIPT");
    port::AssetStore assets;port::NaomiVisuals bank;game::World world;std::string error;
    Require(assets.Open(argv[1],&error),error.c_str());
    Require(assets.LoadNaomiVisuals(argv[2],&error),error.c_str());
    Require(assets.SetNaomiGraphics(true,&error),error.c_str());
    Require(bank.Load(argv[2],&error),error.c_str());
    Require(world.Load(argv[1],0,&error),error.c_str());
    std::printf("BANK model-mappings=%zu mesh-mappings=%zu texture-mappings=%zu\n",
        bank.ModelMappingCount(),bank.MeshMappingCount(),bank.TextureMappingCount());
    game::WorldConfig config;config.eventMode=0;config.scene=0;config.gameMode=6;config.playerState[0]=0;
    config.playerState[1]=9;config.seedCounter=902;config.firstSerial=0x10000;config.startFrame=901;config.gameOver=true;
    Script script(argv[3]);auto input=script.Frame(config.startFrame);
    world.SetInput(0,input[0]);world.SetInput(1,input[1]);Require(world.StartStage(config,&error),error.c_str());
    const std::set<int> frames={12600,19500,39300,45900,67800,83400};
    CameraUseAudit cameras;
    while(world.Frame()<83400) {
        input=script.Frame(world.Frame()+1);world.SetInput(0,input[0]);world.SetInput(1,input[1]);world.Step();
        cameras.Record(world);
        if(frames.count(world.Frame()))Audit(world,assets,bank);
    }
    cameras.Print(assets.Exe());
}
