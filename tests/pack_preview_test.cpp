// Actual installed artwork only; bounded decoding, immutable selection caching,
// upright contact sheets and the native HUD rendering path.
#include "play/pack_preview.h"
#include "render/png_read.h"
#ifdef HOTD2_TEST_PREVIEW_GPU
#include "render/d3d11_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs=std::filesystem;
using Color=std::array<uint8_t,4>;
static void Require(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
static void U32(std::vector<uint8_t>& out,uint32_t n) { for(int shift:{24,16,8,0})out.push_back(uint8_t(n>>shift)); }
static void Chunk(std::vector<uint8_t>& out,const char* name,const std::vector<uint8_t>& data) {
    U32(out,uint32_t(data.size()));const size_t start=out.size();out.insert(out.end(),name,name+4);out.insert(out.end(),data.begin(),data.end());
    uint32_t crc=0xffffffff;for(size_t i=start;i<out.size();++i){crc^=out[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}U32(out,~crc);
}
static std::vector<uint8_t> Png(int width,int height,const std::vector<Color>& colors,bool excess=false) {
    std::vector<uint8_t> result{137,80,78,71,13,10,26,10},header;U32(header,width);U32(header,height);header.insert(header.end(),{8,6,0,0,0});Chunk(result,"IHDR",header);
    std::vector<uint8_t> raw;for(int y=0;y<height;++y){raw.push_back(0);for(int x=0;x<width;++x){const auto c=colors[(size_t(y)*width+x)%colors.size()];raw.insert(raw.end(),c.begin(),c.end());}}
    if(excess)raw.insert(raw.end(),1024,0);
    Require(raw.size()<65536,"test PNG stored block too large");const uint16_t n=uint16_t(raw.size());
    std::vector<uint8_t> z{0x78,0x01,0x01,uint8_t(n),uint8_t(n>>8),uint8_t(~n),uint8_t((~n)>>8)};z.insert(z.end(),raw.begin(),raw.end());
    uint32_t a=1,b=0;for(uint8_t c:raw){a=(a+c)%65521;b=(b+a)%65521;}U32(z,(b<<16)|a);Chunk(result,"IDAT",z);Chunk(result,"IEND",{});return result;
}
static void Write(const fs::path& path,const std::vector<uint8_t>& bytes) { fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());Require(file.good(),"write generated fixture"); }
static void Text(const fs::path& path,const std::string& text) { fs::create_directories(path.parent_path());std::ofstream(path)<<text; }
static Color Pixel(const port::RgbaImage* image,int x,int y) { Require(image!=nullptr,"missing preview image");const auto* p=image->pixels.data()+(y*image->width+x)*4;return {p[0],p[1],p[2],p[3]}; }
int main(int argc,char** argv) {
    const auto temp=fs::temp_directory_path();const auto dir=temp/("hotd2-preview-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result=0;
    try {
        const Color red{240,20,10,255},green{20,220,30,255},blue{10,30,230,255},yellow{220,210,10,255};
        const auto art=Png(2,2,{red,green,blue,yellow});
        play::PackPreview preview;Require(preview.Select({})&&preview.Info().original&&!preview.Image(),"Original fabricated a preview or failed selection");
        std::vector<port::ModelRenderer::SpriteDraw> sprites;preview.AppendSprites(sprites,420,125,200,150);Require(sprites.empty(),"Original emitted artwork");
        Text(dir/"named/pack.ini","format=1\nname=Canal artwork\nauthor=Test Artist\nversion=3\npreview=cover art.png\n");Write(dir/"named/cover art.png",art);
        Require(preview.Select(dir/"named")&&preview.Info().explicitArtwork&&preview.Info().name=="Canal artwork"&&preview.Info().author=="Test Artist"&&preview.Info().version=="3","explicit artwork/metadata not loaded");
        Require(preview.Info().sources==std::vector<std::string>{"cover art.png"},"preview source attribution missing");
        const auto* explicitImage=preview.Image();const auto explicitPixels=explicitImage->pixels;
        Require(Pixel(explicitImage,80,30)==red&&Pixel(explicitImage,170,30)==green&&Pixel(explicitImage,80,160)==blue&&Pixel(explicitImage,170,160)==yellow,"upright PNG orientation changed");
        preview.AppendSprites(sprites,420,125,200,150);Require(sprites.size()==1&&sprites[0].hud&&sprites[0].linear&&sprites[0].texture==explicitImage,"preview did not emit one native HUD sprite");
        Require(sprites[0].v[0].x==420&&sprites[0].v[0].y==125&&sprites[0].v[3].x==620&&sprites[0].v[3].y==275,"preview fit changed requested 4:3 bounds");
        Write(dir/"named/cover art.png",{1});Require(!preview.Select(dir/"named")&&preview.Image()->pixels==explicitPixels,"unchanged selection reread artwork each frame");
        for(int i=0;i<4;++i)Write(dir/"mosaic/tex/bank"/(std::to_string(i)+".png"),Png(1,1,{std::array<Color,4>{red,green,blue,yellow}[i]}));
        Write(dir/"mosaic/tex/bank/0_nr.png",Png(1,1,{{255,255,255,255}}));
        Require(preview.Select(dir/"mosaic")&&preview.Info().sources.size()==4&&!preview.Info().explicitArtwork,"legacy contact sheet not generated");
        Require(Pixel(preview.Image(),40,30)==red&&Pixel(preview.Image(),180,30)==green&&Pixel(preview.Image(),40,150)==blue&&Pixel(preview.Image(),180,150)==yellow,"sorted four-color contact sheet or map exclusion wrong");
        Require(explicitImage->pixels==explicitPixels&&preview.Image()!=explicitImage,"selection invalidated cached renderer texture pointers");
        Require(preview.Select(dir/"named")&&preview.Image()==explicitImage,"returning selection reallocated cached artwork");
        Write(dir/"outside.png",art);Text(dir/"escape/pack.ini","preview=../outside.png\n");preview.Select(dir/"escape");Require(!preview.Image(),"manifest path traversal escaped the pack");
        Text(dir/"absolute/pack.ini","preview="+(dir/"outside.png").generic_string()+"\n");preview.Select(dir/"absolute");Require(!preview.Image(),"absolute manifest preview was allowed");
        Text(dir/"fallback/pack.ini","preview=../outside.png\n");Write(dir/"fallback/bmp/title.png",Png(1,1,{red}));preview.Select(dir/"fallback");
        Require(preview.Image()&&!preview.Info().explicitArtwork&&!preview.Info().message.empty()&&preview.Info().sources==std::vector<std::string>{"bmp/title.png"},"invalid explicit artwork did not fall back safely");
        Write(dir/"bomb/tex/bank/0.png",Png(1,1,{red},true));preview.Select(dir/"bomb");Require(!preview.Image(),"compressed stream expanded past its declared raster");
        auto huge=art;huge[16]=0;huge[17]=0;huge[18]=0x20;huge[19]=0;Write(dir/"huge/tex/bank/0.png",huge);preview.Select(dir/"huge");Require(!preview.Image(),"oversized PNG was decoded");
        Write(dir/"large-file/tex/bank/0.png",art);{std::fstream file(dir/"large-file/tex/bank/0.png",std::ios::binary|std::ios::in|std::ios::out);file.seekp((8u<<20)+1);file.put(0);}preview.Select(dir/"large-file");Require(!preview.Image(),"oversized input file was decoded");
        fs::create_directories(dir/"maps/materials/tex/bank");Write(dir/"maps/materials/tex/bank/0_nr.png",art);preview.Select(dir/"maps");Require(!preview.Image(),"normal/roughness material appeared as color artwork");
        port::PngImage decoded;std::string error;Require(port::ReadPngLimited(art.data(),art.size(),16,decoded,&error)&&decoded.rgba.size()==16,"bounded decoder rejected an exact raster");
        Require(!port::ReadPngLimited(art.data(),art.size(),15,decoded,&error),"RGBA decode budget ignored");
        const auto bomb=Png(1,1,{red},true);Require(port::ReadPng(bomb.data(),bomb.size(),decoded,&error),"ordinary loader behavior unexpectedly changed");
        Require(!port::ReadPngLimited(bomb.data(),bomb.size(),16,decoded,&error),"bounded inflate accepted excess raster bytes");
        auto badChunk=art;for(int i=33;i<37;++i)badChunk[i]=255;
        Require(!port::ReadPngLimited(badChunk.data(),badChunk.size(),16,decoded,&error),"overflowing PNG chunk length was accepted");
#ifdef HOTD2_TEST_PREVIEW_GPU
        if(argc>1) {
            port::GpuDeviceDesc desc;desc.width=640;desc.height=480;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
            if(!std::strcmp(argv[1],"sdlgpu")){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
            gpu=port::CreateD3D11Device(desc,&error);
            Require(gpu!=nullptr,error.c_str());port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error.c_str());
            const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin preview GPU frame");renderer.DrawSprites(640,480,sprites);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read preview GPU pixels");gpu->EndFrame();
            auto gpuColor=[&](int x,int y,const Color& expected){for(int c=0;c<4;++c)Require(std::abs(int(pixels[(y*640+x)*4+c])-int(expected[c]))<=1,"native preview pixel/color orientation differs");};
            gpuColor(480,150,red);gpuColor(560,150,green);gpuColor(480,250,blue);gpuColor(560,250,yellow);
            Require(pixels[(100*640+400)*4]==0,"preview escaped its UI placement");renderer.ClearCache();
            std::printf("PASS %s actual native HUD preview colors, orientation and placement\n",gpu->Name());
        }
#endif
        if(argc>2){play::PackPreview installed;installed.Select(argv[2]);Require(installed.Image()&&!installed.Info().sources.empty(),"installed color pack did not produce actual preview artwork");
            std::printf("PASS installed pack: %s, %zu actual PNG samples decoded into %dx%d contact sheet\n",installed.Info().name.c_str(),installed.Info().sources.size(),installed.Image()->width,installed.Image()->height);}
        preview.ResetAfterRendererCacheClear();preview.Select(dir/"named");Require(!preview.Image(),"explicit cache reset did not allow changed artwork to be rechecked");
        std::puts("PASS installed-art preview metadata, contact sheet, immutable selection cache, bounded PNG and path containment");
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());result=1;}
    if(dir.parent_path()==temp&&dir.filename().string().rfind("hotd2-preview-test-",0)==0)fs::remove_all(dir);
    return result;
}
