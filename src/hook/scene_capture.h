#pragma once

#include <cstdint>

// Captures camera, model placement and transform data from the original game (HOTD2_SCENECAP=1).
struct SceneCaptureHook {
    uintptr_t address;
    void* replacement;
    void** original;
};

bool SceneCaptureEnabled();
void SceneCaptureHooks(SceneCaptureHook out[20]);  // out[19]: _RES, out[4]: _FILTER, out[5..7]: _SPRITES, out[8]: _TASKS (rand), out[9]: _PASS1, out[10]: _SOUNDS, out[11]: _ALLOCS, out[12..13]: _CULL, out[14..18]: model submits (ids)
void SceneCaptureClose();
