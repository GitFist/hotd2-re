#include "asset_store.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "assets/asset_override.h"
#include "game/palette_bank.h"
#include "game/prs.h"
#include "render/png_read.h"

namespace port {

namespace {
std::string TrimPackValue(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }), value.end());
    if (value.size() > 120) value.resize(120);
    return value;
}
std::string LowerPackName(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool ReadMeshSelection(const std::filesystem::path& path,bool* enabled,std::set<uint32_t>* models,std::string* error) {
    *enabled=false;models->clear();std::error_code ec;
    if(!std::filesystem::exists(path,ec))return true;
    if(std::filesystem::file_size(path,ec)>16384||ec){if(error)*error="Mesh manifest is unreadable or exceeds 16 KiB";return false;}
    std::ifstream file(path);bool supported=false;std::string selection;
    auto trim=[](std::string s){const auto first=s.find_first_not_of(" \t\r\n");if(first==std::string::npos)return std::string{};return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);};
    for(std::string line;std::getline(file,line);){if(line.rfind("\xEF\xBB\xBF",0)==0)line.erase(0,3);line=trim(line);
        if(line.empty()||line[0]=='#'||line[0]==';'||line[0]=='[')continue;const auto equals=line.find('=');if(equals==std::string::npos)continue;
        const auto key=LowerPackName(trim(line.substr(0,equals))),value=trim(line.substr(equals+1));
        if(key=="format")supported=value=="1";
        if(key=="meshes"){*enabled=value=="pn-triangles-v1";if(!value.empty()&&value!="none"&&!*enabled){if(error)*error="Unsupported mesh refinement convention";return false;}}
        if(key=="refine_models")selection=value;
    }
    if(!file.eof()){if(error)*error="Cannot read mesh manifest";return false;}
    if(!*enabled&&selection.empty())return true;
    if(!supported||!*enabled){if(error)*error="Mesh selection requires format=1 and meshes=pn-triangles-v1";return false;}
    if(selection.size()>8192){if(error)*error="Mesh selection exceeds 8 KiB";return false;}
    size_t cursor=0;
    while(cursor<selection.size()){const size_t end=selection.find(',',cursor);auto token=trim(selection.substr(cursor,end==std::string::npos?end:end-cursor));
        if(token.rfind("0x",0)==0||token.rfind("0X",0)==0)token.erase(0,2);uint32_t id=0;
        const auto result=std::from_chars(token.data(),token.data()+token.size(),id,16);
        if(token.empty()||result.ec!=std::errc{}||result.ptr!=token.data()+token.size()||id>0xffffu){if(error)*error="refine_models must contain model IDs in hexadecimal (0..FFFF)";return false;}
        models->insert(id);if(models->size()>512){if(error)*error="Mesh selection exceeds 512 model IDs";return false;}
        if(end==std::string::npos)break;cursor=end+1;if(cursor==selection.size()){if(error)*error="Empty model ID in mesh selection";return false;}
    }
    return true;
}
bool PackPngHeader(const std::filesystem::path& path, bool decodeAll, std::string* error, bool normalRoughness = false) {
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > (64u << 20) || ec) {
        *error = "PNG is unreadable or exceeds 64 MiB";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    unsigned char h[33] = {};
    file.read(reinterpret_cast<char*>(h), sizeof(h));
    const unsigned char signature[8] = {137,80,78,71,13,10,26,10};
    if (file.gcount() != sizeof(h) || std::memcmp(h, signature, 8) ||
        h[8] || h[9] || h[10] || h[11] != 13 || std::memcmp(h + 12, "IHDR", 4)) {
        *error = "invalid PNG header";
        return false;
    }
    auto be32 = [](const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; };
    const uint32_t width = be32(h + 16), height = be32(h + 20);
    if (!width || !height || width > 8192 || height > 8192 || uint64_t(width) * height > (16u << 20)) {
        *error = "PNG exceeds the 8192-pixel or 64-MiB decoded-image limit";
        return false;
    }
    const unsigned depth = h[24], color = h[25];
    const bool supported = ((color == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                            ((color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16)) ||
                            (color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)));
    if (!supported || h[26] || h[27] || h[28]) {
        *error = "PNG format is unsupported (non-interlaced PNG required)";
        return false;
    }
    if (normalRoughness && (depth != 8 || color != 6)) {
        *error = "normal/roughness maps require RGBA8 PNG";
        return false;
    }
    if (decodeAll) {
        PngImage image;
        if (!ReadPngFile(path.string(), image, error)) return false;
    }
    return true;
}
} // namespace

