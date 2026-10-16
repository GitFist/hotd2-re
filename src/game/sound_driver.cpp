#include "sound_driver.h"

#include <cctype>

namespace game {

bool SoundDriver::InRing(int ch) const {
    for (const Slot& s : ring_)
        if (s.channel == ch) return true;
    return false;
}

void SoundDriver::PushRing(int ch, uint32_t id) {
    ring_[ringPos_].id = id;
    ring_[ringPos_].channel = ch;
    if (++ringPos_ > 5) ringPos_ = 0;
}

// 0x004AC020
bool SoundDriver::PlayFile(const std::string& path, bool loop, int channel, uint32_t id) {
    // Free finished SE channels that are not cached in the ring.
    for (int ch = 0; ch < kSndSeChannels; ++ch)
        if (!b_.IsPlaying(ch) && !InRing(ch)) b_.Release(ch);

    int ch = channel;
    if (channel == kSndAutoChannel) {
        ch = -1;
        for (int c = 0; c < kSndSeChannels; ++c) {
            if (!b_.IsPlaying(c) && !InRing(c)) {
                ch = c;
                break;
            }
        }
        if (ch < 0) return false;  // all 15 busy or cached
    } else if (static_cast<uint32_t>(channel) >= kSndChannels) {
        return false;
    }

    // Reuse a cached buffer of the same id (one-shots only): duplicate it into ch and play.
    if (id != 0xFFFFFFFF && !loop) {
        for (const Slot& s : ring_) {
            if (s.id == id && b_.Duplicate(s.channel, ch)) {
                cat_[ch] = Classify(ch, path);  // port volume setting
                ApplyGain(ch);
                b_.Play(ch, false);
                PushRing(s.channel, id);  // the source channel is pushed again, not ch
                return true;
            }
        }
    }

    if (channel == kSndAutoChannel) {
        b_.Open(ch, path, false);  // result ignored by the original
        cat_[ch] = Classify(ch, path);  // port volume setting
        ApplyGain(ch);
        PushRing(ch, id);
        if (loop && nLoops_ < 6) {
            loops_[nLoops_].channel = ch;
            loops_[nLoops_].id = id;
            ++nLoops_;
        }
    } else {
        if (b_.IsPlaying(channel)) {
            b_.Pause(channel);
            b_.Stop(channel);
        }
        b_.Release(channel);
        b_.Open(channel, path, channel == kSndBgmChannel);  // BGM: Snd_OpenChannel(15, path, 1, 3000)
        cat_[channel] = Classify(channel, path);  // port volume setting
        ApplyGain(channel);
    }
    return b_.Play(ch, loop);
}

// port volume settings (not in the original)
SoundDriver::Category SoundDriver::Classify(int channel, const std::string& path) {
    if (channel == kSndBgmChannel) return kMusic;
    std::string u;
    for (char c : path) u += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (u.find("GUN") != std::string::npos) return kGuns;  // COMMON\GUN1..5, DC_SE\TOY_GUN1 / AIR_GUN / MCHN_GUN / SHOT_GUN
    return kSfx;
}

void SoundDriver::SetVolumes(const Volumes& v) {
    vol_ = v;
    for (int ch = 0; ch < kSndChannels; ++ch) ApplyGain(ch);
}

// 0x004AC220
void SoundDriver::StopLoopedSe() {
    for (int i = 0; i < nLoops_; ++i) b_.Stop(loops_[i].channel);
    nLoops_ = 0;
}

// 0x004ABF80
void SoundDriver::Command(uint32_t cmd) {
    switch (cmd) {
    case 0x2A0:
        b_.Pause(kSndBgmChannel);
        return;
    case 0x3A0:
        b_.Play(kSndBgmChannel, true);
        return;
    case 0x300A0:
    case 0x1100A0:
        // The original pauses/stops only the channels that are NOT playing (status test inverted); the release
        // of 0..14 that follows stops everything anyway.
        for (int ch = 0; ch < kSndSeChannels; ++ch) {
            if (!b_.IsPlaying(ch)) {
                b_.Pause(ch);
                b_.Stop(ch);
            }
        }
        for (int ch = 0; ch < kSndSeChannels; ++ch) b_.Release(ch);
        return;
    default:
        return;  // 0x100A0 (all sound off), 0x800A0 / 0x900A0 (stereo / mono?) are ignored on PC
    }
}

// 0x00401000
void SoundDriver::StopSlot(int slot) {
    if (slot == 0) {
        b_.Stop(kSndBgmChannel);
        b_.Release(kSndBgmChannel);
    } else if (slot == 1) {
        b_.Stop(kSndVoiceChannel);
        b_.Release(kSndVoiceChannel);
    } else {
        for (int ch = 0; ch < kSndChannels; ++ch)
            if (b_.IsPlaying(ch)) b_.Stop(ch);
    }
}

// 0x0041D350
void SoundDriver::StopAll() {
    curBgm_ = 0;
    StopSlot(slotBgm_);
    StopSlot(slotVoice_);
    for (int i = 1; i < 8; ++i) Command(0x1100A0);
}

// 0x0049E780 (WM_ACTIVATEAPP)
void SoundDriver::OnActivate(bool active) {
    if (active) {
        Command(0x3A0);
    } else {
        Command(0x2A0);
        StopSlot(1);
        Command(0x300A0);
    }
}

// 0x0041CFD0
void SoundDriver::PlayGameSound(const SoundTable& table, uint32_t id, const SoundContext& ctx) {
    const SoundRequest r = ResolveSound(table, id, ctx);
    switch (r.action) {
    case SoundAction::PlayFile:
        if (r.stopLoopedSe) StopLoopedSe();
        PlayFile(r.path, r.loop, r.channel, r.dupId);
        if (r.setCurrentBgm) curBgm_ = id;
        break;
    case SoundAction::StopBgm:
        StopSlot(slotBgm_);
        if (r.clearCurrentBgm) curBgm_ = 0;
        break;
    case SoundAction::StopAllSe:
        for (int i = 1; i < 4; ++i) Command(0x1100A0);
        break;
    case SoundAction::StopVoice:
        StopSlot(slotVoice_);
        break;
    case SoundAction::AllOff:
        Command(0x100A0);
        break;
    case SoundAction::None:
        break;
    }
}

}  // namespace game
