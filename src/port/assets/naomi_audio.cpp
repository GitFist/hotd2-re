#include "naomi_audio.h"
#include "render/png_read.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace port {
namespace {
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
constexpr uint32_t kMaxTrack = 60 * 1024 * 1024;
constexpr std::array<uint32_t, 43> kIndices = {0,1,3,5,6,8,9,10,11,12,13,14,15,16,17,18,19,
    0x1aa9,0x25a9,0x21aa9,0x225a9,0x234a9,0x423a9,0x425a9,0x718a9,0x723a9,
    0xb23a9,0xb25a9,0xf19a9,0x171aa9,0x191aa9,0x1a1ba9,0x1b23a9,0x1b25a9,
    0x1c1aa9,0x1f23a9,0x1f25a9,0x221aa9,0x3919a9,0x3925a9,0x3b19a9,0x4d17a9,0x4f17a9};
std::string SoundPath(uint32_t index) { return "NaomiSound/" + std::to_string(index) + ".wav"; }
}

bool NaomiAudio::Load(const std::string& path) {
    path_.clear(); entries_.clear(); sounds_.clear();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto size = f.tellg();
    constexpr uint32_t headerSize = 16 + 24 * kIndices.size();
    if (size < headerSize || size > 512 * 1024 * 1024) return false;
    std::array<uint8_t, headerSize> header{};
    f.seekg(0); f.read(reinterpret_cast<char*>(header.data()), header.size());
    if (!f || std::memcmp(header.data(), "H2NAUDIO", 8) || U32(header.data() + 8) != 1 ||
        U32(header.data() + 12) != kIndices.size()) return false;
    std::vector<Entry> entries;
    uint32_t next = headerSize;
    for (size_t i = 0; i < kIndices.size(); ++i) {
        const uint8_t* p = header.data() + 16 + 24 * i;
        Entry e{U32(p), U32(p+4), U32(p+8), U32(p+12), U32(p+16), U32(p+20)};
        if (e.index != kIndices[i] || e.offset != next || e.packed < 6 || e.packed > kMaxTrack ||
            e.bytes < 48 || e.bytes > kMaxTrack || (e.bytes - 44) % 4 ||
            uint64_t(e.offset) + e.packed > uint64_t(size)) return false;
        const bool oneShot = e.index == 8 || e.index == 9 || e.index == 12 || e.index == 13;
        if (oneShot ? e.start || e.end : e.start >= e.end || e.end > (e.bytes - 44) / 4) return false;
        entries.push_back(e);
        next += e.packed;
    }
    if (next != uint64_t(size)) return false;
    path_ = path; entries_ = std::move(entries);
    for (const auto& e : entries_)
        sounds_.push_back({e.index, SoundPath(e.index), {e.start, e.end, e.end ? 44100u : 0u}});
    return true;
}

bool NaomiAudio::Read(const std::string& path, std::vector<uint8_t>& wav) const {
    wav.clear();
    for (const auto& e : entries_) {
        if (path != SoundPath(e.index)) continue;
        std::ifstream f(path_, std::ios::binary);
        std::vector<uint8_t> packed(e.packed);
        f.seekg(e.offset); f.read(reinterpret_cast<char*>(packed.data()), packed.size());
        if (!f || !ZlibInflateExact(packed.data(), packed.size(), wav, e.bytes)) return false;
        // Canonical PCM16 stereo 44.1 kHz, no untrusted extra chunks.
        if (std::memcmp(wav.data(), "RIFF", 4) || U32(wav.data()+4) != e.bytes-8 ||
            std::memcmp(wav.data()+8, "WAVEfmt ", 8) || U32(wav.data()+16) != 16 ||
            U32(wav.data()+20) != 0x00020001 || U32(wav.data()+24) != 44100 ||
            U32(wav.data()+28) != 176400 || U32(wav.data()+32) != 0x00100004 ||
            std::memcmp(wav.data()+36, "data", 4) || U32(wav.data()+40) != e.bytes-44) {
            wav.clear(); return false;
        }
        return true;
    }
    return false;
}
}  // namespace port
