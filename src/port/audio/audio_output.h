#pragma once

// Sound output of the port: a game::SoundBackend (the DirectSound-wrapper channel interface game::SoundDriver drives,
// see game/sound_driver.h) that can be opened on the game directory. Backends: sdl_audio_backend.h (SDL3, every
// platform, the default) and xaudio_backend.h (XAudio2, Windows).

#include <string>
#include <vector>
#include <fstream>

#include "assets/asset_override.h"  // GamePath, SoundPath (the asset pack's copy of a WAV when it has one)
#include "game/sound_driver.h"
#include "game/typing_reference.h"

namespace port {

class AudioOutput : public game::SoundBackend {
public:
    virtual const char* Name() const = 0;
    virtual bool Init(const std::string& gameDir) = 0;  // paths given to Open are relative to gameDir
    void SetTypingReference(const game::TypingReference* reference) { typing_ = reference; }

protected:
    bool ReadSoundBytes(const std::string& gameDir, const std::string& path, std::vector<uint8_t>& out) const {
        if (path.rfind("TypingSound/",0) == 0) {
            const auto* data = typing_ ? typing_->SoundData(path) : nullptr;
            if (!data) return false;
            out = *data; return true;
        }
        std::ifstream file(SoundPath(gameDir,path),std::ios::binary);
        if (!file) return false;
        out.assign(std::istreambuf_iterator<char>(file),{});
        return true;
    }
private:
    const game::TypingReference* typing_ = nullptr;
};

}  // namespace port
