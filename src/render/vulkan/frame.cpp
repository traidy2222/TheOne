#include "backend.hpp"
#include "wait_timing.hpp"
#include "cpu_profiling.hpp"
namespace one {
bool Renderer::Impl::draw(const std::vector<one::Vertex>& vertices, const std::vector<one::Draw>& draws,
                          const one::Camera& camera, float time, bool capture, bool shadows,
                          uint64_t selected, bool classicColor, const one::RenderScene* scene,
                          const uint64_t* revision) {
    if ((!swapchainState.handle || swapchainState.resized || swapchainState.resizeRequested) && !createSwapchain())
        return false;
    startFrame();
    vulkan::CpuStageScope cpuTotal(stats, CpuStage::Total);
    auto prepareStarted = std::chrono::steady_clock::now();
    assetPrepareStart = prepareStarted;
    {
        vulkan::ScopedTiming timing(stats.assetSyncMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Assets);
        synchronizeAssets(scene, draws);
    }
    stats.scenePrepareMs = scene ? scene->preparation.cpuMs : 0;
    uploadStats = {};
    uploadSpent = 0;
    materialAdmission.reset();
    geometryState.stats.uploadedGeometryBytes = geometryState.stats.uploadedPaletteBytes =
        geometryState.stats.uploadedDynamicBytes = 0;
    if (!revision || !frame().geometry.dynamicCached || frame().geometry.dynamicSource != vertices.data() ||
        frame().geometry.dynamicSize != vertices.size() || frame().geometry.dynamicRevision != *revision) {
        uploadVertices(vertices, revision != nullptr);
        geometryState.stats.uploadedDynamicBytes = vertices.size() * sizeof(one::Vertex);
        frame().geometry.dynamicCached = revision != nullptr;
        frame().geometry.dynamicSource = vertices.data();
        frame().geometry.dynamicSize = vertices.size();
        if (revision)
            frame().geometry.dynamicRevision = *revision;
    }
    vulkan::CpuStageScope copies(stats, CpuStage::Copies);
    render::DrawScratchLease scratchLease{drawScratch};
    stats.drawScratchGrowths = drawScratch.assign(draws, scene ? std::span<const Draw>(scene->draws) : std::span<const Draw>{});
    auto& ordered = drawScratch.draws;
    if (scene) {
        {
            vulkan::ScopedTiming timing(stats.geometryPrepareMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Geometry);
            prepareGeometry(*scene);
        }
        auto base=draws.size();
        for(size_t i=0;i<scene->draws.size();++i)ordered[base+i].resolvedGeometry=uint32_t(i);
    }
    copies.finish();
    {
        vulkan::ScopedTiming timing(stats.uniformPaletteMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Uniforms);
        updateUniform(shadows, classicColor, camera);
    }
    {
        vulkan::ScopedTiming timing(stats.visibilityMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Visibility);
        prepareVisibility(ordered, scene, camera, shadows);
    }
    {
        vulkan::ScopedTiming timing(stats.materialPrepareMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Materials);
        prepareDrawMaterials(ordered);
        warmResidentMaterials(ordered);
    }
    {
        vulkan::ScopedTiming timing(stats.residencyMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Residency);
        updateTextureResidency(ordered, camera);
    }
    {
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Uploads);
        flushTextureUploads();
        pumpUploads();
    }
    {
        vulkan::ScopedTiming timing(stats.drawListMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::DrawList);
        finishDrawList(ordered, vertices, scene, camera, revision != nullptr);
    }
    stats.candidateDraws = ordered.size();
    {
        vulkan::ScopedTiming timing(stats.uniformPaletteMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Uniforms);
        instanceBatchesReady = false;
        if (scene) {
            if (instancing) {
                vulkan::ScopedTiming timing(stats.batchMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Batches);
                instanceBatchesReady = batchInstances(ordered, *scene, camera, selected);
                preparePalette(instanceBatchesReady ? instanceTransforms : scene->transforms);
            } else
                preparePalette(scene->transforms);
        }
    }
    stats.prepareMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareStarted).count();
    auto acquireStarted = std::chrono::steady_clock::now();
    uint32_t index = 0;
    VkResult result = vkAcquireNextImageKHR(deviceState.device, swapchainState.handle, UINT64_MAX,
                                            frame().acquired, VK_NULL_HANDLE, &index);
    stats.acquireWaitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - acquireStarted).count();
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        submitUploadsOnly();
        frameReady = false;
        swapchainState.resized = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        check(result, "acquire image");
    bool suboptimal = result == VK_SUBOPTIMAL_KHR;
    auto recordStarted = std::chrono::steady_clock::now();
    check(vkResetCommandBuffer(frame().command, 0), "reset command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame().command, &begin), "begin command buffer");
    if (capture && !swapchainState.captureSupported)
        throw std::runtime_error("GPU surface does not support capture");
    if (frame().queries) {
        vkCmdResetQueryPool(frame().command, frame().queries, 0, 4);
        vkCmdWriteTimestamp(frame().command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame().queries, 0);
    }
    recordScene(index, ordered, camera, time, capture, shadows, selected, scene);
    if (frame().queries)
        vkCmdWriteTimestamp(frame().command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 3);
    check(vkEndCommandBuffer(frame().command), "end command buffer");
    stats.recordMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - recordStarted).count();
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame().acquired;
    submit.pWaitDstStageMask = &waitStage;
    std::vector<VkCommandBuffer> commands;
    for (const auto& upload : frame().uploads)
        commands.push_back(upload.command);
    commands.push_back(frame().command);
    submit.commandBufferCount = uint32_t(commands.size());
    submit.pCommandBuffers = commands.data();
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &swapchainState.presentReady[index];
    {
        vulkan::ScopedTiming timing(stats.submitMs);
        check(vkResetFences(deviceState.device, 1, &frame().fence), "reset submit fence");
        if (frameTrace.calibrate) stats.submitHostMs = frame().submitHostMs = vulkan::FrameTrace::hostMs();
        check(vkQueueSubmit(deviceState.queue, 1, &submit, frame().fence), "submit frame");
    }
    frame().serial = ++submitted;
    frame().submittedAtMs=vulkan::clockMs();
    frame().queryWritten = frame().queries != VK_NULL_HANDLE;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &swapchainState.presentReady[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchainState.handle;
    present.pImageIndices = &index;
    {
        vulkan::ScopedTiming timing(stats.presentMs);
        if (frameTrace.calibrate) stats.presentHostMs = vulkan::FrameTrace::hostMs();
        result = presentationPacing.present(deviceState.queue, present,
                                            swapchainState.presentMode == VK_PRESENT_MODE_FIFO_KHR);
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        swapchainState.resized = true;
    else {
        check(result, "present frame");
        if (suboptimal)
            swapchainState.resized = true;
    }
    if (capture)
        verifyCapture();
    stats.uploadedBytes = geometryState.stats.uploadedGeometryBytes +
                          geometryState.stats.uploadedPaletteBytes +
                          geometryState.stats.uploadedDynamicBytes + uploadStats.textureBytes;
    stats.retiredResources = retired.size();
    {
        vulkan::ScopedTiming timing(stats.memoryStatsMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Memory);
        updateAssetMemory();
    }
    cpuTotal.finish();
    stats.materialWorkMs = materialAdmission.usedMs;
    publishFrameStats();
    frameReady = false;
    frameIndex = (frameIndex + 1) % frameCount;
    return true;
}

bool Renderer::Impl::waitForFrame() {
    if (SDL_GetWindowFlags(deviceState.window) & SDL_WINDOW_MINIMIZED) return false;
    if ((!swapchainState.handle || swapchainState.resized || swapchainState.resizeRequested) && !createSwapchain())
        return false;
    startFrame();
    return true;
}
bool Renderer::Impl::beginFrame() {
    if (!waitForFrame()) return false;
    ImGui_ImplVulkan_NewFrame();
    return true;
}
} // namespace one
