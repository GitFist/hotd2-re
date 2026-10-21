#pragma once

// Read-only artwork preview of an installed visual pack. Selection never enables
// the pack, edits its files or advances the game. Images are decoded only once
// per selected path and retained immutably because ModelRenderer caches pointers.
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render/model_renderer.h"

namespace play {

struct PackPreviewInfo {
    std::string name, author, version;
    std::string caption, message;
    std::vector<std::string> sources; // pack-relative filenames actually shown
    bool original = true;
    bool explicitArtwork = false;
    bool scanLimited = false;
};

class PackPreview {
public:
    // Empty path means Original. Returns true if the selected path changed.
    // An unchanged selection performs no filesystem I/O, including failed loads.
    bool Select(const std::filesystem::path& path);
    const PackPreviewInfo& Info() const;
    const port::RgbaImage* Image() const;
    // One aspect-fit, upright HUD quad in authored 640x480 UI coordinates. Apply
    // the same horizontalScale as UiFont for wide-view menu placement.
    void AppendSprites(std::vector<port::ModelRenderer::SpriteDraw>& out,
                       float x, float y, float width, float height, float horizontalScale = 1) const;
    // Only call after ModelRenderer::ClearCache (or its destruction), because
    // previously emitted SpriteDraw pointers and GPU texture cache keys expire.
    void ResetAfterRendererCacheClear();

private:
    struct Entry { PackPreviewInfo info; port::RgbaImage image; };
    static std::unique_ptr<Entry> Load(const std::filesystem::path& path);
    std::map<std::string, std::unique_ptr<Entry>> cache_; // <=64 immutable 256x192 sheets, <=12 MiB
    Entry empty_, overflow_;
    const Entry* selected_ = nullptr;
    std::string selectedPath_;
    bool haveSelection_ = false;
};

} // namespace play
