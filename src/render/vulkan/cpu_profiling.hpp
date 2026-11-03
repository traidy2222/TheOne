#pragma once
#include <theone/render/frame_stats.hpp>
#include <chrono>
namespace one::vulkan {
class CpuStageScope {
    CpuStageSample* result = nullptr;
    std::chrono::steady_clock::time_point started;
    uint64_t cycles = 0;
    double threadMs = 0;
    bool total = false;

  public:
    CpuStageScope(FrameStats& stats, CpuStage stage);
    ~CpuStageScope() { finish(); }
    void finish();
};
} // namespace one::vulkan
