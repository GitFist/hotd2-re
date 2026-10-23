#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "game/world_sound.h"
#include "port/play/night_shift_menu.h"

namespace play {

// Equal 5 dB master steps give useful control at quiet levels: 1 = -45 dB,
// 5 = -25 dB, 10 = full scale. Zero remains an exact mute.
inline float MasterVolumeGain(int level) {
    level=std::clamp(level,0,10);
    return level==0?0.f:std::pow(10.f,(level-10)*5.f/20.f);
}

// Category sliders remain linear. The attraction has its own whole-mix control,
// independent of the gameplay sliders, and obeys the same master curve.
inline game::SoundDriver::Volumes GameAudioVolumes(int master,int music,int guns,int sfx) {
    game::SoundDriver::Volumes out;
    const float gain=MasterVolumeGain(master)/10.f;
    out.gain[game::SoundDriver::kMusic]=gain*std::clamp(music,0,10);
    out.gain[game::SoundDriver::kGuns]=gain*std::clamp(guns,0,10);
    out.gain[game::SoundDriver::kSfx]=gain*std::clamp(sfx,0,10);
    return out;
}
inline game::SoundDriver::Volumes AttractionAudioVolumes(int master,int attraction) {
    return GameAudioVolumes(master,attraction,attraction,attraction);
}

// Drives only the separate attraction SoundDriver. Stop/mute never touches menu
// feedback, Extras recordings or the live game's channels. While hidden or in
// Extras, retain current music/ambient state but discard one-shot sound events.
class AttractionAudio {
public:
    void Update(game::SoundDriver& driver,const game::SoundTable& table,const AttractPreview& preview,bool audible) {
        audible=audible&&preview.Get()!=nullptr;
        const bool fresh=revision_!=preview.SoundRevision();
        if(fresh) {
            for(const auto& event:preview.WarmupSounds()) Remember(table,event);
            revision_=preview.SoundRevision();
        }
        if(active_&&!audible)driver.StopAll();
        if(!active_&&audible) {
            if(bgm_)game::PlaySoundEvent(driver,table,*bgm_);
            for(const auto& event:loops_)game::PlaySoundEvent(driver,table,event);
            if(bgmPaused_)driver.Command(0x2A0);
        }
        active_=audible;
        if(fresh)for(const auto& event:preview.Sounds()) {
            Remember(table,event);
            if(audible)game::PlaySoundEvent(driver,table,event);
        }
    }

private:
    void Remember(const game::SoundTable& table,const game::World::SoundEvent& event) {
        if(event.kind==game::World::kStopAll) {
            bgm_.reset();loops_.clear();bgmPaused_=false;
        } else if(event.kind==game::World::kStopSlot) {
            if(event.id==0||event.id>1){bgm_.reset();bgmPaused_=false;}
            if(event.id>1)loops_.clear();
        } else if(event.kind==game::World::kCommand) {
            if(event.id==0x2A0)bgmPaused_=true;
            else if(event.id==0x3A0)bgmPaused_=false;
            else if(event.id==0x300A0||event.id==0x1100A0)loops_.clear();
        } else if(event.kind==game::World::kPlayGameSound) {
            const auto request=game::ResolveSound(table,event.id,{event.gameMode,event.gameType});
            if(request.action==game::SoundAction::StopBgm) {
                bgm_.reset();bgmPaused_=false;
            } else if(request.action==game::SoundAction::StopAllSe)loops_.clear();
            else if(request.action==game::SoundAction::PlayFile) {
                if(request.channel==game::kSndBgmChannel) {
                    bgm_=event;bgmPaused_=false;
                } else {
                    if(request.stopLoopedSe)loops_.clear();
                    if(request.loop) {
                        std::erase_if(loops_,[&](const auto& loop){return loop.id==event.id;});
                        if(loops_.size()<6)loops_.push_back(event);
                    }
                }
            }
        }
    }
    uint64_t revision_=std::numeric_limits<uint64_t>::max();
    bool active_=false,bgmPaused_=false;
    std::optional<game::World::SoundEvent> bgm_;
    std::vector<game::World::SoundEvent> loops_;
};
} // namespace play
