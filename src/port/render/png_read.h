#pragma once

// Minimal PNG reader for the asset pack (no external library): zlib inflate (stored, fixed and dynamic Huffman),
// the five scanline filters, colour types gray / gray+alpha / RGB / RGBA / palette (with tRNS), bit depths 1-16
// (16-bit samples keep their top byte), non-interlaced only. Output: RGBA8, rows top-down as in the file.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace port {

struct PngImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4
};

bool ReadPng(const uint8_t* data, size_t size, PngImage& out, std::string* error = nullptr);
bool ReadPngFile(const std::string& path, PngImage& out, std::string* error = nullptr);
// Menu previews decode unselected third-party art without allowing the compressed
// stream to expand past its declared raster. The RGBA output must also fit this
// explicit byte budget. The ordinary asset loader keeps its existing behavior.
bool ReadPngLimited(const uint8_t* data, size_t size, size_t maxRgbaBytes, PngImage& out, std::string* error = nullptr);

// zlib stream (2-byte header, deflate data, adler32 ignored) -> bytes. Returns false on malformed input. sizeHint:
// the expected output size (the buffer is sized to it ahead; 0 = unknown).
bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t sizeHint = 0);

}  // namespace port
