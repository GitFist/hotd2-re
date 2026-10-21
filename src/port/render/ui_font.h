#pragma once

#include <array>
#include "game/world.h"
#include "render/asset_store.h"
#include "render/model_renderer.h"

namespace port {
// Rasterize installed system faces at runtime. No font files or game glyphs are
// redistributed. Other platforms retain the original sprite-font fallback.
class UiFont {
public:
    bool Init();
    bool Available() const { return available_; }
    void Append(const std::vector<game::WorldHud::MenuText>& text, std::vector<ModelRenderer::SpriteDraw>& out, float horizontalScale = 1) const;
private:
    struct Face {
        RgbaImage image;
        std::array<float, 95> advance{};
        int height = 64;
    };
    std::array<Face, 2> faces_;
    bool available_ = false;
};
} // namespace port
