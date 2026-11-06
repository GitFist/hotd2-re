#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace port::update {
inline constexpr uint64_t kNaomiContentBytes = 100214497;
inline constexpr const char* kNaomiContentSha256 =
    "e44b7df12a657c84611324971e87875ec66f5cfd10b5356c960e24a6383c7883";

// Uses the same bounded, verified, cancellable transaction as Typing content.
// Existing banks are preserved; scripted launches and overrides are caller-gated.
bool EnsureNaomiContent(const std::string& gameDirectory, std::string* error = nullptr);
namespace detail {
bool ValidateNaomiContentBank(const std::string& path, std::string* error = nullptr);
bool CommitNaomiContent(const std::string& gameDirectory, const std::string& stagingFilename,
                        const std::atomic<bool>& cancelled, std::string* error = nullptr);
}
} // namespace port::update
