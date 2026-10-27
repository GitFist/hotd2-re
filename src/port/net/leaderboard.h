#pragma once

// Optional client-reported shared records, independent of deterministic gameplay.
// The service uses the configured lobby endpoint, but a distinct socket/protocol.
// Names are display names, not authenticated identities. Completed live runs may
// be submitted automatically; reading rankings never uploads historical saves.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace port::net {

inline constexpr size_t kLeaderboardMaxNameLength = 12;
// Shared score compatibility is independent of release dates, CPU architecture,
// and multiplayer synchronization. Keep this ID across routine updates; change
// it only when scoring or eligibility changes make existing records incomparable.
inline constexpr uint32_t kLeaderboardCompatibilityId = 1;

struct LeaderboardEntry {
    std::string name;          // 1..12 uppercase ASCII characters; local save initials remain three characters
    uint32_t score = 0;       // score, or 60-Hz frames for board 3..12
    uint16_t rounds = 0;      // Horde only
    uint8_t difficulty = 0;   // stage reached for Arcade/Original; difficulty otherwise
    uint8_t players = 1;
};
struct LeaderboardSubmission : LeaderboardEntry {
    int table = 0;            // 0 Arcade, 1 Original, 2 Horde, 3..12 bosses/rush
    // Local-only positive monotonic completion token. A new real run needs a
    // new token even when every visible value matches. Retrying this token is
    // idempotent; once sent, its original name/payload remain frozen.
    uint64_t completionToken = 0;
    // Persist this before sending queued runs. A supplied ID survives client
    // restarts and does not depend on process-local completion-token ordering.
    std::array<uint8_t,16> runId{};
};

bool ValidLeaderboardSubmission(const LeaderboardSubmission& entry);
std::array<uint8_t,16> NewLeaderboardRunId();

class LeaderboardClient {
public:
    LeaderboardClient();
    ~LeaderboardClient();
    LeaderboardClient(const LeaderboardClient&) = delete;
    LeaderboardClient& operator=(const LeaderboardClient&) = delete;

    // Resolves DNS on a worker; returns immediately. No request goes out until
    // Query or Submit. Reconfigure invalidates outstanding requests and results.
    // The protocol's legacy "build" field carries kLeaderboardCompatibilityId.
    bool Configure(const std::string& host, uint16_t port, uint32_t build, std::string* error = nullptr);
    void Query(int table);   // replaces an old query; ignored while a submission is pending
    bool Submit(const LeaderboardSubmission& entry, std::string* error = nullptr);
    void Poll();              // nonblocking; retry/timeout and result collection
    bool Busy() const;
    bool Ready() const;       // completed query, including a reported error
    bool SubmissionFinished() const;
    bool LastSubmitAccepted() const; // stored or duplicate acknowledgement
    bool LastSubmitRetryable() const; // unavailable/busy; rejected and below-cutoff runs are terminal
    uint8_t LastSubmitStatus() const; // wire status 0..5, or 255 for a transport failure
    uint64_t Revision() const;
    int Table() const;
    const std::vector<LeaderboardEntry>& Entries() const;
    const std::string& Error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace port::net
