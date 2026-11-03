#include "render/vulkan/presentation_pacing.hpp"
#include "render/vulkan/presentation_completion.hpp"
#include "render/vulkan/presentation_wait.hpp"
#include <iostream>
#include <stdexcept>
#include <future>
namespace {
uint64_t waitedId, timeout;
unsigned calls;
VkResult nextResult = VK_SUCCESS;
std::promise<void> driverEntered, releaseDriver;
auto releaseSignal = releaseDriver.get_future().share();
VKAPI_ATTR VkResult VKAPI_CALL blockedWait(VkDevice, VkSwapchainKHR, uint64_t, uint64_t) {
    driverEntered.set_value();
    releaseSignal.wait();
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice, VkSwapchainKHR, uint64_t id, uint64_t ns) {
    ++calls;
    waitedId = id;
    timeout = ns;
    return nextResult;
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    try {
        one::vulkan::PresentationPacing pacing;
        pacing.waitForPresent = wait;
        pacing.lastQueued = 10;
        pacing.referenceBlockingWait = true;
        require(pacing.wait({}, {}, true).id == 0 && calls == 0, "Unsupported path waited");
        pacing.enabled = true;
        require(pacing.wait({}, {}, false).id == 0 && calls == 0, "Uncapped path waited");
        pacing.resetSwapchain();
        require(pacing.nextId == 1 && pacing.lastQueued == 0, "Swapchain retained old IDs");
        pacing.lastQueued = 1;
        require(pacing.wait({}, {}, true).id == 0 && calls == 0, "First present waited");
        pacing.lastQueued = 10;
        nextResult = VK_TIMEOUT;
        auto sample = pacing.wait({}, {}, true);
        require(sample.result == VK_TIMEOUT && sample.id == 9 && waitedId == 9,
                "Timeout lost the target or result");
        require(timeout == 16'000'000 && sample.timeoutNs == timeout,
                "Reference wait lost its configured budget");
        require(pacing.lastQueued == 10, "Timeout treated a queued present as complete");
        nextResult = VK_SUCCESS;
        require(pacing.wait({}, {}, true).result == VK_SUCCESS && waitedId == 9,
                "Pacing did not recover after timeout");
        nextResult = VK_ERROR_OUT_OF_DATE_KHR;
        require(pacing.wait({}, {}, true).result == VK_ERROR_OUT_OF_DATE_KHR,
                "Out-of-date result swallowed");
        nextResult = VK_ERROR_DEVICE_LOST;
        require(pacing.wait({}, {}, true).result == VK_ERROR_DEVICE_LOST,
                "Device loss swallowed");
        pacing.resetSwapchain();
        auto previousCalls = calls;
        require(pacing.wait({}, {}, true).id == 0 && calls == previousCalls,
                "Recreated swapchain waited for an obsolete ID");
        // Deterministic delayed-completion tests: no real GPU or wall-clock limits.
        uint64_t clock = 0, largestSleep = 0;
        unsigned polls = 0;
        auto now = [&] { return clock; };
        auto sleep = [&](uint64_t ns) { largestSleep = std::max(largestSleep, ns); clock += ns; };
        auto delayed = [&] { ++polls; return clock >= 47'000'000 ? VK_SUCCESS : VK_TIMEOUT; };
        auto result = one::vulkan::waitForPresentation(16'000'000, delayed, now, sleep);
        require(result == VK_TIMEOUT && clock == 16'000'000 && polls == 17 && largestSleep == 1'000'000,
                "47 ms completion held the optional wait beyond its deadline or spun");
        clock = 46'000'000;
        require(one::vulkan::waitForPresentation(16'000'000, delayed, now, sleep) == VK_SUCCESS &&
                clock == 47'000'000, "Completion did not recover on the next pacing attempt");
        clock = 0;
        auto coarseSleep = [&](uint64_t) { clock += 10'000'000; };
        require(one::vulkan::waitForPresentation(16'000'000, delayed, now, coarseSleep) == VK_TIMEOUT &&
                clock == 10'000'000, "Repeated coarse sleeps amplified a background presentation stall");
        clock = 0;
        auto oversleep = [&](uint64_t) { clock += 20'000'000; };
        require(one::vulkan::waitForPresentation(16'000'000, delayed, now, oversleep) == VK_TIMEOUT &&
                clock == 20'000'000, "Scheduler oversleep restarted the deadline");
        clock = 0;
        require(one::vulkan::waitForPresentation(1, [] { return VK_ERROR_DEVICE_LOST; }, now, sleep) ==
                VK_ERROR_DEVICE_LOST && clock == 0, "Polling hid an error or slept after it");
        one::vulkan::PresentationCompletion completion;
        auto entered = driverEntered.get_future();
        completion.request({}, {}, blockedWait, 9);
        entered.wait();
        clock = 0;
        auto pending = one::vulkan::waitForPresentation(16'000'000,
            [&] { return completion.status(9); }, now, sleep);
        // Release before asserting so a failing assertion cannot strand the worker.
        auto resetting = std::async(std::launch::async, [&] { completion.reset(); });
        releaseDriver.set_value();
        resetting.get();
        require(pending == VK_TIMEOUT && clock == 16'000'000,
                "Blocked driver held the main-thread deadline");
        require(completion.status(9) == VK_TIMEOUT && completion.sample().id == 0,
                "In-flight reset retained the old swapchain's completion");
        nextResult = VK_ERROR_DEVICE_LOST;
        completion.request({}, {}, wait, 1);
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (completion.status(1) == VK_TIMEOUT && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        auto failure = completion.status(1);
        completion.reset();
        require(failure == VK_ERROR_DEVICE_LOST, "Worker lost the driver error");
        std::cout << "Presentation wait budget, timeout recovery and reset passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
