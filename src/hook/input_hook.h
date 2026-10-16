#pragma once

// 0x0041E530 Input_Poll replacement: original poll + per-player raw-input guns (HOTD2_RAWINPUT=1).
bool InputHookEnabled();
void* InputHookReplacement();
void** InputHookOriginal();
