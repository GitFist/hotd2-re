#include "render/front_screens.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

#include "assets/asset_override.h"
#include "game/sprite.h"
#include "render/png_read.h"

namespace port {

namespace {

// The mode-select background (world_front.cpp MenuFrame): six 256 x 256 tiles, the last two showing their left half.
constexpr int kBgW = 640, kBgH = 512;
const char* const kMenuTile[6] = { "tex\\hod2title00.bin", "tex\\hod2title01.bin", "tex\\hod2title02.bin",
                                   "tex\\hod2title03.bin", "tex\\hod2title10.bin", "tex\\hod2title11.bin" };
const int kMenuTilePos[6][3] = { { 0, 0, 256 }, { 256, 0, 256 }, { 0, 256, 256 }, { 256, 256, 256 }, { 512, 0, 128 }, { 512, 256, 128 } };
// The eight labels: three 64 x 64 bright tiles each at x 0xE0 + 0x40 k, y kLabelY[e] (the exe's name tables
// 0x00597954..0x00597AE8, read back from Hod2.exe).
const char* const kLabelBase[8] = { "tex\\arcade0", "tex\\original_0", "tex\\traning_0", "tex\\boss_0",
                                    "tex\\ranking_0", "tex\\option_0", "tex\\network_0", "tex\\exit_0" };
const int kLabelY[8] = { 0x20, 0x50, 0x90, 0xB0, 0xF0, 0x120, 0x150, 0x180 };
constexpr int kLabelX0 = 0xE0, kLabelStep = 0x40, kLabelSize = 64;
// Mask: a pixel is the glyph body when the bright tile differs from the background by more than kBodyDiff (any
// channel); the dark halo painted around the glyphs (near-black, 3 px, identical in both versions) is taken from the
// background within kHaloRadius of the body where its luminance is below kHaloLum; the union grows by kDilate for the
// halo's soft edge.
constexpr int kBodyDiff = 28, kHaloRadius = 3, kHaloLum = 40, kDilate = 2;
constexpr float kDetailGain = 0.5f;  // mirrored detail on top of the smooth fill (0 = smooth only)

bool ClassicMenuBitmap(const std::string& name) {
    for (const char* tile : kMenuTile)
        if (name == tile) return true;
    for (const char* label : kLabelBase)
        for (char tile = '0'; tile <= '2'; ++tile)
            if (name == std::string(label) + tile + ".bin") return true;
    return false;
}

struct Rect {
    int x0 = kBgW, y0 = kBgH, x1 = 0, y1 = 0;  // [x0, x1) x [y0, y1)
    void Add(int x, int y) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x + 1);
        y1 = std::max(y1, y + 1);
    }
    void Grow(int r) {
        x0 = std::max(0, x0 - r);
        y0 = std::max(0, y0 - r);
        x1 = std::min(kBgW, x1 + r);
        y1 = std::min(kBgH, y1 + r);
    }
    bool Empty() const { return x1 <= x0 || y1 <= y0; }
};

// Chebyshev dilation of a mask by r inside rect.
std::vector<uint8_t> Dilate(const std::vector<uint8_t>& m, int r, const Rect& rc) {
    std::vector<uint8_t> out(m.size(), 0);
    for (int y = rc.y0; y < rc.y1; ++y)
        for (int x = rc.x0; x < rc.x1; ++x) {
            if (!m[static_cast<size_t>(y) * kBgW + x]) continue;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < kBgW && yy >= 0 && yy < kBgH) out[static_cast<size_t>(yy) * kBgW + xx] = 1;
                }
        }
    return out;
}

}  // namespace

void FrontScreens::SetModernMenu(bool on, bool renameChallenges) {
    if (on == modernMenu_ && renameChallenges == renameChallenges_) return;
    modernMenu_ = on;
    renameChallenges_ = renameChallenges;
    cleaned_ = false;
    cache_.erase(std::remove_if(cache_.begin(), cache_.end(),
                                [](const auto& e) { return ClassicMenuBitmap(e.first); }),
                 cache_.end());
}

