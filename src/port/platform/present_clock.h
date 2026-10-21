#pragma once

namespace platform {
// Presentation is independent of simulation and network polling. A missed deadline
// drops presentations instead of introducing a burst of queued frames.
class PresentClock {
public:
    bool Due(double now, int cap) {
        if (cap <= 0) { cap_ = 0; return true; }
        if (cap != cap_ || now < last_) { cap_ = cap; next_ = now; }
        last_ = now;
        if (now + 1e-9 < next_) return false;
        const double interval = 1.0 / cap;
        next_ += interval;
        if (next_ < now) next_ = now + interval;
        return true;
    }
private:
    int cap_ = -1;
    double next_ = 0, last_ = 0;
};
} // namespace platform
