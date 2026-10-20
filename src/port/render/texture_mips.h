#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include "asset_store.h"

namespace port {

// Smaller texture levels for minified 3D surfaces. Leave level zero byte-for-byte
// intact. Weight RGB by coverage so transparent border colours cannot turn small
// feathers, leaves and effects into bright/dark fringes. Opaque material variants
// average RGB independently: their unused alpha bits must not darken the texture.
inline std::vector<RgbaImage> BuildTextureMips(const RgbaImage& base, bool alphaWeighted = true) {
    std::vector<RgbaImage> levels;
    if (base.width <= 0 || base.height <= 0 ||
        base.pixels.size() != static_cast<size_t>(base.width) * base.height * 4) return levels;
    const RgbaImage* src = &base;
    while (src->width > 1 || src->height > 1) {
        RgbaImage dst;
        dst.width = std::max(1, src->width / 2);
        dst.height = std::max(1, src->height / 2);
        dst.pixels.resize(static_cast<size_t>(dst.width) * dst.height * 4);
        for (int y = 0; y < dst.height; ++y) for (int x = 0; x < dst.width; ++x) {
            const int x0 = x * src->width / dst.width, x1 = (x + 1) * src->width / dst.width;
            const int y0 = y * src->height / dst.height, y1 = (y + 1) * src->height / dst.height;
            uint32_t alpha = 0, colour[3] = {};
            for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx) {
                const auto* pixel = &src->pixels[(static_cast<size_t>(sy) * src->width + sx) * 4];
                alpha += pixel[3];
                for (int c = 0; c < 3; ++c) colour[c] += pixel[c] * (alphaWeighted ? pixel[3] : 1);
            }
            auto* pixel = &dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4];
            const int count = (x1 - x0) * (y1 - y0);
            const uint32_t weight = alphaWeighted ? alpha : static_cast<uint32_t>(count);
            for (int c = 0; c < 3; ++c) pixel[c] = weight ? static_cast<uint8_t>((colour[c] + weight / 2) / weight) : 0;
            pixel[3] = static_cast<uint8_t>((alpha + count / 2) / count);
        }
        levels.push_back(std::move(dst));
        src = &levels.back();
    }
    return levels;
}

} // namespace port