TexturePackInfo InspectTexturePack(const std::filesystem::path& path, bool decodeAll) {
    TexturePackInfo info;
    std::error_code ec;
    info.path = std::filesystem::weakly_canonical(path, ec);
    if (ec) info.path = path.lexically_normal();
    info.name = info.path.filename().string();
    if (path.empty() || !std::filesystem::is_directory(info.path, ec)) {
        info.error = "Pack directory is missing or unreadable";
        return info;
    }
    const auto manifestPath = info.path / "pack.ini";
    info.hasManifest = std::filesystem::is_regular_file(manifestPath, ec);
    if (info.hasManifest) {
        if (std::filesystem::file_size(manifestPath, ec) > 16384 || ec) {
            info.error = "pack.ini is unreadable or exceeds 16 KiB";
            return info;
        }
        std::ifstream manifest(manifestPath);
        for (std::string line; std::getline(manifest, line);) {
            if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
            line = TrimPackValue(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const auto key = LowerPackName(TrimPackValue(line.substr(0, eq)));
            const auto value = TrimPackValue(line.substr(eq + 1));
            if (key == "name" && !value.empty()) info.name = value;
            else if (key == "author") info.author = value;
            else if (key == "version") info.version = value;
            else if (key == "format" && value != "1") info.error = "Unsupported pack.ini format (expected 1)";
            else if (key == "materials") {
                info.normalRoughness = value == "normal-roughness-v1";
                if (!value.empty() && value != "none" && !info.normalRoughness) info.error = "Unsupported material-map format";
            }
        }
        if (!manifest.eof()) info.error = "Cannot read pack.ini";
        std::set<uint32_t> models;std::string reason;
        if(!ReadMeshSelection(manifestPath,&info.pnTriangles,&models,&reason)&&info.error.empty())info.error=reason;
        info.refinedModels=models.size();
    }
    auto inspect = [&](const std::filesystem::path& png, size_t& count) {
        ++count;
        if (info.error.empty()) {
            std::string reason;
            if (!PackPngHeader(png, decodeAll, &reason))
                info.error = png.lexically_relative(info.path).generic_string() + ": " + reason;
        }
    };
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    const auto tex = info.path / "tex";
    if (std::filesystem::is_directory(tex, ec)) {
        for (std::filesystem::directory_iterator banks(tex, options, ec), end; banks != end && !ec; banks.increment(ec)) {
            if (!banks->is_directory(ec)) continue;
            for (std::filesystem::directory_iterator images(banks->path(), options, ec); images != end && !ec; images.increment(ec)) {
                if (!images->is_regular_file(ec) || images->path().extension() != ".png") continue;
                const std::string index = images->path().stem().string();
                if (index.empty() || !std::all_of(index.begin(), index.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) continue;
                inspect(images->path(), info.textures);
            }
        }
    }
    const auto bmp = info.path / "bmp";
    if (std::filesystem::is_directory(bmp, ec)) {
        for (std::filesystem::directory_iterator images(bmp, options, ec), end; images != end && !ec; images.increment(ec))
            if (images->is_regular_file(ec) && images->path().extension() == ".png") inspect(images->path(), info.frontScreens);
    }
    const auto materials = info.path / "materials" / "tex";
    if (info.normalRoughness && std::filesystem::is_directory(materials, ec)) {
        for (std::filesystem::directory_iterator banks(materials, options, ec), end; banks != end && !ec; banks.increment(ec)) {
            if (!banks->is_directory(ec)) continue;
            for (std::filesystem::directory_iterator images(banks->path(), options, ec); images != end && !ec; images.increment(ec)) {
                if (!images->is_regular_file(ec) || images->path().extension() != ".png") continue;
                const std::string stem = images->path().stem().string();
                if (stem.size() <= 3 || stem.compare(stem.size()-3,3,"_nr")) continue;
                const std::string index = stem.substr(0,stem.size()-3);
                if (!std::all_of(index.begin(), index.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) continue;
                ++info.materialMaps;
                if (info.error.empty()) {
                    std::string reason;
                    if (!PackPngHeader(images->path(), decodeAll, &reason, true))
                        info.error = images->path().lexically_relative(info.path).generic_string() + ": " + reason;
                }
            }
        }
    }
    info.hasAudio = std::filesystem::is_directory(info.path / "Sound", ec) || std::filesystem::is_directory(info.path / "sound", ec);
    if (!info.textures && !info.frontScreens && !info.materialMaps && !info.refinedModels && info.error.empty()) info.error = "No supported images or opted-in visual materials/meshes";
    info.valid = info.error.empty();
    info.fullyValidated = info.valid && decodeAll;
    return info;
}

std::vector<TexturePackInfo> DiscoverTexturePacks(const std::vector<std::filesystem::path>& roots) {
    std::vector<TexturePackInfo> found;
    std::set<std::string> seen;
    auto consider = [&](const std::filesystem::path& path) {
        auto info = InspectTexturePack(path);
        std::string identity = info.path.generic_string();
#ifdef _WIN32
        identity = LowerPackName(identity); // Windows pack paths are case-insensitive
#endif
        if ((!info.textures && !info.frontScreens && !info.materialMaps && !info.hasManifest) || !seen.insert(identity).second) return;
        found.push_back(std::move(info));
    };
    for (const auto& root : roots) {
        consider(root);
        std::error_code ec;
        for (std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec))
            if (it->is_directory(ec)) consider(it->path());
    }
    std::sort(found.begin(), found.end(), [](const TexturePackInfo& a, const TexturePackInfo& b) {
        const auto an = LowerPackName(a.name), bn = LowerPackName(b.name);
        return an == bn ? a.path < b.path : an < bn;
    });
    return found;
}

bool AssetStore::Open(const std::filesystem::path& gameDir, std::string* error) {
    dir_ = gameDir;
    if (!exe_.Load((dir_ / "Hod2.exe").string())) {
        if (error) *error = "cannot load Hod2.exe from " + dir_.string();
        return false;
    }
    banks_ = game::LoadPaletteBanks(exe_);
    for (const game::TextureFileEntry& f : game::ListTextureFiles(exe_)) {
        if (byFile_.count(f.name)) continue;  // first table entry for a name wins
        auto& list = byFile_[f.name];
        for (int i = 0; i < f.count; ++i) list.push_back({ f.index, f.name, game::ReadTextureDesc(exe_, f, i), i });
    }
    redBloodTextures_.clear();
    if (auto it = byFile_.find("scr_blood_red.bin"); it != byFile_.end())
        for (const auto& r : it->second) redBloodTextures_.emplace(r.desc.id, r);
    return true;
}

TexturePackInfo AssetStore::ActivePackInfo() const {
    if (!AssetDir().empty()) return InspectTexturePack(AssetDir());
    TexturePackInfo original;
    original.name = "Original";
    original.valid = true;
    return original;
}

const std::vector<uint8_t>& AssetStore::File(const std::string& relPath) {
    auto it = files_.find(relPath);
    if (it != files_.end()) return it->second;
    std::ifstream f(dir_ / relPath, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    std::vector<uint8_t> out = raw;
    if (raw.size() >= 8) {
        uint32_t want;
        memcpy(&want, raw.data(), 4);
        if (want > 0 && want < (64u << 20)) {
            std::vector<uint8_t> dec(want);
            if (game::DecompressPrsSafe(raw.data() + 4, raw.size() - 4, dec.data(), dec.size()) == static_cast<int>(want))
                out = std::move(dec);
        }
    }
    return files_.emplace(relPath, std::move(out)).first->second;
}

std::vector<game::Model> AssetStore::LoadPolFile(const std::string& name, int* failures) {
    const std::vector<uint8_t>& data = File("pol/" + name);
    std::vector<game::Model> models;
    int bad = 0;
    for (const game::PolEntry& e : game::ReadPolHeader(data.data(), data.size())) {
        game::Model m;
        if (game::ParseModel(data.data(), data.size(), e, m)) models.push_back(std::move(m));
        else ++bad;
    }
    if (failures) *failures = bad;
    return models;
}

bool AssetStore::ModelById(uint32_t id, game::Model& out, std::string* polName) {
    uint16_t file = 0;
    if (!exe_.Read(0x004E83B4 + id * 2, &file, 2)) return false;
    const std::string name = exe_.CStr(exe_.U32(0x004D0EF4 + file * 4));
    uint16_t count = 0;
    exe_.Read(0x004E803C + file * 2, &count, 2);
    const uint32_t list = exe_.U32(0x004E794C + file * 4);
    if (name.empty() || !exe_.InData(list) || count == 0 || !std::filesystem::exists(dir_ / "pol" / name)) return false;
    int index = -1;
    for (int k = 0; k < count && index < 0; ++k)
        if (static_cast<uint16_t>(exe_.S16(list + k * 2)) == id) index = k;
    if (index < 0) return false;
    const std::vector<uint8_t>& data = File("pol/" + name);
    const auto entries = game::ReadPolHeader(data.data(), data.size());
    if (index >= static_cast<int>(entries.size())) return false;
    if (polName) *polName = name;
    if (!game::ParseModel(data.data(), data.size(), entries[index], out)) return false;
    // Res_LoadCommon 0x00418200 ends with 0x004192C0 on the common.bin models of list 0x0057A268 (s16, -1 ends):
    // every material's TSP = (TSP & ~0x4000) | 0x2000, i.e. bilinear filtering (the file has them point-sampled)
    for (uint32_t a = 0x0057A268; exe_.InData(a); a += 2) {
        const int16_t listed = exe_.S16(a);
        if (listed == -1) break;
        if (static_cast<uint32_t>(listed) != id) continue;
        for (game::ModelMesh& mesh : out.meshes) mesh.material.words[2] = (mesh.material.words[2] & ~0x4000u) | 0x2000u;
        break;
    }
    return true;
}

std::string AssetStore::TextureFileFor(const std::string& polName) const {
    if (byFile_.count(polName)) return polName;
    if (polName.rfind("pol_", 0) == 0 && byFile_.count(polName.substr(4))) return polName.substr(4);
    return {};
}

const RgbaImage* AssetStore::Texture(const std::string& polName, uint32_t ref) {
    if (ref == 0xFFFFFFFF) return nullptr;
    const std::string texName = TextureFileFor(polName);
    const auto file = byFile_.find(texName);
    if (file == byFile_.end() || ref >= file->second.size()) return nullptr;
    return ResolvedTexture(file->second[ref]);
}

bool AssetStore::RefineModel(uint32_t modelId) {
    if(!AssetDirSet())return false;
    if(!refinementSelectionRead_){refinementSelectionRead_=true;bool enabled=false;std::string ignored;
        if(!ReadMeshSelection(std::filesystem::path(AssetDir())/"pack.ini",&enabled,&refinementModels_,&ignored)||!enabled)refinementModels_.clear();}
    return refinementModels_.find(modelId)!=refinementModels_.end();
}

const RgbaImage* AssetStore::NormalRoughness(const std::string& polName, uint32_t ref) {
    if (AssetDir().empty() || ref == 0xFFFFFFFF) return nullptr;
    if (normalRoughnessMode_ < 0) {
        // Read only the small manifest; discovery/whole-pack validation belongs to
        // the selector, not to an actor's first draw.
        normalRoughnessMode_ = 0;
        const auto manifestPath = std::filesystem::path(AssetDir()) / "pack.ini";
        std::error_code manifestError;
        const auto manifestSize = std::filesystem::file_size(manifestPath,manifestError);
        if (manifestError || manifestSize > 16384) return nullptr;
        std::ifstream manifest(manifestPath);
        bool supported = true;
        for (std::string line; std::getline(manifest,line);) {
            if (line.rfind("\xEF\xBB\xBF",0) == 0) line.erase(0,3);
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const auto key = LowerPackName(TrimPackValue(line.substr(0,eq)));
            const auto value = TrimPackValue(line.substr(eq+1));
            if (key == "materials") normalRoughnessMode_ = value == "normal-roughness-v1" ? 1 : 0;
            if (key == "format" && value != "1") supported = false;
        }
        if (!supported || !manifest.eof()) normalRoughnessMode_ = 0;
    }
    if (!normalRoughnessMode_) return nullptr;
    const std::string texName = TextureFileFor(polName);
    const auto file = byFile_.find(texName);
    if (file == byFile_.end() || ref >= file->second.size()) return nullptr;
    const TexRef* r = &file->second[ref];
    if (redBlood_) {
        const auto blood = redBloodTextures_.find(r->desc.id);
        if (blood != redBloodTextures_.end()) r = &blood->second;
    }
    const TexKey key(r->fileName, static_cast<uint32_t>(r->texIndex));
    if (auto found = normalRoughness_.find(key); found != normalRoughness_.end()) return found->second.get();
    auto& slot = normalRoughness_[key]; // cache missing/bad material maps too
    std::string stem = r->fileName;
    if (stem.size() > 4 && stem.compare(stem.size()-4,4,".bin") == 0) stem.resize(stem.size()-4);
    char names[3][24];
    std::snprintf(names[0],sizeof(names[0]),"%d_nr.png",r->texIndex);
    std::snprintf(names[1],sizeof(names[1]),"%03d_nr.png",r->texIndex);
    std::snprintf(names[2],sizeof(names[2]),"%04d_nr.png",r->texIndex);
    for (const char* name : names) {
        const auto path = AssetOverride("materials/tex/" + stem + "/" + name);
        if (path.empty()) continue;
        PngImage image; std::string error;
        if (!PackPngHeader(path,false,&error,true) || !ReadPngFile(path,image,&error)) {
            std::fprintf(stderr,"material map: %s: %s; using original material\n",path.c_str(),error.c_str());
            break;
        }
        slot = std::make_unique<RgbaImage>();
        slot->width=image.width;slot->height=image.height;
        slot->logicalWidth=r->desc.width;slot->logicalHeight=r->desc.height;
        slot->pixels.resize(image.rgba.size());
        const size_t row=static_cast<size_t>(image.width)*4;
        for (int y=0;y<image.height;++y)
            std::memcpy(slot->pixels.data()+static_cast<size_t>(y)*row,image.rgba.data()+static_cast<size_t>(image.height-1-y)*row,row);
        break;
    }
    return slot.get();
}

const RgbaImage* AssetStore::TextureById(int fileIndex, uint32_t id) {
    for (const auto& [name, list] : byFile_) {
        if (list.empty() || list[0].fileIndex != fileIndex) continue;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].desc.id != id) continue;
            return ResolvedTexture(list[i]);
        }
        return nullptr;
    }
    return nullptr;
}

