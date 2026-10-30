#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace port::update {
inline constexpr uint64_t kTypingContentBytes = 89523650;
inline constexpr const char* kTypingContentSha256 =
    "f0f9ccad3c04add8a6a81edfed7116cc945529097931ac6173ce0bc7f3e6e0de";

// Interactive, one-time data bootstrap for the default game directory. Existing
// ordinary banks cause no HTTP. The caller gates overrides, scripts and opt-out.
// Network/hash work runs on a worker; the native progress window offers Cancel.
// Cancellation returns false with an empty error; failures supply an explanation.
// Both preserve existing content. The caller may continue the base game.
// Non-Windows builds return true without accessing the network or filesystem.
bool EnsureTypingContent(const std::string& gameDirectory, std::string* error = nullptr);

namespace detail {
// The same offline validator/transaction used by the bootstrap. Commit owns its
// .typing-reference-*.part input in gameDirectory and removes it on failure.
bool ValidateTypingContentBank(const std::string& path, std::string* error = nullptr);
bool CommitTypingContent(const std::string& gameDirectory, const std::string& stagingFilename,
                         const std::atomic<bool>& cancelled, std::string* error = nullptr);
}
} // namespace port::update
