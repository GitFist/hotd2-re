#pragma once

// Sound output of the port: a game::SoundBackend (the DirectSound-wrapper channel interface game::SoundDriver drives,
// see game/sound_driver.h) that can be opened on the game directory. Backends: sdl_audio_backend.h (SDL3, every
// platform, the default) and xaudio_backend.h (XAudio2, Windows).

#include <string>

#include "assets/asset_override.h"  // GamePath, SoundPath (the asset pack's copy of a WAV when it has one)
#include "game/sound_driver.h"

namespace port {

class AudioOutput : public game::SoundBackend {
public:
    virtual const char* Name() const = 0;
    virtual bool Init(const std::string& gameDir) = 0;  // paths given to Open are relative to gameDir
};

}  // namespace port
