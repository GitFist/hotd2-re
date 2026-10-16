#include "game/menu_text.h"

#include <cstring>

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

// 0x00488000(id, x, y, depth, sx, sy, rot, alpha, flags): skipped for id < 0; flags 0x2000 = tinted with the light
// colour 0x004AA0A0 set before the draw.
void Spr(std::vector<SpriteDef>& out, int id, float x, float y, float depth, float sx, float sy, int rot, float alpha,
         uint32_t flags, const float tint[3]) {
    if (id < 0) return;
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.alpha = alpha;
    d.blend = -1;
    d.flags = flags;
    if (flags & 0x2000) {
        d.tinted = true;
        memcpy(d.tint, tint, sizeof(d.tint));
    }
    out.push_back(d);
}

}  // namespace

namespace {

// The glyph loop of 0x00487CA0 from a pixel position: the cell advance, the lower-case drop (0x005644FC) and the
// 'C' / 'G' shift (0x004C49C0) are the original's at scale 1 (MenuText) and scale with the glyphs otherwise.
void MenuGlyphs(const ExeImage& exe, float px, float py, const std::string& s, uint32_t flags, float scale, float advance,
                std::vector<SpriteDef>& out) {
    float depth = 1.0f, alpha = 1.0f;
    if (flags & 8) alpha = 0.5f;
    float tint[3] = { 1.0f, 1.0f, 1.0f };
    if (flags & 0x10) {
        tint[1] = tint[2] = 0.0f;
    }
    if (flags & 0x40) tint[0] = tint[1] = tint[2] = 0.6f;
    if (flags & 0x100) depth = 0.9f;
    for (char c : s) {
        float gx = px, gy = py;
        int id = -1;
        const bool lower = c >= 'a' && c <= 'z', upper = c > '@' && c < '[';
        if (lower) {
            gy = py + F32(exe, 0x005644FC) * scale;
            id = exe.S16(0x0056AED0 + static_cast<uint32_t>(c) * 2);
        } else if (upper) {
            id = exe.S16(0x0056AED0 + static_cast<uint32_t>(c) * 2);
            if ((c == 'C' || c == 'G') && !(flags & 0x80)) gx = px - F32(exe, 0x004C49C0) * scale;
        } else if (c == '~') {
            id = 0x7F2;
        } else if (exe.S16(0x0056AED0 + static_cast<uint32_t>(static_cast<uint8_t>(c)) * 2) != 0) {
            id = exe.S16(0x0056AED0 + static_cast<uint32_t>(static_cast<uint8_t>(c)) * 2);
        }
        if (id != -1 || lower || upper || c == '~') Spr(out, id, gx, gy, depth, scale, scale, 1, alpha, 0x2000, tint);
        px = px + advance;
    }
}

}  // namespace

// 0x00487CA0
void MenuText(const ExeImage& exe, float x, float y, const std::string& s, uint32_t flags, std::vector<SpriteDef>& out) {
    const float cw = F32(exe, 0x0055DCF0);  // 16
    float scale = 1.0f;
    float px = (flags & 2) ? F32(exe, 0x004C49CC) - static_cast<float>(static_cast<int>(s.size())) * F32(exe, 0x004C43A0) : x * cw;
    float py = y * F32(exe, 0x004ECB84);
    if (flags & 4) {  // raw position, glyphs at 0.8 (the advance stays one cell)
        scale = 0.8f;
        px = x;
        py = y;
    }
    // flags 4: the original scales the glyphs but not the lower-case drop / 'C' shift: pass scale 1 for those
    if (scale != 1.0f) {
        std::vector<SpriteDef> tmp;
        MenuGlyphs(exe, px, py, s, flags, 1.0f, cw, tmp);
        for (auto& d : tmp) {
            d.sx = d.sy = scale;
            out.push_back(d);
        }
        return;
    }
    MenuGlyphs(exe, px, py, s, flags, 1.0f, cw, out);
}

float MenuTextAdvance(const ExeImage& exe, float scale) { return F32(exe, 0x0055DCF0) * scale; }

void MenuAdjustArrows(float left, float right, float y, std::vector<SpriteDef>& out) {
    for (int side = 0; side < 2; ++side) {
        SpriteDef d;
        d.id = 0xAFF;
        d.x = side ? right : left;
        d.y = y;
        d.sx = d.sy = 0.2f;
        d.depth = 0.8f;
        d.flags = 0xA | (side ? 0x10 : 0);  // centred; the original arrow points left
        out.push_back(d);
    }
}

void MenuTextScaled(const ExeImage& exe, float px, float py, const std::string& s, uint32_t flags, float scale,
                    std::vector<SpriteDef>& out) {
    MenuGlyphs(exe, px, py, s, flags & ~6u, scale, MenuTextAdvance(exe, scale), out);
}

// 0x00487EE0
void MenuNum(const ExeImage& exe, float x, float y, int v, std::vector<SpriteDef>& out) {
    const float cw = F32(exe, 0x0055DCF0);
    x = x * cw;
    y = y * F32(exe, 0x004ECB84);
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    int d = (v % 1000) / 100;
    if (0 < d) Spr(out, exe.S16(0x0056AF30 + static_cast<uint32_t>(d) * 2), x, y, 1, 1, 1, 0, 1, 0, white);
    d = (v % 100) / 10;
    if (0 < d || 99 < v) Spr(out, exe.S16(0x0056AF30 + static_cast<uint32_t>(d) * 2), x + cw, y, 1, 1, 1, 0, 1, 0, white);
    Spr(out, exe.S16(0x0056AF30 + static_cast<uint32_t>(v % 10) * 2), x + static_cast<float>(F64(exe, 0x0056AFD0)), y, 1, 1, 1, 0, 1, 0, white);
}

// 0x00488090(0)
void MenuBackground(std::vector<SpriteDef>& out) {
    int id = 0x7A;
    for (int y = 0; id < 0x8E; y += 0x80) {
        for (int x = 0; x < 0x280; x += 0x80) {
            SpriteDef d;
            d.id = id++;
            d.x = static_cast<float>(x);
            d.y = static_cast<float>(y);
            d.depth = 200.0f;
            out.push_back(d);
        }
    }
}

}  // namespace game
