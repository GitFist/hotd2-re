#include "render/png_read.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace port {

namespace {

// --- inflate (RFC 1951), after Mark Adler's puff, with a 9-bit lookup table in front of the canonical decode -------
// The asset pack's textures are up to 2048 x 2048 RGBA (16 MB raw) and are decoded the first time the game draws
// them, so the decoder's speed is the length of a hitch in play: a 64-bit bit buffer, table lookups for the common
// codes and direct writes into a pre-sized output instead of a push_back per byte.

constexpr int kFastBits = 9;

struct BitReader {
    const uint8_t* p;
    size_t n, pos = 0;
    uint64_t buf = 0;
    int cnt = 0;
    bool err = false;

    // Keep at least 57 bits available; bytes past the end read as zero and are caught by the overrun check.
    void Refill() {
        while (cnt <= 56) {
            buf |= static_cast<uint64_t>(pos < n ? p[pos] : 0) << cnt;
            ++pos;
            cnt += 8;
        }
    }
    void Consume(int k) {
        buf >>= k;
        cnt -= k;
        if (pos > n && (pos - n) * 8 > static_cast<size_t>(cnt)) err = true;  // consumed bits beyond the data
    }
    uint32_t Bits(int k) {
        if (k == 0) return 0;
        if (cnt < k) Refill();
        const uint32_t v = static_cast<uint32_t>(buf & ((1ull << k) - 1));
        Consume(k);
        return v;
    }
    // Byte-align: drop the pending bits of the current byte and give the whole buffered bytes back.
    void Align() {
        const int drop = cnt & 7;
        buf >>= drop;
        cnt -= drop;
        pos -= static_cast<size_t>(cnt / 8);
        buf = 0;
        cnt = 0;
        if (pos > n) err = true;
    }
};

struct Huffman {
    uint16_t count[16] = {};
    uint16_t symbol[320] = {};
    uint16_t fast[1 << kFastBits] = {};  // (length << 12) | symbol for codes of up to kFastBits bits, 0 = longer
};

bool Build(Huffman& h, const uint8_t* lengths, int n) {
    memset(h.count, 0, sizeof(h.count));
    memset(h.fast, 0, sizeof(h.fast));
    for (int i = 0; i < n; ++i) h.count[lengths[i]]++;
    h.count[0] = 0;
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
    // canonical codes, bit-reversed (the stream delivers a code MSB first, the reader is LSB first)
    uint32_t next[16] = {};
    uint32_t code = 0;
    for (int len = 1; len < 16; ++len) {
        code = (code + h.count[len - 1]) << 1;
        next[len] = code;
    }
    for (int i = 0; i < n; ++i) {
        const int len = lengths[i];
        if (!len || len > kFastBits) continue;
        uint32_t c = next[len]++, r = 0;
        for (int b = 0; b < len; ++b, c >>= 1) r = (r << 1) | (c & 1);
        for (uint32_t k = r; k < (1u << kFastBits); k += 1u << len) h.fast[k] = static_cast<uint16_t>((len << 12) | i);
    }
    return true;
}

int Decode(BitReader& br, const Huffman& h) {
    if (br.cnt < 16) br.Refill();
    const uint16_t e = h.fast[br.buf & ((1u << kFastBits) - 1)];
    if (e) {
        br.Consume(e >> 12);
        return br.err ? -1 : (e & 0xFFF);
    }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= static_cast<int>(br.Bits(1));
        if (br.err) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

// out is sized ahead; o is the number of bytes written so far.
void Ensure(std::vector<uint8_t>& out, size_t o, size_t more) {
    if (o + more > out.size()) out.resize(std::max(out.size() * 2, o + more + 4096));
}

bool Codes(BitReader& br, const Huffman& lit, const Huffman& dist, std::vector<uint8_t>& out, size_t& o) {
    for (;;) {
        int sym = Decode(br, lit);
        if (sym < 0) return false;
        if (sym < 256) {
            Ensure(out, o, 1);
            out[o++] = static_cast<uint8_t>(sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            const size_t len = kLenBase[sym] + br.Bits(kLenExtra[sym]);
            const int ds = Decode(br, dist);
            if (ds < 0 || ds >= 30) return false;
            const size_t d = kDistBase[ds] + br.Bits(kDistExtra[ds]);
            if (br.err || d > o) return false;
            Ensure(out, o, len);
            uint8_t* dst = out.data() + o;
            const uint8_t* src = dst - d;
            if (d >= len) memcpy(dst, src, len);
            else
                for (size_t i = 0; i < len; ++i) dst[i] = src[i];  // overlapping copies repeat
            o += len;
        }
    }
}

bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t sizeHint) {
    BitReader br{ data, size };
    size_t o = 0;
    out.resize(std::max<size_t>(sizeHint, 4096));
    uint32_t last;
    do {
        last = br.Bits(1);
        const uint32_t type = br.Bits(2);
        if (br.err) return false;
        if (type == 0) {
            br.Align();
            if (br.err || br.pos + 4 > br.n) return false;
            const uint32_t len = br.p[br.pos] | (br.p[br.pos + 1] << 8);
            const uint32_t nlen = br.p[br.pos + 2] | (br.p[br.pos + 3] << 8);
            br.pos += 4;
            if ((len ^ 0xFFFF) != nlen || br.pos + len > br.n) return false;
            Ensure(out, o, len);
            memcpy(out.data() + o, br.p + br.pos, len);
            o += len;
            br.pos += len;
        } else if (type == 1) {
            static Huffman lit, dist;
            static bool built = false;
            if (!built) {
                uint8_t lengths[288];
                int i = 0;
                for (; i < 144; ++i) lengths[i] = 8;
                for (; i < 256; ++i) lengths[i] = 9;
                for (; i < 280; ++i) lengths[i] = 7;
                for (; i < 288; ++i) lengths[i] = 8;
                Build(lit, lengths, 288);
                for (i = 0; i < 30; ++i) lengths[i] = 5;
                Build(dist, lengths, 30);
                built = true;
            }
            if (!Codes(br, lit, dist, out, o)) return false;
        } else if (type == 2) {
            const int nlen = static_cast<int>(br.Bits(5)) + 257, ndist = static_cast<int>(br.Bits(5)) + 1, ncode = static_cast<int>(br.Bits(4)) + 4;
            if (br.err || nlen > 286 || ndist > 30) return false;
            static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<uint8_t>(br.Bits(3));
            Huffman lencode;
            Build(lencode, lengths, 19);
            int index = 0;
            while (index < nlen + ndist) {
                int sym = Decode(br, lencode);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[index++] = static_cast<uint8_t>(sym);
                } else {
                    uint8_t len = 0;
                    int rep;
                    if (sym == 16) {
                        if (index == 0) return false;
                        len = lengths[index - 1];
                        rep = 3 + static_cast<int>(br.Bits(2));
                    } else if (sym == 17) {
                        rep = 3 + static_cast<int>(br.Bits(3));
                    } else {
                        rep = 11 + static_cast<int>(br.Bits(7));
                    }
                    if (index + rep > nlen + ndist) return false;
                    while (rep--) lengths[index++] = len;
                }
            }
            if (lengths[256] == 0) return false;
            Huffman lit, dist;
            Build(lit, lengths, nlen);
            Build(dist, lengths + nlen, ndist);
            if (!Codes(br, lit, dist, out, o)) return false;
        } else {
            return false;
        }
    } while (!last && !br.err);
    out.resize(o);
    return !br.err;
}

uint32_t BE32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int Paeth(int a, int b, int c) {
    const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

bool Fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

}  // namespace

bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t sizeHint) {
    if (size < 2 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20)) return false;
    return Inflate(data + 2, size - 2, out, sizeHint);
}

