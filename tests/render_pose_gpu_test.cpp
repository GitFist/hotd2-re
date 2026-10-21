// Actual raster positions for rigid/deformed interpolation, sharp current HUD,
// repeated sub-tick phases and byte-exact native restoration. Generated art.
#include "render/render_pose.h"
#include "render/d3d11_device.h"
#include "game/camera.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Card(){
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,1,.25f,.1f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-7.f,-10.f},{-7.f,10.f},{7.f,-10.f},{7.f,10.f}}){game::ModelVertex vertex{};vertex.pos[0]=xy.first;vertex.pos[1]=xy.second;vertex.pos[2]=-100;vertex.normal[2]=1;strip.vertices.push_back(vertex);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
static port::ModelRenderer::SpriteDraw Hud(uint32_t color){
    port::ModelRenderer::SpriteDraw sprite;sprite.isp=4u<<29;sprite.tsp=0x80000;
    sprite.v[0]={24,24,0,1,color,0,0,0};sprite.v[1]={104,24,0,1,color,0,1,0};
    sprite.v[2]={24,104,0,1,color,0,0,1};sprite.v[3]={104,104,0,1,color,0,1,1};return sprite;
}
static double Centroid(const std::vector<uint8_t>& pixels){
    double sum=0,total=0;for(int y=30;y<90;++y)for(int x=0;x<160;++x){const size_t at=(y*160+x)*4;const double weight=std::max(0,int(pixels[at])-int(pixels[at+1]));sum+=weight*(x+.5);total+=weight;}
    Require(total>5000,"generated actor was not rasterized");return sum/total;
}
int main(int argc,char** argv){
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);Require(gpu!=nullptr,error.c_str());
    port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;
    const auto identity=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    for(int samples:{1,2,4}){
        auto options=gpu->EffectiveGraphics();options.msaa=samples;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        for(bool deformed:{false,true}){
            auto card=Card();auto original=card;port::ModelRenderer::SceneItem item;item.model=&card;item.cullable=false;item.noMeshCull=true;
            item.motionTracked=true;item.motionIdentity.task=10;item.motionIdentity.model=1;item.dynamicGeometry=deformed;
            port::RenderPoseHistory history;port::TemporalSceneStamp stamp;stamp.world=123;stamp.simulationFrame=100;
            auto move=[&](float x){if(deformed){card=original;for(auto& vertex:card.meshes[0].strips[0].vertices)vertex.pos[0]+=x;}else item.transform[12]=x;};
            auto render=[&](float alpha,bool interpolation,uint32_t hudColor=0xff00ff00){
                history.Set(interpolation,alpha);auto view=identity;std::vector<port::ModelRenderer::SceneItem> items={item};history.Apply(items,stamp,identity,&view);
                renderer.SetScene(assets,items);const auto vp=view*projection;const std::vector<port::ModelRenderer::SpriteDraw> sprites={Hud(hudColor)};
                const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin render-pose frame");renderer.DrawGame(160,120,vp.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
                std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read interpolated pose");gpu->EndFrame();return pixels;
            };
            move(-20);const auto nativeStart=render(1,false);const double start=Centroid(nativeStart);
            render(.5f,true);Require(!history.Ready(),"first GPU tick claimed readiness");++stamp.simulationFrame;move(20);
            const auto midpoint=render(.5f,true);const double middle=Centroid(midpoint);Require(history.Ready(),"second GPU tick did not become ready");
            const auto quarter=render(.25f,true,0xff0000ff);const double early=Centroid(quarter);
            const auto nativeEnd=render(1,false);const double end=Centroid(nativeEnd);
            Require(std::abs(middle-(start+end)*.5)<.75&&std::abs(early-(start*.75+end*.25))<.75,"GPU actor is not at the actual sub-tick position");
            const size_t hud=(12*160+12)*4;Require(midpoint[hud]==0&&midpoint[hud+1]==255&&quarter[hud]==0&&quarter[hud+2]==255,"HUD color was delayed with scene interpolation");
            Require(render(.2f,false)==nativeEnd,"switching interpolation Off did not restore exact native rasterization");
            // Cut/gap keeps current color without a cross-scene easing frame.
            render(.5f,true);stamp.simulationFrame+=2;move(-20);Require(render(.5f,true)==nativeStart,"simulation gap interpolated across a stale GPU pose");
            Require(!history.Ready(),"simulation gap retained readiness");
            std::printf("PASS %s MSAA%d %s: actual quarter/midpoint pixels, immediate HUD, cut and exact Off\n",gpu->Name(),samples,deformed?"deformed":"rigid");
        }
    }
}
