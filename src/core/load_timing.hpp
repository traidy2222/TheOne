#pragma once
#include <chrono>
namespace one::detail {
// Optional profiling: no clock reads on uninstrumented loads.
class LoadTiming {
    using Clock = std::chrono::steady_clock;
    bool enabled_;
    Clock::time_point start_;

  public:
    explicit LoadTiming(bool enabled) : enabled_(enabled) {
        if (enabled_)
            start_ = Clock::now();
    }
    void record(double* result) {
        if (!enabled_)
            return;
        auto end = Clock::now();
        *result += std::chrono::duration<double, std::milli>(end - start_).count();
        start_ = end;
    }
};
} // namespace one::detail
