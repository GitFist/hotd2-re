// Real corpse-pool and hit-spark assets overlapping the solid-circle crosshair.
// Unrelated blinking sprites must not change their compositing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/camera.h"
#include "game/effects.h"
#include "game/hud.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Mat4 FrameModel(const game::Model& model,bool floor,float dx=0) {
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(const auto& mesh:model.meshes)for(const auto& strip:mesh.strips)for(const auto& v:strip.vertices)
        for(int k=0;k<3;++k){lo[k]=std::min(lo[k],v.pos[k]);hi[k]=std::max(hi[k],v.pos[k]);}
    const float span=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});Require(span>0,"real effect has geometry");
    auto m=game::Mat4::Translate(-(lo[0]+hi[0])*.5f,-(lo[1]+hi[1])*.5f,-(lo[2]+hi[2])*.5f);
    m=m*game::ScaleMatrix(180/span,180/span,180/span);
    if(floor)m=m*game::Mat4::RotateX(0x4000);
    return m*game::Mat4::Translate(dx,0,-400);
}
static port::ModelRenderer::SceneItem Item(const game::Model& model,const std::string& pol,const game::Mat4& m,int priority) {
    port::ModelRenderer::SceneItem item;item.model=&model;item.polName=pol;item.priority=priority;
    item.cullable=false;item.noMeshCull=true;std::memcpy(item.transform,m.m,sizeof(item.transform));return item;
}
static port::ModelRenderer::SpriteDraw Crosshair(port::AssetStore& assets,int player,int style) {
    game::HudState hud;hud.gameMode=6;hud.canShoot=1;
    auto& p=hud.player[player];p.lives=3;p.inputKind=1;p.deviceMode=6;p.gunPosX=320;p.gunPosY=240;p.crosshairStyle=style;
    const auto defs=game::HudCrosshair(hud,player);Require(defs.size()==1,"actual HUD crosshair definition");
    game::SpriteTables table;table.Load(assets.Exe());const auto tex=table.TextureId(defs[0].id);const float white[]={1,1,1};
    const auto rec=game::MakeSpriteRecord(defs[0],tex,white);port::ModelRenderer::SpriteDraw s;
    s.texture=assets.TextureById(table.TextureFile(defs[0].id),tex&0x7fffffffu);Require(s.texture!=nullptr,"crosshair texture resolves");
    game::SpriteQuad(rec,s.texture->LogicalWidth(),s.texture->LogicalHeight(),1.0001f,-.80008f,s.v);
    s.key=rec.key|0xf;s.depth=rec.depth;s.isp=game::SpriteIsp(rec);s.tsp=game::SpriteTsp(rec);return s;
}
int main(int argc,char** argv) {
    if(argc<2){std::fprintf(stderr,"usage: render_overlap_assets_test <game directory> [sdlgpu]\n");return 2;}
    std::string error;port::GpuDeviceDesc desc;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>2&&std::strcmp(argv[2],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
    port::AssetStore assets;Require(assets.Open(argv[1],&error),error.c_str());game::Model pool,spark;std::string poolPol,sparkPol;
    Require(assets.ModelById(0x1A38,pool,&poolPol)&&assets.ModelById(0xE26,spark,&sparkPol),"real corpse-pool and bullet-impact assets resolve");
    port::ModelRenderer::Lighting light;light.enabled=true;
    for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    auto render=[&](const std::vector<port::ModelRenderer::SceneItem>& items,const std::vector<port::ModelRenderer::SpriteDraw>& sprites,float background=0.f){
        renderer.SetScene(assets,items);const float clear[]={background,background,background,1};Require(gpu->BeginFrame(clear),"begin frame");
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read GPU overlap pixels");gpu->EndFrame();return pixels;
    };
    const auto a=Item(pool,poolPol,FrameModel(pool,true,-20),0xc),b=Item(pool,poolPol,FrameModel(pool,true,20),0xc);
    const auto left=render({a},{}),right=render({b},{});size_t poolOverlap=0;
    for(size_t i=0;i<left.size();i+=4)if((left[i]||left[i+1]||left[i+2])&&(right[i]||right[i+1]||right[i+2]))++poolOverlap;
    Require(poolOverlap>100,"real blood pools visibly overlap");
    auto offscreen=Crosshair(assets,0,1);for(auto& v:offscreen.v)v.x+=1000;
    size_t changed=0;
    for(float alpha:{-1.f,.5f}) {
        auto first=a,second=b;first.alpha=second.alpha=alpha;const auto baseline=render({first,second},{});
        for(int count=1;count<=48;++count){const auto pixels=render({first,second},std::vector<port::ModelRenderer::SpriteDraw>(count,offscreen));
            for(size_t i=0;i<pixels.size();i+=4)changed+=std::memcmp(pixels.data()+i,baseline.data()+i,3)!=0;}
    }
    Require(changed==0,"blinking sprites changed real overlapping corpse pools");
    const auto impact=Item(spark,sparkPol,FrameModel(spark,false),8);const auto effect=render({impact},{});
    size_t checked=0;
    // Both numbering interpretations of 'style 2', for both player colours.
    for(int player:{0,1})for(int style:{1,2}) {
        const auto cross=Crosshair(assets,player,style);const auto black=render({}, {cross}),white=render({}, {cross},1);
        const auto combined=render({impact},{cross});size_t overlap=0;
        for(size_t i=0;i<combined.size();i+=4)if(std::memcmp(black.data()+i,white.data()+i,3)==0&&(effect[i]||effect[i+1]||effect[i+2])) {
            ++overlap;Require(std::memcmp(combined.data()+i,black.data()+i,3)==0,"impact replaced an opaque crosshair pixel");
        }
        Require(overlap>5,"real impact visibly overlaps opaque crosshair pixels");checked+=overlap;
    }
    std::printf("PASS %s: %zu overlapping blood pixels stable through 48 HUD-count changes and fades; %zu actual crosshair/impact pixels preserved\n",gpu->Name(),poolOverlap,checked);
}
