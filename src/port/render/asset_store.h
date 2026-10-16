#pragma once

// Loads game assets for the port through the native decoders: models (pol\), textures (tex\, exe
// descriptor tables, palette banks). Decoded files are cached.

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "game/exe_image.h"
#include "game/model.h"
#include "game/texture.h"

namespace port {

struct RgbaImage {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;  // RGBA8, rows in the game's stored order (v = 0 is row 0)
    // The game's own size of this texture (descriptor / BMP header). It differs from width / height only for an
    // asset-pack override (assets/asset_override.h); sprites and blits size their quads by it, UVs are normalized.
    int logicalWidth = 0, logicalHeight = 0;  // 0 = same as width / height
    int LogicalWidth() const { return logicalWidth ? logicalWidth : width; }
    int LogicalHeight() const { return logicalHeight ? logicalHeight : height; }
};

class AssetStore {
public:
    AssetStore() = default;
    ~AssetStore();
    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;

    bool Open(const std::filesystem::path& gameDir, std::string* error);
    // Stop old prefetch jobs and free decoded assets between scenes. Invalidates returned image/file pointers;
    // the renderer must discard its cached models and texture handles before this call.
    void ClearCache();

    // Asset pack only: decode the pack's overrides of texture file fileIndex (index into 0x004D1410, the world's
    // WorldHud::texFiles index) on worker threads, ahead of their first draw. play.exe calls it when the game loads the
    // file (Res_LoadTexFile), so the big PNGs are ready, or well under way, by the time something drawn needs them;
    // Texture() takes a decoded image from the prefetch, waits for one in flight, and decodes itself otherwise.
    void PrefetchFile(int fileIndex);
    // The same for the texture file of a pol file (tex\X.bin for pol\X.bin / pol\pol_X.bin): the model textures, which
    // the game loads with the models (WorldHud::polFileLoads).
    void PrefetchPolFile(const std::string& polName);
    // The same for a texture file by name (tex\<name>).
    void PrefetchTexFile(const std::string& texName) {
        if (!AssetDirSet()) return;
        PrefetchTexName(texName);
    }
    // Up to max prefetched images, moved into the texture cache: the caller creates their GPU textures ahead of use
    // (ModelRenderer::WarmTexture). Images without an override (nullptr results) are dropped on the way.
    std::vector<const RgbaImage*> DrainPrefetched(int max);
    // Statistics: overrides decoded on the calling (main) thread / by the prefetch.
    int OverridesDecodedInline() const { return inlineDecodes_; }
    int OverridesPrefetched() const { return prefetchDecodes_; }

    // Decoded (PRS or raw) contents of a file in the game directory, cached.
    const std::vector<uint8_t>& File(const std::string& relPath);

    // Model by global id: pol file 0x004E83B4[id], position in that file's id list 0x004E794C[file]
    // (count 0x004E803C[file]). Returns false if unresolved. polName receives the file name.
    bool ModelById(uint32_t id, game::Model& out, std::string* polName);

    // All models of a pol file (empty if the file has no model header).
    std::vector<game::Model> LoadPolFile(const std::string& name, int* failures);

    // Texture for a material's texture field: a descriptor index into the texture file named like the pol file
    // (pol\X.bin -> tex\X.bin, verified at runtime; pol\pol_X.bin -> tex\X.bin, inferred).
    // Returns nullptr for 0xFFFFFFFF or unresolved references.
    const RgbaImage* Texture(const std::string& polName, uint32_t ref);

    // Texture with global id id (descriptor textureId) in texture file ileIndex (index into 0x004D1410), e.g.
    // for sprites (game::SpriteTables). Null if missing.
    const RgbaImage* TextureById(int fileIndex, uint32_t id);

    // Texture file used by a pol file's materials, or empty if none.
    std::string TextureFileFor(const std::string& polName) const;

    const game::ExeImage& Exe() const { return exe_; }
    const std::filesystem::path& Dir() const { return dir_; }

private:
    struct TexRef {
        int fileIndex;
        std::string fileName;
        game::TextureDesc desc;
        int texIndex;
    };
    using TexKey = std::pair<std::string, uint32_t>;  // texture file name, descriptor index
    std::unique_ptr<RgbaImage> Decode(const TexRef& r);
    // Asset pack: <HOTD2_ASSET_DIR>/tex/<file stem>/<index>.png (any size, upright) -> flipped to the stored order.
    // Pure (file read + decode): also run by the prefetch threads.
    std::unique_ptr<RgbaImage> LoadOverride(const TexRef& r);
    // The prefetch's result for r if it has one (or is making one: waits), else nullptr with *checked = false.
    std::unique_ptr<RgbaImage> TakePrefetched(const TexRef& r, bool* checked);
    void PrefetchTexName(const std::string& texName);
    static bool AssetDirSet();
    void WorkerLoop();

    std::filesystem::path dir_;
    game::ExeImage exe_;
    std::vector<std::array<uint16_t, 16>> banks_;
    std::map<std::string, std::vector<TexRef>> byFile_;  // texture file name -> descriptors in index order
    std::map<std::string, std::vector<uint8_t>> files_;
    std::map<TexKey, std::unique_ptr<RgbaImage>> textures_;

    // prefetch (asset pack): queue_ -> workers -> ready_ (nullptr = no override for that texture)
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<TexRef> queue_;
    std::set<TexKey> queued_, inFlight_;
    std::map<TexKey, std::unique_ptr<RgbaImage>> ready_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
    int inlineDecodes_ = 0, prefetchDecodes_ = 0;
};

}  // namespace port
