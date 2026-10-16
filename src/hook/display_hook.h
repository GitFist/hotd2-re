#pragma once

// 0x004A6200 HGL_SetDisplayModeByIndex replacement: windowed debug mode (HOTD2_WINDOWED=1).
bool DisplayHookEnabled();
void* DisplayHookReplacement();
void** DisplayHookOriginal();

// Debug: ignore window deactivation so the game keeps running unfocused (HOTD2_KEEPACTIVE=1).
void StartKeepActive();
