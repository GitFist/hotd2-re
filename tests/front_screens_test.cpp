// Classic's opaque labels and background must stay a matching set with legacy
// 2x packs, while title/logo/other front-screen overrides retain their sizing.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "assets/asset_override.h"
#include "render/front_screens.h"

namespace fs = std::filesystem;
static void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void U32(std::vector<uint8_t>& out, uint32_t n) {
    for (int shift : {24,16,8,0}) out.push_back(static_cast<uint8_t>(n >> shift));
}
static void Chunk(std::vector<uint8_t>& out, const char* name, const std::vector<uint8_t>& data) {
    U32(out, static_cast<uint32_t>(data.size())); const size_t first = out.size();
    out.insert(out.end(), name, name + 4); out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = 0xffffffffu;
    for (size_t i = first; i < out.size(); ++i) {
        crc ^= out[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    U32(out, ~crc);
}
static void SolidPng(const fs::path& file, int width, int height) {
    std::vector<uint8_t> png{137,80,78,71,13,10,26,10}, ihdr, raw;
    U32(ihdr,width); U32(ihdr,height); ihdr.insert(ihdr.end(),{8,6,0,0,0}); Chunk(png,"IHDR",ihdr);
    for (int y=0;y<height;++y) {
        raw.push_back(0);
        for (int x=0;x<width;++x) raw.insert(raw.end(),{255,0,255,255});
    }
    std::vector<uint8_t> z{0x78,0x01};
    for (size_t at=0;at<raw.size();) {
        const size_t count=std::min(size_t(65535),raw.size()-at); const unsigned n=static_cast<unsigned>(count);
        z.insert(z.end(),{static_cast<uint8_t>(at+count==raw.size()),static_cast<uint8_t>(n),static_cast<uint8_t>(n>>8),
                         static_cast<uint8_t>(~n),static_cast<uint8_t>((~n)>>8)});
        z.insert(z.end(),raw.begin()+at,raw.begin()+at+count);at+=count;
    }
    uint32_t a=1,b=0;for(uint8_t c:raw){a=(a+c)%65521;b=(b+a)%65521;}U32(z,(b<<16)|a);
    Chunk(png,"IDAT",z);Chunk(png,"IEND",{});
    fs::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()),png.size());Require(out.good(),"write pack fixture");
}
static game::WorldHud Screens() {
    game::WorldHud hud;
    for(const char* n:{"hod2title00","hod2title01","hod2title02","hod2title03","hod2title10","hod2title11"}) {
        game::WorldHud::Screen s; s.file=std::string("tex\\")+n+".bin";s.w=s.h=256;hud.screens.push_back(s);
    }
    for(const char* n:{"arcade0","original_0","traning_0","boss_0","ranking_0","option_0","network_0","exit_0"})
        for(char i='0';i<='2';++i){game::WorldHud::Screen s;s.file=std::string("tex\\")+n+i+".bin";s.w=s.h=64;hud.screens.push_back(s);}
    game::WorldHud::Screen logo;logo.file="tex\\black_scr.bin";logo.w=logo.h=64;logo.srcX=logo.srcY=0;hud.screens.push_back(logo);
    return hud;
}
int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: front_screens_test <game directory>\n");return 2;}
    const auto root=fs::temp_directory_path()/ ("hotd2-front-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto previous=port::AssetDir();int result=0;
    try {
        Require(!std::getenv("HOTD2_ASSET_DIR")||!*std::getenv("HOTD2_ASSET_DIR"),"unset HOTD2_ASSET_DIR for generated pack fixture");
        port::SetAssetDir("");const auto hud=Screens();port::FrontScreens original(argv[1]);original.SetModernMenu(false,true);
        const auto native=original.Draws(hud);Require(native.size()==31,"all thirty real menu bitmaps and unrelated screen load");
        for(size_t i=0;i<hud.screens.size();++i) {
            const auto& tex=*native[i].texture;const auto& name=hud.screens[i].file;
            SolidPng(root/"bmp"/(name.substr(name.find_last_of('\\')+1)+".png"),tex.LogicalWidth()*2,tex.LogicalHeight()*2);
        }
        port::SetAssetDir(root.string());port::FrontScreens classic(argv[1]);classic.SetModernMenu(false,true);
        const auto packed=classic.Draws(hud);Require(packed.size()==native.size(),"pack retains all menu draws");
        for(size_t i=0;i<30;++i) {
            Require(packed[i].texture->pixels==native[i].texture->pixels,"Classic mixed pack background/highlights or lost Challenges cleanup");
            Require(packed[i].texture->width==native[i].texture->width&&packed[i].texture->height==native[i].texture->height,"Classic coupled artwork uses original dimensions");
        }
        Require(packed.back().texture->width==2*native.back().texture->width&&packed.back().texture->LogicalWidth()==native.back().texture->width,"unrelated 2x bitmap keeps physical and logical sizes");
        for(int i=0;i<4;++i)Require(packed.back().v[i].u==native.back().v[i].u&&packed.back().v[i].v==native.back().v[i].v,"2x source-rectangle UVs changed");
        classic.SetModernMenu(true,false);auto modern=classic.Draws(hud);
        Require(modern[0].texture->width==512&&modern[6].texture->width==128,"modern menu still uses pack artwork after style change");
        classic.SetModernMenu(false,true);auto restored=classic.Draws(hud);
        for(size_t i=0;i<30;++i)Require(restored[i].texture->pixels==native[i].texture->pixels,"style change reused incompatible cached menu art");
        puts("PASS Classic coherent original artwork and Challenges cleanup with 2x pack; other screens, logical sizing and style caches preserved");
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());result=1;}
    port::SetAssetDir(previous);
    if(root.parent_path()==fs::temp_directory_path()&&root.filename().string().rfind("hotd2-front-test-",0)==0)fs::remove_all(root);
    return result;
}
