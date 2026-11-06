#include "assets/naomi_visuals.h"
#include "render/asset_store.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs=std::filesystem;
namespace {
int checks=0;
void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
void U32(std::vector<uint8_t>& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(static_cast<uint8_t>(v>>(8*i)));}
void Put(std::vector<uint8_t>& b,size_t p,uint32_t v){for(int i=0;i<4;++i)b[p+i]=static_cast<uint8_t>(v>>(8*i));}
void Float(std::vector<uint8_t>& b,float v){uint32_t n;std::memcpy(&n,&v,4);U32(b,n);}
void Name(std::vector<uint8_t>& b,const char* s){U32(b,static_cast<uint32_t>(std::strlen(s)));b.insert(b.end(),s,s+std::strlen(s));}
uint32_t Crc(const uint8_t* p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;++i){c^=p[i];for(int k=0;k<8;++k)c=(c>>1)^(0xEDB88320u&(0u-(c&1u)));}return~c;}
void Checksum(std::vector<uint8_t>& b){Put(b,12,static_cast<uint32_t>(b.size()-32));Put(b,16,Crc(b.data()+32,b.size()-32));}
void Write(const fs::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());Require(f.good(),"cannot write fixture");}
size_t Faces(const game::Model& m){size_t count=0;for(const auto& mesh:m.meshes)for(const auto& s:mesh.strips){std::vector<game::ModelVertex> tri;game::Triangulate(s,tri);for(size_t i=0;i+2<tri.size();i+=3){float a[3],b[3];for(int k=0;k<3;++k){a[k]=tri[i+1].pos[k]-tri[i].pos[k];b[k]=tri[i+2].pos[k]-tri[i].pos[k];}const float x=a[1]*b[2]-a[2]*b[1],y=a[2]*b[0]-a[0]*b[2],z=a[0]*b[1]-a[1]*b[0];count+=x*x+y*y+z*z>1e-20f;}}return count;}
std::array<float,6> Bounds(const game::Model& model){std::array<float,6> b{1e30f,1e30f,1e30f,-1e30f,-1e30f,-1e30f};for(const auto& m:model.meshes)for(const auto& s:m.strips)for(const auto& v:s.vertices)for(int k=0;k<3;++k){b[k]=std::min(b[k],v.pos[k]);b[k+3]=std::max(b[k+3],v.pos[k]);}return b;}
std::vector<uint8_t> Model(){
    std::vector<uint8_t> b;U32(b,16);U32(b,16);U32(b,228);U32(b,0);U32(b,0);U32(b,0);Float(b,0);Float(b,0);Float(b,0);Float(b,1);
    for(int i=0;i<20;++i){if(i==0)U32(b,0x80000000);else if(i==10)Float(b,.75f);else if(i>=11&&i<=14)Float(b,1);else U32(b,i==19?104:0);}
    U32(b,0x48);U32(b,1);
    for(const auto& v:{std::array<float,8>{0,0,0,0,0,1,0,0},{1,0,0,0,0,1,1,0},{0,1,0,0,0,1,0,-1}})for(float n:v)Float(b,n);
    U32(b,0);Require(b.size()==228,"model fixture layout incorrect");return b;
}
std::vector<uint8_t> Fixture(size_t* descriptor,bool indexed=false,bool invalidIndex=false,uint32_t meshEvidence=1){
    std::vector<uint8_t> p;
    const auto model=Model();U32(p,0);Name(p,"common.bin");U32(p,0);U32(p,0);U32(p,model.size());U32(p,model.size());p.insert(p.end(),model.begin(),model.end());
    auto texture=[&](const char* name,uint16_t color){U32(p,1);Name(p,name);U32(p,1);U32(p,0);U32(p,8);U32(p,8);if(!*descriptor)*descriptor=p.size()+32;
        U32(p,2|(2u<<16));U32(p,0x902);U32(p,0);U32(p,777);for(int i=0;i<4;++i){p.push_back(color&255);p.push_back(color>>8);}};
    texture("common.bin",0xFFFF);texture("scr_blood_red.bin",0xFF00);texture("scr_blood_green.bin",0xF0F0);
    if(indexed){
        auto indexTexture=[&](const char* name,uint32_t format,const std::vector<uint8_t>& pixels){U32(p,1);Name(p,name);U32(p,1);U32(p,0);U32(p,pixels.size());U32(p,pixels.size());U32(p,2|(2u<<16));U32(p,format);U32(p,0);U32(p,999);p.insert(p.end(),pixels.begin(),pixels.end());};
        indexTexture("pal4.bin",0x500,{0x10,0x32});indexTexture("pal8.bin",0x702,{0,1,2,static_cast<uint8_t>(invalidIndex?255:95)});
        U32(p,2);
        for(uint32_t palette=0;palette<2;++palette){U32(p,palette);U32(p,0);const uint32_t colors=palette?16:96;U32(p,colors);for(uint32_t i=0;i<colors;++i){const uint16_t color=i==1?0x8000:i==2?0x8421:i==3?0xFFFF:i==95?0xFC00:0;p.push_back(color&255);p.push_back(color>>8);}}
        U32(p,2);Name(p,"pal4.bin");U32(p,0);U32(p,1);Name(p,"pal8.bin");U32(p,0);U32(p,0);
        U32(p,1);Name(p,"runtime.bin");U32(p,0);U32(p,0);Name(p,"common.bin");U32(p,0);U32(p,0);U32(p,meshEvidence);
    }
    Name(p,"common.bin");U32(p,0);Name(p,"common.bin");U32(p,0);
    Name(p,"common.bin");U32(p,0);Name(p,"common.bin");U32(p,0);U32(p,1);
    std::vector<uint8_t> out{'H','2','N','V','I','S','0','1'};U32(out,indexed?2:1);U32(out,p.size());U32(out,Crc(p.data(),p.size()));U32(out,indexed?6:4);U32(out,1);U32(out,1);out.insert(out.end(),p.begin(),p.end());return out;
}

