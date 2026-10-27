#include "sdl_audio_backend.h"
#include "loop_region.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include "game/sound_table.h"

namespace port {

namespace {

class SdlAudioBackend final : public AudioOutput {
public:
    ~SdlAudioBackend() override { Shutdown(); }
    const char* Name() const override { return "sdl"; }
    bool Init(const std::string& gameDir) override;
    void Shutdown();

    bool Open(int ch, const std::string& path, bool streamed) override;
    bool Duplicate(int src, int dst) override;
    bool Play(int ch, bool loop) override;
    bool SetLoopRegion(int ch, uint32_t startFrame, uint32_t endFrame, uint32_t sourceRate) override;
    void Pause(int ch) override;
    void Stop(int ch) override;
    void Release(int ch) override;
    bool IsPlaying(int ch) override;
    void SetGain(int ch, float gain) override;

private:
    struct Pcm {
        std::vector<uint8_t> bytes;
        SDL_AudioSpec spec = {};
        size_t frameBytes = 1;
    };
    struct Channel {
        std::shared_ptr<Pcm> pcm;
        SDL_AudioStream* stream = nullptr;
        bool streamed = false;
        // Read by the audio thread's get-callback; changed under SDL_LockAudioStream.
        size_t pos = 0;        // next byte of pcm to feed
        bool feeding = false;  // started and not at the end
        bool loop = false;
        PcmLoopRegion loopRegion;
        // Main thread only.
        bool queued = false;   // started since the last (re)start / stop: position valid
        bool running = false;  // device resumed
        float gain = 1.0f;     // port volume setting (SetGain), applied to the stream when it exists
    };
    static void SDLCALL Feed(void* user, SDL_AudioStream* stream, int additional, int total);
    bool CreateStream(Channel& c);
    void DestroyStream(Channel& c);

