#include "texture.h"

#include <algorithm>

namespace game {

uint32_t Twiddle(uint32_t x, uint32_t y) {
    uint32_t r = 0;
    for (uint32_t bit = 0; (x >> bit) || (y >> bit); ++bit) {
        r |= ((y >> bit) & 1u) << (2 * bit);
        r |= ((x >> bit) & 1u) << (2 * bit + 1);
    }
    return r;
}

bool DecodeTexels(const uint8_t* data, size_t size, const TextureDesc& d, const uint16_t* palette,
                  std::vector<uint16_t>& out) {
    const int w = d.width, h = d.height;
    if (w <= 0 || h <= 0 || d.offset > size) {
        return false;
    }
    const uint8_t* base = data + d.offset;
    const size_t avail = size - d.offset;
    auto texel = [&](size_t i, uint16_t& v) {
        if (2 * i + 1 >= avail) return false;
        v = static_cast<uint16_t>(base[2 * i] | (base[2 * i + 1] << 8));
        return true;
    };

    out.assign(static_cast<size_t>(w) * h, 0);
    switch (LayoutOf(d)) {
    case TexLayout::Twiddled:
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (!texel(Twiddle(x, y), out[y * w + x])) return false;
        return true;

    case TexLayout::Rect:
        for (int i = 0; i < w * h; ++i)
            if (!texel(i, out[i])) return false;
        return true;

    case TexLayout::RectTwiddled: {
        const int s = std::min(w, h);
        const int blocks = std::max(w, h) / s;
        for (int b = 0; b < blocks; ++b) {
            const size_t blockBase = static_cast<size_t>(b) * s * s;
            for (int y = 0; y < s; ++y) {
                for (int x = 0; x < s; ++x) {
                    uint16_t v;
                    if (!texel(blockBase + Twiddle(x, y), v)) return false;
                    if (w < h) out[(b * s + y) * w + x] = v;
                    else out[y * w + b * s + x] = v;
                }
            }
        }
        return true;
    }

    case TexLayout::Vq: {
        // 256-entry codebook of 2x2 blocks (texel order: (0,0) (0,1) (1,0) (1,1)), then twiddled indices.
        constexpr size_t kBook = 0x800;
        for (int by = 0; by < h / 2; ++by) {
            for (int bx = 0; bx < w / 2; ++bx) {
                const size_t idx = kBook + Twiddle(bx, by);
                if (idx >= avail) return false;
                const size_t e = base[idx];
                uint16_t t[4];
                for (int k = 0; k < 4; ++k)
                    if (!texel(e * 4 + k, t[k])) return false;
                const int x = bx * 2, y = by * 2;
                out[y * w + x] = t[0];
                out[(y + 1) * w + x] = t[1];
                out[y * w + x + 1] = t[2];
                out[(y + 1) * w + x + 1] = t[3];
            }
        }
        return true;
    }

    case TexLayout::Pal4Twiddled: {
        if (!palette) return false;
        // Non-square: square twiddled blocks along the long axis (as in the 0x004AC270 loop).
        const int s = std::min(w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint32_t blk = (w >= h) ? x / s : y / s;
                const uint32_t t = blk * s * s + Twiddle(x % s, y % s);
                if (t / 2 >= avail) return false;
                out[y * w + x] = palette[(base[t / 2] >> (4 * (t & 1))) & 15];
            }
        }
        return true;
    }
    }
    return false;
}

uint16_t ConvertPaletteEntry(uint16_t v, PixelFormat fmt) {
    switch (fmt) {
    case PixelFormat::Rgb565:
        return static_cast<uint16_t>(((v & 0xFFE0) << 1) | (v & 0x1F));
    case PixelFormat::Argb4444:
        return static_cast<uint16_t>(((v & 0x8000) ? 0xF000 : 0) | (((v >> 11) & 15) << 8) |
                                     (((v >> 6) & 15) << 4) | ((v >> 1) & 15));
    default:
        return v;
    }
}

bool TexelsToRgba(const uint16_t* texels, size_t count, PixelFormat fmt, uint8_t* rgba) {
    for (size_t i = 0; i < count; ++i, rgba += 4) {
        const uint32_t v = texels[i];
        switch (fmt) {
        case PixelFormat::Argb1555: {
            const uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            rgba[0] = static_cast<uint8_t>(r << 3 | r >> 2);
            rgba[1] = static_cast<uint8_t>(g << 3 | g >> 2);
            rgba[2] = static_cast<uint8_t>(b << 3 | b >> 2);
            rgba[3] = (v & 0x8000) ? 255 : 0;
            break;
        }
        case PixelFormat::Rgb565: {
            const uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            rgba[0] = static_cast<uint8_t>(r << 3 | r >> 2);
            rgba[1] = static_cast<uint8_t>(g << 2 | g >> 4);
            rgba[2] = static_cast<uint8_t>(b << 3 | b >> 2);
            rgba[3] = 255;
            break;
        }
        case PixelFormat::Argb4444:
            rgba[0] = static_cast<uint8_t>(((v >> 8) & 15) * 17);
            rgba[1] = static_cast<uint8_t>(((v >> 4) & 15) * 17);
            rgba[2] = static_cast<uint8_t>((v & 15) * 17);
            rgba[3] = static_cast<uint8_t>(((v >> 12) & 15) * 17);
            break;
        default:
            return false;
        }
    }
    return true;
}

}  // namespace game