const RgbaImage* AssetStore::ResolvedTexture(const TexRef& original) {
    const TexRef* r = &original;
    if (redBlood_) {
        // 0x0041CD70 loads the selected blood file before models: its shared global
        // texture IDs replace sprays, pools and wounds from every model file.
        auto blood = redBloodTextures_.find(original.desc.id);
        if (blood != redBloodTextures_.end()) r = &blood->second;
    }
    const TexKey key(r->fileName, static_cast<uint32_t>(r->texIndex));
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second.get();
    auto& slot = textures_[key];
    slot = Decode(*r); // uses the selected color's optional PNG override too
    return slot.get();
}

std::unique_ptr<RgbaImage> AssetStore::LoadOverride(const TexRef& r) {
    if (AssetDir().empty()) return nullptr;
    std::string stem = r.fileName;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".bin") == 0) stem.resize(stem.size() - 4);
    char idx[3][16];
    snprintf(idx[0], sizeof(idx[0]), "%d", r.texIndex);
    snprintf(idx[1], sizeof(idx[1]), "%03d", r.texIndex);
    snprintf(idx[2], sizeof(idx[2]), "%04d", r.texIndex);
    for (const char* i : idx) {
        const std::string path = AssetOverride("tex/" + stem + "/" + i + ".png");
        if (path.empty()) continue;
        PngImage png;
        std::string err;
        if (!ReadPngFile(path, png, &err)) {
            fprintf(stderr, "asset pack: %s: %s\n", path.c_str(), err.c_str());
            return nullptr;
        }
        auto img = std::make_unique<RgbaImage>();
        img->width = png.width;
        img->height = png.height;
        img->logicalWidth = r.desc.width;
        img->logicalHeight = r.desc.height;
        // The pack's PNGs are upright (as tools/tex_export.py writes them); the game stores rows bottom-up.
        img->pixels.resize(png.rgba.size());
        const size_t row = static_cast<size_t>(png.width) * 4;
        for (int y = 0; y < png.height; ++y)
            memcpy(img->pixels.data() + static_cast<size_t>(y) * row, png.rgba.data() + static_cast<size_t>(png.height - 1 - y) * row, row);
        if (AssetOverrideHits() == 1) fprintf(stderr, "asset pack: %s\n", AssetDir().c_str());
        return img;
    }
    return nullptr;
}

