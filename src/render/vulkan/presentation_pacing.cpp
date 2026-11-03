#include "presentation_pacing.hpp"
#include "presentation_completion.hpp"
#include "presentation_wait.hpp"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace one::vulkan {
PresentationPacing::PresentationPacing() : completion(std::make_unique<PresentationCompletion>()) {}
PresentationPacing::~PresentationPacing() = default;
void PresentationPacing::configure(VkPhysicalDevice physical,
                                   std::span<const VkExtensionProperties> available,
                                   std::vector<const char*>& extensions) {
    enabled = false;
    referenceBlockingWait = std::getenv("THEONE_REFERENCE_PRESENT_WAIT_BUDGET") != nullptr;
    waitBudgetNs = referenceBlockingWait ? 50'000'000 : 16'000'000;
    if (std::getenv("THEONE_REFERENCE_FIFO_PACING"))
        return;
    auto has = [&](const char* name) {
        return std::any_of(available.begin(), available.end(),
                           [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    if (!has(VK_KHR_PRESENT_ID_EXTENSION_NAME) || !has(VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
        return;
    waitFeatures.pNext = &idFeatures;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &waitFeatures;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    if (!waitFeatures.presentWait || !idFeatures.presentId)
        return;
    enabled = true;
    extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
    extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
}
void PresentationPacing::initialize(VkDevice device) {
    waitForPresent =
        enabled
            ? reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(device, "vkWaitForPresentKHR"))
            : nullptr;
    enabled = enabled && waitForPresent;
}
void PresentationPacing::resetSwapchain() {
    completion->reset();
    lastQueued = 0;
    nextId = 1;
}
PresentationWait PresentationPacing::wait(VkDevice device, VkSwapchainKHR swapchain, bool fifo) {
    if (!enabled || !fifo || lastQueued < 2)
        return {};
    // Allow two queued presentations: one can be executing while the CPU prepares
    // the next frame. Waiting for the older one normally keeps the WSI queue short.
    // A compositor transition can delay present-ID completion for several refreshes
    // even when rendering is complete. A deadline permits progress after 16 ms,
    // never resource reuse before its fence. Keep FIFO, image count and frame slots.
    // OS scheduling can still overshoot; this is not a hard real-time guarantee.
    auto id = lastQueued - 1;
    PresentationWait sample{VK_SUCCESS, id, waitBudgetNs};
    if (!referenceBlockingWait)
        completion->request(device, swapchain, waitForPresent, id);
    auto check = [&] {
        ++sample.polls;
        auto started = SDL_GetTicksNS();
        auto result = referenceBlockingWait ? waitForPresent(device, swapchain, id, waitBudgetNs) :
                                             completion->status(id);
        sample.checkMs += double(SDL_GetTicksNS() - started) / 1'000'000;
        return result;
    };
    auto sleep = [&](uint64_t ns) {
        auto started = SDL_GetTicksNS();
        SDL_DelayNS(ns);
        sample.sleepMs += double(SDL_GetTicksNS() - started) / 1'000'000;
    };
    sample.result = referenceBlockingWait ? check() :
        waitForPresentation(waitBudgetNs, check, SDL_GetTicksNS, sleep);
    if (!referenceBlockingWait) sample.worker = completion->sample();
    return sample;
}
VkResult PresentationPacing::present(VkQueue queue, VkPresentInfoKHR& info, bool fifo) {
    VkPresentIdKHR id{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    auto originalNext = info.pNext;
    if (enabled && fifo) {
        id.pNext = originalNext;
        id.swapchainCount = 1;
        id.pPresentIds = &nextId;
        info.pNext = &id;
    }
    auto result = vkQueuePresentKHR(queue, &info);
    info.pNext = originalNext;
    if (enabled && fifo && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR))
        lastQueued = nextId++;
    return result;
}
} // namespace one::vulkan
