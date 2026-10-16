#pragma once

#include <cstddef>
#include <cstdint>

namespace game {

// 0x0040ACD0 Lz_DecompressPrs
// Sega PRS decompression. Writes the decoded stream to dst and returns the number of bytes written.
// Like the original, it trusts the input: dst must be large enough (pol/tex files store the
// decoded size in a u32 header before the stream).
int DecompressPrs(const uint8_t* src, uint8_t* dst);

// Bounds-checked variant for tools and the port. Returns -1 on malformed input or overflow.
int DecompressPrsSafe(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);

}  // namespace game
