#include "render/naomi_deformation.h"
#include "render/asset_store.h"
#include "scene/scene.h"
#include "game/skin.h"
#include "game/props.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

static void Require(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct NoopGpu final:port::GpuDevice {
    port::GraphicsOptions graphics;
    std::vector<game::ModelVertex> dynamic;
    std::vector<game::ModelVertex> statics;
    std::vector<port::NaomiPrelitVertex> prelit;
    port::GpuDrawList last;
    const char* Name()const override{return "scene-light-test";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex* v,size_t n)override{if(n)statics.assign(v,v+n);else statics.clear();}
    void SetDynamicVertices(const game::ModelVertex* v,size_t n)override{if(n)dynamic.assign(v,v+n);else dynamic.clear();}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float[4])override{return true;}
    void Submit(const port::GpuDrawList& list)override{prelit=list.naomiPrelitVertices;last=list;}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return graphics;}
};

// Exercise the authored intro task and its renderer handoff: camera motion must
// scroll the reflection, while rendering a frozen snapshot must never advance it.
static void CheckCarReflection(port::AssetStore& assets,const char* directory,bool typing,int players) {
    constexpr std::array<int,3> ids={0x157e,0x157d,0x157b};
    using Glass=std::array<game::WorldDraw,3>;
    using Uvs=std::array<std::vector<std::array<float,2>>,3>;
    std::string error;game::World world;
    Require(world.Load(directory,0,&error),error.c_str());
    game::WorldConfig cfg;cfg.gameMode=6;cfg.scene=0;cfg.startFrame=901;cfg.seedCounter=12345;
    cfg.portFeatures=true;cfg.portTyping=typing;cfg.playerState[0]=0;cfg.playerState[1]=players==2?0:9;
    world.Boot(cfg);Require(world.StartStage(cfg,&error),error.c_str());
    auto glass=[&](Glass& out) {
        unsigned found=0;
        for(const auto& d:world.Draws())for(size_t i=0;i<ids.size();++i)
            if(d.fn==0x0048e600 && (d.skinModel?d.skinModel:d.model)==ids[i] && d.edit && !d.edit->uvs.empty()) {
                out[i]=d;found|=1u<<i;
            }
        return found==7;
    };
    auto render=[&](const Glass& snapshot,bool naomi) {
        Require(assets.SetNaomiGraphics(naomi,&error),error.c_str());
        NoopGpu gpu;gpu.graphics.naomiGraphics=naomi;port::ModelRenderer renderer;
        Require(renderer.Init(&gpu,&error),error.c_str());port::Scene scene;port::SceneDef definition;
        definition.useWorld=definition.externalWorld=true;Require(scene.Init(assets,definition,&error),error.c_str());
        const int frame=world.Frame();const auto random=world.RandState();const auto camera=world.Camera();
        Uvs result;
        for(size_t i=0;i<ids.size();++i) {
            auto d=snapshot[i];
            // Fix placement in front of the camera, so clipping/visibility cannot
            // hide a glass mesh while we inspect its actual uploaded UVs.
            d.viewSpace=true;d.world=game::Mat4::Translate(0,0,-100);
            const std::vector<game::WorldDraw> one={d};
            auto draw=[&] {
                const auto vp=scene.BuildFromWorld(renderer,world,&one);
                renderer.DrawGame(640,480,vp.m,&scene.lighting,&scene.fog,scene.view.m,nullptr,&scene.itemLights,scene.projection.m);
                std::vector<std::array<float,2>> uv;
                for(const auto& submission:gpu.last.draws) {
                    Require(submission.constants<gpu.last.model.size(),"car reflection material constants missing");
                    const float alpha=gpu.last.model[submission.constants].diffuse[3];
                    const bool halfAlpha=std::abs(alpha-.5f)<=(naomi?1.f/255.f:0.f);
                    if(!halfAlpha || !submission.state.linear)
                        std::fprintf(stderr,"car material %s %s model %x frame %d: alpha %.9g linear %u\n",
                                     typing?"Typing":"HOTD2",naomi?"Naomi":"PC",ids[i],world.Frame(),
                                     alpha,unsigned(submission.state.linear));
                    Require(halfAlpha && submission.state.linear,
                            "car glass lost its authored half-alpha or bilinear reflection material");
                    for(uint32_t v=submission.first;v<submission.first+submission.count;++v) {
                        const float* source=nullptr;
                        if(submission.stream==port::GpuDraw::Stream::NaomiPrelit) {
                            Require(v<gpu.prelit.size(),"car reflection prelit range invalid");source=gpu.prelit[v].uv;
                        } else {
                            const auto& vertices=submission.stream==port::GpuDraw::Stream::Dynamic?gpu.dynamic:gpu.statics;
                            Require(v<vertices.size(),"car reflection vertex range invalid");source=vertices[v].uv;
                        }
                        uv.push_back({source[0],source[1]});
                    }
                }
                Require(!uv.empty(),"car glass never reached renderer");return uv;
            };
            result[i]=draw();
            Require(result[i]==draw(),"rendering frozen car glass advanced or changed its reflection");
        }
        Require(world.Frame()==frame && world.RandState()==random &&
                std::memcmp(world.Camera().view.m,camera.view.m,sizeof(camera.view.m))==0,
                "rendering car reflections changed simulation or camera state");
        return result;
    };
    Glass first,current;int firstFrame=-1;
    std::array<Uvs,2> initial;
    while(world.Frame()<2100) {
        world.Step();if(!glass(current))continue;
        for(const auto& d:current)
            Require(d.edit->alpha==.5f && (d.edit->tspOr&0x2000),"intro car task omitted reflection material edit");
        if(firstFrame<0) {
            firstFrame=world.Frame();first=current;
            initial[0]=render(first,false);initial[1]=render(first,true);
        } else if(current[1].edit->uvs.front().uv[0]-first[1].edit->uvs.front().uv[0]>.01f)break;
    }
    Require(firstFrame>=0,"intro never submitted all three edited glass models");
    const auto du=[&](size_t i){return current[i].edit->uvs.front().uv[0]-first[i].edit->uvs.front().uv[0];};
    Require(du(0)==0 && du(1)>.01f && (players==2?du(2)<-.01f:du(2)==0),
            "intro reflection lost windshield/driver/opposite-window scrolling behavior");
    for(int native=0;native<2;++native) {
        const auto moved=render(current,native!=0);
        for(size_t i=0;i<ids.size();++i) {
            Require(moved[i].size()==initial[native][i].size(),"reflection UV edits changed car mesh topology");
            for(size_t v=0;v<moved[i].size();++v)for(size_t axis=0;axis<2;++axis) {
                const float expected=current[i].edit->uvs.front().uv[axis]-first[i].edit->uvs.front().uv[axis];
                Require(std::abs((moved[i][v][axis]-initial[native][i][v][axis])-expected)<1e-5f,
                        "PC/Naomi car renderer lost or changed the animated reflection UV offset");
            }
        }
    }
    const int movedFrame=world.Frame();
    Require(world.StartStage(cfg,&error),error.c_str());
    while(world.Frame()<firstFrame)world.Step();
    Require(glass(current),"restarted intro omitted glass reflection edits");
    for(size_t i=0;i<ids.size();++i) {
        Require(current[i].edit->uvs.size()==first[i].edit->uvs.size(),"restarted glass changed editable vertex count");
        for(size_t v=0;v<first[i].edit->uvs.size();++v)
            Require(current[i].edit->uvs[v].index==first[i].edit->uvs[v].index &&
                    std::memcmp(current[i].edit->uvs[v].uv,first[i].edit->uvs[v].uv,sizeof(first[i].edit->uvs[v].uv))==0,
                    "car reflection UV scroll accumulated across a new game");
    }
    std::printf("PASS %s %dp car reflection: frames %d -> %d, PC/Naomi uploads and restart\n",typing?"Typing":"HOTD2",players,firstFrame,movedFrame);
}
int main(int argc,char**argv) {
    Require(argc==3,"usage: naomi_deformation_test GAME BANK");
    port::AssetStore assets;std::string error;
    Require(assets.Open(argv[1],&error),error.c_str());
    Require(assets.LoadNaomiVisuals(argv[2],&error),error.c_str());
    Require(assets.SetNaomiGraphics(true,&error),error.c_str());
    size_t bound=0,deformed=0,textured=0;
    for (auto [id,address]:game::SkinDefsByModel(assets.Exe())) {
        game::Model pc;Require(assets.ModelById(id,pc,nullptr),"skin source unavailable");
        const auto pcOriginal=pc;
        game::Model prepared=pc;
        bool any=false;
        for (size_t m=0;m<pc.meshes.size();++m) {
            game::ModelMesh native,mesh;uint32_t evidence=0;
            if (!assets.NaomiMeshById(id,static_cast<uint32_t>(m),native,nullptr,&evidence) ||
                !port::PrepareNaomiDeformationMesh(pc.meshes[m],native,&mesh,evidence==16)) continue;
            ++bound;any=true;
            Require(!mesh.textureBank.empty(),"native material lost texture namespace");
            for (size_t s=0;s<mesh.strips.size();++s)
                Require(mesh.strips[s].offsets==pc.meshes[m].strips[s].offsets,"native record addresses replaced PC skin addresses");
            if (mesh.material.Textured()) {
                const auto* texture=assets.Texture(mesh.textureBank,mesh.material.TextureRef());
                Require(texture!=nullptr,"bound skin material fell back or has missing texture");++textured;
            }
            prepared.meshes[m]=std::move(mesh);
        }
        if (!any) continue;
        game::SkinDef def;Require(game::LoadSkinDef(assets.Exe(),address,def),"skin definition unavailable");
        game::Mat4 nodes[4];
        for(int n=0;n<4;++n)nodes[n]=game::Mat4::RotateY(1700+n*1100)*game::Mat4::Translate(float(n+1),2,-3);
        const auto original=prepared;
        game::SkinDeform(def,pc.blockStart,game::Mat4::Identity(),nodes,4,pc);
        game::SkinDeform(def,prepared.blockStart,game::Mat4::Identity(),nodes,4,prepared);
        for(size_t m=0;m<prepared.meshes.size();++m) {
            if(prepared.meshes[m].textureBank.empty())continue;
            const auto& mesh=prepared.meshes[m];
            Require(mesh.material.TextureRef()==original.meshes[m].material.TextureRef() &&
                    mesh.textureBank==original.meshes[m].textureBank,"skinning lost native material/texture");
            for(size_t s=0;s<mesh.strips.size();++s)for(size_t v=0;v<mesh.strips[s].vertices.size();++v) {
                const auto& actual=mesh.strips[s].vertices[v];const auto& before=original.meshes[m].strips[s].vertices[v];
                const auto& expected=pc.meshes[m].strips[s].vertices[v];
                const auto& source=pcOriginal.meshes[m].strips[s].vertices[v];
                if(std::memcmp(expected.pos,source.pos,sizeof(expected.pos))==0 &&
                   std::memcmp(expected.normal,source.normal,sizeof(expected.normal))==0)continue;
                Require(std::memcmp(actual.pos,expected.pos,sizeof(actual.pos))==0 &&
                        std::memcmp(actual.normal,expected.normal,sizeof(actual.normal))==0,"skin write missed a remapped native vertex");
                Require(std::memcmp(actual.uv,before.uv,sizeof(actual.uv))==0,"skinning corrupted native UVs");++deformed;
            }
        }
    }
    Require(bound>0 && deformed>0 && textured>0,"real skinned native fixture coverage missing");
    game::Model pcSkin;game::ModelMesh authoredSkin,preparedSkin;uint32_t skinEvidence=0;
    Require(assets.ModelById(674,pcSkin,nullptr) && !assets.NaomiMeshById(674,0,authoredSkin,nullptr),
            "authored native skin UV exposed to an unaware caller");
    Require(assets.NaomiMeshById(674,0,authoredSkin,nullptr,&skinEvidence) && skinEvidence==16 &&
            !port::PrepareNaomiDeformationMesh(pcSkin.meshes[0],authoredSkin,&preparedSkin) &&
            port::PrepareNaomiDeformationMesh(pcSkin.meshes[0],authoredSkin,&preparedSkin,true),
            "original skin-record proof did not restore authored native UVs safely");
    size_t authoredChanges=0;
    for(size_t s=0;s<preparedSkin.strips.size();++s) {
        Require(preparedSkin.strips[s].offsets==pcSkin.meshes[0].strips[s].offsets,"authored UV source replaced runtime skin addresses");
        for(size_t v=0;v<preparedSkin.strips[s].vertices.size();++v) {
            const auto& actual=preparedSkin.strips[s].vertices[v];
            Require(std::memcmp(actual.uv,authoredSkin.strips[s].vertices[v].uv,sizeof(actual.uv))==0,"skin binding changed original native UVs");
            authoredChanges+=std::memcmp(actual.uv,pcSkin.meshes[0].strips[s].vertices[v].uv,sizeof(actual.uv))!=0;
        }
    }
    Require(authoredChanges>0,"authored native skin fixture did not exercise changed UVs");
    // An unsupported layout must fail atomically, rather than silently applying
    // PC offsets to a different native vertex walk.
    game::ModelMesh pc,native,output;pc.strips.resize(1);native.strips.resize(2);output.textureBank="unchanged";
    Require(!port::PrepareNaomiDeformationMesh(pc,native,&output) && output.textureBank=="unchanged","different native topology accepted or failure mutated output");
    // Equal geometry may carry different low-bit record markers after source
    // conversion. The PC wave/page-bend walk must still recognize its vertices.
    pc.strips[0].ctrl=0x20;pc.strips[0].vertices.resize(1);pc.strips[0].offsets={1234};
    auto& vertex=pc.strips[0].vertices[0];vertex.pos[0]=1;vertex.pos[1]=10;
    uint32_t bits=0;std::memcpy(&bits,&vertex.pos[0],4);bits|=1;std::memcpy(&vertex.pos[0],&bits,4);
    native=pc;native.strips[0].vertices[0].pos[0]=1;native.strips[0].offsets={999};native.strips[0].ctrl=0;
    Require(port::PrepareNaomiDeformationMesh(pc,native,&output),"matching wave geometry rejected");
    auto authoredUv=native;authoredUv.strips[0].vertices[0].uv[0]=.75f;
    Require(!port::PrepareNaomiDeformationMesh(pc,authoredUv,&output),"unproven changed native skin UV accepted");
    Require(port::PrepareNaomiDeformationMesh(pc,authoredUv,&output,true) && output.strips[0].vertices[0].uv[0]==.75f &&
            output.strips[0].offsets==pc.strips[0].offsets,"source-proven authored skin UV lost original appearance or PC write addresses");
    authoredUv.strips[0].vertices[0].uv[0]=std::numeric_limits<float>::infinity();output.textureBank="unchanged";
    Require(!port::PrepareNaomiDeformationMesh(pc,authoredUv,&output,true) && output.textureBank=="unchanged",
            "authored skin UV proof bypassed finite/atomic transfer guards");
    Require(port::PrepareNaomiDeformationMesh(pc,native,&output),"wave fixture restore rejected");
    game::Model waving;waving.meshes.push_back(output);
    Require(game::DecorWave(waving,0,0x2000) && waving.meshes[0].strips[0].vertices[0].pos[2]!=0,"native binding disabled a PC inline wave/page-bend record");
    native=pc;native.textureBank="naomi:source.bin";native.material.words[8]=17;
    native.strips[0].vertices[0].uv[1]=.875f;native.strips[0].vertices[0].normal[2]=1;
    native.strips.push_back(native.strips[0]);native.strips.back().vertices[0].normal[2]=-1;
    Require(port::TransferNaomiSourceUv(pc,native,&output),"unambiguous native UV rejected because seam normals differ");
    Require(output.strips[0].vertices[0].uv[1]==.875f && output.strips[0].vertices[0].normal[2]==pc.strips[0].vertices[0].normal[2] &&
            output.strips[0].offsets==pc.strips[0].offsets && output.textureBank==native.textureBank,"native UV transfer changed seam geometry or lost appearance");
    native.strips.back().vertices[0].uv[1]=.5f;output.textureBank="unchanged";
    Require(!port::TransferNaomiSourceUv(pc,native,&output) && output.textureBank=="unchanged","ambiguous source UV accepted or failed transfer partially changed output");
    game::Model wall;Require(assets.ModelById(6180,wall,nullptr),"PC stage surface unavailable");
    uint32_t evidence=0;game::ModelMesh sourceUv,wallUv;
    Require(!assets.NaomiMeshById(6180,101,sourceUv,nullptr),"UV-only source exposed as a full replacement to unaware caller");
    Require(assets.NaomiMeshById(6180,101,sourceUv,nullptr,&evidence) && evidence==12,"verified regrouped native wall source unavailable");
    Require(port::TransferNaomiSourceUv(wall.meshes[101],sourceUv,&wallUv),"original wall UV correspondence failed runtime guard");
    size_t uvChanged=0;
    for(size_t s=0;s<wallUv.strips.size();++s) {
        Require(wallUv.strips[s].ctrl==wall.meshes[101].strips[s].ctrl && wallUv.strips[s].offsets==wall.meshes[101].strips[s].offsets,"source UV transfer changed PC surface connectivity");
        for(size_t v=0;v<wallUv.strips[s].vertices.size();++v) {
            const auto& a=wallUv.strips[s].vertices[v];const auto& b=wall.meshes[101].strips[s].vertices[v];
            Require(std::memcmp(a.pos,b.pos,sizeof(a.pos))==0 && std::memcmp(a.normal,b.normal,sizeof(a.normal))==0,"source UV transfer selected an ambiguous seam position/normal");
            uvChanged+=std::memcmp(a.uv,b.uv,sizeof(a.uv))!=0;
        }
    }
    const auto* wallTexture=assets.Texture(wallUv.textureBank,wallUv.material.TextureRef());
    Require(uvChanged>0 && wallTexture && wallTexture->width==128 && wallTexture->height==128,"native wall UVs or higher-resolution original artwork fell back");
    // Exercise Scene's light cache, rather than only the isolated GPU kernel:
    // two differently colored TABLE submissions must retain both native tints.
    game::World world;Require(world.Load(argv[1],0,&error),error.c_str());
    game::WorldConfig config;config.gameMode=6;config.eventMode=0;config.scene=0;config.startFrame=901;
    Require(world.StartStage(config,&error),error.c_str());world.Step();
    NoopGpu gpu;gpu.graphics.naomiGraphics=true;port::ModelRenderer renderer;
    Require(renderer.Init(&gpu,&error),error.c_str());port::Scene scene;port::SceneDef definition;
    definition.useWorld=true;definition.externalWorld=true;Require(scene.Init(assets,definition,&error),error.c_str());
    port::Scene cameraScene;port::SceneDef cameraDefinition;cameraDefinition.cameraId=0x22;
    Require(cameraScene.Init(assets,cameraDefinition,&error),error.c_str());cameraScene.Build(renderer,319);
    constexpr uint32_t churchPosition[3]={0xc160c1fb,0x41989dfb,0x431ee240};
    Require(std::memcmp(cameraScene.pos,churchPosition,sizeof(churchPosition))==0 && cameraScene.pitch==9255 && cameraScene.yaw==192,
            "static native scene did not use original camera curve/SDK look arithmetic");
    constexpr uint32_t nativeProjection[2]={0x40000117,0x402aac1f};uint32_t projectionBits[2]{};
    std::memcpy(&projectionBits[0],&cameraScene.projection.m[0],4);std::memcpy(&projectionBits[1],&cameraScene.projection.m[5],4);
    Require(std::memcmp(projectionBits,nativeProjection,sizeof(projectionBits))==0,"native static scene projection differs from original stored SDK matrix");
    Require(cameraScene.projection.m[10]==-1.f && cameraScene.projection.m[14]==-9.999999747378752e-6f,
            "native static scene retained the PC clipping distance or imposed an SDK far cutoff");
    std::vector<game::WorldDraw> draws(2);
    for(int i=0;i<2;++i){draws[i].model=0x3a;draws[i].world=game::Mat4::Translate(0,0,-10);draws[i].light.table=true;
        for(int k=0;k<3;++k)draws[i].light.color[k]=k==i?1.f:0.f;}
    scene.BuildFromWorld(renderer,world,&draws);
    std::memcpy(&projectionBits[0],&scene.projection.m[0],4);std::memcpy(&projectionBits[1],&scene.projection.m[5],4);
    Require(std::memcmp(projectionBits,nativeProjection,sizeof(projectionBits))==0,"native world scene projection differs from original stored SDK matrix");
    Require(scene.projection.m[10]==-1.f && scene.projection.m[14]==-9.999999747378752e-6f,
            "native world scene did not preserve reciprocal-depth/near-clip semantics");
    Require(scene.itemLights.size()==2 && scene.itemLights[0].naomiColor[0]==1 && scene.itemLights[0].naomiColor[1]==0 &&
            scene.itemLights[1].naomiColor[0]==0 && scene.itemLights[1].naomiColor[1]==1,"native Scene TABLE cache merged different per-item RGB tints");
    // PC section files split some original objects and merge others. Restore
    // the original sources exactly once at each distinct placement/draw state.
    auto stageDraw=[](int model) {
        game::WorldDraw d;d.model=model;d.fn=0x00401260;d.world=game::Mat4::Identity();return d;
    };
    auto sourceCount=[&](std::vector<game::WorldDraw> submissions) {
        scene.BuildFromWorld(renderer,world,&submissions);
        int count=0;while(scene.ItemModelId(count)>=0)++count;return count;
    };
    for(const auto pair:{std::array<int,2>{6147,6148},{6156,6157}}) {
        Require(sourceCount({stageDraw(pair[0]),stageDraw(pair[1])})==1,"split PC stage files drew the same original object twice");
        Require(sourceCount({stageDraw(pair[0])})==1 && sourceCount({stageDraw(pair[1])})==1,"one split PC file could not restore its whole original object independently");
    }
    Require(sourceCount({stageDraw(6180)})==2,"merged PC stage file omitted one original source");
    Require(sourceCount({stageDraw(6131),stageDraw(6180),stageDraw(6129)})==2 &&
            sourceCount({stageDraw(6129),stageDraw(6180),stageDraw(6131)})==2,"primary/extra sources were not independently deduplicated");
    auto left=stageDraw(6147),right=stageDraw(6148);
    right.world=game::Mat4::Translate(1,0,0);
    Require(sourceCount({left,right})==2,"distinct original stage placements were merged");
    right=stageDraw(6148);right.drawMode=7;
    Require(sourceCount({left,right})==2,"distinct original stage draw modes were merged");
    right=stageDraw(6148);right.light.color[0]=.5f;
    Require(sourceCount({left,right})==2,"distinct original stage lights were merged");
    right=stageDraw(6148);left.fn=right.fn=0;
    Require(sourceCount({left,right})==2,"non-stage source was regrouped without original section evidence");
    left=stageDraw(6147);right=stageDraw(6148);left.envMap=right.envMap=true;
    Require(sourceCount({left,right})==2,"deformed source was regrouped without an ordered vertex correspondence");
    auto mountFrame=[&](uint32_t fn,int node,bool manual,bool native,bool inView=false) {
        NoopGpu recorder;recorder.graphics.naomiGraphics=true;port::ModelRenderer modelRenderer;
        Require(modelRenderer.Init(&recorder,&error),error.c_str());port::Scene sourceScene;
        Require(sourceScene.Init(assets,definition,&error),error.c_str());
        auto d=stageDraw(668);d.fn=fn;d.node=node;d.viewSpace=inView;
        if(inView)d.world=world.Camera().view;
        const std::vector<game::WorldDraw> one={d};const auto vp=sourceScene.BuildFromWorld(modelRenderer,world,&one);
        game::Model expected;std::string expectedBank;
        if(manual) {
            Require(native?assets.NaomiNodeModelById(668,8,expected,&expectedBank):assets.ModelById(668,expected,&expectedBank),
                    "mount reference source unavailable");
            port::ModelRenderer::SceneItem item;item.model=&expected;item.polName=expectedBank;item.cullable=false;item.light=0;
            const auto transform=inView?d.world*world.Camera().inverse:d.world;
            std::memcpy(item.transform,transform.m,sizeof(item.transform));
            modelRenderer.ClearCache();modelRenderer.SetScene(assets,{item});
        }
        modelRenderer.DrawGame(640,480,vp.m,&sourceScene.lighting,&sourceScene.fog,sourceScene.view.m,nullptr,&sourceScene.itemLights,sourceScene.projection.m);
        uint64_t hash=1469598103934665603ull;
        auto add=[&](const void* p,size_t n){const auto* bytes=static_cast<const uint8_t*>(p);for(size_t i=0;i<n;++i){hash^=bytes[i];hash*=1099511628211ull;}};
        for(const auto& draw:recorder.last.draws) {
            add(&draw.state,sizeof(draw.state));add(&draw.count,sizeof(draw.count));
            Require(draw.constants<recorder.last.model.size(),"mount reference model constants missing");
            add(&recorder.last.model[draw.constants],sizeof(port::ModelConstants));
            if(draw.stream==port::GpuDraw::Stream::NaomiPrelit) {
                Require(draw.first+draw.count<=recorder.prelit.size(),"mount prelit range invalid");
                add(recorder.prelit.data()+draw.first,draw.count*sizeof(port::NaomiPrelitVertex));
            } else {
                const auto& vertices=draw.stream==port::GpuDraw::Stream::Dynamic?recorder.dynamic:recorder.statics;
                Require(draw.first+draw.count<=vertices.size(),"mount source vertex range invalid");
                add(vertices.data()+draw.first,draw.count*sizeof(game::ModelVertex));
            }
        }
        Require(!recorder.last.draws.empty(),"mount fixture never submitted geometry");return hash;
    };
    Require(mountFrame(0x0048fe90,8,false,false)==mountFrame(0x0048fe90,8,true,true),"mount node8 did not render the source-proven native model/material");
    Require(mountFrame(0x0048fe90,8,false,false,true)==mountFrame(0x0048fe90,8,true,true,true),"actual view-space mount node8 did not render its native source");
    Require(mountFrame(0x0048fe90,2,false,false)==mountFrame(0x0048fe90,2,true,false),"native node8 model leaked into the reused hidden node2");
    Require(mountFrame(0x00426c30,8,false,false)==mountFrame(0x00426c30,8,true,false),"native mount model leaked into another task context");
    auto fade=stageDraw(0x93e);fade.fn=0;fade.viewSpace=true;fade.alpha=.5f;
    fade.world=game::ScaleMatrix(200,400,1)*game::Mat4::Translate(0,0,-.1f);
    const std::vector<game::WorldDraw> fadeDraws={fade};
    const auto fadeVp=scene.BuildFromWorld(renderer,world,&fadeDraws);gpu.dynamic.clear();gpu.prelit.clear();
    renderer.DrawGame(640,480,fadeVp.m,&scene.lighting,&scene.fog,scene.view.m,nullptr,&scene.itemLights,scene.projection.m);
    Require(!gpu.dynamic.empty() || !gpu.prelit.empty(),"native close ending overlay never entered the exact camera-space stream");
    for(const auto& v:gpu.dynamic)Require(std::abs(v.pos[2]+.1f)<1e-6f,"native ending fade retained the PC clipping workaround or lossy camera round trip");
    for(const auto& v:gpu.prelit)Require(std::abs(v.pos[2]+.1f)<1e-6f,"native lit ending fade retained the PC clipping workaround or lossy camera round trip");
    Require(assets.SetNaomiGraphics(false,&error),error.c_str());gpu.graphics.naomiGraphics=false;
    cameraScene.Build(renderer,319);
    Require(std::memcmp(cameraScene.pos,churchPosition,sizeof(churchPosition))!=0,"native camera curve math leaked into PC scene");
    const auto pcProjection=game::DeviceProjection();
    Require(cameraScene.projection.m[0]==pcProjection.m[0] && cameraScene.projection.m[5]==pcProjection.m[5],"native projection leaked into PC scene");
    scene.BuildFromWorld(renderer,world,&draws);
    Require(scene.itemLights.size()==1,"native TABLE color cache changed legacy PC light grouping");
    Require(sourceCount({stageDraw(6147),stageDraw(6148)})==2 && sourceCount({stageDraw(6180)})==1,
            "original stage regrouping leaked into PC mode");
    game::ModelMesh none;Require(!assets.NaomiMeshById(0x3a,0,none,nullptr),"native binding leaked into PC profile");
    for(int players:{1,2})CheckCarReflection(assets,argv[1],false,players);
    const auto typingBank=std::filesystem::path(argv[1])/"typing_reference.bank";
    if(std::filesystem::exists(typingBank)) {
        Require(assets.LoadTypingReference(typingBank.string(),&error),error.c_str());
        for(int players:{1,2})CheckCarReflection(assets,argv[1],true,players);
    }
    std::printf("PASS: %zu native skinned meshes, %zu deformed vertices, %zu native textures\n",bound,deformed,textured);
}
