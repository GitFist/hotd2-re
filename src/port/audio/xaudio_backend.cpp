#include "xaudio_backend.h"

#include <windows.h>
#include <xaudio2.h>

#include <cstring>
#include <fstream>

#include "game/sound_table.h"

namespace port {

XAudioBackend::~XAudioBackend() { Shutdown(); }

bool XAudioBackend::Init(const std::string& gameDir) {
    dir_ = gameDir;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) comInit_ = true;
    if (FAILED(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
    // The original sets the primary buffer to 44100 Hz / 16-bit / stereo (Snd_Init 0x004ABF50).
    if (FAILED(xa_->CreateMasteringVoice(&master_, 2, 44100))) return false;
    return true;
}

void XAudioBackend::Shutdown() {
    for (Channel& c : ch_) {
        DestroyVoice(c);
        c.pcm.reset();
    }
    if (master_) master_->DestroyVoice();
    master_ = nullptr;
    if (xa_) xa_->Release();
    xa_ = nullptr;
    if (comInit_) CoUninitialize();
    comInit_ = false;
}

void XAudioBackend::DestroyVoice(Channel& c) {
    if (c.voice) c.voice->DestroyVoice();
    c.voice = nullptr;
    c.queued = c.running = false;
}

// 0x004A3EF0 (file part): RIFF walk, PCM data.
bool XAudioBackend::Open(int ch, const std::string& path, bool streamed) {
    Channel& c = ch_[ch];
    if (!xa_ || c.pcm) return false;  // channel in use
    std::ifstream f(SoundPath(dir_, path), std::ios::binary);  // the asset pack's copy first
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    game::WavInfo w;
    if (!game::ParseWavHeader(d.data(), d.size(), w) || w.formatTag != WAVE_FORMAT_PCM) return false;
    auto pcm = std::make_shared<Pcm>();
    size_t n = w.dataSize;
    if (w.dataOffset + n > d.size()) n = d.size() - w.dataOffset;
    pcm->bytes.assign(d.begin() + w.dataOffset, d.begin() + w.dataOffset + n);
    WAVEFORMATEX fx = {};
    fx.wFormatTag = WAVE_FORMAT_PCM;
    fx.nChannels = w.channels;
    fx.nSamplesPerSec = w.sampleRate;
    fx.nAvgBytesPerSec = w.avgBytesPerSec;
    fx.nBlockAlign = w.blockAlign;
    fx.wBitsPerSample = w.bitsPerSample;
    memcpy(pcm->fmt, &fx, sizeof(fx));
    c.pcm = pcm;
    c.streamed = streamed;
    return true;
}

// 0x004A4BE0
bool XAudioBackend::Duplicate(int src, int dst) {
    if (ch_[dst].pcm || !ch_[src].pcm || ch_[src].streamed) return false;
    ch_[dst].pcm = ch_[src].pcm;
    ch_[dst].streamed = false;
    return true;
}

bool XAudioBackend::Submit(Channel& c, bool loop) {
    DestroyVoice(c);
    if (FAILED(xa_->CreateSourceVoice(&c.voice, reinterpret_cast<const WAVEFORMATEX*>(c.pcm->fmt)))) return false;
    c.voice->SetVolume(c.gain);  // port volume setting
    XAUDIO2_BUFFER b = {};
    b.AudioBytes = static_cast<UINT32>(c.pcm->bytes.size());
    b.pAudioData = c.pcm->bytes.data();
    b.Flags = XAUDIO2_END_OF_STREAM;
    b.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(c.voice->SubmitSourceBuffer(&b))) return false;
    c.queued = true;
    c.loop = loop;
    return true;
}

// 0x004A34B0
bool XAudioBackend::Play(int ch, bool loop) {
    Channel& c = ch_[ch];
    if (!c.pcm) return false;
    const bool resume = c.queued && !c.running && c.voice;  // paused: continue from the current position
    if (!resume || c.loop != loop) {
        // A playing static buffer is rewound; a stopped / finished one starts from the beginning.
        if (!Submit(c, loop)) return false;
    }
    if (FAILED(c.voice->Start(0))) return false;
    c.running = true;
    return true;
}

// 0x004A3780
void XAudioBackend::Pause(int ch) {
    Channel& c = ch_[ch];
    if (c.voice && c.running) c.voice->Stop(0);
    c.running = false;
}

// 0x004A39E0
void XAudioBackend::Stop(int ch) { DestroyVoice(ch_[ch]); }

// port volume setting (game::SoundBackend::SetGain)
void XAudioBackend::SetGain(int ch, float gain) {
    Channel& c = ch_[ch];
    c.gain = gain;
    if (c.voice) c.voice->SetVolume(gain);
}

// 0x004A32D0
void XAudioBackend::Release(int ch) {
    DestroyVoice(ch_[ch]);
    ch_[ch].pcm.reset();
}

// 0x004A3280
bool XAudioBackend::IsPlaying(int ch) {
    Channel& c = ch_[ch];
    if (!c.voice || !c.running) return false;
    XAUDIO2_VOICE_STATE s;
    c.voice->GetState(&s, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return s.BuffersQueued > 0;
}

}  // namespace port
