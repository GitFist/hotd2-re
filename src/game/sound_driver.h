#pragma once

// Channel management of the HOTD2 sound layer, ported from the original on top of an abstract backend
// (the original backend is the DirectSound wrapper 0x004A2F50..0x004A4BE0; see notes/agents/sound.md).
//
// 21 channels: 0..14 are picked automatically for SE, 15 = BGM (streamed), 16 = voice, 17 = START_COIN SEs.
// Snd_PlayFile keeps a 6-entry ring of recently opened SE channels (0x008101C8, (channel, id) pairs) whose
// buffers are kept for reuse: playing an id that is in the ring duplicates that buffer into a free channel
// (IDirectSound::DuplicateSoundBuffer) instead of reopening the file. Looped SEs are remembered (up to 6,
// 0x00810150) so an "_OFF" id can stop them all.

#include <cstdint>
#include <string>

#include "sound_table.h"

namespace game {

class SoundBackend {
public:
    virtual ~SoundBackend() = default;
    // 0x004A3EF0 Snd_OpenChannel(ch, path, flags, ms): load a WAV into the channel. streamed: 3000 ms streaming
    // buffer (only channel 15). Fails if the channel is in use or the file is missing.
    virtual bool Open(int ch, const std::string& path, bool streamed) = 0;
    // 0x004A4BE0 Snd_DuplicateChannel(src, dst): dst must be empty, src must hold a static (non-streamed) buffer.
    virtual bool Duplicate(int src, int dst) = 0;
    // 0x004A34B0 Snd_PlayChannel(ch, loop): (re)starts from the current position; a playing static buffer is
    // rewound first. Returns false if the channel has no buffer.
    virtual bool Play(int ch, bool loop) = 0;
    virtual void Pause(int ch) = 0;      // 0x004A3780 Snd_PauseChannel: stop, keep position
    virtual void Stop(int ch) = 0;       // 0x004A39E0 Snd_StopChannel: stop + rewind (buffer kept)
    virtual void Release(int ch) = 0;    // 0x004A32D0 Snd_ReleaseChannels(ch, ch, 0)
    virtual bool IsPlaying(int ch) = 0;  // 0x004A3280 Snd_GetChannelStatus (DSBSTATUS byte != 0)
    // Not in the original (its DirectSound wrapper never sets a volume): the channel's gain 0..1 for the port's volume
    // settings. Applies to the channel's current buffer and is kept for its next one.
    virtual void SetGain(int ch, float gain) { (void)ch; (void)gain; }
};

class SoundDriver {
public:
    explicit SoundDriver(SoundBackend& backend) : b_(backend) {
        for (uint8_t& c : cat_) c = kSfx;
    }

    // 0x0041CFD0 Snd_PlayGameSound.
    void PlayGameSound(const SoundTable& table, uint32_t id, const SoundContext& ctx = {});

    // 0x004AC020 Snd_PlayFile(path, loop, channel (-1 = auto 0..14), id (-1 = none)). Returns the play result.
    bool PlayFile(const std::string& path, bool loop, int channel, uint32_t id);
    // 0x004AC220 Snd_StopLoopedSe: Snd_StopChannel on every remembered looped SE channel, then forget them.
    void StopLoopedSe();
    // 0x004ABF80 Snd_Command(_, cmd, _): 0x2A0 pause BGM, 0x3A0 resume BGM (looped), 0x300A0 / 0x1100A0 stop and
    // release SE channels 0..14; everything else (0x100A0, 0x800A0, 0x900A0) does nothing.
    void Command(uint32_t cmd);
    // 0x00401000 (via 0x0041D4F0): 0 -> stop + release BGM, 1 -> stop + release voice, other -> stop all playing.
    void StopSlot(int slot);
    // 0x0041D350 Snd_StopAll: current BGM = 0, stop BGM and voice, stop + release SE channels.
    void StopAll();
    // WM_ACTIVATEAPP handling in Game_WndProcHook 0x0049E780.
    void OnActivate(bool active);

    // Port Extras: recordings always play once. Selecting a row restarts it; the Play button resumes a paused
    // recording or starts a finished/new one. Pause/Reset only affect the requested music, never another row.
    bool PreviewMusic(const SoundTable& table, uint32_t id, bool restart);
    void PausePreviewMusic(uint32_t id);
    void ResetPreviewMusic(uint32_t id);
    bool PreviewEffect(const SoundTable& table, uint32_t id);
    void StopPreviews();
    static constexpr int kPreviewEffectChannel = 19;  // independent of BGM, gameplay SE and option feedback

    uint32_t CurrentBgm() const { return curBgm_; }  // 0x009C8FB8

    // The port's volume settings (not in the original): a gain 0..1 per category, applied through SoundBackend::SetGain
    // when a channel gets its buffer (PlayFile) and at once to every channel by SetVolumes. Categories: kMusic = the BGM
    // channel 15; kGuns = the gunshot samples (a file name containing GUN: COMMON\GUN1..5, DC_SE\TOY_GUN1 / AIR_GUN /
    // MCHN_GUN / SHOT_GUN); kSfx = everything else (voices and the coin channel included).
    enum Category : uint8_t { kMusic = 0, kGuns = 1, kSfx = 2 };
    struct Volumes {
        float gain[3] = { 1.0f, 1.0f, 1.0f };  // indexed by Category
    };
    void SetVolumes(const Volumes& v);
    const Volumes& GetVolumes() const { return vol_; }
    static Category Classify(int channel, const std::string& path);

    struct Slot { int channel; uint32_t id; };
    const Slot* Ring() const { return ring_; }
    int LoopCount() const { return nLoops_; }

private:
    bool InRing(int ch) const;
    void PushRing(int ch, uint32_t id);

    SoundBackend& b_;
    Slot ring_[6] = {};      // 0x008101C8 (zero-initialised: channel 0 starts out "cached")
    int ringPos_ = 0;        // 0x008101FC
    Slot loops_[6] = {};     // 0x00810150
    int nLoops_ = 0;         // 0x008101F8
    uint32_t curBgm_ = 0;    // 0x009C8FB8
    int slotBgm_ = 0;        // 0x009C90A8 (set to 0 by 0x0040AD70)
    int slotVoice_ = 1;      // 0x009C90AC (set to 1 by 0x0040AD70)
    uint32_t previewMusicId_ = 0;
    bool previewMusicPaused_ = false;
    bool previewResumeOnActivate_ = false;
    // port volume settings
    void ApplyGain(int ch) { b_.SetGain(ch, vol_.gain[cat_[ch]]); }
    Volumes vol_;
    uint8_t cat_[kSndChannels];  // category of the channel's current buffer
};

}  // namespace game
