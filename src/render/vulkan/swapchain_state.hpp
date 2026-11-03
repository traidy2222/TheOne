#pragma once
#include <vulkan/vulkan.h>
#include <vector>
namespace one::vulkan {

struct SwapchainState {
    uint64_t generation = 0;
    double rebuildMs = 0, idleMs = 0, targetsMs = 0;
    bool vsync = true;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkSemaphore> presentReady;
    VkExtent2D extent{};
    uint32_t minImages = 2;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    bool captureSupported = false, resized = true, resizeRequested = false;
};
} // namespace one::vulkan