AssetStore::~AssetStore() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    for (std::thread& t : workers_) t.join();
}

bool AssetStore::AssetDirSet() { return !AssetDir().empty(); }

void AssetStore::ClearCache() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    for (auto& worker : workers_) worker.join();
    workers_.clear();
    queued_.clear(); inFlight_.clear(); ready_.clear();
    textures_.clear(); normalRoughness_.clear(); normalRoughnessMode_=-1; files_.clear();
    refinementSelectionRead_=false;refinementModels_.clear();
    stop_ = false;
}

void AssetStore::PrefetchFile(int fileIndex) {
    if (AssetDir().empty()) return;
    for (const auto& [name, list] : byFile_)
        if (!list.empty() && list[0].fileIndex == fileIndex) PrefetchTexName(name);
}

void AssetStore::PrefetchPolFile(const std::string& polName) {
    if (AssetDir().empty()) return;
    const std::string tex = TextureFileFor(polName);
    if (!tex.empty()) PrefetchTexName(tex);
}

void AssetStore::PrefetchTexName(const std::string& name) {
    static const bool log = getenv("HOTD2_PLAY_TIMING") != nullptr;
    auto found = byFile_.find(name);
    if (found == byFile_.end()) return;
    const std::vector<TexRef>& list = found->second;
    std::lock_guard<std::mutex> lk(mu_);
    int added = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        const TexKey key(name, static_cast<uint32_t>(i));
        if (textures_.count(key) || ready_.count(key) || queued_.count(key) || inFlight_.count(key)) continue;
        queue_.push_back(list[i]);
        queued_.insert(key);
        ++added;
    }
    if (log && added) fprintf(stderr, "prefetch %s (%d of %d textures)\n", name.c_str(), added, static_cast<int>(list.size()));
    if (workers_.empty()) {
        const unsigned hw = std::thread::hardware_concurrency();
        const int n = static_cast<int>(std::min(4u, std::max(1u, hw > 1 ? hw - 1 : 1u)));
        for (int k = 0; k < n; ++k) workers_.emplace_back([this] { WorkerLoop(); });
    }
    cv_.notify_all();
}

