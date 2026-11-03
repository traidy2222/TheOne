#pragma once
#include <chrono>
namespace one::vulkan {
class ScopedTiming {
    double& result_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();

  public:
    explicit ScopedTiming(double& result) : result_(result) {}
    ~ScopedTiming() {
        result_ +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    }
};
} // namespace one::vulkan