std::unique_ptr<RgbaImage> FrontScreens::Load(const std::string& name) {
    std::unique_ptr<RgbaImage> img;
    std::ifstream f(dir_ + "\\" + name, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    auto u32 = [&](size_t o) { return o + 4 <= b.size() ? static_cast<uint32_t>(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) : 0u; };
    if (b.size() > 54 && b[0] == 'B' && b[1] == 'M') {
        const uint32_t off = u32(10), hdr = u32(14), w = u32(18), comp = u32(30);
        const int32_t hRaw = static_cast<int32_t>(u32(22));
        const int bpp = b[28] | b[29] << 8;
        const uint32_t h = static_cast<uint32_t>(hRaw < 0 ? -hRaw : hRaw);
        if (comp == 0 && (bpp == 8 || bpp == 24 || bpp == 32) && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
            img = std::make_unique<RgbaImage>();
            img->width = static_cast<int>(w);
            img->height = static_cast<int>(h);
            img->pixels.assign(static_cast<size_t>(w) * h * 4, 0);
            const size_t stride = ((static_cast<size_t>(w) * bpp + 31) / 32) * 4;
            const size_t pal = 14 + hdr;
            for (uint32_t y = 0; y < h; ++y) {
                const size_t row = off + stride * (hRaw < 0 ? y : h - 1 - y);  // bottom-up file rows -> top-down image
                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t r = 0, g = 0, bl = 0;
                    size_t p = 0;
                    if (bpp == 8) p = row + x < b.size() ? pal + 4 * static_cast<size_t>(b[row + x]) : b.size();
                    else p = row + x * static_cast<size_t>(bpp / 8);
                    if (p + 2 < b.size()) {
                        bl = b[p];
                        g = b[p + 1];
                        r = b[p + 2];
                    }
                    uint8_t* o = &img->pixels[(static_cast<size_t>(y) * w + x) * 4];
                    o[0] = r;
                    o[1] = g;
                    o[2] = bl;
                    o[3] = 255;
                }
            }
            // Asset pack: bmp/<file name>.png (the name without its tex\ directory, as tools/remaster.py writes it)
            // replaces the pixels (any size, upright); blits keep the BMP's own size.
            const size_t slash = name.find_last_of("\\/");
            const std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
            // Classic highlights are opaque crops of the original background,
            // whose grey labels also supply the unselected entries. The legacy
            // 2x pack replaces that background with label-free screenshot art.
            // Keep this coupled set of thirty bitmaps together in Classic mode;
            // all other screens and game textures still use the selected pack.
            const bool classicMenu = !modernMenu_ && ClassicMenuBitmap(name);
            if (const std::string o = classicMenu ? std::string{} : AssetOverride("bmp/" + base + ".png"); !o.empty()) {
                PngImage png;
                if (ReadPngFile(o, png)) {
                    img->logicalWidth = img->width;
                    img->logicalHeight = img->height;
                    img->width = png.width;
                    img->height = png.height;
                    img->pixels = std::move(png.rgba);
                }
            }
        }
    }
    return img;
}

const RgbaImage* FrontScreens::Bitmap(const std::string& name) {
    if ((modernMenu_ || renameChallenges_) && !cleaned_ && name.compare(0, 13, "tex\\hod2title") == 0) CleanMenuBackground();
    for (auto& [n, img] : cache_)
        if (n == name) return img.get();
    std::unique_ptr<RgbaImage> img = Load(name);
    const RgbaImage* p = img.get();
    cache_.emplace_back(name, std::move(img));
    return p;
}

