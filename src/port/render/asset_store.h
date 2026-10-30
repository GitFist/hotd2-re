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
#include "game/typing_reference.h"
#include "render/blood_color.h"

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

// Read-only installed visual-pack catalogue. Discovery never selects a pack or mutates its
// files. A legacy tex/<bank>/<index>.png or bmp/<file>.png pack needs no manifest; an optional
// pack.ini can describe it with format=1, name=, author= and version=. Missing overrides use
// original assets. hasAudio warns that the existing asset override directory also contains
// sound replacements, so choosing it is not necessarily a graphics-only change.
struct TexturePackInfo {
    std::filesystem::path path;
    std::string name, author, version, error;
    size_t textures = 0, frontScreens = 0;
    size_t materialMaps = 0;
    bool valid = false, hasAudio = false, hasManifest = false, fullyValidated = false;
    bool normalRoughness = false; // materials=normal-roughness-v1 in pack.ini
    bool pnTriangles = false; // meshes=pn-triangles-v1, explicit bounded refine_models list
    size_t refinedModels = 0;
};
// Default checks layout, PNG headers and supported image sizes without decoding an entire
// high-resolution pack at menu startup. decodeAll additionally checks every PNG with the
// actual game decoder; it can be slow and is intended for an explicit validation operation.
TexturePackInfo InspectTexturePack(const std::filesystem::path& path, bool decodeAll = false);
// Each root may be a pack or a directory of installed packs. Only immediate children are
// examined, paths are deduplicated, and results are sorted by display name.
std::vector<TexturePackInfo> DiscoverTexturePacks(const std::vector<std::filesystem::path>& roots);

class AssetStore {
public:
    AssetStore() = default;
    ~AssetStore();
    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;

    bool Open(const std::filesystem::path& gameDir, std::string* error);
    bool LoadTypingReference(const std::string& path, std::string* error);
    std::string PolFileName(uint32_t file) const;
    // Inspect the pack used by asset lookups, including HOTD2_ASSET_DIR. This is
    // independent of a menu's saved next-launch selection; call outside the frame loop.
    TexturePackInfo ActivePackInfo() const;
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

    // Matches the original global blood texture override by texture ID. Rendering only.
    void SetBloodColor(BloodColor color) { bloodColor_ = color; }
    BloodColor GetBloodColor() const { return bloodColor_; }

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
    // Optional authored material map, enabled only by pack.ini materials=normal-roughness-v1:
    // materials/tex/<bank>/<index>_nr.png. RGBA8 tangent normal XYZ, perceptual roughness A.
    // No generated/invented maps, color-image substitution or model-specific exceptions.
    const RgbaImage* NormalRoughness(const std::string& polName, uint32_t ref);
    // Explicit visual-model selection from the active pack's manifest. No mesh
    // assets are imported; this permits bounded render-only refinement of the
    // named original model IDs after all native CPU deformation.
    bool RefineModel(uint32_t modelId);

    // Texture with global id id (descriptor textureId) in texture file ileIndex (index into 0x004D1410), e.g.
    // for sprites (game::SpriteTables). Null if missing.
    const RgbaImage* TextureById(int fileIndex, uint32_t id);
    bool SourceSprite(int id, int* file, uint32_t* texture) const;
    int SourceModel(int id) const { return typing_ ? typing_->SourceModel(id) : 0; }
    const game::TypingReference* PreparedTypingReference() const { return typing_.get(); }

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
    const RgbaImage* ResolvedTexture(const TexRef& r);
    std::map<uint32_t, TexRef> redBloodTextures_;
    std::map<std::pair<BloodColor, uint32_t>, std::unique_ptr<RgbaImage>> recoloredBloodTextures_;
    BloodColor bloodColor_ = BloodColor::Green;
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
    std::unique_ptr<game::TypingReference> typing_;
    std::vector<std::array<uint16_t, 16>> banks_;
    std::map<std::string, std::vector<TexRef>> byFile_;  // texture file name -> descriptors in index order
    std::map<std::string, std::vector<uint8_t>> files_;
    std::map<TexKey, std::unique_ptr<RgbaImage>> textures_;
    std::map<TexKey, std::unique_ptr<RgbaImage>> normalRoughness_;
    int normalRoughnessMode_ = -1;
    bool refinementSelectionRead_ = false;
    std::set<uint32_t> refinementModels_;

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
