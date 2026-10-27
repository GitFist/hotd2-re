#pragma once

// The front end's bitmap screens (game::WorldHud::screens: the PC-only tex\*.bin files that are plain BMPs - boot logos,
// mode-select background and labels) as 2D draws for ModelRenderer, like the original's 0x0049EBE0: one quad per slot,
// strip (x, y) (x, y + h) (x + w, y) (x + w, y + h), uv (0, 0)..(u, v), rhw 0.5, vertex alpha. Drawn first and without
// depth test (the original draws them during the update, before the scene). Not compared with the original's output.
//
// Modern main menu (SetModernMenu, game::WorldConfig::portModernMenu): the eight grey labels are painted into the
// mode-select background tiles hod2title00..11. With the modern menu on, the first use of a tile in-paints them out,
// once, in memory: the glyph footprint comes from the bright label tiles (arcade00.. etc.: pixels that differ from the
// background at the label's position, plus the dark halo around them), the 640x480 background is composed from the
// six tiles, the masked pixels are filled by iterative averaging from their unmasked neighbours until converged, with
// a little mirrored high-frequency detail from the nearest boundary on top, and the result goes back into the cached
// tiles. Nothing is written to disk. An asset-pack override of any of the tiles switches the cleanup off (the pack's
// own background is shown as it is). Classic uses the original background and opaque highlight bitmaps as one
// matching set: legacy packs replace the background with label-free art intended for the modern menu.

#include <memory>
#include <string>
#include <vector>

#include "game/world.h"
#include "render/model_renderer.h"

namespace port {

class FrontScreens {
public:
    explicit FrontScreens(std::string gameDir) : dir_(std::move(gameDir)) {}
    std::vector<ModelRenderer::SpriteDraw> Draws(const game::WorldHud& h);
    // The mode-select background without the baked labels (see above). Drops cached menu bitmaps on a change.
    // Classic port menus also remove the old Training label so World can draw Challenges.
    void SetModernMenu(bool on, bool renameChallenges = false);
    bool ModernMenu() const { return modernMenu_; }
    // Load (and cache) a bitmap now: play.exe warms the mode select's tiles at boot, so an asset pack's big PNGs are
    // not decoded in the frame the menu first shows.
    void Warm(const std::string& name) { Bitmap(name); }

private:
    std::unique_ptr<RgbaImage> Load(const std::string& name);  // the BMP (+ asset-pack override), top row first; null if unreadable
    const RgbaImage* Bitmap(const std::string& name);          // cached; null if unreadable
    void CleanMenuBackground();                                // in-paints the labels out of the six tiles and caches them
    std::string dir_;
    std::vector<std::pair<std::string, std::unique_ptr<RgbaImage>>> cache_;
    bool modernMenu_ = false;
    bool renameChallenges_ = false;
    bool cleaned_ = false;
};

}  // namespace port
