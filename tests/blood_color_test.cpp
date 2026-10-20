// Blood variants share global IDs with effect and enemy-model textures.
#include "render/asset_store.h"
#include "assets/asset_override.h"
#include "game/palette_bank.h"
#include <cstdio>
#include <cstdlib>
#include <map>
static void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(int argc,char** argv){
    Check(argc>1,"usage: blood_color_test <game directory> [asset pack]");
    if(argc>2)port::SetAssetDir(argv[2]);
    port::AssetStore assets;std::string error;Check(assets.Open(argv[1],&error),error.c_str());
    Check(!assets.RedBlood(),"green is the default");
    const auto files=game::ListTextureFiles(assets.Exe());
    std::map<uint32_t,const port::RgbaImage*> reds;
    for(const auto& f:files)if(f.name=="scr_blood_red.bin")for(int i=0;i<f.count;++i){
        auto d=game::ReadTextureDesc(assets.Exe(),f,i);auto image=assets.Texture(f.name,i);
        Check(image!=nullptr,"red source decodes");reds.emplace(d.id,image);
    }
    Check(!reds.empty(),"red texture set exists");
    int mapped=0,different=0,redPixels=0,greenPixels=0,scaled=0,unaffected=0;
    for(const auto& f:files)for(int i=0;i<f.count;++i){
        auto d=game::ReadTextureDesc(assets.Exe(),f,i);auto found=reds.find(d.id);
        if(found==reds.end()){
            if(unaffected<5&&f.name=="common.bin"){
                assets.SetRedBlood(false);auto before=assets.Texture(f.name,i);
                assets.SetRedBlood(true);Check(assets.Texture(f.name,i)==before,"non-blood texture unchanged");++unaffected;
            }
            continue;
        }
        assets.SetRedBlood(false);auto green=assets.Texture(f.name,i);Check(green!=nullptr,"original shared texture decodes");
        auto red=found->second;
        assets.SetRedBlood(true);
        Check(assets.Texture(f.name,i)==red,"model material resolves selected global blood ID");
        Check(assets.TextureById(f.index,d.id)==red,"sprite resolves selected global blood ID");
        assets.SetRedBlood(false);Check(assets.Texture(f.name,i)==green,"switching back restores cached original");
        if(green->pixels!=red->pixels)++different;
        ++mapped;
    }
    for(auto [id,img]:reds){
        scaled+=img->width>img->LogicalWidth();
        for(size_t p=0;p<img->pixels.size();p+=4)if(img->pixels[p+3]>64&&img->pixels[p]>img->pixels[p+1]+30)++redPixels;
    }
    for(const auto& f:files)if(f.name=="scr_blood_green.bin")for(int i=0;i<f.count;++i){
        auto image=assets.Texture(f.name,i);
        for(size_t p=0;p<image->pixels.size();p+=4)if(image->pixels[p+3]>64&&image->pixels[p+1]>image->pixels[p]+30)++greenPixels;
    }
    Check(mapped>int(reds.size())&&different>0&&unaffected==5,"shared effects and model textures exercised");
    Check(redPixels>100&&greenPixels>100,"red and green variants have their respective colors");
    if(argc>2)Check(scaled>0,"optional high-resolution red textures are used");
    const auto expected=reds.begin()->second->pixels;const auto id=reds.begin()->first;
    assets.SetRedBlood(true);assets.ClearCache();Check(assets.RedBlood(),"scene cache reset retains color");
    Check(assets.TextureById(0x1BA,id)->pixels==expected,"selected texture survives scene reload");
    printf("PASS %zu blood IDs, %d shared references, %d color changes, %d scaled red textures\n",reds.size(),mapped,different,scaled);
}
