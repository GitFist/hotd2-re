#include "sound_table.h"

#include <cstring>

#include "exe_image.h"
#include "typing_reference.h"

namespace game {

namespace {
constexpr uint32_t kSeRecord = 0x34;
constexpr uint32_t kVoiceRecord = 0x24;
constexpr uint32_t kListEnd = 0xFFFFFFFF;
constexpr uint32_t kSeEnd = 0xFFFF;

// BGM ids that play once (0x0041CFD0): OVR_AR, CLR2, HOD1_ADV.
bool BgmPlaysOnce(uint32_t id) { return id == 0x10000009 || id == 0x10000025 || id == 0x10000014; }
}  // namespace

bool SoundTable::Load(const ExeImage& exe) {
    se_.clear();
    voices_.clear();
    bgmA_.clear();
    bgmB_.clear();
    loopIds_.clear();
    loopOffIds_.clear();

    for (uint32_t a = kSeTable; exe.U32(a) != kSeEnd; a += kSeRecord) {
        SeEntry e;
        e.id = exe.U32(a);
        e.file = exe.CStr(a + 4, 0x30);
        if (e.file.empty()) return false;
        se_.push_back(e);
        if (se_.size() > 1000) return false;
    }
    for (int i = 0; i < kVoiceCount; ++i) {
        const uint32_t a = kVoiceTable + i * kVoiceRecord;
        VoiceEntry v;
        exe.Read(a, &v.group, 2);
        v.number = exe.S16(a + 2);
        v.file = exe.CStr(a + 4, 0x20);
        voices_.push_back(v);
    }
    for (int i = 0; i < kBgmCountA; ++i) {
        const uint32_t p = exe.U32(kBgmTableA + 4 * i);
        bgmA_.push_back(p ? exe.CStr(p) : std::string());
    }
    for (int i = 0; i < kBgmCountB; ++i) {
        const uint32_t p = exe.U32(kBgmTableB + 4 * i);
        bgmB_.push_back(p ? exe.CStr(p) : std::string());
    }
    for (int i = 0; exe.U32(kLoopSeList + 4 * i) != kListEnd && i < 64; ++i) {
        loopIds_.push_back(exe.U32(kLoopSeList + 4 * i));
        loopOffIds_.push_back(exe.U32(kLoopOffList + 4 * i));
    }
    return !se_.empty() && !bgmA_.empty();
}

const SeEntry* SoundTable::FindSe(uint32_t id) const {
    // Linear search like the original (first match wins).
    for (const SeEntry& e : se_)
        if (e.id == id) return &e;
    return nullptr;
}

const TypingReferenceSound* SoundTable::TypingSound(uint32_t id) const {
    return typing_ ? typing_->Sound(id) : nullptr;
}

// 0x0041CFD0
SoundRequest ResolveSound(const SoundTable& table, uint32_t id, const SoundContext& ctx) {
    SoundRequest r;
    if (id & kTypingSoundFlag) {
        const uint32_t sourceId = id & ~kTypingSoundFlag;
        const bool bgm = (sourceId >> 28) == 1;
        const uint32_t key = sourceId | (bgm && ctx.gameState == 6 && ctx.altFlag == 0 ? 0x80000000u : 0u);
        const auto* sound = table.TypingSound(key);
        if (!sound || sound->path.empty()) return r;
        r.action = SoundAction::PlayFile;
        r.path = sound->path;
        r.loop = (sound->flags & 1) != 0;
        r.stopLoopedSe = (sound->flags & 2) != 0;
        r.channel = bgm ? kSndBgmChannel : (sourceId >> 28) == 2 ? kSndVoiceChannel :
                    sourceId == 0x21A9 || sourceId == 0x121A9 ? kSndCoinChannel : kSndAutoChannel;
        if ((sourceId >> 28) == 0 && r.channel == kSndAutoChannel) r.dupId = id;
        r.setCurrentBgm = bgm;
        return r;
    }
    switch (id >> 28) {
    case 0: {
        if (id == 0) return r;
        if (id == 0x100A0) {  // Snd_Command(0, 0x100A0): no-op on PC; prints "SS_SND_ALL_SOUND_OFF"
            r.action = SoundAction::AllOff;
            return r;
        }
        const SeEntry* e = table.FindSe(id);
        if (!e) return r;
        r.action = SoundAction::PlayFile;
        r.path = "Sound\\SE\\" + e->file;
        if (id == 0x21A9 || id == 0x121A9) {  // START_COIN\COIN1_16 / START1_22: own channel
            r.channel = kSndCoinChannel;
            return r;
        }
        // Parallel lists: an id in the first plays looped; an id in the second ("_OFF") first stops every
        // looped SE (Snd_StopLoopedSe 0x004AC220). The scan stops at the first hit.
        const auto& loops = table.LoopIds();
        const auto& offs = table.LoopOffIds();
        for (size_t i = 0; i < loops.size(); ++i) {
            if (id == loops[i]) {
                r.loop = true;
                break;
            }
            if (id == offs[i]) {
                r.stopLoopedSe = true;
                break;
            }
        }
        r.channel = kSndAutoChannel;
        r.dupId = id;
        return r;
    }
    case 1: {
        const int i = static_cast<int>(id & 0xFFF);
        const bool alt = ctx.gameState == 6 && ctx.altFlag == 0;
        const auto& t = alt ? table.BgmB() : table.BgmA();
        if (i >= static_cast<int>(t.size()) || t[i].empty()) return r;  // (no bounds check in the original)
        r.action = SoundAction::PlayFile;
        r.path = "Sound\\BGM\\" + t[i];
        r.channel = kSndBgmChannel;
        r.loop = !BgmPlaysOnce(id);
        r.setCurrentBgm = true;
        return r;
    }
    case 2: {
        const int i = static_cast<int>(id & 0xFFF);
        if (i >= static_cast<int>(table.Voices().size())) return r;  // (no bounds check in the original)
        const VoiceEntry& v = table.Voices()[i];
        if (v.number == -1) return r;
        r.action = SoundAction::PlayFile;
        r.path = "Sound\\Voice\\" + v.file;
        r.channel = kSndVoiceChannel;
        return r;
    }
    case 8:
        // 0x0041D3E0: 0x80000001 -> Snd_Command(1..3, 0x1100A0); 0x80000002 -> stop slot 0x009C90AC (= 1, voice);
        // anything else -> stop slot 0x009C90A8 (= 0, BGM). 0x80000000 also clears the current BGM id.
        if (id == 0x80000001) r.action = SoundAction::StopAllSe;
        else if (id == 0x80000002) r.action = SoundAction::StopVoice;
        else r.action = SoundAction::StopBgm;
        r.clearCurrentBgm = id == 0x80000000;
        return r;
    default:
        return r;
    }
}

// Chunk walk of 0x004A3EF0.
bool ParseWavHeader(const uint8_t* data, size_t size, WavInfo& out) {
    size_t pos = 0;
    auto rd32 = [&](uint32_t& v) {
        if (pos + 4 > size) return false;
        memcpy(&v, data + pos, 4);
        pos += 4;
        return true;
    };
    bool haveFmt = false;
    uint32_t tag, len;
    while (rd32(tag)) {
        if (tag == 0x46464952) {  // "RIFF": skip the size, keep scanning inside ("WAVE" is then ignored)
            if (!rd32(len)) return false;
        } else if (tag == 0x45564157) {  // "WAVE"
        } else if (tag == 0x20746D66) {  // "fmt "
            if (!rd32(len)) return false;
            uint8_t f[0x12] = {};
            const size_t n = len < 0x13 ? len : 0x12;
            if (pos + n > size) return false;
            memcpy(f, data + pos, n);
            pos += len;  // the original skips len - 0x12 after reading 0x12; no pad byte handling
            memcpy(&out.formatTag, f + 0, 2);
            memcpy(&out.channels, f + 2, 2);
            memcpy(&out.sampleRate, f + 4, 4);
            memcpy(&out.avgBytesPerSec, f + 8, 4);
            memcpy(&out.blockAlign, f + 12, 2);
            memcpy(&out.bitsPerSample, f + 14, 2);
            haveFmt = true;
        } else if (tag == 0x61746164) {  // "data"
            if (!rd32(len)) return false;
            out.dataOffset = static_cast<uint32_t>(pos);
            out.dataSize = len;
            return haveFmt;
        } else {
            if (!rd32(len)) return false;
            pos += len;
        }
    }
    return false;
}

}  // namespace game
