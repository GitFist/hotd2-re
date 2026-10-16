#pragma once

// HOTD2 sound ids -> files, from the tables used by Snd_PlayGameSound 0x0041CFD0 (see notes/agents/sound.md).
//
// A sound id is a 32-bit value; the top nibble selects the category:
//   0x0xxxxxxx  SE     exact match in the SE table 0x005845F8 (0x34-byte records: u32 id, char file[0x30]),
//                      file under Sound\SE\. Ids look like (sample << 16) | (bank << 8) | 0xA9 (Dreamcast
//                      sound-driver packing; the PC code only compares whole ids).
//   0x1xxxxxxx  BGM    index = id & 0xFFF into one of two char* tables (0x00580354 "AR" set, 0x005803F8
//                      alternate set), file under Sound\BGM\.
//   0x2xxxxxxx  Voice  index = id & 0xFFF into 0x00580448 (0x24-byte records: u16 group, s16 number, char file[0x20]),
//                      file under Sound\Voice\. number == -1 marks an empty slot.
//   0x8xxxxxxx  stop commands (0x80000000 stop BGM, 0x80000001 stop all SE, 0x80000002 stop voice).
//   0x000100A0  "SS_SND_ALL_SOUND_OFF" (a no-op on PC), 0 = nothing.

#include <cstdint>
#include <string>
#include <vector>

namespace game {

class ExeImage;

struct SeEntry {
    uint32_t id = 0;
    std::string file;  // relative to Sound\SE\ (e.g. "COMMON\SIBUKI8_16.WAV")
};

struct VoiceEntry {
    uint16_t group = 0;   // 2 COM, 3 ORG, 4..9 ST1..ST6 (folder)
    int16_t number = -1;  // -1 = empty slot (Snd_PlayGameSound ignores it); otherwise an unused serial number
    std::string file;     // relative to Sound\Voice\ (e.g. "ST1\01_GA.WAV")
};

class SoundTable {
public:
    static constexpr uint32_t kSeTable = 0x005845F8;
    static constexpr uint32_t kVoiceTable = 0x00580448;
    static constexpr uint32_t kBgmTableA = 0x00580354;   // "_AR" tracks (default)
    static constexpr uint32_t kBgmTableB = 0x005803F8;   // alternate tracks (game state 6 and 0x009CA08C == 0)
    static constexpr uint32_t kLoopSeList = 0x005887FC;  // SE ids played looped, -1 terminated
    static constexpr uint32_t kLoopOffList = 0x005888B0; // parallel list: "_OFF" ids that stop all looped SE
    static constexpr int kVoiceCount = 467;              // records up to the SE table
    static constexpr int kBgmCountA = 41;                // table A runs into table B
    static constexpr int kBgmCountB = 20;                // table B runs into the voice table

    bool Load(const ExeImage& exe);

    const std::vector<SeEntry>& Se() const { return se_; }
    const std::vector<VoiceEntry>& Voices() const { return voices_; }
    const std::vector<std::string>& BgmA() const { return bgmA_; }  // "" = null pointer
    const std::vector<std::string>& BgmB() const { return bgmB_; }
    const std::vector<uint32_t>& LoopIds() const { return loopIds_; }
    const std::vector<uint32_t>& LoopOffIds() const { return loopOffIds_; }

    const SeEntry* FindSe(uint32_t id) const;

private:
    std::vector<SeEntry> se_;
    std::vector<VoiceEntry> voices_;
    std::vector<std::string> bgmA_, bgmB_;
    std::vector<uint32_t> loopIds_, loopOffIds_;
};

// Game state that Snd_PlayGameSound reads.
struct SoundContext {
    int gameState = 0;   // 0x009C8E98
    int altFlag = 0;     // 0x009CA08C (also selects the alternate stage tables)
};

enum class SoundAction {
    None,       // unknown id / empty slot / id 0
    AllOff,     // 0x000100A0: debug string only on PC
    PlayFile,   // play `path` (see channel/loop)
    StopBgm,    // 0x8xxxxxxx except 1/2: stop + release channel 15
    StopAllSe,  // 0x80000001: stop + release SE channels 0..14
    StopVoice,  // 0x80000002: stop + release channel 16
};

// Sound channels (Snd_CreateDirectSound allocates 21 records of 0x44 bytes at 0x007DE5A8).
constexpr int kSndChannels = 21;
constexpr int kSndSeChannels = 15;   // 0..14 picked automatically
constexpr int kSndBgmChannel = 15;   // streamed, 3000 ms buffer
constexpr int kSndVoiceChannel = 16;
constexpr int kSndCoinChannel = 17;  // START_COIN SEs (0x21A9, 0x121A9)
constexpr int kSndAutoChannel = -1;

struct SoundRequest {
    SoundAction action = SoundAction::None;
    std::string path;           // relative to the game dir, as the game builds it ("Sound\SE\...")
    int channel = kSndAutoChannel;
    bool loop = false;
    bool stopLoopedSe = false;  // "_OFF" id: Snd_StopLoopedSe 0x004AC220 runs before the play
    uint32_t dupId = 0xFFFFFFFF;  // id passed to Snd_PlayFile (used to reuse a cached buffer), -1 if none
    bool setCurrentBgm = false; // 0x009C8FB8 = id (BGM) ...
    bool clearCurrentBgm = false; // ... or = 0 (0x80000000)
};

// 0x0041CFD0 Snd_PlayGameSound: decodes an id into what it asks the sound layer to do.
SoundRequest ResolveSound(const SoundTable& table, uint32_t id, const SoundContext& ctx = {});

// WAV header walk as in Snd_OpenChannel 0x004A3EF0: RIFF chunks are scanned until "data"; "fmt " is read
// (first 0x12 bytes). The original static buffers take the data chunk size; streams read to end of file.
struct WavInfo {
    uint16_t formatTag = 0, channels = 0, bitsPerSample = 0, blockAlign = 0;
    uint32_t sampleRate = 0, avgBytesPerSec = 0;
    uint32_t dataOffset = 0, dataSize = 0;
};
bool ParseWavHeader(const uint8_t* data, size_t size, WavInfo& out);

}  // namespace game
