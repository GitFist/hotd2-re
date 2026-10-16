#pragma once

#include <cstdint>

// One replaced function in the original exe.
//   address     - entry point in the original exe (VA, see notes/symbols.csv)
//   replacement - our rewritten function
//   original    - receives a trampoline to the original, so a rewrite can fall back to it
struct Hook {
    const char* name;
    uintptr_t address;
    void* replacement;
    void** original;
};

// PE timestamp of the Hod2.exe all addresses refer to (2001-05-09, SHA-256 c6b41167...).
constexpr unsigned long kExpectedTimestamp = 0x3AF8ED51;

// Defined in hooks.cpp: the table of every active replacement.
extern Hook g_hooks[];
extern const int g_hookCount;

bool InstallHooks();
void RemoveHooks();
