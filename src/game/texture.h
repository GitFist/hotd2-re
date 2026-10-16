#pragma once

// HOTD2 texture decoding (Dreamcast PVR texel layouts), from Tex_CreateSurface 0x004AC270.
// Descriptors live in Hod2.exe tables (see notes/formats.md); texel data is the decoded tex\*.bin.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace game {

struct TextureDesc {
    int16_t width = 0;
    int16_t height = 0;
    uint32_t format = 0;  // bits 0-2 pixel format, bits 8-15 layout
    uint32_t offset = 0;  // into the decoded file
    uint32_t id = 0;
};

enum class PixelFormat : uint32_t { Argb1555 = 0, Rgb565 = 1, Argb4444 = 2, Yuv422 = 3 };

enum class TexLayout : uint32_t {
    Twiddled = 0x01,
    Vq = 0x03,
    Pal4Twiddled = 0x05,
    Rect = 0x09,
    RectTwiddled = 0x0D,
};

inline PixelFormat PixelFormatOf(const TextureDesc& d) { return static_cast<PixelFormat>(d.format & 7); }
inline TexLayout LayoutOf(const TextureDesc& d) { return static_cast<TexLayout>((d.format >> 8) & 0xFF); }

// Morton index used by the PVR layouts: y in the even bits, x in the odd bits.
uint32_t Twiddle(uint32_t x, uint32_t y);

// Decodes to row-major 16-bit texels (still in the descriptor's pixel format).
// palette: 16 entries for Pal4Twiddled (the game's runtime palette at 0x00810200), else unused.
// Returns false on unsupported layout or out-of-bounds data.
bool DecodeTexels(const uint8_t* data, size_t size, const TextureDesc& d, const uint16_t* palette,
                  std::vector<uint16_t>& out);

// Palette banks hold ARGB1555; Tex_CreateSurface converts each entry to the texture's surface format.
uint16_t ConvertPaletteEntry(uint16_t argb1555, PixelFormat fmt);

// Converts 16-bit texels to 8-bit RGBA. Returns false for unsupported pixel formats (YUV422).
bool TexelsToRgba(const uint16_t* texels, size_t count, PixelFormat fmt, uint8_t* rgba);

}  // namespace game
