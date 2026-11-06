// Native camera-space flash precision and bounded autosort compositing. Uses
// actual P1/P2 assets; no emulator, BIOS or timing-sensitive capture is needed.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/camera.h"
#include "game/mstack.h"
#include "render/model_renderer.h"
#include "render/render_pose.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_D3D12
#include "render/d3d12_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
using Item=port::ModelRenderer::SceneItem;
struct RecordedGpu final:port::GpuDevice {
    port::GraphicsOptions graphics;
    port::GpuDrawList last;
    std::vector<game::ModelVertex> vertices;
    std::vector<game::ModelVertex> dynamicVertices;
    const char* Name()const override{return "recorded";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex* v,size_t n)override{if(n)vertices.assign(v,v+n);else vertices.clear();}
    void SetDynamicVertices(const game::ModelVertex* v,size_t n)override{if(n)dynamicVertices.assign(v,v+n);else dynamicVertices.clear();}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float[4])override{return true;}
    void Submit(const port::GpuDrawList& v)override{last=v;}
    void EndFrame()override{}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}
    int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return graphics;}
};
struct CameraPose {game::Mat4 world,view;};
static CameraPose Camera(bool far,float dx=0) {
    if(!far)return{game::Mat4::Identity(),game::Mat4::Identity()};
    // Authored Chapter 5 camera 215, t=770. The old MV->world->MV path
    // subtracts positions of order 8800 to recover a muzzle depth of one.
    const float p[3]={578.0675659f+dx,-22.5590229f,-8795.1708984f};
    auto world=game::Mat4::Identity();
    game::mx::Translate(world,p[0],p[1],p[2]);
    game::mx::RotY(world,-535);game::mx::RotX(world,-1169);
    return{world,game::ViewMatrix(p,-1169,-535,0)};
}
static game::Mat4 FlashMatrix(float x,float y,bool secondary) {
    int pitch=0,yaw=0;game::mx::DirToAngles(x,y,-1,&pitch,&yaw);
    auto mv=game::Mat4::Identity();game::mx::Translate(mv,x,y,-1);
    game::mx::RotY(mv,yaw+0x8000);game::mx::RotX(mv,-pitch);
    game::mx::Scale(mv,.1f,.1f,.1f);
    if(secondary)game::mx::Scale(mv,.5f,.5f,.5f);
    return mv;
}
static Item Effect(const game::Model& model,const std::string& pol,const game::Mat4& mv,const CameraPose& camera) {
    Item item;item.model=&model;item.polName=pol;item.priority=0xC;
    item.directModelView=true;std::memcpy(item.modelView,mv.m,sizeof(item.modelView));
    const auto world=mv*camera.world;std::memcpy(item.transform,world.m,sizeof(item.transform));
    item.shadowCaster=false;
    return item;
}
static void Record(port::ModelRenderer& renderer,RecordedGpu& gpu,port::AssetStore& assets,
                   const std::vector<Item>& items,const CameraPose& camera,const game::Mat4& projection) {
    renderer.SetScene(assets,items);const auto vp=camera.view*projection;
    renderer.DrawGame(640,480,vp.m,nullptr,nullptr,camera.view.m,nullptr,nullptr,projection.m);
    Require(!gpu.last.draws.empty(),"native view effects lost all draws");
    Require(renderer.lastPass1.size()==items.size(),"native effect pass count");
    const auto identity=game::Mat4::Identity();
    for(size_t i=0;i<gpu.last.draws.size();++i) {
        const auto& draw=gpu.last.draws[i];const auto& c=gpu.last.model[draw.constants];
        Require(std::memcmp(c.modelView,identity.m,sizeof(c.modelView))==0,"sorted native vertices were projected through the world camera");
        Require(draw.stream==port::GpuDraw::Stream::Dynamic&&draw.first<=gpu.dynamicVertices.size()&&
                draw.count<=gpu.dynamicVertices.size()-draw.first,"sorted flash range exceeds merged dynamic stream");
        Require(draw.state.zfunc==4&&!draw.state.zwrite,"native autosort failed to preserve opaque depth without writes");
        Require(c.flags[2]<0,"native sorted translucency retained PC alpha testing");
    }
    for(const auto& e:renderer.lastPass1)Require(std::isfinite(e.depth),"non-finite native geometry sort depth");
    for(const auto& v:gpu.dynamicVertices)for(float p:v.pos)Require(std::isfinite(p),"non-finite split flash vertex");
}
static std::vector<uint8_t> Pixels(port::GpuDevice& gpu,const port::GpuDrawList& list,const float clear[4]) {
    Require(gpu.BeginFrame(clear),"begin muzzle frame");gpu.Submit(list);
    std::vector<uint8_t> pixels;Require(gpu.ReadPixels(&pixels),"read muzzle pixels");gpu.EndFrame();return pixels;
}
static game::Model Quad(bool primary) {
    game::Model m;m.sphere[3]=4;game::ModelMesh mesh;auto* w=mesh.material.words;
    w[0]=0x8200002c;w[1]=0x83000000;w[2]=primary?0x84902412:0x94902424;
    w[8]=0;w[9]=0xffffffffu; // test supplies a native DECAL texture after recording
    const float material[5]={.75f,primary?.92f:.44f,1,1,1};std::memcpy(w+10,material,sizeof(material));
    const float radius=4;std::memcpy(w+7,&radius,4);
    game::ModelStrip strip;strip.ctrl=0x40;
    for(int i=0;i<4;++i) {
        game::ModelVertex v{};v.pos[0]=(i&1)?1.f:-1.f;v.pos[1]=(i&2)?1.f:-1.f;
        // The front geometry includes a foot on the rear plane, as live frames
        // 4/7/8 do; its interior still lies in front of the secondary quad.
        v.pos[2]=primary?.5f*float((i&1)+((i&2)?2:0)):0.f;
        if(!primary){v.pos[0]*=2;v.pos[1]*=2;}
        v.normal[2]=1;strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}

int main(int argc,char** argv) {
    if(argc<3){std::fprintf(stderr,"usage: render_naomi_muzzle_test <game directory> <naomi bank> [d3d12|sdlgpu]\n");return 2;}
    std::string error;port::GpuDeviceDesc desc;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_D3D12
    if(argc>3&&std::strcmp(argv[3],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#endif
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>3&&std::strcmp(argv[3],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());auto options=gpu->EffectiveGraphics();options.naomiGraphics=true;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    RecordedGpu recorded;recorded.graphics.naomiGraphics=true;
    port::ModelRenderer recorder,renderer;Require(recorder.Init(&recorded,&error)&&renderer.Init(gpu.get(),&error),error.c_str());
    const auto projection=game::DeviceView()*game::DeviceProjection();
    port::AssetStore empty;
    const float clear[4]={20.f/255,20.f/255,20.f/255,1};

    // Independent alpha equation. The source materials deliberately retain the
    // native .44/.92 face alphas; DECAL uses the texture alpha instead.
    const auto primary=Quad(true),secondary=Quad(false);const auto near=Camera(false);
    port::RgbaImage front,rear;front.width=front.height=rear.width=rear.height=1;
    front.pixels={0,96,32,128};rear.pixels={160,32,0,128};
    const auto frontTexture=gpu->CreateTexture(front),rearTexture=gpu->CreateTexture(rear);
    Require(frontTexture&&rearTexture,"known-color textures");
    auto known=[&](float x,float y) {
        std::vector<Item> items={Effect(primary,"",FlashMatrix(x,y,false),near),Effect(secondary,"",FlashMatrix(x,y,true),near)};
        Record(recorder,recorded,empty,items,near,projection);
        Require(recorder.lastPass1[0].item==1&&recorder.lastPass1[1].item==0,"rear quad must precede front additive geometry");
        auto list=recorded.last;gpu->SetStaticVertices(recorded.vertices.data(),recorded.vertices.size());
        gpu->SetDynamicVertices(recorded.dynamicVertices.data(),recorded.dynamicVertices.size());
        for(auto& draw:list.draws){draw.texture=draw.state.dstBlend==2?frontTexture:rearTexture;list.model[draw.constants].flags[0]=1;}
        const auto pixels=Pixels(*gpu,list,clear);
        const int px=int(std::round(320+320*projection.m[0]*x));
        const int py=int(std::round(240-240*projection.m[5]*y));
        const size_t at=(size_t(py)*640+px)*4;
        for(int k=0;k<3;++k) {
            const float alpha=128.f/255;
            const float expected=(20*(1-alpha)+rear.pixels[k]*alpha)+front.pixels[k]*alpha;
            Require(std::abs(int(pixels[at+k])-int(std::round(expected)))<=1,"native rear-alpha/front-additive compositing equation");
        }
        // A later opaque depth probe behind the flash must still pass: sorted
        // translucency cannot leave a depth write that rejects unrelated draws.
        auto probe=list;probe.draws={list.draws[0]};auto& d=probe.draws[0];auto& c=probe.model[d.constants];
        d.state.blendEnable=0;d.state.zwrite=1;d.texture=rearTexture;
        // Native DECAL + IgnoreTexAlpha: the depth probe must output the raw
        // texture color regardless of the flash's baked face color/alpha.
        c.flags[3]=20;
        auto merged=recorded.dynamicVertices;const uint32_t first=static_cast<uint32_t>(merged.size());
        for(uint32_t i=0;i<d.count;++i)merged.push_back(recorded.dynamicVertices[d.first+i]);
        d.first=first;
        const auto probeAt=[&](float depth) {
            for(uint32_t i=0;i<d.count;++i)for(int k=0;k<3;++k)
                merged[first+i].pos[k]=recorded.dynamicVertices[list.draws[0].first+i].pos[k]*depth;
            gpu->SetDynamicVertices(merged.data(),merged.size());
        };
        probeAt(1.2f);const auto withProbe=[&] {
            Require(gpu->BeginFrame(clear),"begin depth probe");gpu->Submit(list);gpu->Submit(probe);
            std::vector<uint8_t> p;Require(gpu->ReadPixels(&p),"read depth probe");gpu->EndFrame();return p;
        }();
        for(int k=0;k<3;++k)Require(withProbe[at+k]==rear.pixels[k],"translucent muzzle wrote depth");
        // Opaque foreground at the sampled pixel blocks both effect layers.
        probeAt(.81f);const auto occluded=[&] {
            Require(gpu->BeginFrame(clear),"begin opaque occlusion");gpu->Submit(probe);gpu->Submit(list);
            std::vector<uint8_t> p;Require(gpu->ReadPixels(&p),"read opaque occlusion");gpu->EndFrame();return p;
        }();
        for(int k=0;k<3;++k)Require(occluded[at+k]==rear.pixels[k],"native effect bypassed opaque depth test");
    };
    for(auto xy:{std::pair<float,float>{0.f,0.f},{-.35f,-.22f},{.35f,.22f},{-.4f,.25f},{.4f,-.25f}})known(xy.first,xy.second);

    // Intersecting rapid-shot planes reverse their order across the screen.
    // A single centroid/model order fails one side; BSP must split the geometry.
    auto crossedRed=Quad(false),crossedGreen=Quad(false);
    const float redTag=.41f,greenTag=.61f;
    std::memcpy(crossedRed.meshes[0].material.words+11,&redTag,4);
    std::memcpy(crossedGreen.meshes[0].material.words+11,&greenTag,4);
    for(auto pair:{std::pair<game::Model*,float>{&crossedRed,.5f},{&crossedGreen,-.5f}})
        for(auto& v:pair.first->meshes[0].strips[0].vertices){v.pos[0]*=.1f;v.pos[1]*=.1f;v.pos[2]=-1+pair.second*v.pos[0];}
    std::vector<uint8_t> crossedBaseline;
    for(bool farCamera:{false,true}) {
        const auto camera=Camera(farCamera);
        const auto identity=game::Mat4::Identity();
        Record(recorder,recorded,empty,{Effect(crossedRed,"",identity,camera),Effect(crossedGreen,"",identity,camera)},camera,projection);
        Require(recorded.dynamicVertices.size()>12,"crossing translucent planes were not split");
        auto list=recorded.last;
        for(auto& d:list.draws){d.texture=list.model[d.constants].diffuse[3]<.5f?rearTexture:frontTexture;list.model[d.constants].flags[0]=1;}
        gpu->SetStaticVertices(recorded.vertices.data(),recorded.vertices.size());
        gpu->SetDynamicVertices(recorded.dynamicVertices.data(),recorded.dynamicVertices.size());
        const auto pixels=Pixels(*gpu,list,clear);
        for(int x:{288,352})for(int k=0;k<3;++k) {
            const float a=128.f/255;
            const auto& back=x<320?rear.pixels:front.pixels;
            const auto& frontColor=x<320?front.pixels:rear.pixels;
            const float expected=20*(1-a)*(1-a)+back[k]*a*(1-a)+frontColor[k]*a;
            Require(std::abs(int(pixels[(240*640+x)*4+k])-int(std::round(expected)))<=1,"intersecting native alpha planes composite in wrong order");
        }
        if(farCamera)Require(pixels==crossedBaseline,"far camera changed split-plane pixels");else crossedBaseline=pixels;
    }

    // Reorder only the native slots; an unrelated model and sprite keep their
    // existing positions even when their depths fall between the native layers.
    auto frontItem=Effect(primary,"",FlashMatrix(0,0,false),near);
    auto rearItem=Effect(secondary,"",FlashMatrix(0,0,true),near);
    auto other=rearItem;other.directModelView=false;other.cullable=false;other.noMeshCull=true;other.transform[14]=-.98f;
    other.dynamicGeometry=true;
    port::ModelRenderer::SpriteDraw sprite;sprite.key=0x8000000C;sprite.depth=-.99f;
    const std::vector<port::ModelRenderer::SpriteDraw> sprites={sprite};
    recorder.SetScene(empty,{frontItem,other,rearItem});
    recorder.DrawGame(640,480,projection.m,nullptr,nullptr,near.view.m,&sprites,nullptr,projection.m);
    Require(recorder.lastPass1.size()==4&&recorder.lastPass1[0].item==1&&recorder.lastPass1[1].sprite==0&&
            recorder.lastPass1[2].item==2&&recorder.lastPass1[3].item==0,"native sorting moved unrelated draw slots");
    std::vector<game::ModelVertex> prefix;game::Triangulate(secondary.meshes[0].strips[0],prefix);
    Require(recorded.dynamicVertices.size()>prefix.size()&&
            std::memcmp(prefix.data(),recorded.dynamicVertices.data(),prefix.size()*sizeof(game::ModelVertex))==0,
            "sorted flashes overwrote unrelated dynamic geometry");

    // Actual native frames, including coexisting P1/P2 flashes from rapid shots.
    renderer.ClearCache();recorder.ClearCache();
    port::AssetStore assets;Require(assets.Open(argv[1],&error)&&assets.LoadNaomiVisuals(argv[2],&error)&&assets.SetNaomiGraphics(true,&error),error.c_str());
    struct Asset {game::Model model;std::string pol;};std::array<Asset,36> models;
    auto asset=[&](int player,bool back,int frame)->Asset&{return models[(player*2+int(back))*9+frame];};
    for(int p=0;p<2;++p)for(bool back:{false,true})for(int f=0;f<9;++f) {
        auto& a=asset(p,back,f);const int base=back?(p?0xB84:0xB76):(p?0x17F:0x175);
        Require(assets.NaomiModelById(base+f,a.model,&a.pol),"actual native flash family mapping");
    }
    const auto far=Camera(true);size_t frames=0;
    auto itemsAt=[&](const CameraPose& camera,int frame,float x,float y) {
        std::vector<Item> out;
        for(int shot=0;shot<3;++shot)for(bool back:{false,true}) {
            const int player=shot&1,age=(frame+shot*2)%9;auto& a=asset(player,back,age);
            out.push_back(Effect(a.model,a.pol,FlashMatrix(x+shot*.008f,y-shot*.006f,back),camera));
        }
        return out;
    };
    auto render=[&](const std::vector<Item>& items,const CameraPose& camera) {
        renderer.SetScene(assets,items);const auto vp=camera.view*projection;
        Require(gpu->BeginFrame(clear),"begin actual flash frame");
        renderer.DrawGame(640,480,vp.m,nullptr,nullptr,camera.view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> p;Require(gpu->ReadPixels(&p),"read actual flash frame");gpu->EndFrame();return p;
    };
    for(int f=0;f<9;++f)for(auto xy:{std::pair<float,float>{0,0},{-.4f,-.25f},{.4f,.25f}}) {
        const auto a=itemsAt(near,f,xy.first,xy.second),b=itemsAt(far,f,xy.first,xy.second);
        Record(recorder,recorded,assets,b,far,projection);
        const auto farVertices=recorded.dynamicVertices;
        Record(recorder,recorded,assets,a,near,projection);
        Require(farVertices.size()==recorded.dynamicVertices.size()&&
                std::memcmp(farVertices.data(),recorded.dynamicVertices.data(),farVertices.size()*sizeof(game::ModelVertex))==0,
                "far camera changed actual split vertices/depth");
        const auto first=render(a,near),second=render(b,far);Require(first==second,"far camera changed actual rapid P1/P2 flash pixels");
        size_t visible=0;for(size_t i=0;i<first.size();i+=4)if(first[i]!=20||first[i+1]!=20||first[i+2]!=20)++visible;
        Require(visible>4,"actual native flash frame has no visible effect");++frames;
    }
    const auto mv=FlashMatrix(0,0,true),roundtrip=(mv*far.world)*far.view;
    Require(std::memcmp(mv.m,roundtrip.m,sizeof(mv.m))!=0,"regression camera did not exercise lossy world roundtrip");

    // Original NL near clipping uses 1e-5, independently verified in retained
    // native states and the C22A0/C29A0 clipping path. Nondegenerate surfaces
    // of real common149/151/152 cross the PC .8 plane at offcenter aims.
    // Common150/152 centered are mandatory unchanged controls: their higher-Z
    // authored A,B,A,B strips have zero area and must not count as lost tips.
    // A dim known texel avoids both transparent
    // source artwork and saturated additive overlap hiding this regression.
    auto nativeProjection=projection;nativeProjection.m[10]=-1;nativeProjection.m[14]=-9.999999747378752e-6f;
    port::RgbaImage probeImage;probeImage.width=probeImage.height=1;probeImage.pixels={24,40,16,64};
    const auto probeTexture=gpu->CreateTexture(probeImage);Require(probeTexture!=0,"create near-clip source probe");
    struct NearCase {int frame;float x,y;bool clipped;};
    const NearCase nearFixtures[]={{1,0,0,false},{3,0,0,false},
        {0,.36f,.09f,true},{2,.36f,0,true},{3,.36f,-.24f,true}};
    size_t nearCases=0,nearRestored=0,centerUnchanged=0,nearUnchanged=0;
    for(const auto& fixture:nearFixtures) {
        const int frame=fixture.frame;const std::pair<float,float> xy{fixture.x,fixture.y};
        auto& source=asset(0,false,frame);
        const auto item=Effect(source.model,source.pol,FlashMatrix(xy.first,xy.second,false),near);
        Record(recorder,recorded,assets,{item},near,nativeProjection);
        size_t pcClipped=0;float minimumDepth=1e30f;
        for(const auto& v:recorded.dynamicVertices){minimumDepth=std::min(minimumDepth,-v.pos[2]);if(-v.pos[2]<.8f)++pcClipped;}
        const bool centered=xy.first==0 && xy.second==0;
        if((pcClipped!=0)!=fixture.clipped)std::fprintf(stderr,"near fixture common%d frame%d aim%.3f,%.3f minimumDepth%.9f clipped%zu expectedCut%d\n",149+frame,frame,xy.first,xy.second,minimumDepth,pcClipped,fixture.clipped);
        Require((pcClipped!=0)==fixture.clipped,"native near fixture no longer matches its nondegenerate source geometry");
        auto original=recorded.last,pcNear=recorded.last;
        for(auto* list:{&original,&pcNear})for(auto& draw:list->draws){draw.texture=probeTexture;list->model[draw.constants].flags[0]=1;}
        for(auto& c:pcNear.model){std::memcpy(c.projection,projection.m,sizeof(c.projection));for(int row=0;row<4;++row)c.projection[row*4+2]=projection.m[row*4+3]-projection.m[row*4+2];}
        gpu->SetDynamicVertices(recorded.dynamicVertices.data(),recorded.dynamicVertices.size());
        const auto kept=Pixels(*gpu,original,clear),clipped=Pixels(*gpu,pcNear,clear);
        uint64_t sourceEnergy=0,pcEnergy=0;for(size_t i=0;i<kept.size();i+=4)for(int k=0;k<3;++k){sourceEnergy+=kept[i+k];pcEnergy+=clipped[i+k];}
        const bool correct=pcClipped?sourceEnergy>pcEnergy:kept==clipped;
        if(!correct)std::fprintf(stderr,"near fixture frame%d aim%.3f,%.3f minimumDepth%.9f clipped%zu sourceEnergy%llu pcEnergy%llu\n",frame,xy.first,xy.second,minimumDepth,pcClipped,static_cast<unsigned long long>(sourceEnergy),static_cast<unsigned long long>(pcEnergy));
        Require(correct,pcClipped?"native near plane did not restore real common149/151/152 source surfaces":"near-plane change altered an unclipped native source flash");
        if(pcClipped)++nearRestored;else {++nearUnchanged;if(centered)++centerUnchanged;}
        ++nearCases;
    }
    Require(centerUnchanged==2&&nearRestored==3,"mandatory native source clipping/restoration controls are incomplete");

    // Interpolation changes the world camera, not immediate native effect MV.
    port::RenderPoseHistory history;history.Set(true,.5f);port::TemporalSceneStamp stamp;stamp.world=1;stamp.simulationFrame=100;
    auto interpolationItems=itemsAt(far,0,0,0);for(auto& i:interpolationItems){i.motionTracked=true;i.motionIdentity.role=1;}
    auto view=far.view;history.Apply(interpolationItems,stamp,far.world,&view);
    const auto moved=Camera(true,10);++stamp.simulationFrame;interpolationItems=itemsAt(moved,0,0,0);
    for(auto& i:interpolationItems){i.motionTracked=true;i.motionIdentity.role=1;}
    const auto before=interpolationItems;view=moved.view;history.Apply(interpolationItems,stamp,moved.world,&view);
    Require(history.Ready()&&std::memcmp(view.m,moved.view.m,sizeof(view.m))!=0,"interpolated camera regression was not active");
    for(size_t i=0;i<before.size();++i) {
        Require(std::memcmp(before[i].transform,interpolationItems[i].transform,sizeof(before[i].transform))==0,"native effect acquired interpolated camera reanchor");
        Require(std::memcmp(before[i].modelView,interpolationItems[i].modelView,sizeof(before[i].modelView))==0,"interpolation changed native effect MV");
    }

    // The optional matrix and autosort corrections are ignored by the PC mode.
    renderer.ClearCache();recorder.ClearCache();Require(assets.SetNaomiGraphics(false,&error),error.c_str());
    options.naomiGraphics=false;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    game::Model pc;std::string pcPol;Require(assets.ModelById(0x175,pc,&pcPol),"original PC muzzle");
    auto pcItem=Effect(pc,pcPol,FlashMatrix(0,0,false),far);pcItem.cullable=false;pcItem.noMeshCull=true;
    const auto flagged=render({pcItem},far);pcItem.directModelView=false;
    Require(render({pcItem},far)==flagged,"native precision metadata changed PC pixels");
    recorded.graphics.naomiGraphics=false;recorder.SetScene(assets,{pcItem});const auto vp=far.view*projection;
    recorder.DrawGame(640,480,vp.m,nullptr,nullptr,far.view.m,nullptr,nullptr,projection.m);
    for(const auto& d:recorded.last.draws)Require(d.state.zwrite==1&&d.state.zfunc==4,"native autosort state leaked into PC");
    std::printf("PASS %s: known-alpha compositing/opaque depth, %zu actual rapid P1/P2 cases, %zu real native near cases (%zu restored/%zu unchanged), interpolation and PC isolation\n",gpu->Name(),frames,nearCases,nearRestored,nearUnchanged);
}