void FrontScreens::CleanMenuBackground() {
    cleaned_ = true;
    std::unique_ptr<RgbaImage> tiles[6];
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        tiles[i] = Load(kMenuTile[i]);
        // a missing tile, an unexpected size or an asset-pack override: the tiles are shown as they are
        if (!tiles[i] || tiles[i]->width != 256 || tiles[i]->height != 256 || tiles[i]->logicalWidth != 0) ok = false;
    }
    auto cacheTiles = [&] {
        for (int i = 0; i < 6; ++i) {
            bool have = false;
            for (auto& [n, img] : cache_)
                if (n == kMenuTile[i]) have = true;
            if (!have) cache_.emplace_back(kMenuTile[i], std::move(tiles[i]));
        }
    };
    if (!ok) {
        cacheTiles();
        return;
    }
    // the background, float RGB
    std::vector<float> bg(static_cast<size_t>(kBgW) * kBgH * 3, 0.0f);
    for (int i = 0; i < 6; ++i) {
        const int tx = kMenuTilePos[i][0], ty = kMenuTilePos[i][1], tw = kMenuTilePos[i][2];
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < tw; ++x) {
                const uint8_t* s = &tiles[i]->pixels[(static_cast<size_t>(y) * 256 + x) * 4];
                float* d = &bg[(static_cast<size_t>(ty + y) * kBgW + tx + x) * 3];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
            }
    }
    // the glyph body: where a bright label tile differs from the background under it
    std::vector<uint8_t> body(static_cast<size_t>(kBgW) * kBgH, 0);
    Rect rc;
    for (int e = modernMenu_ ? 0 : 2; e < (modernMenu_ ? 8 : 3); ++e)
        for (int k = 0; k < 3; ++k) {
            const std::unique_ptr<RgbaImage> lab = Load(std::string(kLabelBase[e]) + static_cast<char>('0' + k) + ".bin");
            if (!lab || lab->width != kLabelSize || lab->height != kLabelSize || lab->logicalWidth != 0) continue;
            const int lx = kLabelX0 + kLabelStep * k, ly = kLabelY[e];
            for (int y = 0; y < kLabelSize; ++y)
                for (int x = 0; x < kLabelSize; ++x) {
                    const uint8_t* s = &lab->pixels[(static_cast<size_t>(y) * kLabelSize + x) * 4];
                    const float* d = &bg[(static_cast<size_t>(ly + y) * kBgW + lx + x) * 3];
                    const float diff = std::max({ std::fabs(d[0] - s[0]), std::fabs(d[1] - s[1]), std::fabs(d[2] - s[2]) });
                    if (diff > kBodyDiff) {
                        body[static_cast<size_t>(ly + y) * kBgW + lx + x] = 1;
                        rc.Add(lx + x, ly + y);
                    }
                }
        }
    if (rc.Empty()) {
        cacheTiles();
        return;
    }
    rc.Grow(kHaloRadius + kDilate + 2);
    auto lum = [&](int x, int y) {
        const float* d = &bg[(static_cast<size_t>(y) * kBgW + x) * 3];
        return (d[0] + d[1] + d[2]) / 3.0f;
    };
    // the dark halo: within kHaloRadius of the body and near-black in the background
    std::vector<uint8_t> mask = Dilate(body, kHaloRadius, rc);
    for (int y = rc.y0; y < rc.y1; ++y)
        for (int x = rc.x0; x < rc.x1; ++x) {
            const size_t i = static_cast<size_t>(y) * kBgW + x;
            if (mask[i] && !body[i] && lum(x, y) >= kHaloLum) mask[i] = 0;
        }
    mask = Dilate(mask, kDilate, rc);
    // smooth fill: Jacobi averaging from the known neighbours (a masked pixel counts as known once it has a value),
    // until the largest change is below a quarter of a level
    std::vector<float> cur = bg;
    std::vector<uint8_t> known(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) {
        known[i] = mask[i] ? 0 : 1;
        if (mask[i]) cur[i * 3] = cur[i * 3 + 1] = cur[i * 3 + 2] = 0.0f;
    }
    std::vector<float> next = cur;
    std::vector<uint8_t> nextKnown = known;
    for (int it = 0; it < 400; ++it) {
        float maxDelta = 0.0f;
        for (int y = rc.y0; y < rc.y1; ++y)
            for (int x = rc.x0; x < rc.x1; ++x) {
                const size_t i = static_cast<size_t>(y) * kBgW + x;
                if (!mask[i]) continue;
                float acc[3] = { 0, 0, 0 };
                int cnt = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || xx >= kBgW || yy < 0 || yy >= kBgH) continue;
                        const size_t j = static_cast<size_t>(yy) * kBgW + xx;
                        if (!known[j]) continue;
                        acc[0] += cur[j * 3];
                        acc[1] += cur[j * 3 + 1];
                        acc[2] += cur[j * 3 + 2];
                        ++cnt;
                    }
                if (!cnt) continue;
                for (int c = 0; c < 3; ++c) {
                    const float v = acc[c] / static_cast<float>(cnt);
                    if (known[i]) maxDelta = std::max(maxDelta, std::fabs(v - cur[i * 3 + c]));
                    next[i * 3 + c] = v;
                }
                nextKnown[i] = 1;
            }
        cur.swap(next);
        known.swap(nextKnown);
        next = cur;
        nextKnown = known;
        if (it > 5 && maxDelta < 0.25f) break;
    }
    // mirrored detail: bg minus its 3 x 3 mean over the unmasked pixels, read at the reflection of the pixel across
    // its nearest boundary in each of the four directions, weighted 1 / d^2
    if (kDetailGain > 0.0f) {
        const int R = 1;
        std::vector<float> result = cur;
        auto detail = [&](int x, int y, float out[3]) {
            float acc[3] = { 0, 0, 0 };
            int cnt = 0;
            for (int dy = -R; dy <= R; ++dy)
                for (int dx = -R; dx <= R; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || xx >= kBgW || yy < 0 || yy >= kBgH) continue;
                    const size_t j = static_cast<size_t>(yy) * kBgW + xx;
                    if (mask[j]) continue;
                    for (int c = 0; c < 3; ++c) acc[c] += bg[j * 3 + c];
                    ++cnt;
                }
            const size_t i = static_cast<size_t>(y) * kBgW + x;
            for (int c = 0; c < 3; ++c) out[c] = cnt ? bg[i * 3 + c] - acc[c] / static_cast<float>(cnt) : 0.0f;
        };
        static const int kDir[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
        for (int y = rc.y0; y < rc.y1; ++y)
            for (int x = rc.x0; x < rc.x1; ++x) {
                const size_t i = static_cast<size_t>(y) * kBgW + x;
                if (!mask[i]) continue;
                float num[3] = { 0, 0, 0 }, den = 0.0f;
                for (const auto& dir : kDir) {
                    int d = 0;
                    for (int s = 1; s < 64; ++s) {
                        const int xx = x + dir[0] * s, yy = y + dir[1] * s;
                        if (xx < 0 || xx >= kBgW || yy < 0 || yy >= kBgH) break;
                        if (!mask[static_cast<size_t>(yy) * kBgW + xx]) {
                            d = s;
                            break;
                        }
                    }
                    if (!d) continue;
                    const int qx = x + dir[0] * 2 * d, qy = y + dir[1] * 2 * d;
                    if (qx < 0 || qx >= kBgW || qy < 0 || qy >= kBgH || mask[static_cast<size_t>(qy) * kBgW + qx]) continue;
                    float dt[3];
                    detail(qx, qy, dt);
                    const float w = 1.0f / static_cast<float>(d * d);
                    for (int c = 0; c < 3; ++c) num[c] += w * dt[c];
                    den += w;
                }
                if (den > 0.0f)
                    for (int c = 0; c < 3; ++c) result[i * 3 + c] = cur[i * 3 + c] + kDetailGain * num[c] / den;
            }
        cur.swap(result);
    }
    // back into the tiles
    for (int i = 0; i < 6; ++i) {
        const int tx = kMenuTilePos[i][0], ty = kMenuTilePos[i][1], tw = kMenuTilePos[i][2];
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < tw; ++x) {
                const size_t j = static_cast<size_t>(ty + y) * kBgW + tx + x;
                if (!mask[j]) continue;
                uint8_t* d = &tiles[i]->pixels[(static_cast<size_t>(y) * 256 + x) * 4];
                for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(cur[j * 3 + c])), 0, 255));
            }
    }
    cacheTiles();
}

