// Exercise actual GPU targets/samplers/shaders, rather than just option serialization.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "render/asset_store.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void Identity(float m[16]) { std::fill(m,m+16,0.f); m[0]=m[5]=m[10]=m[15]=1; }
static port::ModelConstants Constants() {
    port::ModelConstants c{};
    Identity(c.world); Identity(c.viewProj);
    c.diffuse[0]=c.diffuse[1]=c.diffuse[2]=c.diffuse[3]=1;
    c.flags[1]=1; c.flags[2]=-1;
    return c;
}
static std::vector<uint8_t> Render(port::GpuDevice& gpu, const port::GpuDrawList& list) {
    static int capture = 0;
    ++capture;
    const float clear[4]={0,0,0,1};
    Require(gpu.BeginFrame(clear),"begin graphics frame");
    gpu.Submit(list);
    std::vector<uint8_t> pixels;
    const bool read = gpu.ReadPixels(&pixels);
    if (!read) std::fprintf(stderr,"Readback failed at capture %d (MSAA %d, FXAA %d)\n",capture,gpu.EffectiveGraphics().msaa,gpu.EffectiveGraphics().postAA);
#ifdef HOTD2_TEST_SDL_GPU
    if (!read && !std::strcmp(gpu.Name(),"sdlgpu")) std::fprintf(stderr,"SDL readback: %s\n",SDL_GetError());
#endif
    Require(read,"read graphics frame");
    gpu.EndFrame();
    return pixels;
}
static std::vector<uint8_t> Present(port::GpuDevice& gpu,const port::GpuDrawList& list,int w,int h) {
    const float clear[4]={0,0,0,1};Require(gpu.BeginFrame(clear),"begin presentation capture");gpu.Submit(list);
    std::vector<uint8_t> pixels;Require(gpu.ReadPresentation(w,h,&pixels),"read actual final presentation pixels");gpu.EndFrame();
    Require(pixels.size()==static_cast<size_t>(w)*h*4,"presentation capture returned wrong dimensions");return pixels;
}
static int Partial(const std::vector<uint8_t>& p) {
    int n=0; for (size_t i=0;i<p.size();i+=4) if(p[i]>0 && p[i]<255) ++n; return n;
}
static double Mean(const std::vector<uint8_t>& p) {
    double n=0; for (size_t i=0;i<p.size();i+=4) n+=p[i]; return n/(p.size()/4);
}
static double CenterVariation(const std::vector<uint8_t>& p) {
    double mean=0, square=0; int count=0;
    for(int y=12;y<52;++y) for(int x=12;x<52;++x) {
        double v=p[(y*64+x)*4]; mean+=v; square+=v*v; ++count;
    }
    return square/count-mean*mean/count/count;
}
static void SetQuad(port::GpuDevice& gpu,float u=1,float v=1,float offset=0) {
    std::array<game::ModelVertex,6> vertices{};
    const float corners[6][4]={{-.9f,-.9f,0,v},{.9f,-.9f,u,v},{-.9f,.9f,0,0},
                              {-.9f,.9f,0,0},{.9f,-.9f,u,v},{.9f,.9f,u,0}};
    for(int i=0;i<6;++i) {
        vertices[i].pos[0]=corners[i][0]; vertices[i].pos[1]=corners[i][1]; vertices[i].pos[2]=.5f;
        vertices[i].normal[2]=1; vertices[i].uv[0]=corners[i][2]+offset; vertices[i].uv[1]=corners[i][3]+offset;
    }
    gpu.SetStaticVertices(vertices.data(),vertices.size());
}

