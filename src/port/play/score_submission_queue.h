#pragma once

#include "net/leaderboard.h"
#include <chrono>
#include <deque>
#include <string>

namespace play {

// The saved queue owns immutable names, payloads and run IDs. It is independent
// of local high-score tables and must receive only newly completed eligible runs.
class ScoreSubmissionQueue {
public:
    using Clock = std::chrono::steady_clock;
    ScoreSubmissionQueue()=default;
    ~ScoreSubmissionQueue();
    ScoreSubmissionQueue(const ScoreSubmissionQueue&)=delete;
    ScoreSubmissionQueue& operator=(const ScoreSubmissionQueue&)=delete;
    // A missing file is an empty queue. An unreadable/corrupt file is preserved
    // and disables writes rather than silently discarding pending scores.
    bool Load(const std::string& path);
    // true transfers ownership to this queue, including when a disk error means
    // it must retry saving. Nothing is transmitted before a successful save.
    bool Enqueue(const port::net::LeaderboardSubmission& entry);
    // Polls the client, processes its acknowledgement, and schedules retries.
    // Queries may share this client, but submission ownership stays here.
    void Tick(port::net::LeaderboardClient& client, Clock::time_point now=Clock::now());
    size_t PendingCount() const { return entries_.size(); }
    size_t RejectedCount() const { return rejected_.size(); }
    const std::string& Status() const { return status_; }
    uint64_t Revision() const { return revision_; }

private:
    bool Save();
    void SetStatus(const std::string& status);
    std::deque<port::net::LeaderboardSubmission> entries_;
    struct Rejected { port::net::LeaderboardSubmission entry; uint8_t status=2; };
    std::deque<Rejected> rejected_;
    std::string path_,status_;
    uint64_t revision_=0;
    intptr_t lease_=-1;
    bool writable_=false,dirty_=false,inFlight_=false;
    unsigned failures_=0;
    Clock::time_point nextAttempt_{},nextSave_{};
};

} // namespace play
