#pragma once
#include <vulkan/vulkan.h>
#include <theone/render/frame_stats.hpp>
namespace one::vulkan {
// Optional diagnostic only: no extension or calibration calls unless requested.
struct FrameTrace {
    PFN_vkGetCalibratedTimestampsEXT calibrate = nullptr;
    static bool requested();
    static bool supported(VkInstance instance, VkPhysicalDevice physical);
    static double hostMs();
    void initialize(VkDevice device);
    void completed(VkDevice device, float period, uint64_t start, uint64_t end, double submitted,
                   double observed, FrameStats& stats) const;
};
} // namespace one::vulkan
