// Real temporal accumulation: dense motion/depth, reactive scene sprites, pre-HUD
// composition, camera resets and missing-metadata rejection. Generated art only.
#include "game/camera.h"
#include "render/d3d11_device.h"
#include "render/model_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Card() {
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;
    w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,.4f,.6f,.8f};std::memcpy(w+10,material,sizeof(material));
    game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-23,-27},{-23,27},{23,-27},{23,27}}){game::ModelVertex vertex{};vertex.pos[0]=xy.first;vertex.pos[1]=xy.second;vertex.pos[2]=-100;vertex.normal[2]=1;strip.vertices.push_back(vertex);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
static port::ModelRenderer::SpriteDraw Sprite(uint32_t color,bool hud,const port::RgbaImage* texture=nullptr) {
    port::ModelRenderer::SpriteDraw sprite;sprite.isp=4u<<29;sprite.tsp=0x80000;sprite.hud=hud;sprite.texture=texture;
    const float left=hud?24.f:260.f,top=hud?24.f:180.f,right=hud?104.f:380.f,bottom=hud?104.f:300.f;
    sprite.v[0]={left,top,0,1,color,0,0,0};sprite.v[1]={right,top,0,1,color,0,1,0};
    sprite.v[2]={left,bottom,0,1,color,0,0,1};sprite.v[3]={right,bottom,0,1,color,0,1,1};return sprite;
}
static double Difference(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b) {
    double sum=0;for(size_t i=0;i<a.size();i+=4)sum+=std::abs(int(a[i])-int(b[i]));return sum;
}
int main(int argc,char** argv) {
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
    port::AssetStore assets;auto card=Card();port::ModelRenderer::SceneItem item;item.model=&card;item.cullable=false;item.noMeshCull=true;item.motionTracked=true;item.motionIdentity.task=1;
    const float angle=.47f;item.transform[0]=item.transform[5]=std::cos(angle);item.transform[1]=std::sin(angle);item.transform[4]=-std::sin(angle);
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    port::TemporalFrameInfo frame;frame.enabled=true;
    auto render=[&](const std::vector<port::ModelRenderer::SpriteDraw>& sprites=std::vector<port::ModelRenderer::SpriteDraw>{}){
        renderer.SetScene(assets,{item});renderer.SetTemporalFrame(frame);const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin temporal frame");
        renderer.DrawGame(160,120,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read temporal frame");gpu->EndFrame();++frame.frameId;frame.reset=false;return pixels;
    };
    for(int samples:{1,2,4}) {
        auto options=gpu->EffectiveGraphics();options.msaa=samples;options.temporalAA=false;
        if(!gpu->ConfigureGraphics(options,&error)){std::printf("SKIP MSAA%d %s\n",samples,error.c_str());continue;}
        renderer.ClearCache();frame.reset=true;frame.jitterPixels[0]=frame.jitterPixels[1]=0;
        const auto native=render();options.temporalAA=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        Require(gpu->RequiresSceneProcessing(),"TAA did not request pre-HUD processing");frame.reset=true;
        Require(render()==native,"first zero-jitter temporal frame changed native color");Require(render()==native,"static flat/edge color changed without jitter");
        double nativeFlicker=0,taaFlicker=0;
        for(bool enabled:{false,true}){
            options.temporalAA=enabled;Require(gpu->ConfigureGraphics(options,&error),error.c_str());frame.reset=true;std::vector<uint8_t> previous;
            for(int step=0;step<24;++step){frame.jitterPixels[0]=(step&1)?.375f:-.375f;frame.jitterPixels[1]=(step&2)?.25f:-.25f;const auto pixels=render();
                if(step>=8){const double change=Difference(pixels,previous);if(enabled)taaFlicker+=change;else nativeFlicker+=change;}previous=pixels;}
        }
        Require(nativeFlicker>100,"jitter fixture did not exercise subpixel edges");
        std::printf("%s MSAA%d temporal variation %.0f -> %.0f\n",gpu->Name(),samples,nativeFlicker,taaFlicker);
        Require(taaFlicker<nativeFlicker*.8,"temporal history did not reduce subpixel edge variation");
        frame.jitterPixels[0]=frame.jitterPixels[1]=0;frame.reset=true;Require(render()==native,"camera cut reused stale color history");
        port::RgbaImage checker[2];for(int variant=0;variant<2;++variant){auto& image=checker[variant];image.width=image.height=30;image.pixels.resize(30*30*4);
            for(int y=0;y<30;++y)for(int x=0;x<30;++x){const auto at=(y*30+x)*4;const uint8_t value=((x+y+variant)&1)?255:0;image.pixels[at]=image.pixels[at+1]=image.pixels[at+2]=value;image.pixels[at+3]=255;}}
        std::vector<uint8_t> references[2];options.temporalAA=false;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        for(int variant=0;variant<2;++variant)references[variant]=render({Sprite(0xffffffff,false,&checker[variant]),Sprite(variant?0xff00ff00:0xffff0000,true)});
        options.temporalAA=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());frame.reset=true;
        for(int step=0;step<6;++step){frame.jitterPixels[0]=(step&1)?.375f:-.375f;frame.jitterPixels[1]=.25f;
            const uint32_t color=(step&1)?0xff00ff00:0xffff0000;const auto sprites=std::vector<port::ModelRenderer::SpriteDraw>{Sprite(0xffffffff,false,&checker[step&1]),Sprite(color,true)};
            const auto temporal=render(sprites);const auto& current=references[step&1];
            for(int y=49;y<70;++y)for(int x=69;x<90;++x)for(int k=0;k<4;++k)Require(temporal[(y*160+x)*4+k]==current[(y*160+x)*4+k],"reactive scene sprite retained old history");
            const int hud=(12*160+12)*4;Require(temporal[hud]==((color>>16)&255)&&temporal[hud+1]==((color>>8)&255),"HUD was blended with temporal history");
        }
        frame.enabled=false;frame.reset=true;render();
        const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin metadata-free frame");gpu->Submit(port::GpuDrawList{160,120});
        Require(!gpu->ProcessScene(&error),"TAA silently accepted missing geometry motion");gpu->EndFrame();frame.enabled=true;
        options.temporalAA=false;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(!gpu->RequiresSceneProcessing(),"Off still extracted HUD");frame.reset=true;frame.jitterPixels[0]=frame.jitterPixels[1]=0;Require(render()==native,"TAA Off did not restore native image");
        std::printf("PASS %s MSAA%d: temporal stabilization, flat identity, cuts, reactive effects, HUD isolation, missing motion rejection and exact Off\n",gpu->Name(),samples);
    }
}
