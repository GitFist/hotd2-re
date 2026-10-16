#pragma once

// Fixed 60 Hz game clock (platform-free): the original runs one game frame per 1/60 s; rendering follows the display.
// Advance(now) returns how many game frames to run for the time elapsed since the previous call. After a stall
// (> 0.25 s) the backlog is dropped instead of fast-forwarding.

namespace platform {

class FixedClock {
public:
    explicit FixedClock(double hz = 60.0) : hz_(hz) {}
    void Reset(double now) {
        last_ = now;
        accumulator_ = 0;
    }
    int Advance(double now) {
        accumulator_ += now - last_;
        last_ = now;
        if (accumulator_ > 0.25) accumulator_ = 0.25;
        const int steps = static_cast<int>(accumulator_ * hz_);
        accumulator_ -= steps / hz_;
        return steps;
    }

private:
    double hz_;
    double last_ = 0, accumulator_ = 0;
};

}  // namespace platform
