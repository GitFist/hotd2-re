#pragma once

#include "exe_image.h"

namespace game {

// 0x0041C9E0 Tex_GetPaletteBank: palette bank used by 4bpp textures of a texture file.
// Per-index tables are read from the exe image. Validated against the emulated original for every
// (file, index) pair in tests/bank_test.cpp.
int GetPaletteBank(const ExeImage& exe, int fileIndex, int texIndex);

}  // namespace game
