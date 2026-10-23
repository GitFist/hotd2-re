#pragma once

#include <algorithm>
#include <vector>

#include "game/world_sound.h"
#include "play/model_gallery.h"

namespace play {

// Drain even without an audio device. Gallery stops also cover paused/muted
// loops and voices; feedback remains a separate, one-shot UI channel.
inline void ApplyModelGalleryAudio(ModelGallery& gallery, game::SoundDriver* driver,
                                   const game::SoundTable& table) {
    const bool stop=gallery.TakeStopSounds();
    const bool stopLoops=gallery.TakeStopLoops();
    const auto sounds=gallery.TakeSounds();
    const auto feedback=gallery.TakeFeedbackSounds();
    if(!driver) return;
    if(stop) { driver->StopPreviews(); driver->StopLoopedSe(); driver->StopAll(); }
    else if(stopLoops) driver->StopLoopedSe();
    std::vector<uint32_t> startedLoops;
    for(uint32_t id:sounds) {
        const auto cue=game::ResolveSound(table,id);
        if(cue.stopLoopedSe) { driver->StopLoopedSe(); startedLoops.clear(); continue; } // Native _OFF entries have no WAV payload.
        // Unmuting/resuming on the final tick can also wrap the clip in this
        // frame. Both paths request its motor, but only one copy should play.
        if(cue.loop && std::find(startedLoops.begin(),startedLoops.end(),id)!=startedLoops.end()) continue;
        if(cue.action==game::SoundAction::PlayFile && driver->PlayFile(cue.path,cue.loop,cue.channel,id) && cue.loop)
            startedLoops.push_back(id);
    }
    for(uint32_t id:feedback) game::PlayOptionSound(*driver,table,id);
}

} // namespace play
