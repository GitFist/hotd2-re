#pragma once

// Read-only view of Hod2.exe for data tables (texture descriptors, etc.), addressed by VA.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "texture.h"

namespace game {

class ExeImage {
public:
    bool Load(const std::string& path);

    bool Read(uint32_t va, void* out, size_t size) const;
    uint32_t U32(uint32_t va) const;
    int16_t S16(uint32_t va) const;
    std::string CStr(uint32_t va, size_t maxLen = 64) const;  // empty if not a mapped ASCII string
    bool InData(uint32_t va) const;  // inside .rdata/.data

private:
    struct Section { uint32_t va, vsize, rawOffset, rawSize; };
    std::vector<uint8_t> file_;
    std::vector<Section> sections_;
    uint32_t imageBase_ = 0;
};

// Texture file tables (see notes/formats.md).
struct TextureFileEntry {
    int index;
    std::string name;       // file in tex\ (e.g. "common.bin")
    uint32_t descTable;     // VA of 16-byte descriptors
    int count;
};

std::vector<TextureFileEntry> ListTextureFiles(const ExeImage& exe);

// Static 16-colour ARGB1555 palette banks (table 0x0057A010, copied to 0x00810200 at startup).
std::vector<std::array<uint16_t, 16>> LoadPaletteBanks(const ExeImage& exe);
TextureDesc ReadTextureDesc(const ExeImage& exe, const TextureFileEntry& f, int i);

}  // namespace game