void TypingCoexistence(port::AssetStore& assets, const fs::path& bank) {
    std::string error;
    Require(assets.LoadTypingReference(bank.string(), &error), error.c_str());
    const auto& reference = *assets.PreparedTypingReference();
    struct PreparedModel {
        uint32_t id;
        std::string bank;
        game::Model model;
        std::vector<std::pair<uint32_t, port::RgbaImage>> textures;
    };
    std::vector<PreparedModel> prepared;
    // The word frame, A glyph and Strength's changed body all need their Typing data.
    for (int source : {0x129E, 7050, 0x471}) {
        PreparedModel fixture;
        fixture.id = reference.SourceModel(source);
        Require(fixture.id >= 0x4000, "Typing fixture must use its own model identity");
        Require(assets.ModelById(fixture.id, fixture.model, &fixture.bank), "prepared Typing model is missing");
        Require(fixture.bank.rfind("typing_", 0) == 0 || fixture.bank.rfind("pol_typing_", 0) == 0,
                "Typing model must retain its isolated resource name");
        for (const auto& mesh : fixture.model.meshes) {
            if (!mesh.material.Textured()) continue;
            const uint32_t texture = mesh.material.TextureRef();
            const auto* image = assets.Texture(fixture.bank, texture);
            Require(image && !image->pixels.empty(), "prepared Typing model texture is missing");
            fixture.textures.emplace_back(texture, *image);
        }
        Require(!fixture.textures.empty(), "Typing fixture must exercise its original texture bank");
        prepared.push_back(std::move(fixture));
    }
    int fontFile = 0;
    uint32_t fontTexture = 0;
    Require(assets.SourceSprite(0x4F74 + 'A' - 32, &fontFile, &fontTexture), "Typing name-entry glyph is missing");
    const auto* font = assets.TextureById(fontFile, fontTexture);
    Require(font && font->width == 512 && font->height == 256, "Typing name-entry atlas is missing");
    const auto originalFont = *font;
    for (bool naomi : {true, false}) {
        Require(assets.SetNaomiGraphics(naomi, &error), error.c_str());
        const auto* sharedTexture = assets.Texture("common.bin", 44);
        Require(sharedTexture && sharedTexture->width == (naomi ? 128 : 64) && sharedTexture->LogicalWidth() == 64,
                "shared world textures must switch profiles while Typing data is loaded");
        bool sharedModel = false;
        for (int source = 1; source < 0x4000 && !sharedModel; ++source) {
            const int id = reference.SourceModel(source);
            if (id <= 0 || id >= 0x4000) continue;
            game::Model model;
            std::string resource;
            if (assets.NaomiModelById(uint32_t(id), model, &resource)) {
                Require(resource.rfind("naomi:", 0) == 0 && !model.meshes.empty(),
                        "shared Typing world model must use its verified Naomi resource");
                sharedModel = true;
            }
        }
        Require(sharedModel == naomi, "shared Typing world geometry must follow the selected profile");
        for (const auto& fixture : prepared) {
            game::Model actual, replacement;
            game::ModelMesh replacementMesh;
            std::string resource;
            Require(!assets.NaomiModelById(fixture.id, replacement, nullptr) &&
                    !assets.NaomiMeshById(fixture.id, 0, replacementMesh, nullptr),
                    "Naomi must not replace Typing-specific model or mesh identities");
            Require(assets.ModelById(fixture.id, actual, &resource) && resource == fixture.bank &&
                    actual.root == fixture.model.root && actual.meshes.size() == fixture.model.meshes.size(),
                    "Typing model identity must survive profile switches");
            for (size_t m = 0; m < actual.meshes.size(); ++m) {
                const auto& before = fixture.model.meshes[m];
                const auto& after = actual.meshes[m];
                Require(std::equal(std::begin(before.material.words), std::end(before.material.words), after.material.words) &&
                        before.strips.size() == after.strips.size(), "Typing materials must remain intact");
                for (size_t s = 0; s < after.strips.size(); ++s) {
                    const auto& a = before.strips[s];
                    const auto& b = after.strips[s];
                    Require(a.ctrl == b.ctrl && a.triangleList == b.triangleList && a.offsets == b.offsets &&
                            a.vertices.size() == b.vertices.size() &&
                            (a.vertices.empty() || std::memcmp(a.vertices.data(), b.vertices.data(),
                                                              a.vertices.size() * sizeof(game::ModelVertex)) == 0),
                            "Typing glyph and actor geometry must remain intact");
                }
            }
            for (const auto& [texture, original] : fixture.textures) {
                const auto* image = assets.Texture(resource, texture);
                Require(image && image->width == original.width && image->height == original.height &&
                        image->pixels == original.pixels, "Typing model textures must remain intact");
            }
        }
        font = assets.TextureById(fontFile, fontTexture);
        Require(font && font->width == originalFont.width && font->height == originalFont.height &&
                font->pixels == originalFont.pixels, "Typing sprite font must survive profile switches");
    }
    std::puts("PASS shared Naomi visuals with original Typing models, glyphs, palettes and PC restoration");
}
}
int main(int argc,char** argv){
    const auto temp=fs::temp_directory_path();const auto dir=temp/("hotd2-naomi-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(dir);int result=0;
    try{
        const auto path=dir/"good.bank";size_t descriptor=0;auto bytes=Fixture(&descriptor);Write(path,bytes);std::string error;
        port::NaomiVisuals bank;Require(port::NaomiVisuals::Inspect(path,&error),"valid bank header not discovered");Require(bank.Load(path,&error),error.c_str());Require(bank.TextureMappingCount()==1&&bank.ModelMappingCount()==1,"bank correspondence counts incorrect");
        const auto* map=bank.TextureMapping("common.bin",0);Require(map&&map->bank=="common.bin"&&map->index==0,"texture correspondence missing");
        game::Model model;std::string name;Require(bank.Model("common.bin",0,model,&name)&&name=="common.bin"&&Faces(model)==1,"native model parse lost geometry");Require(!bank.Model("common.bin",1,model,&name),"missing model correspondence fabricated");
        size_t indexedDescriptor=0;Write(dir/"indexed.bank",Fixture(&indexedDescriptor,true));port::NaomiVisuals indexed;Require(indexed.Load(dir/"indexed.bank",&error),error.c_str());Require(indexed.PaletteTextureCount()==2&&indexed.MeshMappingCount()==1,"native palette or mesh bindings lost");std::vector<uint8_t> rgba;
        Require(indexed.DecodeRgba("pal4.bin",0,rgba)&&rgba.size()==16&&rgba[4]==8&&rgba[5]==8&&rgba[6]==8&&rgba[7]==255,"native4-bit palette lost1555 precision or rectangular twiddle order");
        Require(indexed.DecodeRgba("pal8.bin",0,rgba)&&rgba[12]==255&&rgba[13]==0&&rgba[14]==0&&rgba[15]==255,"native8-bit palette failed indices above15");game::ModelMesh mesh;Require(indexed.Mesh("runtime.bin",0,0,mesh,&name)&&name=="common.bin"&&mesh.strips.size()==1,"explicit grouped native mesh correspondence lost");
        Write(dir/"uv-source.bank",Fixture(&indexedDescriptor,true,false,12));port::NaomiVisuals uvSource;Require(uvSource.Load(dir/"uv-source.bank",&error),error.c_str());uint32_t evidence=0;Require(!uvSource.Mesh("runtime.bin",0,0,mesh,nullptr),"UV-only source exposed as a whole mesh replacement");Require(uvSource.Mesh("runtime.bin",0,0,mesh,nullptr,&evidence)&&evidence==12,"explicit UV-only source evidence lost");
        Write(dir/"skin-uv-source.bank",Fixture(&indexedDescriptor,true,false,16));Require(uvSource.Load(dir/"skin-uv-source.bank",&error),error.c_str());Require(!uvSource.Mesh("runtime.bin",0,0,mesh,nullptr),"authored skin UV source exposed to an unaware caller");Require(uvSource.Mesh("runtime.bin",0,0,mesh,nullptr,&evidence)&&evidence==16,"verified authored skin UV evidence lost");
        Write(dir/"bad-palette.bank",Fixture(&indexedDescriptor,true,true));Require(!indexed.Load(dir/"bad-palette.bank",&error),"native palette index beyond original source colors accepted");Require(indexed.PaletteTextureCount()==2,"failed palette bank load replaced validated source");
        auto corrupt=bytes;corrupt.back()^=1;Write(dir/"crc.bank",corrupt);Require(!bank.Load(dir/"crc.bank",&error)&&error.find("checksum")!=std::string::npos,"corrupt payload accepted");Require(bank.Available()&&bank.ModelMappingCount()==1,"failed bank load replaced previous data");
        corrupt=bytes;corrupt.resize(corrupt.size()-1);Write(dir/"short.bank",corrupt);Require(!bank.Load(dir/"short.bank",&error),"truncated bank accepted");
        corrupt=bytes;Put(corrupt,descriptor+8,0xFFFFFFF0);Checksum(corrupt);Write(dir/"offset.bank",corrupt);Require(!bank.Load(dir/"offset.bank",&error),"out-of-bounds native texture accepted despite corrected file checksum");
        corrupt=bytes;Put(corrupt,descriptor,3|(2u<<16));Checksum(corrupt);Write(dir/"dimension.bank",corrupt);Require(!bank.Load(dir/"dimension.bank",&error),"invalid native dimensions accepted");
        corrupt=bytes;const size_t modelStart=32+4+4+std::strlen("common.bin")+16;Put(corrupt,modelStart+124,0xFFFFFFFF);Checksum(corrupt);Write(dir/"vertices.bank",corrupt);Require(!bank.Load(dir/"vertices.bank",&error),"unbounded native vertex count accepted despite corrected file checksum");
        port::AssetStore assets;Require(!assets.SetNaomiGraphics(true,&error)&&!assets.NaomiGraphics()&&assets.VisualRevision()==0,"missing bank changed graphics mode");Require(assets.LoadNaomiVisuals(path,&error),error.c_str());Require(!assets.NaomiGraphics()&&assets.NaomiAvailable(),"bank load enabled mode without selection");
        Require(assets.SetNaomiGraphics(true,&error)&&assets.NaomiGraphics(),"Naomi activation failed");const auto rev=assets.VisualRevision();assets.SetBloodColor(port::BloodColor::Red);const auto* red=assets.Texture("naomi:common.bin",0);Require(red&&red->width==2&&red->pixels[0]==255&&red->pixels[1]==0&&red->pixels[2]==0,"native red bank not substituted");
        assets.SetBloodColor(port::BloodColor::Green);const auto* green=assets.Texture("naomi:common.bin",0);Require(green&&green->pixels[0]==0&&green->pixels[1]==255,"native green bank not substituted");assets.SetBloodColor(port::BloodColor::Blue);const auto* blue=assets.Texture("naomi:common.bin",0);Require(blue&&blue->pixels[2]==255&&blue->pixels[0]==0,"native alternate blood color lost red artwork");
        Require(!assets.LoadNaomiVisuals(dir/"crc.bank",&error)&&assets.NaomiGraphics()&&assets.VisualRevision()==rev,"bad replacement bank partially changed active mode");Require(assets.SetNaomiGraphics(false,&error)&&!assets.NaomiGraphics()&&assets.VisualRevision()>rev,"PC restoration failed");Require(!assets.Texture("naomi:common.bin",0),"native namespace leaked while PC selected");
        if(argc>=3){port::AssetStore real;Require(real.Open(argv[1],&error),error.c_str());real.SetBloodColor(port::BloodColor::Red);const auto* pc=real.Texture("common.bin",44);Require(pc&&pc->width==64,"PC muzzle fixture mismatch");Require(real.LoadNaomiVisuals(argv[2],&error),error.c_str());Require(real.SetNaomiGraphics(true,&error),error.c_str());const auto* muzzle=real.Texture("common.bin",44);Require(muzzle&&muzzle->width==128&&muzzle->LogicalWidth()==64,"native muzzle upgrade/logical sizing missing");
            const auto* overlay=real.Texture("common.bin",27);Require(overlay&&overlay->width==128&&overlay->height==256&&overlay->LogicalWidth()==128&&overlay->LogicalHeight()==128,"native screen-blood upgrade lost image or logical dimensions");const auto* raw=real.Texture("naomi:common.bin",43);Require(raw&&raw->pixels==overlay->pixels,"mapped screen overlay differs from native material path");
            const auto* firstOverlay=real.Texture("common.bin",26);const auto* firstNative=real.Texture("naomi:common.bin",42);Require(firstOverlay&&firstNative&&firstOverlay->height==256&&firstOverlay->pixels==firstNative->pixels,"first player-damage overlay bypassed native red blood");game::Model damage;std::string damageName;Require(real.NaomiModelById(2353,damage,&damageName)&&damageName=="naomi:common.bin"&&damage.meshes.size()==1&&damage.meshes[0].material.TextureRef()==42,"first player-damage model lacks its native overlay material");
            const auto* church=real.Texture("st1_1.bin",15);const auto* nativeChurch=real.Texture("naomi:st1_1.bin",219);Require(church&&nativeChurch&&church->width==256&&church->LogicalWidth()==128&&church->pixels==nativeChurch->pixels,"merged PC church lost its verified native per-mesh texture binding");
            const auto* gable=real.Texture("st1_01c.bin",7);const auto* nativeGable=real.Texture("naomi:st1_1.bin",284);Require(gable&&nativeGable&&gable->width==128&&gable->height==128&&gable->LogicalWidth()==64&&gable->pixels==nativeGable->pixels,"opening church gable retained the reduced PC source art");
            const auto* glyph=real.Texture("scr_jimaku_e.bin",1);const auto* nativeGlyph=real.Texture("naomi:scr_jimaku_e.bin",1);Require(glyph&&nativeGlyph&&glyph->pixels==nativeGlyph->pixels,"English text retained its PC4-bit glyph rather than native8-bit art");
            Require(real.Texture("naomi:scr_jimaku.bin",0)!=nullptr,"native Japanese4-bit subtitle palette unavailable");
            game::Model semantic;std::string semanticName;
            Require(real.NaomiModelById(6048,semantic,&semanticName)&&semanticName=="naomi:common.bin"&&semantic.meshes.size()==2&&semantic.meshes[0].material.TextureRef()==111&&semantic.meshes[1].material.TextureRef()==112,"original sky-table binding retained the PC stage source");
            Require(real.NaomiModelById(4304,semantic,&semanticName)&&semanticName=="naomi:common.bin"&&semantic.meshes[0].material.TextureRef()==91,"floor-shadow callback selected alternate native art");
            Require(real.NaomiModelById(2352,semantic,&semanticName)&&semanticName=="naomi:etc_1.bin"&&semantic.meshes.size()==3&&semantic.meshes[0].material.TextureRef()==5&&semantic.meshes[1].material.TextureRef()==6,"shutter callback lost cross-bank native source");
            Require(real.NaomiModelById(6086,semantic,&semanticName)&&semanticName=="naomi:etc_1.bin"&&semantic.meshes.size()==3&&semantic.meshes[0].material.TextureRef()==172&&semantic.meshes[1].material.TextureRef()==177,"intact prop callback lost cross-bank native source");
            for(uint32_t id:{6145u,6146u})Require(real.NaomiModelById(id,semantic,&semanticName)&&semanticName=="naomi:etc_door.bin"&&semantic.meshes.size()==2&&semantic.meshes[0].material.TextureRef()==34,"authored stage1 ornament retained its PC source");
            Require(real.NaomiModelById(55,semantic,&semanticName)&&semanticName=="naomi:car_1.bin"&&semantic.meshes.size()==10&&semantic.meshes[0].material.TextureRef()==13&&semantic.meshes[1].material.TextureRef()==5,"original story-car event selected intact or alternate Native artwork");
            const auto* bossArt=real.Texture("boss5b.bin",3);const auto* nativeBossArt=real.Texture("naomi:boss5.bin",8);Require(bossArt&&nativeBossArt&&bossArt->pixels==nativeBossArt->pixels,"ambiguous geometry prevented unanimous original boss artwork binding");
            for(const auto& pair:{std::array<uint32_t,2>{40,791},{46,837}}){const auto* actual=real.Texture("st1_04b.bin",pair[0]);const auto* source=real.Texture("naomi:st1_1.bin",pair[1]);Require(actual&&source&&actual->width==source->width&&actual->height==source->height&&actual->pixels==source->pixels,"split chapter1 exact original artwork remained a PC texture");}
            for(const auto& pair:{std::array<uint32_t,2>{14,49},{16,56}}){const auto* actual=real.Texture("st5_02b.bin",pair[0]);const auto* source=real.Texture("naomi:st5.bin",pair[1]);Require(actual&&source&&actual->width==source->width&&actual->height==source->height&&actual->pixels==source->pixels,"split chapter5 exact original artwork remained a PC texture");}
            game::ModelMesh city;Require(real.NaomiMeshById(6143,0,city,nullptr)&&city.textureBank=="naomi:st1_1.bin"&&city.material.TextureRef()==860,"live split-city mesh lacks verified native source binding");
            Require(real.NaomiMeshById(6099,40,city,nullptr)&&city.textureBank=="naomi:st1_1.bin"&&city.material.TextureRef()==388,"unique rigid native source lost its authored wrapped UV mesh");
            for(const auto& pair:{std::array<uint32_t,2>{94,460},{99,417}})Require(real.NaomiMeshById(6148,pair[0],city,nullptr)&&city.textureBank=="naomi:st1_1.bin"&&city.material.TextureRef()==pair[1],"shop surface retained reduced PC art after local source proof");
            Require(real.NaomiMeshById(6223,19,city,nullptr)&&city.textureBank=="naomi:st1_1.bin"&&city.material.TextureRef()==335,"rounded chapter3 wall surface lost exact original Native quad");
            for(uint32_t meshIndex:{63u,64u,65u})Require(real.NaomiMeshById(461,meshIndex,city,nullptr)&&city.textureBank=="naomi:st1_1.bin"&&city.material.TextureRef()==(meshIndex==63?17u:147u),"chapter2 sign surface retained reduced PC source");
            evidence=0;Require(!real.NaomiMeshById(6180,101,city,nullptr),"larger source-UV mesh exposed to unaware rigid replacement");Require(real.NaomiMeshById(6180,101,city,nullptr,&evidence)&&evidence==12&&city.material.TextureRef()==591&&city.textureBank=="naomi:st1_1.bin","original source-UV material/namespace lost");
            game::Model pcFrames,nativeFrames;std::string frameBank;Require(real.ModelById(6306,pcFrames,nullptr)&&real.NaomiModelById(6306,nativeFrames,&frameBank)&&frameBank=="naomi:st5.bin"&&pcFrames.meshes.size()==54&&nativeFrames.meshes.size()==60,"original chapter5 whole object source lost");size_t oldFrameVertices=0,nativeFrameVertices=0;for(const auto& m:pcFrames.meshes)for(const auto& s:m.strips)oldFrameVertices+=s.vertices.size();for(const auto& m:nativeFrames.meshes){Require(m.textureBank=="naomi:st5.bin","whole chapter5 object escaped native texture namespace");for(const auto& s:m.strips)nativeFrameVertices+=s.vertices.size();}Require(oldFrameVertices==5407&&nativeFrameVertices==11297&&Faces(nativeFrames)>Faces(pcFrames),"chapter5 original3D frames remained reduced flat PC cards");
            game::Model pool;game::ModelMesh poolMesh;std::string poolName;const auto* poolTexture=real.Texture("common.bin",36);Require(real.NaomiModelById(0x1A38,pool,&poolName)&&pool.meshes.size()==1&&pool.meshes[0].material.TextureRef()==53&&poolTexture&&poolTexture->width==64,"blood pool selected non-blood quad or reduced PC art");Require(real.NaomiMeshById(0x1A38,0,poolMesh,nullptr)&&poolMesh.textureBank=="naomi:common.bin"&&poolMesh.material.TextureRef()==53,"native blood-pool mesh namespace lost");
            for(uint32_t phase=0;phase<30;++phase){game::Model oldRipple,ripple;bool valid=real.ModelById(0x15E4+phase,oldRipple,nullptr)&&real.NaomiModelById(0x15E4+phase,ripple,nullptr);if(valid){const auto a=Bounds(oldRipple),b=Bounds(ripple);for(int k=0;k<6;++k)valid=valid&&std::abs(a[k]-b[k])<1e-5f;size_t vertices=0;for(const auto& mesh:ripple.meshes){valid=valid&&mesh.material.TextureRef()==37&&mesh.material.Tsp()==0x20080452;for(const auto& strip:mesh.strips)vertices+=strip.vertices.size();}const size_t expected=phase<15?380:phase<22?412:phase<29?444:476;valid=valid&&vertices==expected;}Require(valid,"original opaque3D ripple phase lost native vertex packaging, material or local frame");}
            Require(bank.Load(argv[2],&error),error.c_str());Require(bank.PaletteTextureCount()==334,"native palette descriptor coverage regressed");const auto* commonData=bank.Data(0,"common.bin");Require(commonData!=nullptr,"native common source missing");const auto nativeHeaders=game::ReadPolHeader(commonData->data(),commonData->size());
            const auto* stageData=bank.Data(0,"st5.bin");Require(stageData!=nullptr,"original whole-stage bank missing");const auto stageHeaders=game::ReadPolHeader(stageData->data(),stageData->size());
            for(const auto& pair:{std::array<uint32_t,2>{6296,18},{6291,13},{6293,15},{6294,16},{6295,17},{6292,14},{6313,53},{6306,28},{6418,70},{6422,74},{6336,62},{6429,81},{6430,82},{6432,84},{6436,88},{6425,77},{6423,75},{6415,67},{6424,76}}){game::Model stage;std::string stageName;Require(real.NaomiModelById(pair[0],stage,&stageName)&&stageName=="naomi:st5.bin"&&stage.root==stageHeaders[pair[1]].root,"original chapter5/6 section object selected wrong whole native source");}
            const auto* originalOnly=bank.ModelMapping("st6_01b.bin",8);Require(!originalOnly||originalOnly->evidence!=13,"Original-only stage object inferred from an Arcade section row");const auto* source6418=bank.ModelMapping("st6_01.bin",8);Require(source6418&&source6418->index==70&&source6418->evidence==13,"lower-authority duplicate geometry overrode original section source");
            const auto* spatialData=bank.Data(0,"st1_1.bin");Require(spatialData!=nullptr,"original spatial stage bank missing");const auto spatialHeaders=game::ReadPolHeader(spatialData->data(),spatialData->size());
            for(const auto& pair:{std::array<uint32_t,2>{6147,96},{6148,96},{6156,103},{6157,103},{6131,88},{6129,86},{6180,88}}){game::Model whole;std::string sourceName;uint32_t sourcePart=0,proof=0;Require(real.NaomiModelSourceById(pair[0],&sourceName,&sourcePart,&proof)&&sourceName=="naomi:st1_1.bin"&&sourcePart==pair[1]&&proof==14&&real.NaomiModelById(pair[0],whole,nullptr)&&whole.root==spatialHeaders[pair[1]].root,"original grouped stage source identity or geometry lost");}
            for(const auto& pair:{std::array<uint32_t,2>{5956,59},{461,1},{6223,132},{6742,185},{6745,188}}){game::Model whole;std::string sourceName;uint32_t sourcePart=0,proof=0;Require(real.NaomiModelSourceById(pair[0],&sourceName,&sourcePart,&proof)&&sourceName=="naomi:st1_1.bin"&&sourcePart==pair[1]&&proof==15&&real.NaomiModelById(pair[0],whole,nullptr)&&whole.root==spatialHeaders[pair[1]].root,"authored spatial stage object selected wrong original source");}
            game::Model additional;std::string additionalBank;uint32_t additionalPart=0,additionalProof=0;Require(real.NaomiAdditionalStageModelById(6180,0,additional,&additionalBank,&additionalPart,&additionalProof)&&additionalBank=="naomi:st1_1.bin"&&additionalPart==86&&additionalProof==14&&additional.root==spatialHeaders[86].root,"merged PC stage lost its second original source");Require(!real.NaomiAdditionalStageModelById(6180,1,additional,nullptr,nullptr,nullptr)&&!real.NaomiAdditionalStageModelById(6147,0,additional,nullptr,nullptr,nullptr),"unproven additional stage geometry fabricated");
            evidence=0;Require(!real.NaomiMeshById(674,0,city,nullptr),"authored native skin UV bypassed evidence-aware guard");Require(real.NaomiMeshById(674,0,city,nullptr,&evidence)&&evidence==16&&city.textureBank=="naomi:boss1z.bin"&&city.material.TextureRef()==8&&city.strips.size()==2&&city.strips[0].vertices.size()==6&&city.strips[1].vertices.size()==10,"original16-vertex skin UV/material source lost");
            const auto* judgmentData=bank.Data(0,"boss1z.bin");Require(judgmentData!=nullptr,"original Judgment source bank missing");const auto judgmentHeaders=game::ReadPolHeader(judgmentData->data(),judgmentData->size());
            for(const auto& pair:{std::array<uint32_t,2>{699,27},{696,24},{673,1},{672,0}}){game::Model rigid;std::string sourceName;Require(real.NaomiModelById(pair[0],rigid,&sourceName)&&sourceName=="naomi:boss1z.bin"&&rigid.root==judgmentHeaders[pair[1]].root,"source-proven Judgment rigid retained PC geometry/UVs");for(const auto& mesh:rigid.meshes)Require(mesh.textureBank==sourceName,"Judgment rigid lost original material namespace");}
            const auto* magicianData=bank.Data(0,"boss5.bin");Require(magicianData!=nullptr,"original Magician source bank missing");const auto magicianHeaders=game::ReadPolHeader(magicianData->data(),magicianData->size());
            for(const auto& pair:{std::array<uint32_t,2>{1098,0},{1264,166},{1756,658}}){game::Model source;std::string sourceBank;Require(real.NaomiModelById(pair[0],source,&sourceBank)&&sourceBank=="naomi:boss5.bin"&&source.root==magicianHeaders[pair[1]].root,"original Magician bone/frame selected alternate material role");for(const auto& m:source.meshes)Require(m.textureBank==sourceBank,"Magician original material namespace lost");if(pair[0]==1098)Require(source.meshes[0].material.Tsp()==0x20082453,"original Magician body selected the alternate point-filter copy");}
            for(uint32_t part:{133u,134u,137u,138u,139u,153u,154u,155u,160u,161u,164u,165u,166u,180u,181u,182u}){game::Model old;game::ModelMesh original;uint32_t proof=0;bool valid=real.ModelById(1835+part,old,nullptr)&&old.meshes.size()==1&&real.NaomiMeshById(1835+part,0,original,nullptr,&proof)&&proof==18&&original.textureBank=="naomi:boss6.bin"&&original.strips.size()==old.meshes[0].strips.size();for(size_t si=0;valid&&si<original.strips.size();++si){const auto& a=old.meshes[0].strips[si];const auto& b=original.strips[si];valid=a.triangleList==b.triangleList&&a.vertices.size()==b.vertices.size();for(size_t vi=0;valid&&vi<a.vertices.size();++vi)for(int k=0;k<3;++k)valid=valid&&std::abs(a.vertices[vi].normal[k]-b.vertices[vi].normal[k])<1e-6f;}Require(valid,"Emperor surface selected its opposite-normal twin or lost source material");}
            struct RigidSource {uint32_t id,part;const char* bank;};
            auto verifyRigid=[&](const RigidSource& expected){const auto* source=bank.Data(0,expected.bank);Require(source!=nullptr,"original live rigid bank missing");const auto headers=game::ReadPolHeader(source->data(),source->size());game::Model actual;std::string resource;Require(expected.part<headers.size()&&real.NaomiModelById(expected.id,actual,&resource)&&resource==std::string("naomi:")+expected.bank&&actual.root==headers[expected.part].root,"original live rigid selected a different source role");for(const auto& m:actual.meshes)Require(m.textureBank==resource,"original live rigid material namespace lost");};
            for(const auto& expected:{RigidSource{4199,1,"komono_7.bin"},{4204,6,"komono_7.bin"},{4206,8,"komono_7.bin"},{4305,480,"common.bin"},{6420,72,"st5.bin"},{6300,22,"st5.bin"},{6302,24,"st5.bin"},{6304,26,"st5.bin"},{6328,68,"komono_st6.bin"},{6332,71,"komono_st6.bin"},{6333,68,"komono_st6.bin"},{3689,0,"hito_buru.bin"},{3690,1,"hito_buru.bin"}})verifyRigid(expected);
            for(uint32_t phase=0;phase<40;++phase)verifyRigid({3237+phase,72+phase,"garasu.bin"});
            for(uint32_t phase=0;phase<5;++phase)verifyRigid({6064+phase,174+phase,"eff_2.bin"});
            verifyRigid({51,8,"car_1.bin"});game::Model burningCar;std::string burningBank;Require(real.NaomiModelById(51,burningCar,&burningBank)&&burningCar.meshes.size()==12,"Judgment burning cars lost the original546-vertex source");constexpr uint32_t burningTextures[]={11,3,7,15,1,0xFFFFFFFFu,0xFFFFFFFFu,10,2,6,0xFFFFFFFFu,24};for(uint32_t mi=0;mi<12;++mi){game::ModelMesh source;Require(burningCar.meshes[mi].material.TextureRef()==burningTextures[mi]&&real.NaomiMeshById(51,mi,source,nullptr)&&source.textureBank=="naomi:car_1.bin"&&source.material.TextureRef()==burningTextures[mi],"Judgment burning car inherited gray parked-car materials");}const auto* redCar=real.Texture(burningBank,burningCar.meshes[0].material.TextureRef());size_t redPaintPixels=0;if(redCar)for(size_t pixel=0;pixel+3<redCar->pixels.size();pixel+=4){const int r=redCar->pixels[pixel],g=redCar->pixels[pixel+1],b=redCar->pixels[pixel+2];redPaintPixels+=r>50&&r*2>g*3&&r*2>b*3;}Require(redCar&&redCar->width==256&&redPaintPixels>10000,"original red burning-car paint was replaced by gray artwork");
            verifyRigid({1509,411,"boss5.bin"});game::Model afterimage;Require(real.NaomiModelById(1509,afterimage,nullptr)&&afterimage.flags==1&&afterimage.meshes.size()==1&&afterimage.meshes[0].material.SpecularExponent()==0&&afterimage.meshes[0].material.Tsp()==0x8411A4E5,"original Magician afterimage selected full-intensity duplicate instead of stored source0542");
            const auto* mountData=bank.Data(0,"boss1q.bin");Require(mountData!=nullptr,"original Mount source bank missing");const auto mountHeaders=game::ReadPolHeader(mountData->data(),mountData->size());game::Model mount;std::string mountBank;Require(real.NaomiNodeModelById(668,8,mount,&mountBank)&&mountBank=="naomi:boss1q.bin"&&mount.root==mountHeaders[14].root,"original Mount live node8 source missing");for(const auto& m:mount.meshes)Require(m.textureBank==mountBank,"Mount role material namespace lost");Require(!real.NaomiModelById(668,mount,nullptr)&&!real.NaomiNodeModelById(668,2,mount,nullptr)&&!real.NaomiNodeModelById(668,7,mount,nullptr)&&!real.NaomiNodeModelById(669,8,mount,nullptr),"node-specific Mount source leaked to another role or global ID");
            for(uint32_t part=0;part<114;++part){const auto* source=bank.ModelMapping("komono_1.bin",part);Require(source&&source->bank=="komono_1.bin"&&source->index==part+1,"original breakable skeleton selected the wrong material variant");}
            for(auto pair:{std::array<uint32_t,2>{39,9},{33,3},{34,4},{61,31},{62,32}}){const auto* source=bank.ModelMapping("char_adv04.bin",pair[0]);Require(source&&source->bank=="boss1z.bin"&&source->index==pair[1],"original cutscene bone selected reduced PC or wrong Native boss material");}
            for(uint32_t part=0;part<8;++part){const auto* source=bank.ModelMapping("char_adv04.bin",63+part);Require(source&&source->bank=="boss1z_wing.bin"&&source->index==part,"cutscene wing or skinned twin lost original Native source");}
            struct HydraBinding { uint32_t pc,part;const char* bank; };
            for(const auto& expected:{HydraBinding{898,13,"boss3l.bin"},{899,14,"boss3l.bin"},{901,16,"boss3l.bin"},{902,17,"boss3l.bin"},{911,5,"boss3.bin"},{912,6,"boss3.bin"},{914,8,"boss3.bin"}}){const auto* source=bank.Data(0,expected.bank);Require(source!=nullptr,"original Hydra source bank missing");const auto entries=game::ReadPolHeader(source->data(),source->size());game::Model hydra;std::string hydraName;Require(real.NaomiModelById(expected.pc,hydra,&hydraName)&&hydraName==std::string("naomi:")+expected.bank&&hydra.root==entries[expected.part].root,"live original Hydra skeleton selected wrong source mesh");if(expected.pc==911||expected.pc==912){game::Model old;Require(real.ModelById(expected.pc,old,nullptr)&&Faces(hydra)>Faces(old),"native Hydra jaw retained reduced PC surface geometry");}}
            constexpr uint32_t bloodParts[]={616,617,618,619,620,621,622,623,624,625,627,628,629,630,631,632,633,634,635,636,637,638,639,650,640,641,642,643,644,645,646,647,648,649,651,652,653,654,655,656,657,658,659,661,662,663,664,665,666,667,668,669,670,671,672,673,674,660};
            for(uint32_t frame=0;frame<58;++frame){game::Model polygon;std::string polygonName;const auto* source=bank.ModelMapping("__naomi_polygon_blood__.bin",frame);bool valid=source&&source->index==bloodParts[frame]&&real.NaomiPolygonBloodFrame(frame,polygon,&polygonName)&&polygonName=="naomi:common.bin"&&polygon.root==nativeHeaders[bloodParts[frame]].root;size_t vertices=0;for(const auto& mesh:polygon.meshes){valid=valid&&mesh.material.TextureRef()==54&&mesh.textureBank=="naomi:common.bin";for(const auto& strip:mesh.strips)vertices+=strip.vertices.size();}Require(valid&&vertices==(bloodParts[frame]==660?244u:242u),"native polygon-blood frame order or original opaque mesh lost");}Require(!real.NaomiPolygonBloodFrame(58,pool,nullptr),"native polygon-blood lifetime exceeded58 source frames");
            // Cover every stored flash frame, including the tutorial's tenth
            // frame and P2's otherwise geometry-identical blue first frame.
            struct FlashFamily { uint32_t model,texture; };
            for(const auto& family:{FlashFamily{373,40},{383,41},{2934,60},{2948,59}})for(uint32_t tick=0;tick<10;++tick){
                game::Model pcFlash,nativeFlash;std::string nativeFlashName;
                bool match=real.ModelById(family.model+tick,pcFlash,nullptr)&&real.NaomiModelById(family.model+tick,nativeFlash,&nativeFlashName)&&nativeFlashName=="naomi:common.bin"&&pcFlash.meshes.size()==1&&nativeFlash.meshes.size()==1&&nativeFlash.meshes[0].material.TextureRef()==family.texture;
                if(match){const auto& pcMesh=pcFlash.meshes[0];const auto& nativeMesh=nativeFlash.meshes[0];match=pcMesh.strips.size()==nativeMesh.strips.size()&&pcMesh.material.DiffuseA()==nativeMesh.material.DiffuseA();
                    for(size_t strip=0;match&&strip<pcMesh.strips.size();++strip){const auto& a=pcMesh.strips[strip];const auto& b=nativeMesh.strips[strip];match=a.triangleList==b.triangleList&&a.vertices.size()==b.vertices.size();
                        for(size_t vertex=0;match&&vertex<a.vertices.size();++vertex){for(int k=0;k<3;++k)match=match&&std::abs(a.vertices[vertex].pos[k]-b.vertices[vertex].pos[k])<1e-5f;for(int k=0;k<2;++k)match=match&&std::abs(a.vertices[vertex].uv[k]-b.vertices[vertex].uv[k])<1e-5f;}}
                }
                Require(match,"native muzzle frame lost its player color, animation geometry, UVs or fade alpha");
            }
            game::Model original,native;std::string originalName,nativeName;Require(real.ModelById(7945,original,&originalName)&&Faces(original)==198,"PC David source changed");Require(real.NaomiModelById(7945,native,&nativeName)&&nativeName=="naomi:znsam2.bin"&&Faces(native)==214,"native David head not restored");Require(real.ModelById(7945,original,&originalName)&&Faces(original)==198,"native mode changed CPU source geometry");Require(real.SetNaomiGraphics(false,&error),"real PC restore failed");pc=real.Texture("common.bin",44);Require(pc&&pc->width==64,"switch back retained native texture");Require(!real.NaomiModelById(7945,native,&nativeName),"native model leaked into PC mode");
            Require(!real.NaomiPolygonBloodFrame(0,pool,nullptr),"native-only blood leaked into PC mode");
            Require(!real.NaomiAdditionalStageModelById(6180,0,pool,nullptr,nullptr,nullptr),"additional original stage geometry leaked into PC mode");
            Require(!real.NaomiNodeModelById(668,8,pool,nullptr),"original Mount role leaked into PC mode");
            std::puts("PASS real cartridge-derived textures, red overlays, rigid David head, original CPU geometry and PC restoration");
            const auto typingBank = fs::path(argv[1]) / "typing_reference.bank";
            if (fs::exists(typingBank)) TypingCoexistence(real, typingBank);
        }
        std::printf("PASS %d Naomi visual bank/profile checks\n",checks);
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());result=1;}
    if(dir.parent_path()==temp&&dir.filename().string().rfind("hotd2-naomi-test-",0)==0)fs::remove_all(dir);return result;
}
