#pragma once

#include <cstdint>

// Runtime check of game::ParseModel against the original renderer's strip submissions (HOTD2_MODELCHECK=1).
struct ModelCheckHook {
    uintptr_t address;
    void* replacement;
    void** original;
};

bool ModelCheckEnabled();
void ModelCheckHooks(ModelCheckHook out[4]);
void ModelCheckReport();