std::vector<ModelRenderer::SpriteDraw> FrontScreens::Draws(const game::WorldHud& h) {
    std::vector<ModelRenderer::SpriteDraw> out;
    const size_t n = h.screens.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& s = h.screens[i];
        ModelRenderer::SpriteDraw d;
        d.texture = Bitmap(s.file);
        if (!d.texture) continue;
        const uint32_t a = static_cast<uint32_t>(std::clamp(static_cast<int>(s.alpha * 255.0f), 0, 255));
        const float px[4] = { s.x, s.x, s.x + s.w, s.x + s.w }, py[4] = { s.y, s.y + s.h, s.y, s.y + s.h };
        float u0 = 0, v0 = 0, u1 = s.u, v1 = s.v;
        if (s.srcX >= 0) {  // pixel copy (BitBlt): the source rectangle in bitmap pixels
            const float tw = static_cast<float>(d.texture->LogicalWidth()), th = static_cast<float>(d.texture->LogicalHeight());
            u0 = static_cast<float>(s.srcX) / tw;
            v0 = static_cast<float>(s.srcY) / th;
            u1 = (static_cast<float>(s.srcX) + s.w) / tw;
            v1 = (static_cast<float>(s.srcY) + s.h) / th;
        }
        const float pu[4] = { u0, u0, u1, u1 }, pv[4] = { v0, v1, v0, v1 };
        for (int k = 0; k < 4; ++k) d.v[k] = { px[k], py[k], 0.0f, 0.5f, (a << 24) | 0xFFFFFFu, 0, pu[k], pv[k] };
        game::SpriteRecord rec;
        rec.key = a < 255 ? 0x80000000u : 0u;
        rec.flags = 0x700;  // depth test always
        d.isp = game::SpriteIsp(rec);
        d.tsp = game::SpriteTsp(rec);
        d.key = rec.key | 0xF;
        // first in the pass-1 list, in slot / blit order (equal keys and depths would leave the order to qsort)
        d.depth = 1e6f + static_cast<float>(n - 1 - i);
        out.push_back(d);
    }
    return out;
}

}  // namespace port
