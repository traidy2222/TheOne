#include "frame_trace.hpp"
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace one::vulkan {
bool FrameTrace::requested() {
#ifdef _WIN32
    return std::getenv("THEONE_PROFILE_WAITS") != nullptr;
#else
    return false;
#endif
}
bool FrameTrace::supported(VkInstance instance, VkPhysicalDevice physical) {
    auto enumerate = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
    uint32_t count = 0;
    if (!enumerate || enumerate(physical, &count, nullptr) != VK_SUCCESS)
        return false;
    std::vector<VkTimeDomainEXT> domains(count);
    if (enumerate(physical, &count, domains.data()) != VK_SUCCESS)
        return false;
    bool gpu = false, host = false;
    for (auto domain : domains) {
        gpu |= domain == VK_TIME_DOMAIN_DEVICE_EXT;
        host |= domain == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
    }
    return gpu && host;
}
double FrameTrace::hostMs() {
#ifdef _WIN32
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return double(f.QuadPart);
    }();
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return double(value.QuadPart) * 1000.0 / frequency;
#else
    return 0;
#endif
}
void FrameTrace::initialize(VkDevice device) {
    calibrate = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
        vkGetDeviceProcAddr(device, "vkGetCalibratedTimestampsEXT"));
}
void FrameTrace::completed(VkDevice device, float period, uint64_t start, uint64_t end, double submitted,
                           double observed, FrameStats& stats) const {
#ifdef _WIN32
    if (!calibrate || !submitted)
        return;
    VkCalibratedTimestampInfoEXT info[2]{};
    for (auto& i : info)
        i.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    info[0].timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
    info[1].timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
    uint64_t values[2]{}, deviation = 0;
    if (calibrate(device, 2, info, values, &deviation) != VK_SUCCESS)
        return;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    double host = double(values[1]) * 1000.0 / double(frequency.QuadPart);
    auto toHost = [&](uint64_t tick) { return host + double(int64_t(tick - values[0])) * period / 1e6; };
    stats.gpuStartHostMs = toHost(start);
    stats.gpuEndHostMs = toHost(end);
    stats.submitToGpuMs = stats.gpuStartHostMs - submitted;
    stats.gpuDoneToFenceMs = observed - stats.gpuEndHostMs;
    stats.calibrationDeviationMs = double(deviation) / 1e6;
    stats.calibratedTimingAvailable = true;
#endif
}
} // namespace one::vulkan