    std::string dir_;
    bool init_ = false;
    SDL_AudioDeviceID master_ = 0;
    Channel ch_[game::kSndChannels];
};

bool SdlAudioBackend::Init(const std::string& gameDir) {
    dir_ = gameDir;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    init_ = true;
    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, 44100 };
    master_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    return master_ != 0;
}

void SdlAudioBackend::Shutdown() {
    for (Channel& c : ch_) {
        DestroyStream(c);
        c.pcm.reset();
        c.loopRegion = {};
    }
    if (master_) SDL_CloseAudioDevice(master_);
    master_ = 0;
    if (init_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    init_ = false;
}

void SDLCALL SdlAudioBackend::Feed(void* user, SDL_AudioStream* stream, int additional, int) {
    Channel& c = *static_cast<Channel*>(user);  // the stream lock is held
    if (!c.feeding || !c.pcm || c.pcm->bytes.empty() || additional <= 0) return;
    const Pcm& p = *c.pcm;
    const bool regionLoop = c.loop && c.loopRegion.Active();
    const size_t end = regionLoop ? size_t(c.loopRegion.end) * p.frameBytes : p.bytes.size();
    const size_t restart = regionLoop ? size_t(c.loopRegion.begin) * p.frameBytes : 0;
    size_t need = (static_cast<size_t>(additional) + p.frameBytes - 1) / p.frameBytes * p.frameBytes;
    while (need > 0) {
        if (c.pos >= end) c.pos = restart;  // prefix once, then the loop region (one-shots stop below)
        const size_t n = std::min(need, end - c.pos);
        SDL_PutAudioStreamData(stream, p.bytes.data() + c.pos, static_cast<int>(n));
        c.pos += n;
        need -= n;
        if (c.pos >= end && !c.loop) {
            // End of a one-shot buffer: flush the resampler's held-back frames so the rest plays out and the queue
            // drains to zero (IsPlaying).
            c.feeding = false;
            SDL_FlushAudioStream(stream);
            return;
        }
    }
}

bool SdlAudioBackend::CreateStream(Channel& c) {
    c.stream = SDL_OpenAudioDeviceStream(master_, &c.pcm->spec, &SdlAudioBackend::Feed, &c);  // a new logical device, paused
    if (c.stream) SDL_SetAudioStreamGain(c.stream, c.gain);
    return c.stream != nullptr;
}

void SdlAudioBackend::DestroyStream(Channel& c) {
    if (c.stream) SDL_DestroyAudioStream(c.stream);  // also closes its logical device
    c.stream = nullptr;
    c.pos = 0;
    c.feeding = c.queued = c.running = false;
}

// 0x004A3EF0 (file part): RIFF walk, PCM data.
bool SdlAudioBackend::Open(int ch, const std::string& path, bool streamed) {
    Channel& c = ch_[ch];
    if (!master_ || c.pcm) return false;  // channel in use
    std::ifstream f(SoundPath(dir_, path), std::ios::binary);  // the asset pack's copy first
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    game::WavInfo w;
    if (!game::ParseWavHeader(d.data(), d.size(), w) || w.formatTag != 1 /* WAVE_FORMAT_PCM */) return false;
    if (w.bitsPerSample != 8 && w.bitsPerSample != 16) return false;
    auto pcm = std::make_shared<Pcm>();
    size_t n = w.dataSize;
    if (w.dataOffset + n > d.size()) n = d.size() - w.dataOffset;
    pcm->bytes.assign(d.begin() + w.dataOffset, d.begin() + w.dataOffset + n);
    pcm->spec.format = w.bitsPerSample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    pcm->spec.channels = w.channels;
    pcm->spec.freq = static_cast<int>(w.sampleRate);
    pcm->frameBytes = std::max<size_t>(1, w.blockAlign);
    c.pcm = pcm;
    c.streamed = streamed;
    c.loopRegion = {};
    if (!CreateStream(c)) {
        c.pcm.reset();
        return false;
    }
    return true;
}

// 0x004A4BE0
bool SdlAudioBackend::Duplicate(int src, int dst) {
    if (ch_[dst].pcm || !ch_[src].pcm || ch_[src].streamed) return false;
    ch_[dst].pcm = ch_[src].pcm;
    ch_[dst].streamed = false;
    ch_[dst].loopRegion = {};
    if (!CreateStream(ch_[dst])) {
        ch_[dst].pcm.reset();
        return false;
    }
    return true;
}

bool SdlAudioBackend::SetLoopRegion(int ch, uint32_t startFrame, uint32_t endFrame, uint32_t sourceRate) {
    Channel& c = ch_[ch];
    if (!c.pcm || !c.stream || c.queued) return false;
    PcmLoopRegion region;
    if (!ResolveLoopRegion(startFrame, endFrame, sourceRate, static_cast<uint32_t>(c.pcm->spec.freq),
                           c.pcm->bytes.size() / c.pcm->frameBytes, region)) return false;
    SDL_LockAudioStream(c.stream);
    c.loopRegion = region;
    SDL_UnlockAudioStream(c.stream);
    return true;
}

// 0x004A34B0
bool SdlAudioBackend::Play(int ch, bool loop) {
    Channel& c = ch_[ch];
    if (!c.pcm || !c.stream) return false;
    const bool resume = c.queued && !c.running;  // paused: continue from the current position
    if (!resume || c.loop != loop) {
        // A playing static buffer is rewound; a stopped / finished one starts from the beginning.
        SDL_LockAudioStream(c.stream);
        SDL_ClearAudioStream(c.stream);
        c.pos = 0;
        c.loop = loop;
        c.feeding = true;
        SDL_UnlockAudioStream(c.stream);
        c.queued = true;
    }
    if (!SDL_ResumeAudioStreamDevice(c.stream)) return false;
    c.running = true;
    return true;
}

// 0x004A3780
void SdlAudioBackend::Pause(int ch) {
    Channel& c = ch_[ch];
    if (c.stream && c.running) SDL_PauseAudioStreamDevice(c.stream);
    c.running = false;
}

// 0x004A39E0: stop + rewind (buffer kept)
void SdlAudioBackend::Stop(int ch) {
    Channel& c = ch_[ch];
    if (!c.stream) return;
    SDL_PauseAudioStreamDevice(c.stream);
    SDL_LockAudioStream(c.stream);
    SDL_ClearAudioStream(c.stream);
    c.pos = 0;
    c.feeding = false;
    SDL_UnlockAudioStream(c.stream);
    c.queued = c.running = false;
}

// 0x004A32D0
void SdlAudioBackend::Release(int ch) {
    DestroyStream(ch_[ch]);
    ch_[ch].pcm.reset();
    ch_[ch].loopRegion = {};
}

// 0x004A3280
bool SdlAudioBackend::IsPlaying(int ch) {
    Channel& c = ch_[ch];
    if (!c.stream || !c.running) return false;
    SDL_LockAudioStream(c.stream);
    const bool playing = c.feeding || SDL_GetAudioStreamQueued(c.stream) > 0;
    SDL_UnlockAudioStream(c.stream);
    return playing;
}

// port volume setting (game::SoundBackend::SetGain)
void SdlAudioBackend::SetGain(int ch, float gain) {
    Channel& c = ch_[ch];
    c.gain = gain;
    if (c.stream) SDL_SetAudioStreamGain(c.stream, gain);
}

}  // namespace

std::unique_ptr<AudioOutput> CreateSdlAudio() { return std::make_unique<SdlAudioBackend>(); }

}  // namespace port
