// True Hor+ projection, centered HUD, full-width rays and real chapter scene coverage.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "play/widescreen.h"
#include "game/world_modes.h"
#include "game/hud.h"
#include "scene/scene.h"

static void Require(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
class RecordingGpu final:public port::GpuDevice {
public:
 const char* Name() const override{return "CPU draw recorder";}
 port::GpuTexture CreateTexture(const port::RgbaImage&,bool=false) override{return ++next_;}
 void ClearTextures() override{}
 void SetStaticVertices(const game::ModelVertex*,size_t) override{}
 void SetDynamicVertices(const game::ModelVertex*,size_t) override{}
 void SetSpriteVertices(const game::TLVertex*,size_t) override{}
 bool BeginFrame(const float[4]) override{draws=0;return true;}
 void Submit(const port::GpuDrawList& list) override{draws+=list.draws.size();}
 void EndFrame() override{}
 bool ReadPixels(std::vector<uint8_t>*) override{return false;}
 int TargetWidth() const override{return 640;}
 int TargetHeight() const override{return 480;}
 size_t draws=0;
private:uintptr_t next_=0;
};
static void MathAndRays() {
 const auto native=game::DeviceView()*game::DeviceProjection();
 const auto same=port::HorPlusProjection(native,4.f/3.f);
 Require(std::memcmp(same.m,native.m,sizeof(native.m))==0,"native projection remains bit exact");
 game::WorldInput raw=game::MakeWorldInput(.3f,.2f,1,0);
 auto old=play::AspectedWorldInput(raw,4.f/3.f);
 Require(old.x==raw.x&&old.y==raw.y&&old.deviceMode==6&&old.pressed==raw.pressed,"4:3 input path remains native");
 for(float aspect:{16.f/9.f,21.f/9.f,32.f/9.f}) {
  const float width=480*aspect;const auto projection=port::HorPlusProjection(native,aspect);
  Require(projection.m[5]==native.m[5]&&projection.m[14]==native.m[14],"wide preserves vertical FOV and depth");
  for(float nx:{0.f,.01f,.2f,.5f,.8f,.99f,1.f})for(float ny:{0.f,.25f,.5f,.75f,1.f}) {
   auto in=play::AspectedWorldInput(game::MakeWorldInput(nx,ny,1,0),nx,ny,aspect);
   Require(in.deviceMode==game::kPortWideGunMode&&in.x>0&&in.x<32768,"wide aim fits existing signed 16-bit input codec");
   const int decodedX=static_cast<int16_t>(static_cast<uint16_t>(in.x));
   const int decodedY=static_cast<int16_t>(static_cast<uint16_t>(in.y));
   int16_t gx,gy,remoteX,remoteY;bool valid,remoteValid;
   game::GunCalibrateWide(in.x,in.y,&gx,&gy,&valid);
   game::GunCalibrateWide(decodedX,decodedY,&remoteX,&remoteY,&remoteValid);
   Require(valid&&remoteValid&&gx==remoteX&&gy==remoteY,"encoded ray decodes identically without receiving display aspect");
   const float projectedX=.5f+gx/width,projectedY=.5f-gy/480.f;
   Require(std::fabs(projectedX-nx)*width<=1.01f&&std::fabs(projectedY-ny)*480<=1.01f,"wide aim and projection agree across full viewport within one logical pixel");
   const float distance=1000,focal=native.m[0]*320;
   const auto ray=game::MakeShotRay(float(gx),float(gy),focal);
   const float target[]={gx*distance/focal,gy*distance/focal,-distance};
   // Original hit testing quantizes yaw/pitch to 1/65536 turn before measuring
   // distance. Half a world unit at this distance remains below one aim pixel.
   Require(game::ShotHitsSphere(ray,target,.5f),"wide-edge ray hits rendered target direction within original angular quantization");
   const auto remote=game::MakeShotRay(float(remoteX),float(remoteY),focal);
   Require(std::memcmp(ray.dir,remote.dir,sizeof(ray.dir))==0,"different display peers produce identical replicated ray");
   const float hudX=port::CenteredViewportX(float(gx)+320,aspect);
   Require(std::fabs(hudX/640-projectedX)<.00001f,"centered crosshair aligns with expanded geometry");
  }
  auto reload=game::MakeWorldInput(.5f,.5f,2,0);
  auto wideReload=play::AspectedWorldInput(reload,aspect);
  Require(wideReload.x==reload.x&&wideReload.y==reload.y&&wideReload.offscreen==reload.offscreen&&wideReload.pressed==reload.pressed,"off-screen reload semantics preserved");
 }
 int16_t x,y;bool valid;game::GunCalibrateWide(65535,65535,&x,&y,&valid);Require(!valid,"sentinel cannot become valid wide aim");
}
static void ExtraGeometryIsVisible() {
 RecordingGpu gpu;port::ModelRenderer renderer;std::string error;Require(renderer.Init(&gpu,&error),error.c_str());
 game::Model model;game::ModelMesh mesh;auto& words=mesh.material.words;
 words[1]=4u<<29;words[2]=0x80000;words[8]=words[9]=0xffffffff;
 const float sphere[]={400,0,-640,24};std::memcpy(words+4,sphere,sizeof(sphere));
 const float color[]={1,1,0,0};std::memcpy(words+11,color,sizeof(color));game::ModelStrip strip;
 for(auto xy:{std::pair<float,float>{384,-16},{384,16},{416,-16},{416,16}}) {
  game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=-640;v.normal[2]=1;strip.vertices.push_back(v);
 }
 mesh.strips.push_back(strip);model.meshes.push_back(mesh);port::AssetStore assets;
 port::ModelRenderer::SceneItem item;item.model=&model;item.cullable=false;renderer.SetScene(assets,{item});
 const auto view=game::DeviceView();const auto native=view*game::DeviceProjection();const float clear[]={0,0,0,1};
 // Passing gameView enables the real gameplay frustum path; the model viewer
 // deliberately submits all meshes when gameView is absent.
 gpu.BeginFrame(clear);renderer.DrawGame(640,480,native.m,nullptr,nullptr,view.m);const auto narrowDraws=gpu.draws;
 const auto wide=port::HorPlusProjection(native,16.f/9.f);
 gpu.BeginFrame(clear);renderer.DrawGame(640,480,wide.m,nullptr,nullptr,view.m);
 Require(narrowDraws==0&&gpu.draws==1,"Hor+ reveals additional geometry that native frustum actually culls");
}
static void ChapterCoverage(const char* dir) {
 port::AssetStore assets;std::string error;Require(assets.Open(dir,&error),error.c_str());
 RecordingGpu gpu;port::ModelRenderer renderer;Require(renderer.Init(&gpu,&error),error.c_str());
 port::Scene scene;port::SceneDef def;def.useWorld=def.externalWorld=true;
 Require(scene.Init(assets,def,&error),error.c_str());game::World world;
 Require(world.Load(dir),"load chapter world");
 for(int chapter=0;chapter<6;++chapter) {
  game::WorldConfig cfg;cfg.gameMode=6;cfg.gameOver=true;cfg.portFeatures=true;
  cfg.eventMode=chapter;cfg.scene=game::SceneSelectFirstScene(chapter);cfg.playerState[0]=0;
  Require(world.StartStage(cfg,&error),error.c_str());
  int warmup=0;
  // Chapters have different loading/title-card lengths. Wait for actual world
  // geometry instead of assuming every opening reaches 3D on frame360.
  for(;warmup<3600;++warmup) {world.Step();if(warmup>=359&&!world.Draws().empty())break;}
  if(world.Draws().empty())fprintf(stderr,"chapter %d no geometry after %d ticks, mode %d\n",chapter+1,warmup,world.GameMode());
  Require(!world.Draws().empty(),"real chapter reaches authored geometry within bounded opening warmup");
  scene.PrepareWorld(renderer,world);scene.SetViewportAspect(4.f/3.f);
  scene.BuildFromWorld(renderer,world);const auto native=scene.projection;const auto rng=world.RandState();const auto frame=world.Frame();
  for(float aspect:{16.f/9.f,21.f/9.f,32.f/9.f}) {
   scene.SetViewportAspect(aspect);const auto vp=scene.BuildFromWorld(renderer,world);
   Require(std::fabs(scene.projection.m[0]-native.m[0]*port::ViewportXScale(aspect))<.000001f,"chapter uses actual wider projection");
   Require(scene.projection.m[5]==native.m[5],"chapter preserves vertical framing");
   const float clear[]={0,0,0,1};gpu.BeginFrame(clear);
   renderer.DrawGame(640,480,vp.m,&scene.lighting,&scene.fog,scene.view.m,nullptr,&scene.itemLights,scene.projection.m);gpu.EndFrame();
   Require(gpu.draws>0,"wide chapter survives scene and mesh culling");
   Require(world.RandState()==rng&&world.Frame()==frame,"wide rendering does not change gameplay clock or RNG");
  }
  printf("chapter %d: real geometry in 16:9 / 21:9 / 32:9, unchanged gameplay RNG\n",chapter+1);
 }
}
int main(int argc,char** argv) {
 if(argc!=2)return 2;MathAndRays();ExtraGeometryIsVisible();ChapterCoverage(argv[1]);
 puts("PASS: native bit-exact defaults, Hor+ vertical framing, full-width aim, replicated rays, centered HUD and six chapter scene/cull checks");
}
