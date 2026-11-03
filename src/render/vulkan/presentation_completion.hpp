#pragma once
#include <vulkan/vulkan.h>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include "presentation_sample.hpp"
namespace one::vulkan {
// Driver waits may block even with timeout=0. Only this worker calls them in the
// normal path. reset() joins before the owner destroys the swapchain or device.
class PresentationCompletion {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    bool stopping = false;
    uint64_t requested = 0, completed = 0;
    VkResult failure = VK_SUCCESS;
    PresentationCompletionSample last;
    void run(VkDevice device, VkSwapchainKHR swapchain, PFN_vkWaitForPresentKHR wait);
  public:
    ~PresentationCompletion() { reset(); }
    void request(VkDevice device, VkSwapchainKHR swapchain, PFN_vkWaitForPresentKHR wait, uint64_t id);
    VkResult status(uint64_t id);
    PresentationCompletionSample sample();
    void reset();
};
} // namespace one::vulkan
