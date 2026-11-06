#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace port::update {
inline constexpr uint64_t kNaomiAudioContentBytes = 235489234;
inline constexpr const char* kNaomiAudioContentSha256 =
    "bea90d1027d83f22773ac71aed04f76505e9ba9f5324276ae21086ebacb39647";
bool EnsureNaomiAudioContent(const std::string& gameDirectory, std::string* error = nullptr);
namespace detail {
bool ValidateNaomiAudioContentBank(const std::string& path, std::string* error = nullptr);
bool CommitNaomiAudioContent(const std::string& gameDirectory, const std::string& stagingFilename,
                            const std::atomic<bool>& cancelled, std::string* error = nullptr);
}
}
