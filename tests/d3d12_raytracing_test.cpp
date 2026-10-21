// A real geometric shadow must move with its actual opaque occluder. No assets.
#include "render/asset_store.h"
#include "game/camera.h"
#include "render/d3d12_device.h"
#include "render/model_renderer.h"
#include "render/native_d3d12_raytracing.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Require(bool value,const char* why){if(!value){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Card(float z,float halfX,float halfY) {
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,1,1,1};std::memcpy(w+10,material,sizeof(material));game::ModelStrip strip;
    model.sphere[2]=z;model.sphere[3]=std::sqrt(halfX*halfX+halfY*halfY);std::memcpy(w+4,model.sphere,sizeof(model.sphere));
    for(auto xy:{std::pair<float,float>{-halfX,-halfY},{-halfX,halfY},{halfX,-halfY},{halfX,halfY}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
int main(int argc,char** argv) {
    const bool requireMissingCompiler=argc>1&&!std::strcmp(argv[1],"--require-no-compiler");
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;
    auto gpu=port::CreateD3D12Device(desc,&error);Require(gpu!=nullptr,error.c_str());
    const auto initial=port::GetNativeD3D12RaytracingInfo(*gpu);Require(!initial.enabled,"ray shadows default Off");
    Require(!requireMissingCompiler||!initial.compilerAvailable,"missing optional compiler is reported before enabling shadows");
    game::Model wall=Card(-100,80,60),occluder=Card(-80,6,12);
    port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;
    port::ModelRenderer::SceneItem W,O;W.model=&wall;O.model=&occluder;W.cullable=O.cullable=false;W.noMeshCull=O.noMeshCull=true;O.transform[12]=20;
    port::ModelRenderer::Lighting light;light.enabled=true;light.dirToLight[0]=light.dirToLight[2]=std::sqrt(.5f);light.dirToLight[1]=0;
    for(int k=0;k<3;++k){light.ambient[k]=.1f;light.diffuse[k]=.9f;}
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection(),vp=view*projection;
    auto render=[&](){renderer.SetScene(assets,{W,O});const float black[4]={0,0,0,1};Require(gpu->BeginFrame(black),"begin actual DXR frame");
        renderer.DrawGame(160,120,vp.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read actual DXR frame");gpu->EndFrame();return pixels;};
    const auto baseline=render();
    if(!port::ConfigureNativeD3D12Raytracing(*gpu,true,&error)) {
        Require(!port::GetNativeD3D12RaytracingInfo(*gpu).enabled,"unsupported DXR request did not enable false state");
        Require(render()==baseline,"unsupported DXR request preserves raster image");
        if(argc>1&&!std::strcmp(argv[1],"--require")){fprintf(stderr,"FAIL: required hardware DXR unavailable: %s\n",error.c_str());return 1;}
        printf("%s: %s\n",requireMissingCompiler?"PASS missing compiler rejects DXR and preserves exact raster":"SKIP optional DXR",error.c_str());return 0;
    }
    Require(!requireMissingCompiler,"missing compiler must not silently enable DXR");
    const auto shadow=render();const auto info=port::GetNativeD3D12RaytracingInfo(*gpu);
    Require(initial.compilerAvailable&&info.compilerAvailable,"actual app-local compiler availability is exposed before enabling shadows");
    printf("DXR supported=%d compiler=%d enabled=%d geometries=%u triangles=%u\n",info.supported,info.compiler,info.enabled,info.geometries,info.triangles);
    printf("Actual AS submission/fence CPU cost %.3fms\n",info.synchronizationCpuMs);
    Require(info.supported&&info.compiler&&info.enabled&&info.geometries==2&&info.triangles==4,"genuine opaque triangle BLAS/TLAS built");
    const size_t center=(60*160+80)*4,lit=(60*160+36)*4;
    printf("center native=%d ray=%d; unoccluded native=%d ray=%d; %u geometries %u triangles\n",baseline[center],shadow[center],baseline[lit],shadow[lit],info.geometries,info.triangles);
    Require(baseline[center]>120&&shadow[center]+80<baseline[center],"actual offset opaque occluder casts a directional shadow");
    Require(std::abs(int(shadow[lit])-int(baseline[lit]))<3,"unoccluded direct light remains stable");
    O.transform[12]=40;const auto moved=render();Require(moved[center]>shadow[center]+80,"shadow moves with world-transformed occluder");
    printf("Warm AS submission/fence CPU cost %.3fms\n",port::GetNativeD3D12RaytracingInfo(*gpu).synchronizationCpuMs);
    Require(port::ConfigureNativeD3D12Raytracing(*gpu,false,&error),error.c_str());O.transform[12]=20;
    Require(render()==baseline,"turning ray shadows Off restores byte-exact native raster");
    // A caster outside the raster frustum must still block a ray through the
    // visible wall. This proves the dedicated unculled caster packet is used.
    light.dirToLight[0]=3/std::sqrt(10.f);light.dirToLight[2]=1/std::sqrt(10.f);
    O.transform[12]=60;O.cullable=true;O.noMeshCull=false;
    const auto offscreenNative=render();Require(port::ConfigureNativeD3D12Raytracing(*gpu,true,&error),error.c_str());
    const auto offscreenShadow=render();
    Require(offscreenShadow[center]+40<offscreenNative[center],"frustum-culled opaque geometry still casts a real shadow");
    Require(port::GetNativeD3D12RaytracingInfo(*gpu).geometries==2,"unculled caster packet retains offscreen object");
    puts("PASS: native DXR1.1 real BLAS/TLAS, inline directional query, transformed moving occluder and exact Off fallback");
}
