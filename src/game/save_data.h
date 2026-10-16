#pragma once

// The original's persistent data (settings, rankings, training / boss grades, original-mode items): one block of
// 0xF4C bytes at 0x009C9120 that Game_Save 0x004011F0 -> 0x004A0BD0 checksums (0x004A06C0, stored at 0x009CA068),
// scrambles (0x004A06F0) and writes as four 0x3DB-byte files disguised as game data (0x004A05C0: names 0x005985C4,
// time stamps copied from pol\st_adver07.bin); Game_Load 0x004A0B60 reads them (0x004A0540) or falls back to the
// defaults 0x00401060 / 0x00401130. notes/agents/modes.md section 2.
//
// The port reads the original's files read-only (LoadOriginal) and keeps its own copy in a separate file (LoadNative /
// SaveNative): the game's files are never written.

#include <cstdint>
#include <string>
#include <vector>

#include "game/exe_image.h"

namespace game {

struct RankEntry {           // 16 bytes (ranking tables 0x0057A354[12], copied to / from the save at 0x009C97A0)
    char name[4] = {};       // +0x00 three letters + 0
    int8_t b4 = 0;           // +0x04 character (arcade / original: sprite 0xA4B + b4 * 2), -1 in boss tables
    int8_t b5 = 0;           // +0x05 stage reached (sprite 0x00530040[b5]) / boss difficulty
    int16_t pad = 0;         // +0x06
    int32_t score = 0;       // +0x08 score (boss tables: time in frames)
    int32_t c = -1;          // +0x0C
};

class SaveData {
public:
    static constexpr uint32_t kBase = 0x009C9120, kSize = 0xF4C;
    static constexpr int kTables = 12;

    // The four files of the original (0x005985C4), relative to the game directory.
    static const char* const kFiles[4];

    // Decodes the original's files (read-only). false (and Defaults) when a file is missing or the checksum / magic
    // 0x009CA05F == 7 fails, as Game_Load 0x004A0B60 does.
    bool LoadOriginal(const std::string& gameDir, const ExeImage& exe, std::string* why = nullptr);
    // 0x00401060 (+ 0x0041C490(0) default rankings, 0x00401130 default settings)
    void Defaults(const ExeImage& exe);
    // 0x00401130: the settings part only (options screen "default")
    void DefaultSettings(const ExeImage& exe);
    // The port's own copy (plain block + checksum; never the game's files).
    bool LoadNative(const std::string& path);
    bool SaveNative(const std::string& path) const;
    std::vector<uint8_t> NativeBytes() const;
    bool Valid() const { return raw_.size() == kSize; }

    uint8_t U8(uint32_t va) const { return In(va, 1) ? raw_[va - kBase] : 0; }
    int8_t S8(uint32_t va) const { return static_cast<int8_t>(U8(va)); }
    void SetU8(uint32_t va, uint8_t v) {
        if (In(va, 1)) raw_[va - kBase] = v;
    }
    int32_t S32(uint32_t va) const;
    void SetS32(uint32_t va, int32_t v);
    RankEntry Entry(int table, int i) const;      // 0x009C97A0 + table * 0xA0 + i * 0x10
    void SetEntry(int table, int i, const RankEntry& e);
    const std::vector<uint8_t>& Raw() const { return raw_; }

    // Field addresses (save block)
    static constexpr uint32_t kRankings = 0x009C97A0;  // 12 tables x 10 entries x 16 bytes
    static constexpr uint32_t kDifficulty = 0x009C9F20, kLives = 0x009C9F21, kCredits = 0x009C9F25;  // -1 free play
    static constexpr uint32_t kTrainGrades = 0x009C9F29;  // 10 bytes (-> 0x009C8D40)
    static constexpr uint32_t kBossGrades = 0x009C9F33;   // 10 bytes (-> 0x009A2C10)
    static constexpr uint32_t kItems = 0x009C9F3D;        // 33 bytes (-> 0x009C90C0)
    static constexpr uint32_t kFlags = 0x009C9F5E;
    static constexpr uint32_t kPlayerCfg = 0x009C9F60;    // 2 x 0x7C (crosshair style +0, calibration ...)

private:
    bool In(uint32_t va, uint32_t n) const { return raw_.size() == kSize && va >= kBase && va + n <= kBase + kSize; }
    std::vector<uint8_t> raw_;
};

}  // namespace game