void AssetStore::WorkerLoop() {
    for (;;) {
        TexRef r;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            r = queue_.front();
            queue_.pop_front();
            const TexKey key(r.fileName, static_cast<uint32_t>(r.texIndex));
            queued_.erase(key);
            inFlight_.insert(key);
        }
        std::unique_ptr<RgbaImage> img = LoadOverride(r);
        {
            std::lock_guard<std::mutex> lk(mu_);
            const TexKey key(r.fileName, static_cast<uint32_t>(r.texIndex));
            inFlight_.erase(key);
            ready_[key] = std::move(img);
            ++prefetchDecodes_;
        }
        cv_.notify_all();
    }
}

std::vector<const RgbaImage*> AssetStore::DrainPrefetched(int max) {
    std::vector<const RgbaImage*> out;
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = ready_.begin(); it != ready_.end() && static_cast<int>(out.size()) < max;) {
        if (!it->second) {  // no override: stays here, Texture() then decodes the game's own texture when asked
            ++it;
            continue;
        }
        const RgbaImage* p = it->second.get();
        textures_[it->first] = std::move(it->second);
        out.push_back(p);
        it = ready_.erase(it);
    }
    return out;
}

std::unique_ptr<RgbaImage> AssetStore::TakePrefetched(const TexRef& r, bool* checked) {
    *checked = false;
    if (AssetDir().empty() || workers_.empty()) return nullptr;
    const TexKey key(r.fileName, static_cast<uint32_t>(r.texIndex));
    std::unique_lock<std::mutex> lk(mu_);
    if (queued_.count(key)) {  // not started: decode it here, so a worker does not duplicate the work
        queued_.erase(key);
        for (auto it = queue_.begin(); it != queue_.end(); ++it)
            if (it->fileName == r.fileName && it->texIndex == r.texIndex) {
                queue_.erase(it);
                break;
            }
        return nullptr;
    }
    cv_.wait(lk, [&] { return !inFlight_.count(key); });
    auto it = ready_.find(key);
    if (it == ready_.end()) return nullptr;
    *checked = true;
    std::unique_ptr<RgbaImage> img = std::move(it->second);
    ready_.erase(it);
    return img;
}

