#pragma once

// Plays a World::SoundEvent through game::SoundDriver: Snd_PlayGameSound 0x0041CFD0 with the game mode / type of the
// moment the world issued it (the BGM table choice), or the direct sound-layer calls (Snd_StopAll 0x0041D350,
// Snd_Command 0x004ABF80, Snd_StopSlot 0x0041D4F0).

#include "game/sound_driver.h"
#include "game/world.h"

namespace game {

inline void PlaySoundEvent(SoundDriver& driver, const SoundTable& table, const World::SoundEvent& s) {
    switch (s.kind) {
    case World::kStopAll:
        driver.StopAll();
        break;
    case World::kCommand:
        driver.Command(s.id);
        break;
    case World::kStopSlot:
        driver.StopSlot(static_cast<int>(s.id));
        break;
    default: {
        SoundContext ctx;
        ctx.gameState = s.gameMode;  // 0x009C8E98
        ctx.altFlag = s.gameType;    // 0x009CA08C
        driver.PlayGameSound(table, s.id, ctx);
        break;
    }
    }
}

}  // namespace game
