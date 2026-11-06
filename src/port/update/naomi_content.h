#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace port::update {
inline constexpr uint64_t kNaomiContentBytes = 100214744;
inline constexpr const char* kNaomiContentSha256 =
    "3eac6163eeb37e8b81180e96d91285bf727b78a4640d99d244d9fe6f5c27ad5f";

// Uses the same bounded, verified, cancellable transaction as Typing content.
// Replaces only the verified previous official bank; other existing banks are
// preserved. Scripted launches and overrides are caller-gated.
bool EnsureNaomiContent(const std::string& gameDirectory, std::string* error = nullptr);
namespace detail {
bool ValidateNaomiContentBank(const std::string& path, std::string* error = nullptr);
bool CommitNaomiContent(const std::string& gameDirectory, const std::string& stagingFilename,
                        const std::atomic<bool>& cancelled, std::string* error = nullptr);
}
} // namespace port::update