std::unique_ptr<RgbaImage> AssetStore::Decode(const TexRef& r) {
    bool checked = false;
    if (auto o = TakePrefetched(r, &checked)) return o;
    if (!checked && !AssetDir().empty()) {
        if (auto o = LoadOverride(r)) {
            ++inlineDecodes_;
            static const bool log = getenv("HOTD2_PLAY_TIMING") != nullptr;
            if (log) fprintf(stderr, "inline decode %s/%d (file %d)\n", r.fileName.c_str(), r.texIndex, r.fileIndex);
            return o;
        }
    }
    if (!std::filesystem::exists(dir_ / "tex" / r.fileName)) return nullptr;
    const std::vector<uint8_t>& data = File("tex/" + r.fileName);
    uint16_t palette[16] = {};
    const uint16_t* pal = nullptr;
    if (game::LayoutOf(r.desc) == game::TexLayout::Pal4Twiddled) {
        // common.bin is loaded by Res_LoadCommon without a bank (-> bank 0).
        int bank = r.fileIndex == 1 ? -1 : game::GetPaletteBank(exe_, r.fileIndex, r.texIndex);
        if (bank < 0 || bank > 0x3F) bank = 0;
        for (int k = 0; k < 16; ++k)
            palette[k] = game::ConvertPaletteEntry(bank < static_cast<int>(banks_.size()) ? banks_[bank][k] : 0,
                                                   game::PixelFormatOf(r.desc));
        pal = palette;
    }
    std::vector<uint16_t> texels;
    if (!game::DecodeTexels(data.data(), data.size(), r.desc, pal, texels)) return nullptr;
    auto img = std::make_unique<RgbaImage>();
    img->width = r.desc.width;
    img->height = r.desc.height;
    img->pixels.resize(texels.size() * 4);
    if (!game::TexelsToRgba(texels.data(), texels.size(), game::PixelFormatOf(r.desc), img->pixels.data())) return nullptr;
    return img;
}

}  // namespace port
