#pragma once

// XAudio2 implementation of game::SoundBackend for the port: one source voice per original DirectSound channel.
// Like the PC original there is no volume / pan / 3D control (the DirectSound wrapper never calls
// SetVolume / SetPan; the 3D interfaces are created but unused).
// Differences: the whole data chunk is loaded at once (the original streams BGM through a 3000 ms ring buffer
// and plays to end of file, i.e. also the few trailing LIST-chunk bytes).

#include <memory>
#include <string>
#include <vector>

#include "audio_output.h"
#include "loop_region.h"

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace port {

class XAudioBackend : public AudioOutput {
public:
    ~XAudioBackend() override;
    const char* Name() const override { return "xaudio2"; }
    bool Init(const std::string& gameDir) override;  // paths given to Open are relative to gameDir
    void Shutdown();

    bool Open(int ch, const std::string& path, bool streamed) override;
    bool Duplicate(int src, int dst) override;
    bool Play(int ch, bool loop) override;
    bool SetLoopRegion(int ch, uint32_t startFrame, uint32_t endFrame, uint32_t sourceRate) override;
    void Pause(int ch) override;
    void Stop(int ch) override;
    void Release(int ch) override;
    bool IsPlaying(int ch) override;
    void SetGain(int ch, float gain) override;  // port volume setting (the source voice's volume)

private:
    struct Pcm {
        std::vector<uint8_t> bytes;
        unsigned char fmt[18] = {};  // WAVEFORMATEX
    };
    struct Channel {
        std::shared_ptr<Pcm> pcm;
        IXAudio2SourceVoice* voice = nullptr;
        bool streamed = false;
        bool queued = false;   // buffer submitted (position valid)
        bool running = false;  // started
        bool loop = false;
        PcmLoopRegion loopRegion;
        float gain = 1.0f;     // port volume setting, applied to each new source voice
    };
    void DestroyVoice(Channel& c);
    bool Submit(Channel& c, bool loop);

    std::string dir_;
    IXAudio2* xa_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    Channel ch_[game::kSndChannels];
    bool comInit_ = false;
};

}  // namespace port
