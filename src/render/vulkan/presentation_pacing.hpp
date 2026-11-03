#pragma once
#include <vulkan/vulkan.h>
#include <span>
#include <vector>
#include "presentation_sample.hpp"
#include <memory>
namespace one::vulkan {
class PresentationCompletion;
struct PresentationWait {
    VkResult result = VK_SUCCESS;
    uint64_t id = 0, timeoutNs = 0;
    uint32_t polls = 0;
    double checkMs = 0, sleepMs = 0;
    PresentationCompletionSample worker;
};
// Bound FIFO presentation backlog independently of resource frame slots.
struct PresentationPacing {
    PresentationPacing();
    ~PresentationPacing();
    VkPhysicalDevicePresentIdFeaturesKHR idFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR waitFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    PFN_vkWaitForPresentKHR waitForPresent = nullptr;
    uint64_t nextId = 1, lastQueued = 0;
    uint64_t waitBudgetNs = 16'000'000;
    bool enabled = false;
    bool referenceBlockingWait = false;
    std::unique_ptr<PresentationCompletion> completion;
    void configure(VkPhysicalDevice physical, std::span<const VkExtensionProperties> available,
                   std::vector<const char*>& extensions);
    void initialize(VkDevice device);
    void resetSwapchain();
    PresentationWait wait(VkDevice device, VkSwapchainKHR swapchain, bool fifo);
    VkResult present(VkQueue queue, VkPresentInfoKHR& info, bool fifo);
};
} // namespace one::vulkan
