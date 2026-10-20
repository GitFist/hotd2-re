#include "asset_store.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "assets/asset_override.h"
#include "game/palette_bank.h"
#include "game/prs.h"
#include "render/png_read.h"

namespace port {

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
    textures_.clear(); files_.clear();
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
