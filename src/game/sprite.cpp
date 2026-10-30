#include "game/sprite.h"

#include <cmath>
#include <cstring>

namespace game {

namespace {

float F(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

// __ftol (0x004ACF50): truncation toward zero.
uint32_t Ftol(double v) { return static_cast<uint32_t>(static_cast<int64_t>(v)); }

// 0x00598B78: corner positions of the strip in anchor units (0..4 across the quad, 2 = centre).
const int kCorner[4][2] = { { 1, 3 }, { 3, 3 }, { 1, 1 }, { 3, 1 } };

}  // namespace

SpriteDef SpriteDef::FromWords(const uint32_t w[14]) {
    SpriteDef d;
    d.id = static_cast<int>(w[0]);
    d.x = F(w[1]);
    d.y = F(w[2]);
    d.depth = F(w[3]);
    d.sx = F(w[4]);
    d.sy = F(w[5]);
    d.rot = static_cast<int>(w[10]);
    d.alpha = F(w[11]);
    d.blend = static_cast<int>(w[12]);
    d.flags = w[13];
    return d;
}

SpriteRecord SpriteRecord::FromWords(const uint32_t w[15]) {
    SpriteRecord r;
    r.key = w[0];
    r.depth = F(w[1]);
    r.texture = w[3];
    r.color = w[4];
    r.rot = static_cast<int>(w[5]);
    r.flags = w[6];
    r.x = F(w[7]);
    r.y = F(w[8]);
    r.sx = F(w[9]);
    r.sy = F(w[10]);
    r.u0 = F(w[11]);
    r.v0 = F(w[12]);
    r.u1 = F(w[13]);
    r.v1 = F(w[14]);
    return r;
}

// 0x0041C630 passes the uv rectangle (0, 0, 1, 1); 0x004ACD20 flips it with flags 0x10 / 0x20.
SpriteRecord MakeSpriteRecord(const SpriteDef& def, uint32_t textureId, const float lightColor[3], bool translucentOff) {
    SpriteRecord r;
    r.depth = def.depth;
    r.texture = textureId;
    r.rot = def.rot;
    r.flags = def.flags;
    r.x = def.x;
    r.y = def.y;
    r.sx = def.sx;
    r.sy = def.sy;
    r.u0 = def.atlas ? def.uv[0] : 0;
    r.v0 = def.atlas ? def.uv[1] : 0;
    r.u1 = def.atlas ? def.uv[2] : 1;
    r.v1 = def.atlas ? def.uv[3] : 1;
    if (def.flags & 0x10) {
        r.u0 = 1.0f - r.u0;
        r.u1 = 1.0f - r.u1;
    }
    if (def.flags & 0x20) {
        r.v0 = 1.0f - r.v0;
        r.v1 = 1.0f - r.v1;
    }
    // Translucent unless forced off, blend 0, or (blend != 2 and alpha > 1); opaque sprites use alpha 1.
    float alpha = 1.0f;
    if (!translucentOff && def.blend != 0 && (def.blend == 2 || !(def.alpha > 1.0f))) {
        r.key = 0x80000000u;
        alpha = def.alpha;
    }
    const uint32_t a = Ftol(alpha * 255.0f);
    if (def.flags & 0x2000) {
        r.color = a;
        for (int k = 0; k < 3; ++k) r.color = (r.color << 8) | Ftol(lightColor[k] * 255.0f);
    } else {
        r.color = (a << 24) | 0xFFFFFF;
    }
    return r;
}

void SpriteQuad(const SpriteRecord& rec, int texWidth, int texHeight, float zScale, float zOffset, TLVertex out[4]) {
    const double hx = static_cast<double>(texWidth) * rec.sx * 0.5;                    // x87, not rounded
    const float hy = static_cast<float>(static_cast<double>(texHeight) * rec.sy * 0.5);  // stored as float
    const double angle = rec.rot * 9.587379924285257e-05;                               // 2*pi/65536 (0x004C4370)
    const double c = std::cos(angle), s = std::sin(angle);  // fcos / fsin (extended)
    const double cf = static_cast<float>(c);                 // the game rounds cos to float where marked
    int ax = 1, ay = 1;  // anchor: 1 = left / top, 2 = centre, 3 = right / bottom (flags 0 = top-left)
    if (rec.flags & 0xF) {
        ax = static_cast<int>(rec.flags & 3);
        ay = static_cast<int>((rec.flags >> 2) & 3);
    }
    const float rhw = 1.0f / rec.depth;
    const float z = (zScale * rec.depth + zOffset) * rhw;
    for (int k = 0; k < 4; ++k) {
        double x = rec.x;
        float y = rec.y;
        const int dx = kCorner[k][0] - ax, dy = kCorner[k][1] - ay;
        if (dx != 0) {
            x = dx * static_cast<double>(static_cast<float>(cf * hx)) + x;
            y = static_cast<float>(y - dx * s * hx);
        }
        if (dy != 0) {
            x = dy * static_cast<double>(static_cast<float>(s * hy)) + x;
            y = static_cast<float>(dy * cf * hy + y);
        }
        TLVertex& v = out[k];
        v.x = static_cast<float>(x);
        v.y = y;
        v.z = z;
        v.rhw = rhw;
        v.diffuse = rec.color;
        v.specular = 0;
        v.u = (k & 1) ? rec.u1 : rec.u0;
        v.v = (k & 2) ? rec.v1 : rec.v0;
    }
}

void TypingSpriteQuad(const SpriteRecord& rec, int texWidth, int texHeight, TLVertex out[4]) {
    SpriteRecord layout = rec;
    layout.depth = 1; // XY/UV/colour are independent of depth; avoid division at authored depth0.
    SpriteQuad(layout, texWidth, texHeight, 0, 0, out);
    const float z = rec.depth < 1.0f ? rec.depth * .1f : (1.0f - 1.0f / rec.depth) * .9f + .1f;
    for (int k = 0; k < 4; ++k) { out[k].z = z; out[k].rhw = 1; }
}

int ComparePass1(uint32_t keyA, float depthA, uint32_t keyB, float depthB) {
    const int d = static_cast<int>(keyA & 0xF) - static_cast<int>(keyB & 0xF);
    if (d != 0) return d;
    const double diff = static_cast<double>(depthB) - depthA;  // x87: fld b; fsub a; compare with 0.0
    if (diff == 0) return 0;
    return diff < 0 ? -1 : 1;
}

uint32_t SpriteIsp(const SpriteRecord& rec) {
    uint32_t zf = (rec.flags >> 8) & 7;
    if (zf == 0) zf = 4;
    return zf << 29;  // ZWRITE on (bit 26 clear)
}

uint32_t SpriteTsp(const SpriteRecord& rec) {
    const uint32_t base = (rec.flags & 0x800) ? 0x18000u : 0x818000u;  // clamp u/v; fog off unless 0x800
    return (rec.key & 0x80000000u) ? (base | 0x94100000u) : (base | 0x20000000u);
}

std::vector<SpriteDef> TitleLogoSprites() {
    // Spr_DrawSimple(id, x, y, depth, sx, sy, rot, flags)
    auto simple = [](int id, float x, float y, uint32_t flags) {
        SpriteDef d;
        d.id = id;
        d.x = x;
        d.y = y;
        d.flags = flags;
        return d;
    };
    std::vector<SpriteDef> out;
    for (int i = 0, x = 0x24; x < 0x2A4; ++i, x += 0x80) out.push_back(simple(0xAB1 + i, static_cast<float>(x), 28.0f, 0));
    out.push_back(simple(0x8FE, 372.0f, 379.0f, 0xA));
    out.push_back(simple(0xA5A, 320.0f, 454.0f, 0xE));
    out.push_back(simple(0x231, 218.0f, 426.0f, 0));
    return out;
}

SpriteDef PressStartSprite() {
    // Definition 0x005A4CE8 as HUD_Init 0x004066D0 fills it.
    SpriteDef d;
    uint32_t depth = 0x3F7FF972;  // 0.9999
    memcpy(&d.depth, &depth, 4);
    d.rot = 0;
    d.alpha = 1.0f;
    d.blend = -1;
    d.flags = 5;
    d.id = 0x900;
    d.x = 160.0f;   // 0x43200000
    d.y = 334.0f;   // 0x43A70000
    d.sx = d.sy = 1.0f;
    return d;
}

bool SpriteTables::Load(const ExeImage& exe) {
    exe_ = &exe;
    return true;
}

int SpriteTables::TextureFile(int id) const { return static_cast<int>(exe_->U32(0x0057A5BC + id * 4)); }
uint32_t SpriteTables::TextureId(int id) const { return exe_->U32(0x0057D448 + id * 4); }

}  // namespace game
