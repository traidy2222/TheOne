#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <span>
namespace one::vulkan {
inline VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> modes, bool vsync) {
    if (!vsync)
        for (auto candidate : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            if (std::find(modes.begin(), modes.end(), candidate) != modes.end())
                return candidate;
    return VK_PRESENT_MODE_FIFO_KHR;
}
} // namespace one::vulkan
