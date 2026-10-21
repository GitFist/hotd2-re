// Actual shipped chapter geometry: render-only effects must not step gameplay.
#include "game/world_modes.h"
#include "scene/scene.h"
#include "render/d3d12_device.h"
#include "render/native_d3d12_raytracing.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
static void Require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
static int Run(const char* directory){
    port::AssetStore assets;std::string error;Require(assets.Open(directory,&error),error);port::GpuDeviceDesc desc;desc.width=640;desc.height=480;auto gpu=port::CreateD3D12Device(desc,&error);Require(bool(gpu),error);
    const auto initial=port::GetNativeD3D12RaytracingInfo(*gpu);Require(initial.supported&&initial.compilerAvailable&&!initial.enabled,"real hardware and compiler are available while shadows default Off");
    port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error);port::Scene scene;port::SceneDef definition;definition.useWorld=definition.externalWorld=true;Require(scene.Init(assets,definition,&error),error);game::World world;Require(world.Load(directory),"load actual chapter runtime");
    for(int chapter=0;chapter<6;++chapter){
        game::WorldConfig config;config.gameMode=6;config.gameOver=true;config.portFeatures=true;config.eventMode=chapter;config.scene=game::SceneSelectFirstScene(chapter);config.playerState[0]=0;Require(world.StartStage(config,&error),error);
        for(int warmup=0;warmup<3600;++warmup){world.Step();if(warmup>=359&&!world.Draws().empty())break;}
        Require(!world.Draws().empty(),"chapter opening reaches authored geometry");
        // An opening can cut back to a title card, particularly chapter three.
        // Advance past the first second, then wait for the next authored 3D
        // frame rather than requiring a particular transition tick to be 3D.
        for(int extra=0;extra<60;++extra)world.Step();
        for(int wait=0;wait<3600&&world.Draws().empty();++wait)world.Step();
        Require(!world.Draws().empty(),"chapter reaches authored geometry after its opening transition");
        const auto frame=world.Frame();const auto rng=world.RandState();scene.PrepareWorld(renderer,world);const auto viewProjection=scene.BuildFromWorld(renderer,world);
        auto render=[&](){const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin actual chapter GPU frame");renderer.DrawGame(640,480,viewProjection.m,&scene.lighting,&scene.fog,scene.view.m,nullptr,&scene.itemLights,scene.projection.m);std::vector<uint8_t> image;Require(gpu->ReadPixels(&image),"read actual chapter GPU pixels");gpu->EndFrame();Require(world.Frame()==frame&&world.RandState()==rng,"rendering preserves actual gameplay frame and RNG");return image;};
        Require(port::ConfigureNativeD3D12Raytracing(*gpu,false,&error),error);const auto original=render();Require(port::ConfigureNativeD3D12Raytracing(*gpu,true,&error),error);
        // NeedsShadowCasters changed, so rebuild the same frozen frame's packet.
        scene.BuildFromWorld(renderer,world);const auto shadow=render();auto info=port::GetNativeD3D12RaytracingInfo(*gpu);Require(info.enabled&&info.geometries>0&&info.triangles>0,"actual chapter produces genuine lit opaque AS geometry");const double first=info.synchronizationCpuMs;
        std::vector<double> warm;for(int repeat=0;repeat<4;++repeat){render();warm.push_back(port::GetNativeD3D12RaytracingInfo(*gpu).synchronizationCpuMs);}std::sort(warm.begin(),warm.end());
        Require(port::ConfigureNativeD3D12Raytracing(*gpu,false,&error),error);scene.BuildFromWorld(renderer,world);const auto restored=render();Require(restored==original,"actual chapter native image restores exactly when ray shadows turn Off");
        size_t changed=0;for(size_t p=0;p<shadow.size();p+=4)if(shadow[p]!=original[p]||shadow[p+1]!=original[p+1]||shadow[p+2]!=original[p+2])++changed;
        printf("chapter%d frame%lld: %u opaque geometries, %u triangles; AS fence first%.3fms warm-median%.3fms; changed%zu pixels; exact Off, unchanged gameplay\n",chapter+1,static_cast<long long>(frame),info.geometries,info.triangles,first,(warm[1]+warm[2])*.5,changed);
    }
    puts("PASS: native DXR on actual six-chapter geometry, measured AS synchronization, exact Off restoration and unchanged gameplay clock/RNG");return 0;
}
int main(int argc,char** argv){try{Require(argc==2,"usage: d3d12_ray_world_test <original-game-directory>");return Run(argv[1]);}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
