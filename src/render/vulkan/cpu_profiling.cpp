#include "cpu_profiling.hpp"
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace one::vulkan {
namespace {
uint64_t threadCycles() {
#ifdef _WIN32
    ULONG64 value = 0;
    QueryThreadCycleTime(GetCurrentThread(), &value);
    return value;
#else
    return 0;
#endif
}
double threadTime() {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
        auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        return double(ticks(kernel) + ticks(user)) / 10000.0;
    }
#endif
    return 0;
}
} // namespace
CpuStageScope::CpuStageScope(FrameStats& stats, CpuStage stage) {
    static const bool enabled = std::getenv("THEONE_PROFILE_CPU") != nullptr;
    if (!enabled)
        return;
    result = &stats.cpuStages[size_t(stage)];
    total = stage == CpuStage::Total;
    cycles = threadCycles();
    if (total)
        threadMs = threadTime();
    started = std::chrono::steady_clock::now();
}
void CpuStageScope::finish() {
    if (!result)
        return;
    result->wallMs +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    result->cycles += threadCycles() - cycles;
    if (total)
        result->threadMs += threadTime() - threadMs;
    result = nullptr;
}
} // namespace one::vulkan
