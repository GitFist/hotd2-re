#pragma once

// Optional client-reported shared records, independent of deterministic gameplay.
// The service uses the configured lobby endpoint, but a distinct socket/protocol.
// Initials are display names, not authenticated identities. Only an explicit user
// publish action should call Submit; reading this client never uploads old saves.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace port::net {

struct LeaderboardEntry {
    std::string name;
    uint32_t score = 0;       // score, or 60-Hz frames for board 3..12
    uint16_t rounds = 0;      // Horde only
    uint8_t difficulty = 0;   // stage reached for Arcade/Original; difficulty otherwise
    uint8_t players = 1;
};
struct LeaderboardSubmission : LeaderboardEntry {
    int table = 0;            // 0 Arcade, 1 Original, 2 Horde, 3..12 bosses/rush
    // Local-only positive monotonic completion token. A new real run needs a
    // new token even when every visible value matches. Retrying this token is
    // idempotent; once sent, its original initials/payload remain frozen.
    uint64_t completionToken = 0;
};

class LeaderboardClient {
public:
    LeaderboardClient();
    ~LeaderboardClient();
    LeaderboardClient(const LeaderboardClient&) = delete;
    LeaderboardClient& operator=(const LeaderboardClient&) = delete;

    // Resolves DNS on a worker; returns immediately. No request goes out until
    // Query or Submit. Reconfigure invalidates outstanding requests and results.
    bool Configure(const std::string& host, uint16_t port, uint32_t build, std::string* error = nullptr);
    void Query(int table);   // replaces an old query; ignored while a submission is pending
    bool Submit(const LeaderboardSubmission& entry, std::string* error = nullptr);
    void Poll();              // nonblocking; retry/timeout and result collection
    bool Busy() const;
    bool Ready() const;       // completed query, including a reported error
    bool SubmissionFinished() const;
    bool LastSubmitAccepted() const; // stored or duplicate acknowledgement
    uint64_t Revision() const;
    int Table() const;
    const std::vector<LeaderboardEntry>& Entries() const;
    const std::string& Error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace port::net
