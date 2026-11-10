#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "game/sound_table.h"

namespace port {
// Lazy, lossless original-driver music/effect renders. Only the index is resident;
// opening a sound inflates that recording with an exact output-size limit.
class NaomiAudio {
public:
    bool Load(const std::string& path);
    bool Read(const std::string& path, std::vector<uint8_t>& wav) const;
    // Match the existing client's music level at playback; keep the bank and
    // raw reads lossless for source verification. Effects retain native gain.
    bool ReadPlayback(const std::string& path, std::vector<uint8_t>& wav) const;
    const std::vector<game::NaomiSound>& Sounds() const { return sounds_; }
private:
    struct Entry { uint32_t index, offset, packed, bytes, start, end; };
    std::string path_;
    std::vector<Entry> entries_;
    std::vector<game::NaomiSound> sounds_;
};
}  // namespace port