bool ReadPng(const uint8_t* data, size_t size, PngImage& out, std::string* error) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (size < 8 || memcmp(data, sig, 8) != 0) return Fail(error, "not a PNG");
    size_t pos = 8;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = 0, interlace = 0;
    std::vector<uint8_t> idat, palette, trns;
    bool ihdr = false;
    while (pos + 8 <= size) {
        const uint32_t len = BE32(data + pos);
        const char* type = reinterpret_cast<const char*>(data + pos + 4);
        pos += 8;
        if (pos + len + 4 > size) return Fail(error, "truncated chunk");
        const uint8_t* d = data + pos;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return Fail(error, "bad IHDR");
            w = BE32(d);
            h = BE32(d + 4);
            depth = d[8];
            ctype = d[9];
            interlace = d[12];
            ihdr = true;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            palette.assign(d, d + len);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            trns.assign(d, d + len);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), d, d + len);
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += len + 4;  // data + crc
    }
    if (!ihdr || w == 0 || h == 0 || w > 16384 || h > 16384) return Fail(error, "bad dimensions");
    if (interlace) return Fail(error, "interlaced PNGs are not supported");
    int channels;
    switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return Fail(error, "bad colour type");
    }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) return Fail(error, "bad bit depth");
    if (ctype != 0 && ctype != 3 && depth < 8) return Fail(error, "bad bit depth for colour type");
    const size_t bitsPerPixel = static_cast<size_t>(channels) * depth;
    const size_t stride = (w * bitsPerPixel + 7) / 8;
    const size_t bpp = bitsPerPixel < 8 ? 1 : bitsPerPixel / 8;
    std::vector<uint8_t> raw;
    if (!ZlibInflate(idat.data(), idat.size(), raw, (stride + 1) * h)) return Fail(error, "bad IDAT stream");
    if (raw.size() < (stride + 1) * h) return Fail(error, "short image data");
    // Unfilter in place (each row is preceded by its filter byte); the previous row is already unfiltered.
    const std::vector<uint8_t> zero(stride, 0);
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    out.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* cur = raw.data() + y * (stride + 1) + 1;
        const uint8_t* prev = y ? cur - (stride + 1) : zero.data();
        switch (cur[-1]) {
        case 0: break;
        case 1:
            for (size_t i = bpp; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + cur[i - bpp]);
            break;
        case 2:
            for (size_t i = 0; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + prev[i]);
            break;
        case 3:
            for (size_t i = 0; i < bpp; ++i) cur[i] = static_cast<uint8_t>(cur[i] + (prev[i] >> 1));
            for (size_t i = bpp; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + ((cur[i - bpp] + prev[i]) >> 1));
            break;
        case 4:
            for (size_t i = 0; i < bpp; ++i) cur[i] = static_cast<uint8_t>(cur[i] + prev[i]);  // Paeth(0, b, 0) = b
            for (size_t i = bpp; i < stride; ++i) cur[i] = static_cast<uint8_t>(cur[i] + Paeth(cur[i - bpp], prev[i], prev[i - bpp]));
            break;
        default: return Fail(error, "bad filter type");
        }
        uint8_t* o = out.rgba.data() + static_cast<size_t>(y) * w * 4;
        if (depth == 8 && ctype == 6) {  // the pack's textures: straight copy
            memcpy(o, cur, static_cast<size_t>(w) * 4);
            continue;
        }
        if (depth == 8 && ctype == 2 && trns.empty()) {
            for (uint32_t x = 0; x < w; ++x, o += 4) {
                o[0] = cur[x * 3];
                o[1] = cur[x * 3 + 1];
                o[2] = cur[x * 3 + 2];
            }
            continue;
        }
        for (uint32_t x = 0; x < w; ++x, o += 4) {
            auto sample = [&](int ch) -> int {  // 8-bit sample ch of pixel x
                if (depth == 8) return cur[x * channels + ch];
                if (depth == 16) return cur[(x * channels + ch) * 2];
                const size_t bit = x * depth;
                const int v = (cur[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
                return ctype == 3 ? v : v * 255 / ((1 << depth) - 1);
            };
            switch (ctype) {
            case 0: {
                const int g = sample(0);
                o[0] = o[1] = o[2] = static_cast<uint8_t>(g);
                if (trns.size() >= 2 && depth <= 8 && g == trns[1]) o[3] = 0;
                break;
            }
            case 2:
                o[0] = static_cast<uint8_t>(sample(0));
                o[1] = static_cast<uint8_t>(sample(1));
                o[2] = static_cast<uint8_t>(sample(2));
                if (trns.size() >= 6 && depth == 8 && o[0] == trns[1] && o[1] == trns[3] && o[2] == trns[5]) o[3] = 0;
                break;
            case 3: {
                const size_t idx = static_cast<size_t>(sample(0));
                if (idx * 3 + 2 >= palette.size()) return Fail(error, "palette index out of range");
                o[0] = palette[idx * 3];
                o[1] = palette[idx * 3 + 1];
                o[2] = palette[idx * 3 + 2];
                o[3] = idx < trns.size() ? trns[idx] : 255;
                break;
            }
            case 4:
                o[0] = o[1] = o[2] = static_cast<uint8_t>(sample(0));
                o[3] = static_cast<uint8_t>(sample(1));
                break;
            case 6:
                o[0] = static_cast<uint8_t>(sample(0));
                o[1] = static_cast<uint8_t>(sample(1));
                o[2] = static_cast<uint8_t>(sample(2));
                o[3] = static_cast<uint8_t>(sample(3));
                break;
            }
        }
    }
    return true;
}

bool ReadPngFile(const std::string& path, PngImage& out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Fail(error, "cannot open");
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    return ReadPng(d.data(), d.size(), out, error);
}

}  // namespace port
