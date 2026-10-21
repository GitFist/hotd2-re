#include "game/camera.h"
#include "assets/asset_override.h"
#include "render/d3d11_device.h"
#include "render/model_renderer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model SmoothModel(){
    const float p[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const int triangles[8][3]={{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
    game::Model model;model.sphere[3]=1;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,.8f,.6f,.3f};std::memcpy(w+10,material,sizeof(material));
    for(const auto& tri:triangles){game::ModelStrip strip;strip.triangleList=true;for(int index:tri){game::ModelVertex v{};std::copy(p[index],p[index]+3,v.pos);std::copy(p[index],p[index]+3,v.normal);strip.vertices.push_back(v);}mesh.strips.push_back(strip);}
    model.meshes.push_back(mesh);return model;
}
int main(int argc,char** argv){
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
    const auto directory=std::filesystem::temp_directory_path()/("hotd2-mesh-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);std::ofstream(directory/"pack.ini")<<"format=1\nmeshes=pn-triangles-v1\nrefine_models=0x1234\n";
    struct Cleanup{std::filesystem::path path;std::string old=port::AssetDir();~Cleanup(){port::SetAssetDir(old);std::error_code ignored;if(path.parent_path()==std::filesystem::temp_directory_path()&&path.filename().string().rfind("hotd2-mesh-fixture-",0)==0)std::filesystem::remove_all(path,ignored);}} cleanup{directory};port::SetAssetDir(directory.string());
    port::AssetStore assets;Require(assets.RefineModel(0x1234)&&!assets.RefineModel(0x4321),"active pack model selection wrong");
    auto model=SmoothModel();const auto original=model;port::ModelRenderer::SceneItem item;item.model=&model;item.motionTracked=true;item.motionIdentity.model=0x1234;
    item.transform[0]=item.transform[5]=item.transform[10]=35;item.transform[14]=-100;item.cullable=false;item.noMeshCull=true;
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();port::ModelRenderer::Lighting light;light.enabled=true;
    for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    auto render=[&]{renderer.SetScene(assets,{item});const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin mesh-refinement frame");renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read mesh-refinement frame");gpu->EndFrame();return pixels;};
    auto changed=[](const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){size_t count=0;for(size_t p=0;p<a.size();p+=4)if(a[p]!=b[p])++count;return count;};
    for(int samples:{1,2,4}){auto options=gpu->EffectiveGraphics();options.msaa=samples;options.meshRefinement=0;if(!gpu->ConfigureGraphics(options,&error)){std::printf("SKIP MSAA%d %s\n",samples,error.c_str());continue;}
        const auto native=render();Require(renderer.SceneStats().triangles==8,"baseline topology changed");options.meshRefinement=2;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto smooth=render();
        Require(renderer.SceneStats().triangles==72,"selected mesh was not actually subdivided");Require(changed(native,smooth)>100,"refined smooth geometry produced no curved silhouette");
        item.motionIdentity.model=0x4321;Require(render()==native&&renderer.SceneStats().triangles==8,"unselected model was refined");item.motionIdentity.model=0x1234;
        item.dynamicGeometry=true;Require(render()==smooth,"post-deformation dynamic path differs from static refinement");
        for(auto& strip:model.meshes[0].strips)for(auto& vertex:strip.vertices)vertex.pos[0]*=.5f;
        Require(changed(render(),smooth)>100,"CPU-deformed positions were not refined afresh");model=original;item.dynamicGeometry=false;
        item.alpha=.5f;const auto alpha=render();options.meshRefinement=0;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render()==alpha,"forced-alpha model acquired curved geometry");item.alpha=-1;
        Require(render()==native,"mesh refinement Off failed byte-exact native restoration");
        std::printf("PASS %s MSAA%d: actual PN silhouette, pack selection, topology counts, dynamic deformation, alpha exclusion and exact Off\n",gpu->Name(),samples);
    }
}
