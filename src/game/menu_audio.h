#pragma once

#include <cstdint>

namespace game {

// Sound Preview uses an index into the original recording table, not the packed SE command itself.
inline constexpr uint32_t kOptionSelectSound = 0x000300A9;  // Preview 74: Sound/SE/DC_SE/OK1_22.wav
inline constexpr uint32_t kOptionBackSound = 0x000500A9;    // Preview 76: Sound/SE/DC_SE/RET_OPTION_22.wav
// The original scene-change stop releases channels 0..14. Keep local option feedback on an unused channel.
inline constexpr int kOptionSoundChannel = 18;

}  // namespace game
