#include "backend.hpp"
#include "presentation_pacing.hpp"
#include "wait_timing.hpp"
namespace one {
void Renderer::Impl::createFrameResources() {
    for (auto& f : frames) {
        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        a.commandPool = deviceState.pool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(deviceState.device, &a, &f.command), "allocate frame commands");
        VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(deviceState.device, &s, nullptr, &f.acquired), "create acquire semaphore");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(deviceState.device, &fence, nullptr, &f.fence), "create frame fence");
        if (deviceState.timestampBits) {
            VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            q.queryType = VK_QUERY_TYPE_TIMESTAMP;
            q.queryCount = 12;
            check(vkCreateQueryPool(deviceState.device, &q, nullptr, &f.queries), "create frame queries");
        }
    }
}
void Renderer::Impl::destroyFrameResources() {
    for (auto& f : frames) {
        releaseUploads(f);
        if (f.geometry.vertices)
            vkDestroyBuffer(deviceState.device, f.geometry.vertices, nullptr);
        if (f.geometry.vertexMemory)
            vkFreeMemory(deviceState.device, f.geometry.vertexMemory, nullptr);
        if (f.queries)
            vkDestroyQueryPool(deviceState.device, f.queries, nullptr);
        if (f.fence)
            vkDestroyFence(deviceState.device, f.fence, nullptr);
        if (f.acquired)
            vkDestroySemaphore(deviceState.device, f.acquired, nullptr);
        f = {};
    }
}
void Renderer::Impl::startFrame() {
    if (frameReady)
        return;
    stats = {};
    stats.gpuTimingSupported = deviceState.timestampBits != 0;
    stats.frame = submitted + 1;
    stats.slot = frameIndex;
    stats.framesInFlight = frameCount;
    stats.presentationPacing = presentationPacing.enabled && swapchainState.presentMode == VK_PRESENT_MODE_FIFO_KHR;
    if (frameTrace.calibrate) stats.presentationWaitHostMs = vulkan::FrameTrace::hostMs();
    auto presentationCpuStart = vulkan::waitThreadCpuMs();
    {
        vulkan::ScopedTiming timing(stats.presentationWaitMs);
        auto wait = presentationPacing.wait(deviceState.device, swapchainState.handle, stats.presentationPacing);
        stats.presentationWaitId = wait.id;
        stats.presentationWaitPolls = wait.polls;
        stats.presentationCheckMs = wait.checkMs;
        stats.presentationSleepMs = wait.sleepMs;
        stats.presentationWorkerId = wait.worker.id;
        stats.presentationWorkerMs = wait.worker.driverMs;
        stats.presentationWaitBudgetMs = double(wait.timeoutNs) / 1'000'000;
        auto result = wait.result;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) swapchainState.resized = true;
        else if (result == VK_TIMEOUT) stats.presentationWaitTimedOut = true;
        else check(result, "wait FIFO presentation");
    }
    stats.presentationWaitThreadCpuMs = vulkan::waitThreadCpuMs() - presentationCpuStart;
    stats.waitedFrame=frame().serial;
    stats.submitAgeMs=frame().serial?vulkan::clockMs()-frame().submittedAtMs:0;
    stats.fencePending=vkGetFenceStatus(deviceState.device,frame().fence)==VK_NOT_READY;
    auto cpuStart=vulkan::waitThreadCpuMs();
    {
        vulkan::ScopedTiming timing(stats.fenceWaitMs);
        check(vkWaitForFences(deviceState.device, 1, &frame().fence, VK_TRUE, UINT64_MAX), "wait frame slot");
    }
    stats.fenceThreadCpuMs=vulkan::waitThreadCpuMs()-cpuStart;
    if (frameTrace.calibrate) frame().fenceObservedHostMs = vulkan::FrameTrace::hostMs();
    completed = std::max(completed, frame().serial);
    readFrameQueries();
    { vulkan::ScopedTiming timing(stats.uploadReleaseMs); releaseUploads(frame()); }
    collectRetired();
    stats.gpuFrame = lastGpu.gpuFrame;
    stats.gpuTimingAvailable = lastGpu.gpuTimingAvailable;
    stats.gpuShadowMs = lastGpu.gpuShadowMs;
    stats.gpuMainMs = lastGpu.gpuMainMs;
    stats.gpuTotalMs = lastGpu.gpuTotalMs;
    stats.gpuUploadMs = lastGpu.gpuUploadMs;
    frameReady = true;
}
void Renderer::Impl::retire(std::function<void()> destroy, uint64_t bytes) {
    retired.push_back({submitted, std::move(destroy), bytes});
}
} // namespace one
