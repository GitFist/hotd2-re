// Installed-pack discovery is read-only and must reject unsupported visual packs,
// expose mixed audio content, and distinguish cheap inspection from full decoding.
#include "render/asset_store.h"
#include "assets/asset_override.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void U32(std::vector<uint8_t>& out, uint32_t n) {
    for (int shift : {24,16,8,0}) out.push_back(static_cast<uint8_t>(n >> shift));
}
static void Chunk(std::vector<uint8_t>& out, const char* name, const std::vector<uint8_t>& payload) {
    U32(out,static_cast<uint32_t>(payload.size()));
    const size_t begin = out.size();
    out.insert(out.end(),name,name+4); out.insert(out.end(),payload.begin(),payload.end());
    uint32_t crc = 0xffffffffu;
    for (size_t i=begin;i<out.size();++i) {
        crc ^= out[i];
        for (int bit=0;bit<8;++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    U32(out,~crc);
}
static std::vector<uint8_t> Png() {
    std::vector<uint8_t> png{137,80,78,71,13,10,26,10}, ihdr;
    U32(ihdr,1); U32(ihdr,1); ihdr.insert(ihdr.end(),{8,6,0,0,0}); Chunk(png,"IHDR",ihdr);
    const std::vector<uint8_t> raw{0,255,0,0,255};
    std::vector<uint8_t> z{0x78,0x01,0x01,0x05,0x00,0xfa,0xff};
    z.insert(z.end(),raw.begin(),raw.end());
    uint32_t a=1,b=0; for (uint8_t c:raw) { a=(a+c)%65521; b=(b+a)%65521; }
    U32(z,(b<<16)|a); Chunk(png,"IDAT",z); Chunk(png,"IEND",{});
    return png;
}
static void Write(const fs::path& path, const std::vector<uint8_t>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(data.data()),data.size());
    Require(file.good(),"cannot create test image");
}
static void Text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path()); std::ofstream(path) << text;
}
int main(int argc,char** argv) {
    const auto temp = fs::temp_directory_path();
    const auto dir = temp / ("hotd2-pack-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        const auto png = Png();
        Write(dir / "legacy/tex/stage/000.png",png);
        // Optional material-map suffixes are not color-image overrides.
        Write(dir / "legacy/tex/stage/000_nr.png",png);
        auto legacy = port::InspectTexturePack(dir / "legacy");
        Require(legacy.valid && legacy.textures==1 && !legacy.hasManifest && !legacy.fullyValidated,"legacy image layout was not recognized");
        Require(port::InspectTexturePack(dir / "legacy",true).fullyValidated,"valid PNG did not pass the actual decoder");
        Write(dir / "named/bmp/hod2title.bin.png",png);
        Text(dir / "named/pack.ini","format=1\nname=Canal artwork\nauthor=Test\nversion=2\n");
        Text(dir / "named/Sound/SE/example.wav","placeholder");
        auto named = port::InspectTexturePack(dir / "named");
        Require(named.valid && named.hasManifest && named.hasAudio && named.frontScreens==1 &&
                named.name=="Canal artwork" && named.author=="Test" && named.version=="2","manifest metadata or mixed-content warning missing");
        Text(dir / "audio-only/Sound/example.wav","placeholder");
        Require(!port::InspectTexturePack(dir / "audio-only").valid,"audio-only directory accepted as a visual pack");
        Write(dir / "material-only/tex/stage/0_nr.png",png);
        Require(!port::InspectTexturePack(dir / "material-only").valid,"unrecognized material suffix accepted as a color override");
        Write(dir / "broken/tex/stage/0.png",{1,2,3});
        Require(!port::InspectTexturePack(dir / "broken").valid,"invalid PNG header accepted");
        auto interlaced=png; interlaced[28]=1;
        Write(dir / "interlaced/tex/stage/0.png",interlaced);
        Require(!port::InspectTexturePack(dir / "interlaced").valid,"unsupported interlace accepted");
        auto huge=png; huge[16]=1;
        Write(dir / "huge/tex/stage/0.png",huge);
        Require(!port::InspectTexturePack(dir / "huge").valid,"excessive image size accepted");
        auto truncated=png; truncated.resize(33);
        Write(dir / "truncated/tex/stage/0.png",truncated);
        Require(port::InspectTexturePack(dir / "truncated").valid,"header-only inspection unexpectedly decodes a whole pack");
        Require(!port::InspectTexturePack(dir / "truncated",true).valid,"full validation accepted a truncated image");
        Text(dir / "future/pack.ini","format=999\nname=Future\n");
        Require(!port::InspectTexturePack(dir / "future").valid,"unknown manifest format accepted");
        Require(!port::InspectTexturePack(dir / "missing").valid,"missing directory accepted");
        const auto all = port::DiscoverTexturePacks({dir,dir/"legacy",dir/"legacy/.",dir/"missing"});
        Require(all.size()==7,"discovery duplicated a path or exposed audio/material-only content");
        Require(all[1].name=="Canal artwork","pack sort order is unstable");
        Write(dir / "mapped/materials/tex/stage/000_nr.png",png);
        Require(!port::InspectTexturePack(dir / "mapped").valid,"material pack enabled without explicit manifest convention");
        Text(dir / "mapped/pack.ini","format=1\nname=Authored materials\nmaterials=normal-roughness-v1\n");
        const auto mapped=port::InspectTexturePack(dir / "mapped",true);
        Require(mapped.valid && mapped.fullyValidated && mapped.normalRoughness && mapped.materialMaps==1 &&
                mapped.textures==0,"authored material-only pack did not validate");
        Text(dir / "mapped/pack.ini","format=1\nmaterials=unknown\n");
        Require(!port::InspectTexturePack(dir / "mapped").valid,"unknown material convention accepted");
        Text(dir / "meshes/pack.ini","format=1\nmeshes=pn-triangles-v1\nrefine_models=0x135f, 0x03ae,135F\n");
        const auto meshes=port::InspectTexturePack(dir/"meshes");
        Require(meshes.valid&&meshes.pnTriangles&&meshes.refinedModels==2,"explicit model selection was not deduplicated/validated");
        Text(dir / "bad-meshes/pack.ini","format=1\nmeshes=pn-triangles-v1\nrefine_models=../../game\n");
        Require(!port::InspectTexturePack(dir/"bad-meshes").valid,"invalid model ID accepted");
        Text(dir / "bad-meshes/pack.ini","format=1\nmeshes=unknown\nrefine_models=135F\n");
        Require(!port::InspectTexturePack(dir/"bad-meshes").valid,"unknown mesh convention accepted");
        Text(dir / "bad-meshes/pack.ini","format=1\nmeshes=pn-triangles-v1\nrefine_models=10000\n");
        Require(!port::InspectTexturePack(dir/"bad-meshes").valid,"out-of-range model ID accepted");
        {
            const auto previous=port::AssetDir();port::SetAssetDir((dir/"meshes").string());port::AssetStore selection;
            const auto activeMesh=selection.ActivePackInfo();
            Require(activeMesh.valid&&activeMesh.refinedModels==2&&activeMesh.materialMaps==0&&
                    activeMesh.path==meshes.path,"active pack metadata did not follow the actual asset directory");
            Require(selection.RefineModel(0x135f)&&selection.RefineModel(0x3ae)&&!selection.RefineModel(0x1234),"active pack did not restrict selected model IDs");
            selection.ClearCache();port::SetAssetDir((dir/"bad-meshes").string());Require(!selection.RefineModel(0x135f),"invalid pack enabled geometry refinement");
            port::SetAssetDir("");const auto original=selection.ActivePackInfo();
            Require(original.valid&&original.path.empty()&&original.name=="Original"&&original.materialMaps==0&&original.refinedModels==0,
                    "Original exposed inactive pack features");
            port::SetAssetDir(previous);
        }
        if(argc>1) {
            Require(!std::getenv("HOTD2_ASSET_DIR") || !*std::getenv("HOTD2_ASSET_DIR"),"unset HOTD2_ASSET_DIR before the optional material-loader integration check");
            struct RestoreAssets {std::string old=port::AssetDir();~RestoreAssets(){port::SetAssetDir(old);}} restore;
            port::AssetStore assets;std::string error,pol;game::Model model;
            Require(assets.Open(argv[1],&error),error.c_str());
            Require(assets.ModelById(0x135f,model,&pol),"real descriptor fixture did not resolve");
            const auto& material=model.meshes.front().material;
            Require(material.Textured(),"descriptor fixture is not textured");
            const auto ref=material.TextureRef();std::string bank=assets.TextureFileFor(pol);
            if(bank.size()>4)bank.resize(bank.size()-4);
            const auto pack=dir/"loader";
            Write(pack/"materials"/"tex"/bank/(std::to_string(ref)+"_nr.png"),png);
            port::SetAssetDir(pack.string());
            Require(!assets.NormalRoughness(pol,ref),"material map loaded without its explicit convention");
            assets.ClearCache();Text(pack/"pack.ini","format=1\nmaterials=normal-roughness-v1\n");
            const auto* color=assets.Texture(pol,ref);const auto* map=assets.NormalRoughness(pol,ref);
            Require(color && map && map->width==1 && map->height==1 && map->pixels==std::vector<uint8_t>({255,0,0,255}),"generic bank/index material map did not decode");
            Require(map->LogicalWidth()==color->LogicalWidth()&&map->LogicalHeight()==color->LogicalHeight(),"material override changed original logical texture sizing");
            Require(assets.NormalRoughness(pol,ref)==map,"material map cache lost image pointer stability");
            Require(!assets.NormalRoughness(pol,0xffffffffu),"missing material reference resolved unexpectedly");
            std::puts("PASS generated material map resolved through real game descriptors without actor-specific code");
        }
        std::puts("PASS visual-pack layouts, metadata, mixed content, header/decoder validation, limits, deduplication and stable discovery");
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FAIL: %s\n",e.what()); result=1;
    }
    // The only removable tree is this test's freshly created direct child of temp.
    if (dir.parent_path()==temp && dir.filename().string().rfind("hotd2-pack-test-",0)==0) fs::remove_all(dir);
    return result;
}
