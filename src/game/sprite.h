#pragma once

// HOTD2 2D sprites: screen-space textured quads drawn through the model pass system. See notes/formats.md
// "2D sprites". From Spr_Draw 0x0041C630, Spr_MakeRecord 0x004ACD20, Spr_Submit 0x004A7A40 and
// Spr_DrawRecord 0x004A7AB0. Validated against the original's captured records and vertices (sprite_test).

#include <cstdint>
#include <vector>

#include "game/exe_image.h"

namespace game {

// Definition passed to Spr_Draw (14 words; words 6-9 are not read).
struct SpriteDef {
    int id = 0;               // global sprite id
    float x = 0, y = 0;       // screen position of the anchor (640x480 space)
    float depth = 1;          // eye depth: rhw = 1/depth, also the pass-1 sort key
    float sx = 1, sy = 1;     // scale (1 = texture size)
    int rot = 0;              // 65536 = full turn
    float alpha = 1;
    int blend = -1;           // 0: opaque; 2: always translucent; else translucent unless alpha > 1
    uint32_t flags = 0;       // bits 0-1 / 2-3 anchor x / y (1 left/top, 2 centre, 3 right/bottom; 0 = top-left),
                              // 0x10 / 0x20 flip u / v,
                              // 0x700 depth func, 0x800 fog on, 0x2000 tint with the light colour
    // The light colour 0x007E7998 a flags-0x2000 sprite is tinted with, when the drawing code set it (0x004AA0A0 before
    // the draw, e.g. the options screen's selected line); not part of the 14 words. tinted = false: the scene's colour.
    bool tinted = false;
    float tint[3] = { 1, 1, 1 };
    uint32_t source = 0;  // tests: the original drawing function (game::World select screens / name entry); not drawn
    // Authored atlas quads (the PC typing result screen) select a subrectangle
    // while retaining the sprite's normal texture, sizing and residency rules.
    bool atlas = false;
    float uv[4] = {0, 0, 1, 1};
    static SpriteDef FromWords(const uint32_t w[14]);
};

// The 15-word record Spr_MakeRecord builds (and Spr_Submit defers to pass 1 when word 0 has bit 31).
struct SpriteRecord {
    uint32_t key = 0;         // bit 31: translucent
    float depth = 1;
    uint32_t texture = 0;     // global texture id (bit 31 selects the second texture table)
    uint32_t color = 0;       // ARGB
    int rot = 0;
    uint32_t flags = 0;
    float x = 0, y = 0, sx = 1, sy = 1;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    static SpriteRecord FromWords(const uint32_t w[15]);
};

// Pre-transformed vertex (D3DFVF_TLVERTEX, 0x1C4).
struct TLVertex {
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float u, v;
};

// 0x004ACD20. lightColor: the env light colour (0x007E7998, used with flag 0x2000). translucentOff: the
// global bit 0x00810A20 & 1 (forces opaque).
SpriteRecord MakeSpriteRecord(const SpriteDef& def, uint32_t textureId, const float lightColor[3], bool translucentOff = false);

// 0x004A7AB0: triangle-strip quad. zScale / zOffset: the projection terms (0x007E7970 / 0x007E7980; the game's
// projection gives 1.0001 / -0.80008).
void SpriteQuad(const SpriteRecord& rec, int texWidth, int texHeight, float zScale, float zOffset, TLVertex out[4]);
// Supplied English PC typing reference 0x004BF330: screen sprites have a
// piecewise authored depth and rhw=1 rather than the camera's near-plane map.
void TypingSpriteQuad(const SpriteRecord& rec, int texWidth, int texHeight, TLVertex out[4]);

// Pass-1 order (comparator 0x004A8A20, used with CrtQsort): key bits 0-3 ascending, then depth descending.
// key: the deferred-list key (record key | 0x007E78B8; 0xF for every sprite captured so far).
int ComparePass1(uint32_t keyA, float depthA, uint32_t keyB, float depthB);

// Material words Spr_DrawRecord passes to Mdl_ApplyPvrState: ISP (depth func) and TSP (blend, fog, clamp).
uint32_t SpriteIsp(const SpriteRecord& rec);
uint32_t SpriteTsp(const SpriteRecord& rec);

// Title_DrawLogo 0x004960A0 (task started by 0x00496090; sets list priority 0xF via 0x004A7A00): the title
// logo strips 0xAB1..0xAB5, "presented by" 0x8FE, 0xA5A and the copyright line 0x231, via Spr_DrawSimple
// 0x0041C6D0 (alpha 1, blend -1).
std::vector<SpriteDef> TitleLogoSprites();

// "PRESS START BUTTON" (0x004067D0): the definition 0x005A4CE8 (filled by HUD_Init 0x004066D0: depth 0.9999,
// flags 5, alpha 1, blend -1) with id 0x900 at (160, 334), scale 1, drawn while (counter & 0x20) == 0.
// counter = 0x005A4D44: reset to 0 by HUD_Init, then accumulates the frame counter 0x009A5C40.
// (Also requires 0x004C4B90[mode] != 0 and 0x009A5C44 > 3, true in the title and the attract demo.)
SpriteDef PressStartSprite();
inline bool PressStartVisible(uint32_t counter) { return (counter & 0x20) == 0; }

// Static tables: sprite id -> texture file slot (0x0057A5BC, index into the tex name table 0x004D1410) and
// global texture id (0x0057D448).
class SpriteTables {
public:
    bool Load(const ExeImage& exe);
    int TextureFile(int id) const;
    uint32_t TextureId(int id) const;

private:
    const ExeImage* exe_ = nullptr;
};

}  // namespace game
