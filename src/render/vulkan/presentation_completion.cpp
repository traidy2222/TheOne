#include "presentation_completion.hpp"
#include <algorithm>
#include <chrono>
namespace one::vulkan {
void PresentationCompletion::request(VkDevice device, VkSwapchainKHR swapchain,
                                     PFN_vkWaitForPresentKHR wait, uint64_t id) {
    std::lock_guard lock(mutex);
    requested = std::max(requested, id);
    if (!worker.joinable())
        worker = std::thread(&PresentationCompletion::run, this, device, swapchain, wait);
    changed.notify_one();
}
void PresentationCompletion::run(VkDevice device, VkSwapchainKHR swapchain, PFN_vkWaitForPresentKHR wait) {
    std::unique_lock lock(mutex);
    for (;;) {
        changed.wait(lock, [&] { return stopping || (requested > completed && failure == VK_SUCCESS); });
        if (stopping) return;
        auto id = requested;
        lock.unlock();
        auto started = std::chrono::steady_clock::now();
        auto result = wait(device, swapchain, id, 50'000'000);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        lock.lock();
        last = {id, ms};
        if (result == VK_SUCCESS)
            completed = std::max(completed, id);
        else if (result != VK_TIMEOUT)
            failure = result;
        else // Avoid a retry spin if a driver returns timeout immediately.
            changed.wait_for(lock, std::chrono::milliseconds(1), [&] { return stopping || requested > id; });
    }
}
VkResult PresentationCompletion::status(uint64_t id) {
    std::lock_guard lock(mutex);
    if (failure != VK_SUCCESS) return failure;
    return completed >= id ? VK_SUCCESS : VK_TIMEOUT;
}
PresentationCompletionSample PresentationCompletion::sample() {
    std::lock_guard lock(mutex);
    return last;
}
void PresentationCompletion::reset() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    changed.notify_one();
    if (worker.joinable()) worker.join();
    std::lock_guard lock(mutex);
    stopping = false;
    requested = completed = 0;
    failure = VK_SUCCESS;
    last = {};
}
} // namespace one::vulkan
