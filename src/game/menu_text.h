#pragma once

// The menu screens' text and background of the original (the options screen 0x004869E0 and the other mode-select
// screens), as sprite definitions: used by game::World's options screen (world_modes_options.cpp, verified against
// build\scene_capture_modes_options.txt) and by the port's own pause menu (src/port/play/pause_menu.h), which copies
// that look.

#include <cstdint>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/sprite.h"

namespace game {

// 0x00487CA0(x, y, text, flags): text in character cells (16 x 24 px: 0x0055DCF0 / 0x004ECB84). flags: 2 centred on
// x 320, 4 raw pixel position and scale 0.8, 8 half alpha, 0x10 red, 0x40 grey, 0x80 no 'C' / 'G' shift, 0x100 depth
// 0.9. Glyph ids 0x0056AED0[c] (lower case 7 px lower), '~' = 0x7F2; tinted sprites (flags 0x2000). Appends to out.
void MenuText(const ExeImage& exe, float x, float y, const std::string& text, uint32_t flags, std::vector<SpriteDef>& out);

// Port only (not in the original): the same glyphs at a raw pixel position (px, py = the top-left of the first cell)
// scaled uniformly by `scale` (cell advance, the lower-case drop and the 'C' / 'G' shift scale with it). flags as
// MenuText except 2 / 4 (position flags), which are ignored. The modern main menu's entries (world_front.cpp).
void MenuTextScaled(const ExeImage& exe, float px, float py, const std::string& text, uint32_t flags, float scale,
                    std::vector<SpriteDef>& out);
// The advance of one cell of MenuTextScaled (16 px x scale): the width of a text is text.size() x this.
float MenuTextAdvance(const ExeImage& exe, float scale);

// Small previous/next controls made from the original chapter-selection arrow sprite. Coordinates are centres.
void MenuAdjustArrows(float left, float right, float y, std::vector<SpriteDef>& out);

// 0x00487EE0(x, y, value): up to three digits 0x0056AF30[d] at cell (x, y); the third digit at 0x0056AFD0 px.
void MenuNum(const ExeImage& exe, float x, float y, int value, std::vector<SpriteDef>& out);

// 0x00488090(0): the menu screens' background, tiles 0x7A..0x8D on a 128 px grid (5 x 4) at depth 200.
void MenuBackground(std::vector<SpriteDef>& out);

}  // namespace game
