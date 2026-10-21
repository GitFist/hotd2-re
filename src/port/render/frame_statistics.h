#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace port {
// Recent render delivery intervals: wall time, separate from GPU or game time.
class FrameStatistics {
   public:
    void Observe(double milliseconds) {
        if (!std::isfinite(milliseconds) || milliseconds <= 0) return;
        values_[next_] = milliseconds;
        next_ = (next_ + 1) % values_.size();
        count_ = std::min(count_ + 1, values_.size());
    }
    double Percentile(double fraction) const {
        if (!count_) return 0;
        std::vector<double> sorted(values_.begin(), values_.begin() + count_);
        std::sort(sorted.begin(), sorted.end());
        const auto rank = static_cast<size_t>(std::ceil(std::clamp(fraction, 0., 1.) * count_));
        return sorted[rank ? rank - 1 : 0];
    }

   private:
    std::array<double, 240> values_{};
    size_t next_ = 0, count_ = 0;
};
}  // namespace port
