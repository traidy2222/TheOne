#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>
namespace one::vulkan {
// Keep the optional pacing deadline outside the driver's blocking wait. Sleep
// between status checks; neither a stalled compositor nor a timeout is completion.
template<class Poll, class Clock, class Sleep>
VkResult waitForPresentation(uint64_t budgetNs, Poll poll, Clock now, Sleep sleep) {
    const auto started = now();
    uint64_t lastSleep = 0;
    for (;;) {
        auto result = poll();
        if (result != VK_TIMEOUT)
            return result;
        auto elapsed = now() - started;
        // Background Windows timers can wake much later than requested. Do not
        // start another sleep when the last observed sleep would cross the budget.
        if (elapsed >= budgetNs || lastSleep > budgetNs - elapsed)
            return VK_TIMEOUT;
        auto beforeSleep = now();
        sleep(std::min<uint64_t>(1'000'000, budgetNs - elapsed));
        lastSleep = now() - beforeSleep;
    }
}
} // namespace one::vulkan
