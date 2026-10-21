// Real output-resolution raster UI, not a resized low-resolution overlay.
#include "render/d3d11_device.h"
#include "render/model_renderer.h"
#include "game/camera.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static game::Model Model(){
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,.3f,.2f,.1f};std::memcpy(w+10,material,sizeof(material));game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-100.f,-75.f},{-100.f,75.f},{100.f,-75.f},{100.f,75.f}}){game::ModelVertex vertex{};vertex.pos[0]=xy.first;vertex.pos[1]=xy.second;vertex.pos[2]=-100;vertex.normal[2]=1;strip.vertices.push_back(vertex);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
static port::ModelRenderer::SpriteDraw Sprite(float left,float top,bool hud,const port::RgbaImage* texture){
    port::ModelRenderer::SpriteDraw sprite;sprite.hud=hud;sprite.texture=texture;sprite.isp=4u<<29;sprite.tsp=0x80000;
    sprite.v[0]={left,top,0,1,0xffffffff,0,0,0};sprite.v[1]={left+128,top,0,1,0xffffffff,0,1,0};
    sprite.v[2]={left,top+128,0,1,0xffffffff,0,0,1};sprite.v[3]={left+128,top+128,0,1,0xffffffff,0,1,1};return sprite;
}
int main(int argc,char** argv){
    std::string error;port::GpuDeviceDesc desc;desc.width=160;desc.height=120;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
    gpu=port::CreateD3D11Device(desc,&error);Require(gpu!=nullptr,error.c_str());Require(gpu->GetGraphicsCapabilities().outputResolutionHud,"real output-HUD capability missing");
    port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());port::AssetStore assets;auto model=Model();
    port::ModelRenderer::SceneItem item;item.model=&model;item.cullable=false;item.noMeshCull=true;item.motionTracked=true;item.motionIdentity.task=1;
    port::RgbaImage checker;checker.width=checker.height=64;checker.pixels.resize(64*64*4);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const size_t at=(y*64+x)*4;const uint8_t c=((x+y)&1)?224:32;checker.pixels[at]=32;checker.pixels[at+1]=c;checker.pixels[at+2]=64;checker.pixels[at+3]=255;}
    const std::vector<port::ModelRenderer::SpriteDraw> sprites={Sprite(48,48,true,&checker),Sprite(360,240,false,&checker)};
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    uint64_t frame=0;
    auto drawFrame=[&]{
        renderer.SetScene(assets,{item});port::TemporalFrameInfo temporal;temporal.enabled=gpu->EffectiveGraphics().temporalAA;temporal.reset=true;temporal.frameId=++frame;renderer.SetTemporalFrame(temporal);
        const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin output HUD frame");renderer.DrawGame(gpu->TargetWidth(),gpu->TargetHeight(),projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
    };
    auto render=[&](int outW,int outH){drawFrame();
        std::vector<uint8_t> pixels;Require(gpu->ReadPresentation(outW,outH,&pixels),"read actual output HUD target");gpu->EndFrame();return pixels;
    };
    auto renderLinear=[&](int outW,int outH){drawFrame();std::vector<float> pixels;Require(gpu->ReadLinearPresentation(outW,outH,&pixels),"read actual scRGB HUD target");gpu->EndFrame();return pixels;};
    auto compareHud=[&](const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,int width,int height,const char* stage="sharp"){
        const int left=int(width*54.f/640),right=int(width*166.f/640),top=int(height*54.f/480),bottom=int(height*166.f/480);
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)for(int k=0;k<4;++k)if(a[(y*width+x)*4+k]!=b[(y*width+x)*4+k]){
            std::fprintf(stderr,"HUD mismatch %s MSAA%d AA%d %dx%d at %d,%d channel%d: %u vs %u\n",stage,gpu->EffectiveGraphics().msaa,gpu->EffectiveGraphics().postAA,width,height,x,y,k,a[(y*width+x)*4+k],b[(y*width+x)*4+k]);Require(false,"HUD was filtered, graded twice, lost depth or rasterized at input size");}
    };
    auto variation=[&](const std::vector<uint8_t>& pixels,int width,int height){
        double sum=0;for(int y=int(height*.12f);y<int(height*.33f);++y)for(int x=int(width*.09f);x<int(width*.25f);++x)sum+=std::abs(int(pixels[(y*width+x)*4+1])-int(pixels[(y*width+x+1)*4+1]));return sum;
    };
    for(int samples:{1,2,4})for(auto size:{std::pair<int,int>{320,240},{347,257}}){
        const int outW=size.first,outH=size.second;port::PresentParams present;present.w=outW;present.h=outH;gpu->SetPresent(present);
        port::GraphicsOptions options;options.msaa=samples;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(gpu->ResizeTarget(160,120),"restore input resolution");const auto old=render(outW,outH);
        // The output UI is deliberately single sampled. A multisampled checker
        // reference can blend the shared triangle edge at fractional dimensions.
        Require(gpu->ResizeTarget(outW,outH),"full-resolution HUD reference");auto referenceOptions=options;referenceOptions.msaa=1;Require(gpu->ConfigureGraphics(referenceOptions,&error),error.c_str());
        const auto reference=render(outW,outH);Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(gpu->ResizeTarget(160,120),"restore input for independent HUD");
        options.outputResolutionHud=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto sharp=render(outW,outH);
        Require(gpu->TargetWidth()==160&&gpu->TargetHeight()==120&&gpu->HudTargetWidth()==outW&&gpu->HudTargetHeight()==outH,"HUD target replaced internal scene dimensions");
        compareHud(sharp,reference,outW,outH);Require(variation(sharp,outW,outH)>variation(old,outW,outH)*1.5+1000,"output HUD did not recover genuine pixel detail");
        // A world sprite stays in the internal scene and keeps its old pixels.
        for(int y=int(outH*.55f);y<int(outH*.72f);++y)for(int x=int(outW*.6f);x<int(outW*.72f);++x)for(int k=0;k<4;++k)Require(sharp[(y*outW+x)*4+k]==old[(y*outW+x)*4+k],"world sprite was incorrectly extracted or reordered with HUD");
        for(int aa:{1,2,3}){
            options.postAA=aa;options.sharpening=3;options.spatialUpscaler=1;options.bloom=2;options.temporalAA=aa==1;
            Require(gpu->ConfigureGraphics(options,&error),error.c_str());compareHud(render(outW,outH),reference,outW,outH,"post AA");
            if(options.temporalAA){std::vector<float> motion;Require(gpu->ReadMotionPixels(&motion)&&motion.size()==160u*120*4,"HUD output changed input-resolution motion/depth metadata");}
        }
        // Color and CRT phosphors/glow still happen after the
        // new HUD, exactly once. Compare actual reference pixels, not a formula.
        options.postAA=options.sharpening=options.spatialUpscaler=options.bloom=0;options.temporalAA=false;
        options.gamma=43;options.brightness=54;options.saturation=37;options.colorGrade=2;options.crtMask=2;options.crtGlow=1;
        Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto graded=render(outW,outH);
        options.outputResolutionHud=false;options.msaa=1;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(gpu->ResizeTarget(outW,outH),"resize graded reference");const auto gradedReference=render(outW,outH);compareHud(graded,gradedReference,outW,outH,"grade/CRT");
        Require(gpu->ResizeTarget(160,120),"restore final input");options=port::GraphicsOptions{};options.msaa=samples;Require(gpu->ConfigureGraphics(options,&error),error.c_str());Require(render(outW,outH)==old,"HUD Off did not restore exact original mixed image");
        std::printf("PASS %s MSAA%d %dx%d: genuine HUD pixel detail, internal world sprites, TAA/spatial isolation, once-only grade/CRT and exact Off\n",gpu->Name(),samples,outW,outH);
    }
    // Diagnostic FP16 output proves the HUD receives the same single scRGB
    // conversion as a full-resolution reference, without changing a display.
    port::PresentParams hdrPresent;hdrPresent.w=320;hdrPresent.h=240;gpu->SetPresent(hdrPresent);
    port::GraphicsOptions hdrOptions;hdrOptions.outputResolutionHud=true;hdrOptions.hdrPaperWhite=200;Require(gpu->ConfigureGraphics(hdrOptions,&error),error.c_str());const auto linear=renderLinear(320,240);
    hdrOptions.outputResolutionHud=false;Require(gpu->ConfigureGraphics(hdrOptions,&error),error.c_str());Require(gpu->ResizeTarget(320,240),"full-resolution scRGB reference");const auto linearReference=renderLinear(320,240);
    for(int y=30;y<78;++y)for(int x=30;x<78;++x)for(int k=0;k<4;++k)Require(linear[(y*320+x)*4+k]==linearReference[(y*320+x)*4+k],"HUD scRGB transfer/paper white was applied more than once");
    Require(gpu->ResizeTarget(160,120),"restore input after scRGB proof");
    // Invalid output requests reject transactionally, and a resize failure in a
    // later frame exposes an explicit error while retaining readable input UI.
    port::PresentParams present;present.w=320;present.h=240;gpu->SetPresent(present);port::GraphicsOptions options;options.outputResolutionHud=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    present.w=9000;present.h=1;gpu->SetPresent(present);Require(!gpu->ConfigureGraphics(options,&error),"invalid output target accepted");render(320,240);
    Require(!gpu->HudCompositionError().empty()&&gpu->HudTargetWidth()==160,"failed HUD resize was silent or lost internal fallback");
    present.w=320;present.h=240;gpu->SetPresent(present);render(320,240);Require(gpu->HudCompositionError().empty()&&gpu->HudTargetWidth()==320,"valid HUD resize did not recover");
}
