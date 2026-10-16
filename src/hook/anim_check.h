#pragma once

#include <cstdint>

// Runtime check of game::EvaluatePose against the original node transforms (HOTD2_ANIMCHECK=1).
struct AnimCheckHook {
    uintptr_t address;
    void* replacement;
    void** original;
};

bool AnimCheckEnabled();
void AnimCheckHooks(AnimCheckHook out[3]);
void AnimCheckReport();
