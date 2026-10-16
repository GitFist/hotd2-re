#include "prs.h"

namespace game {

namespace {

// Control bits are consumed LSB-first; a new control byte is fetched from the data stream
// whenever the current one runs out, exactly as the original does (ah = bits left, al = bits).
template <bool Checked>
int Decode(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    const uint8_t* const srcEnd = src + srcSize;
    uint8_t* const dstStart = dst;
    unsigned bits = 0;
    int bitsLeft = 0;
    bool overrun = false;

    auto readByte = [&]() -> unsigned {
        if constexpr (Checked) {
            if (src >= srcEnd) {
                overrun = true;
                return 0;
            }
        }
        return *src++;
    };
    auto readBit = [&]() -> unsigned {
        if (--bitsLeft < 0) {
            bits = readByte();
            bitsLeft = 7;
        }
        unsigned b = bits & 1;
        bits >>= 1;
        return b;
    };

    for (;;) {
        if (readBit()) {
            unsigned c = readByte();
            if constexpr (Checked) {
                if (overrun || static_cast<size_t>(dst - dstStart) >= dstSize) return -1;
            }
            *dst++ = static_cast<uint8_t>(c);
            continue;
        }

        int offset;
        unsigned size;
        if (readBit()) {
            unsigned w = readByte();
            w |= readByte() << 8;
            if constexpr (Checked) {
                if (overrun) return -1;
            }
            if (w == 0) {
                break;
            }
            offset = static_cast<int>(w >> 3) - 0x2000;
            size = w & 7;
            size = size ? size + 2 : readByte() + 1;
        } else {
            const unsigned hi = readBit();  // separate statements: operand order is unspecified in C++
            const unsigned lo = readBit();
            size = ((hi << 1) | lo) + 2;
            offset = static_cast<int>(readByte()) - 0x100;
        }

        if constexpr (Checked) {
            const size_t written = static_cast<size_t>(dst - dstStart);
            if (overrun || static_cast<size_t>(-offset) > written || written + size > dstSize) return -1;
        }
        // Byte-by-byte on purpose: overlapping copies replicate runs.
        const uint8_t* from = dst + offset;
        while (size--) {
            *dst++ = *from++;
        }
    }
    return static_cast<int>(dst - dstStart);
}

}  // namespace

int DecompressPrs(const uint8_t* src, uint8_t* dst) {
    return Decode<false>(src, SIZE_MAX, dst, SIZE_MAX);
}

int DecompressPrsSafe(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    return Decode<true>(src, srcSize, dst, dstSize);
}

}  // namespace game
