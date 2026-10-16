#include "exe_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace game {

namespace {
constexpr uint32_t kNameTable = 0x004D1410;   // char* per texture file
constexpr uint32_t kDescTable = 0x0055B9B8;   // descriptor array pointer per file
constexpr uint32_t kCountTable = 0x0055C0A8;  // s16 count per file
}  // namespace

bool ExeImage::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    file_.assign(std::istreambuf_iterator<char>(f), {});
    if (file_.size() < 0x200) return false;
    uint32_t peOff;
    memcpy(&peOff, &file_[0x3C], 4);
    if (peOff + 0x18 + 0xE0 > file_.size() || memcmp(&file_[peOff], "PE\0\0", 4) != 0) return false;
    uint16_t nSections, optSize;
    memcpy(&nSections, &file_[peOff + 6], 2);
    memcpy(&optSize, &file_[peOff + 20], 2);
    memcpy(&imageBase_, &file_[peOff + 24 + 28], 4);
    const size_t sec = peOff + 24 + optSize;
    for (int i = 0; i < nSections; ++i) {
        const uint8_t* s = &file_[sec + i * 40];
        Section x;
        memcpy(&x.vsize, s + 8, 4);
        memcpy(&x.va, s + 12, 4);
        memcpy(&x.rawSize, s + 16, 4);
        memcpy(&x.rawOffset, s + 20, 4);
        x.va += imageBase_;
        sections_.push_back(x);
    }
    return true;
}

bool ExeImage::Read(uint32_t va, void* out, size_t size) const {
    for (const Section& s : sections_) {
        if (va >= s.va && va + size <= s.va + std::max(s.vsize, s.rawSize)) {
            const uint32_t rel = va - s.va;
            uint8_t* o = static_cast<uint8_t*>(out);
            for (size_t i = 0; i < size; ++i) {
                // Bytes past the raw data (uninitialized .data) read as zero.
                o[i] = (rel + i < s.rawSize && s.rawOffset + rel + i < file_.size()) ? file_[s.rawOffset + rel + i] : 0;
            }
            return true;
        }
    }
    return false;
}

uint32_t ExeImage::U32(uint32_t va) const {
    uint32_t v = 0;
    Read(va, &v, 4);
    return v;
}

int16_t ExeImage::S16(uint32_t va) const {
    int16_t v = 0;
    Read(va, &v, 2);
    return v;
}

std::string ExeImage::CStr(uint32_t va, size_t maxLen) const {
    std::string s;
    for (size_t i = 0; i < maxLen; ++i) {
        char c;
        if (!Read(va + static_cast<uint32_t>(i), &c, 1)) return {};
        if (c == 0) return s;
        if (c < 0x20 || c > 0x7E) return {};
        s += c;
    }
    return {};
}

bool ExeImage::InData(uint32_t va) const {
    return va >= 0x004C4000 && va < 0x009CC000;
}

std::vector<TextureFileEntry> ListTextureFiles(const ExeImage& exe) {
    std::vector<TextureFileEntry> out;
    int misses = 0;
    for (int n = 0; misses < 8; ++n) {
        const std::string name = exe.CStr(exe.U32(kNameTable + n * 4));
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) {
            misses = 0;
            const uint32_t desc = exe.U32(kDescTable + n * 4);
            const int count = exe.S16(kCountTable + n * 2);
            if (exe.InData(desc) && count > 0 && count <= 4096) {
                out.push_back({ n, name, desc, count });
            }
        } else {
            ++misses;
        }
    }
    return out;
}

std::vector<std::array<uint16_t, 16>> LoadPaletteBanks(const ExeImage& exe) {
    constexpr uint32_t kPaletteTable = 0x0057A010;  // entries: s16 ?, s16 count (0x10), u32 colours*, s32 n
    std::vector<std::array<uint16_t, 16>> banks;
    for (;;) {
        const uint32_t e = kPaletteTable + 2 + static_cast<uint32_t>(banks.size()) * 12;
        const int16_t count = exe.S16(e);
        const uint32_t ptr = exe.U32(e + 2);
        int32_t n = 0;
        exe.Read(e + 6, &n, 4);
        if (count != 0x10 || n < 1 || !exe.InData(ptr)) break;
        std::array<uint16_t, 16> bank{};
        exe.Read(ptr, bank.data(), 32);
        banks.push_back(bank);
    }
    return banks;
}

TextureDesc ReadTextureDesc(const ExeImage& exe, const TextureFileEntry& f, int i) {
    TextureDesc d;
    uint8_t raw[16];
    exe.Read(f.descTable + i * 16, raw, 16);
    memcpy(&d.width, raw + 0, 2);
    memcpy(&d.height, raw + 2, 2);
    memcpy(&d.format, raw + 4, 4);
    memcpy(&d.offset, raw + 8, 4);
    memcpy(&d.id, raw + 12, 4);
    return d;
}

}  // namespace game
