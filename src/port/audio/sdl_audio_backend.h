#pragma once

// SDL3 implementation of the port's sound output (AudioOutput / game::SoundBackend), the cross-platform replacement
// of the XAudio2 backend with the same channel behaviour (xaudio_backend.h): one SDL audio stream per original
// DirectSound channel, each on its own logical device (so a channel pauses / resumes exactly where it was), fed from
// the whole PCM data chunk by a get-callback that wraps around for looped channels. A silent master logical device
// opened first at 44100 Hz / stereo asks for the original's primary-buffer format (Snd_Init 0x004ABF50).
// No volume / pan / 3D, like the original. SDL mixes in float and converts mono to stereo by duplication.

#include <memory>

#include "audio_output.h"

namespace port {

std::unique_ptr<AudioOutput> CreateSdlAudio();

}  // namespace port
