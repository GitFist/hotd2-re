#pragma once

// Automatic gun input for capture runs (HOTD2_INPUT_AUTO=1: P1, =2: P1 and P2). NOT device input.
//
// Once per boot frame (0x009A5C40) Decide() reads game memory (never writes it) and returns the scripted gun state for a
// player: aim (normalized 0..1, like the HOTD2_INPUT_SCRIPT lines) and buttons (1 trigger, 2 reload, 4 start). The
// input hook turns the decision into a script line, logs it to game\input_auto.txt and applies it through the same path
// as HOTD2_INPUT_SCRIPT, so a later run with HOTD2_INPUT_SCRIPT=game\input_auto.txt reproduces the game exactly.

#include <cstdint>

namespace autoplay {

struct Decision {
    float x = 0.5f, y = 0.5f;  // normalized; the hook maps x * 640 / y * 480 to game pixels (floor)
    uint8_t buttons = 0;       // 1 trigger, 2 reload, 4 start
};

// players: 1 (P1) or 2 (P1 and P2). Returns false if disabled.
void Init(int players);
int Players();

// prev: the decision applied on the previous frame for this player.
Decision Decide(int player, uint32_t frame, const Decision& prev);

// One-line statistics for the hook log (shots, hits, reloads, starts, aim checks).
void LogStats();

}  // namespace autoplay
