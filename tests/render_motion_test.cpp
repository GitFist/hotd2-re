// Dense geometry motion and actual prior-depth rejection. Generated assets only.
#include "game/camera.h"
#include "render/d3d11_device.h"
#include "render/d3d12_device.h"
#include "render/model_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Card(float z,float halfX,float halfY) {
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,.4f,.6f,.8f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-halfX,-halfY},{-halfX,halfY},{halfX,-halfY},{halfX,halfY}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
int main(int argc,char** argv) {
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;
    std::unique_ptr<port::GpuDevice> gpu;
    if(argc>1&&std::strcmp(argv[1],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;
    game::Model wall=Card(-100,100,80),object=Card(-90,20,20);
    auto make=[&](const game::Model* model,uint32_t task){port::ModelRenderer::SceneItem item;item.model=model;item.cullable=false;item.noMeshCull=true;item.motionTracked=true;item.motionIdentity.task=task;item.motionIdentity.model=task;return item;};
    auto W=make(&wall,1),O=make(&object,2);
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    auto view=game::Mat4::Identity();const auto projection=game::DeviceView()*game::DeviceProjection();
    port::TemporalFrameInfo frame;frame.enabled=true;
    struct Result{std::vector<uint8_t> color;std::vector<float> motion;};
    auto render=[&](std::vector<port::ModelRenderer::SceneItem> items,bool read=true){
        renderer.SetScene(assets,items);renderer.SetTemporalFrame(frame);const auto vp=view*projection;const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin motion frame");
        renderer.DrawGame(160,120,vp.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);Result result;
        Require(gpu->ReadPixels(&result.color),"read motion frame color");
        if(read)Require(gpu->ReadMotionPixels(&result.motion),"backend did not produce a real motion target");
        gpu->EndFrame();++frame.frameId;frame.reset=false;return result;
    };
    auto value=[](const Result& result,int x,int y,int channel){return result.motion[(y*160+x)*4+channel];};
    for(int samples:{1,2,4}){
        auto options=gpu->EffectiveGraphics();options.msaa=samples;
        if(!gpu->ConfigureGraphics(options,&error)){std::printf("SKIP unsupported MSAA%d: %s\n",samples,error.c_str());continue;}
        renderer.ClearCache();frame.reset=true;frame.enabled=true;frame.jitterPixels[0]=frame.jitterPixels[1]=0;O.transform[12]=0;
        const auto initial=render({W,O});Require(value(initial,80,60,3)==0,"new object/history was valid on first frame");
        const auto still=render({W,O});
        std::printf("%s MSAA%d static metadata: %.5f %.5f depth %.7f valid %.1f\n",gpu->Name(),samples,value(still,80,60,0),value(still,80,60,1),value(still,80,60,2),value(still,80,60,3));
        Require(std::abs(value(still,80,60,0))<.05f&&std::abs(value(still,80,60,1))<.05f&&value(still,80,60,3)>.9f,"static visible geometry did not produce valid zero motion");
        O.transform[12]=5;const auto moved=render({O,W});
        Require(value(moved,80,60,0)<-1&&std::abs(value(moved,80,60,1))<.05f,"object velocity has wrong sign/units or follows submission order");
        frame.jitterPixels[0]=.25f;frame.jitterPixels[1]=-.25f;const auto jittered=render({W,O});
        Require(std::abs(value(jittered,80,60,0))<.08f&&std::abs(value(jittered,80,60,1))<.08f,"static motion contains projection jitter");
        // A previously occluded wall pixel must reject the old foreground depth,
        // while nearby continuously visible wall pixels retain valid history.
        frame.jitterPixels[0]=frame.jitterPixels[1]=0;frame.reset=true;O.transform[12]=0;render({W,O});
        O.transform[12]=200;const auto revealed=render({W,O});
        Require(value(revealed,80,60,3)<.1f,"disoccluded surface accepted old foreground history");
        Require(value(revealed,30,60,3)>.9f,"depth rejection invalidated an unchanged visible surface");
        frame.reset=true;const auto cut=render({W});Require(value(cut,80,60,3)==0,"explicit camera cut retained valid history");
        frame.enabled=false;const auto native=render({W},false);frame.enabled=true;frame.reset=true;
        const auto enabled=render({W});Require(native.color==enabled.color,"motion buffer changed original color image with zero jitter");
        std::printf("PASS %s MSAA%d: dense geometry motion, reordered actors, jitter removal, actual disocclusion, cuts and unchanged color\n",gpu->Name(),samples);
    }
}