int main(int argc,char** argv) {
    std::string error;
    port::GpuDeviceDesc desc; desc.width=desc.height=64;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1 && !std::strcmp(argv[1],"sdlgpu")) {
        Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());
        gpu=port::CreateSdlGpuDevice(desc,&error);
    } else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());
    port::GraphicsOptions options;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    std::array<game::ModelVertex,3> triangle{};
    triangle[0].pos[0]=-.83f; triangle[0].pos[1]=-.8f;
    triangle[1].pos[0]=.82f; triangle[1].pos[1]=-.76f;
    triangle[2].pos[0]=-.67f; triangle[2].pos[1]=.85f;
    for(auto& v:triangle) {v.pos[2]=.5f;v.normal[2]=1;}
    gpu->SetStaticVertices(triangle.data(),triangle.size());
    port::GpuDrawList list; list.width=list.height=64; list.model.push_back(Constants());
    port::GpuDraw draw; draw.count=3; list.draws.push_back(draw);
    const auto original=Render(*gpu,list);
    Require(Partial(original)==0,"Off no longer produces native unfiltered triangle edges");
    for(int samples:{2,4}) {
        options.msaa=samples;
        Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        const auto antialiased=Render(*gpu,list);
        Require(Partial(antialiased)>20,"MSAA did not resolve intermediate coverage at triangle edges");
        Require(gpu->EffectiveGraphics().msaa==samples,"effective MSAA count not reported");
        // Multiple submissions preserve the multisample colour/depth, including translucent scene passes.
        const float clear[4]={0,0,0,1}; Require(gpu->BeginFrame(clear),"begin split draw");
        gpu->Submit(list); gpu->Submit(port::GpuDrawList{64,64});
        std::vector<uint8_t> split; Require(gpu->ReadPixels(&split),"read split draw"); gpu->EndFrame();
        Require(split==antialiased,"multisample target lost earlier Submit contents");
    }
    options.msaa=1; options.postAA=1;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Partial(Render(*gpu,list))>20,"FXAA did not smooth the high contrast diagonal");
    options.postAA=2;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Partial(Render(*gpu,list))>20,"three-pass SMAA did not smooth the high contrast diagonal");
    options.postAA=3;Require(gpu->GetGraphicsCapabilities().cmaa2,"CMAA2 storage formats unavailable on test device");
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Partial(Render(*gpu,list))>20,"Intel CMAA2 compute passes did not smooth the diagonal");
    options.postAA=0;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Render(*gpu,list)==original,"turning AA Off did not restore the exact native image");
    auto invalid=options; invalid.msaa=3;
    Require(!gpu->ConfigureGraphics(invalid,&error),"invalid sample count was accepted");
    Require(gpu->EffectiveGraphics().msaa==1 && Render(*gpu,list)==original,"failed graphics change mutated the live configuration");
    const float black[4]={0,0,0,1}; Require(gpu->BeginFrame(black),"begin transaction guard");
    Require(!gpu->ConfigureGraphics(options,&error),"graphics target changed inside a frame");
    gpu->Submit(list); gpu->EndFrame();

    options.brightness=75;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Mean(Render(*gpu,list))>Mean(original)+5,"brightness adjustment did not change final pixels");
    options=port::GraphicsOptions{};
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Render(*gpu,list)==original,"neutral final settings did not restore exact native output");
    const auto nativeUpscale=Present(*gpu,list,128,128);
    options.spatialUpscaler=1;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto easu=Present(*gpu,list,128,128);
    Require(easu!=nativeUpscale,"FSR1 EASU did not change spatial reconstruction");
    Require(std::abs(Mean(easu)-Mean(nativeUpscale))<3,"FSR1 changed overall image brightness");
    options.sharpening=3;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Present(*gpu,list,128,128)!=easu,"FSR1 RCAS did not sharpen reconstructed edges");
    options=port::GraphicsOptions{};Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Present(*gpu,list,128,128)==nativeUpscale,"turning FSR1 Off did not restore original scaling");

    // AF must improve a highly anisotropic footprint, while keeping authored point close-ups.
    port::RgbaImage stripes; stripes.width=stripes.height=256; stripes.pixels.resize(256*256*4);
    for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
        auto* p=stripes.pixels.data()+(y*256+x)*4;
        p[0]=p[1]=p[2]=(y/8)%2?255:0; p[3]=255;
    }
    auto texture=gpu->CreateTexture(stripes); Require(texture!=0,"stripe texture creation");
    SetQuad(*gpu,8,.5f); list.model[0]=Constants(); list.model[0].flags[0]=1;
    list.draws[0].count=6; list.draws[0].texture=texture; list.draws[0].state.mipmapped=1; list.draws[0].state.linear=1;
    const auto isotropic=Render(*gpu,list);
    options.anisotropy=16; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto anisotropic=Render(*gpu,list);
    const double before=CenterVariation(isotropic),after=CenterVariation(anisotropic);
    Require(after>before+100,"anisotropic filtering did not retain detail along the short texture axis");
    port::RgbaImage checker; checker.width=checker.height=8; checker.pixels.resize(8*8*4);
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        auto* p=checker.pixels.data()+(y*8+x)*4; p[0]=p[1]=p[2]=(x+y)%2?255:0;p[3]=255;
    }
    texture=gpu->CreateTexture(checker); Require(texture!=0,"point checker texture creation");
    SetQuad(*gpu); list.draws[0].texture=texture; list.draws[0].state.linear=0;
    options.anisotropy=1; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto point=Render(*gpu,list);
    for(int factor:{2,4,8,16}) {
        options.anisotropy=factor; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        Require(Render(*gpu,list)==point,"AF blurred authored point magnification");
    }
    for(int address:{1,2,3}) {
        SetQuad(*gpu,1,1,-.7f);
        list.draws[0].state.addrU=list.draws[0].state.addrV=address;
        options.anisotropy=1; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        const auto reference=Render(*gpu,list);
        options.anisotropy=16; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
        Require(Render(*gpu,list)==reference,"AF point magnification changed negative-UV wrap/mirror/clamp addressing");
    }
    SetQuad(*gpu); list.draws[0].state.addrU=list.draws[0].state.addrV=1;
    std::array<game::TLVertex,4> sprite{};
    const float spriteCorners[4][4]={{480,0,0,0},{480,120,0,1},{640,0,1,0},{640,120,1,1}};
    for(int i=0;i<4;++i) {
        sprite[i].x=spriteCorners[i][0]; sprite[i].y=spriteCorners[i][1]; sprite[i].z=.1f; sprite[i].rhw=1;
        sprite[i].diffuse=0xffffffff; sprite[i].u=spriteCorners[i][2]; sprite[i].v=spriteCorners[i][3];
    }
    gpu->SetSpriteVertices(sprite.data(),sprite.size());
    port::SpriteConstants sc{}; sc.screen[2]=-1; sc.screen[3]=1; list.sprite.push_back(sc);
    port::GpuDraw hud; hud.pipeline=port::GpuDraw::Pipeline::Sprite; hud.stream=port::GpuDraw::Stream::Sprite;
    hud.count=4;hud.texture=texture;hud.state.zfunc=8;hud.state.zwrite=0; list.draws.push_back(hud);
    options=port::GraphicsOptions{}; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto spriteReference=Render(*gpu,list);
    options=port::GraphicsOptions{}; options.textureFilter=2;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto filtered=Render(*gpu,list);
    Require(Partial(filtered)>Partial(point)+100,"3D linear override did not change magnification");
    for(int y=1;y<15;++y) for(int x=49;x<63;++x)
        Require(std::memcmp(filtered.data()+(y*64+x)*4,spriteReference.data()+(y*64+x)*4,4)==0,"3D texture filter override blurred the HUD sprite");
    // A flat middle tone is unaffected by FXAA; avoid the quad boundary and sprite.
    list.draws.resize(1); list.model[0].flags[0]=0;
    list.model[0].diffuse[0]=list.model[0].diffuse[1]=list.model[0].diffuse[2]=.37f;
    options=port::GraphicsOptions{}; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto flat=Render(*gpu,list); options.postAA=1;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str()); const auto fxaaFlat=Render(*gpu,list);
    for(int y=12;y<52;++y) for(int x=12;x<52;++x)
        Require(std::memcmp(flat.data()+(y*64+x)*4,fxaaFlat.data()+(y*64+x)*4,4)==0,"FXAA changed a flat interior");
    options.postAA=2;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto smaaFlat=Render(*gpu,list);
    for(int y=12;y<52;++y)for(int x=12;x<52;++x)
        Require(std::memcmp(flat.data()+(y*64+x)*4,smaaFlat.data()+(y*64+x)*4,4)==0,"SMAA changed a flat interior");
    options.postAA=3;Require(gpu->ConfigureGraphics(options,&error),error.c_str());const auto cmaaFlat=Render(*gpu,list);
    for(int y=20;y<44;++y)for(int x=20;x<44;++x)
        Require(std::memcmp(flat.data()+(y*64+x)*4,cmaaFlat.data()+(y*64+x)*4,4)==0,"CMAA2 changed a flat interior");
    options.postAA=0;options.spatialUpscaler=1;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto fsrFlat=Present(*gpu,list,128,128);
    for(int y=30;y<98;++y)for(int x=30;x<98;++x)
        Require(std::abs(int(fsrFlat[(y*128+x)*4])-int(flat[(32*64+32)*4]))<=1,"FSR1 changed a flat interior");
    options=port::GraphicsOptions{}; options.gamma=75;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Mean(Render(*gpu,list))>Mean(flat)+10,"gamma did not lift midtones");
    options=port::GraphicsOptions{}; options.contrast=75;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Mean(Render(*gpu,list))<Mean(flat)-3,"contrast adjustment did not change midtones");
    options=port::GraphicsOptions{}; options.saturation=0;
    list.model[0].diffuse[0]=.9f;list.model[0].diffuse[1]=.3f;list.model[0].diffuse[2]=.1f;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto desaturated=Render(*gpu,list);
    for(size_t i=0;i<desaturated.size();i+=4) Require(desaturated[i]==desaturated[i+1] && desaturated[i+1]==desaturated[i+2],"saturation zero is not neutral greyscale");
    options=port::GraphicsOptions{}; options.bloom=3;
    list.model[0].diffuse[0]=list.model[0].diffuse[1]=list.model[0].diffuse[2]=1;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto glow=Render(*gpu,list);
    options.bloom=0; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto noGlow=Render(*gpu,list);
    Require(Mean(glow)>Mean(noGlow)+.1,"SDR bloom did not add bright-area glow");
    options=port::GraphicsOptions{}; options.sharpening=3;options.textureFilter=2;
    list.model[0].flags[0]=1; // filtered checker provides a genuine soft edge to sharpen
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto sharp=Render(*gpu,list);
    options.sharpening=0; Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    Require(Render(*gpu,list)!=sharp,"sharpening did not change a filtered edge");
    options=port::GraphicsOptions{}; options.colorGrade=3;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    list.model[0].diffuse[0]=.9f; list.model[0].diffuse[1]=.3f; list.model[0].diffuse[2]=.1f;
    const auto monochrome=Render(*gpu,list);
    for(size_t i=0;i<monochrome.size();i+=4) Require(monochrome[i]==monochrome[i+1] && monochrome[i+1]==monochrome[i+2],"monochrome grade is not neutral greyscale");
    port::RgbaImage coverageTexture; coverageTexture.width=coverageTexture.height=1;
    coverageTexture.pixels={255,255,255,128};
    const auto halfAlpha=gpu->CreateTexture(coverageTexture); Require(halfAlpha!=0,"coverage test texture");
    list.draws[0].texture=halfAlpha;list.model[0]=Constants();list.model[0].flags[0]=1;list.model[0].flags[2]=.2f;list.model[0].flags[3]=2;
    options=port::GraphicsOptions{};options.msaa=4;
    Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto hardCutout=Render(*gpu,list);
    options.alphaToCoverage=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const auto softCutout=Render(*gpu,list);
    const size_t center=(32*64+32)*4;
    Require(hardCutout[center]==255 && softCutout[center]>0 && softCutout[center]<255,"alpha-to-coverage did not convert cutout alpha to MSAA coverage");
    Require(gpu->ResizeTarget(96,80),"resize after changing AA/filtering");
    Require(gpu->TargetWidth()==96 && gpu->TargetHeight()==80,"resize did not publish actual target dimensions");
    std::printf("%s graphics: MSAA resolve/retention, FXAA, Off restore, transactions, AF detail %.1f -> %.1f, point MAG, grading passed\n",gpu->Name(),before,after);
    return 0;
}
