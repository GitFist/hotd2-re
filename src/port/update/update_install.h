#pragma once

#include <string>

namespace port::update {

// Returns true only after the helper has acquired the installation lock, shown
// its progress window, and is waiting for this process to exit. The caller must
// then quit normally.
bool StartUpdate(const std::string& tag, std::string* error);

enum class Recovery { None, Started, Failed };
// Call before loading game data or optional runtime DLLs. Failed recovery must
// stop startup, so a partly updated installation never reaches gameplay.
Recovery RecoverInterruptedUpdate(std::string* error);

#ifdef _WIN32
// Quotes one argument for CreateProcessW / CommandLineToArgvW.
std::wstring QuoteWindowsArgument(const std::wstring& argument);
#endif

}  // namespace port::update
