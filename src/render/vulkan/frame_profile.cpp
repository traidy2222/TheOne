#include "backend.hpp"
namespace one {
void Renderer::Impl::readFrameQueries() {
    auto& f = frame();
    if (!f.queries)
        return;
    uint64_t mask =
        deviceState.timestampBits >= 64 ? UINT64_MAX : (uint64_t(1) << deviceState.timestampBits) - 1;
    auto elapsed = [&](uint64_t a, uint64_t b) {
        return double((b - a) & mask) * deviceState.timestampPeriod / 1e6;
    };
    if(!f.uploadQueryWritten)lastGpu.gpuUploadMs=0;
    if (f.uploadQueryWritten) {
        uint64_t values[2]{};
        auto result = vkGetQueryPoolResults(deviceState.device, f.queries, 4, 2, sizeof(values), values,
                                            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result == VK_SUCCESS) {
            lastGpu.gpuUploadMs = elapsed(values[0], values[1]);
            if (lastGpu.gpuUploadMs > 0 && f.uploadBytes) {
                auto rate = double(f.uploadBytes) / lastGpu.gpuUploadMs;
                uploads.bytesPerMs =
                    std::clamp(uploads.bytesPerMs * .5 + rate * .5, 4096.0, 64.0 * 1024 * 1024);
            }
            f.uploadQueryWritten = false;
        } else if (result != VK_NOT_READY)
            check(result, "read upload timestamps");
    }
    if (!f.queryWritten)
        return;
    uint64_t values[4]{};
    auto result = vkGetQueryPoolResults(deviceState.device, f.queries, 0, 4, sizeof(values), values,
                                        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result == VK_NOT_READY)
        return;
    check(result, "read frame timestamps");
    if (f.serial >= lastGpu.gpuFrame) {
        lastGpu.gpuTimingAvailable = true;
        lastGpu.gpuFrame = f.serial;
        lastGpu.gpuShadowMs = elapsed(values[0], values[1]);
        lastGpu.gpuMainMs = elapsed(values[1], values[2]);
        lastGpu.gpuTotalMs = elapsed(values[0], values[3]);
        frameTrace.completed(deviceState.device, deviceState.timestampPeriod, values[0], values[3],
                             f.submitHostMs, f.fenceObservedHostMs, stats);
    }
    f.queryWritten = false;
}
void Renderer::Impl::publishFrameStats() {
    stats.width = swapchainState.extent.width;
    stats.height = swapchainState.extent.height;
    stats.presentMode = uint32_t(swapchainState.presentMode);
    stats.swapchainImages = uint32_t(swapchainState.images.size());
    stats.swapchainGeneration = swapchainState.generation;
    stats.swapchainRebuildMs = swapchainState.rebuildMs;
    stats.swapchainIdleMs = swapchainState.idleMs;
    stats.swapchainTargetsMs = swapchainState.targetsMs;

    displayedStats = stats;
    cpuHistory[historyIndex] =
        float(stats.scenePrepareMs + stats.prepareMs + stats.recordMs + stats.fenceWaitMs +
              stats.acquireWaitMs + stats.submitMs + stats.presentMs + stats.presentationWaitMs + stats.memoryStatsMs);
    gpuHistory[historyIndex] = float(stats.gpuTotalMs);
    historyIndex = (historyIndex + 1) % cpuHistory.size();
}
void Renderer::Impl::drawProfiler(bool* open) {
    const auto& stats = displayedStats;
    if (!ImGui::Begin("Engine profiler", open)) {
        ImGui::End();
        return;
    }
    ImGui::Text("CPU frame %llu | GPU sample %llu | %u frames in flight",
                static_cast<unsigned long long>(stats.frame), static_cast<unsigned long long>(stats.gpuFrame),
                frameCount);
    ImGui::Text("Scene %.3f ms | render prep %.3f ms | record %.3f ms", stats.scenePrepareMs, stats.prepareMs,
                stats.recordMs);
    ImGui::Text("Within preparation: instance batches %.3f ms | texture residency %.3f ms", stats.batchMs,
                stats.residencyMs);
    ImGui::Text("Preparation: visibility %.3f | draw list %.3f | asset sync %.3f ms", stats.visibilityMs,
                stats.drawListMs, stats.assetSyncMs);
    ImGui::Text("Recording: shadow %.3f | main %.3f | memory accounting %.3f ms", stats.shadowRecordMs,
                stats.mainRecordMs, stats.memoryStatsMs);
    ImGui::Text("Fence %.3f ms | acquire %.3f ms | submit %.3f ms | present %.3f ms", stats.fenceWaitMs,
                stats.acquireWaitMs, stats.submitMs, stats.presentMs);
    ImGui::Text("FIFO pacing %s | wait %.3f ms%s", stats.presentationPacing ? "active" : "inactive",
                stats.presentationWaitMs, stats.presentationWaitTimedOut ? " (timeout)" : "");
    if (stats.gpuTimingAvailable)
        ImGui::Text("GPU %.3f ms | shadow %.3f ms | main + UI %.3f ms", stats.gpuTotalMs, stats.gpuShadowMs,
                    stats.gpuMainMs);
    else
        ImGui::TextUnformatted("GPU timestamps unavailable or awaiting first completed frame");
    ImGui::Text("Draws main %zu / %zu | shadow %zu | culled %zu / %zu", stats.mainDraws, stats.candidateDraws,
                stats.shadowDraws, stats.mainCulled, stats.shadowCulled);
    ImGui::Text("Triangles main %zu | shadow %zu | uploads %llu bytes", stats.mainTriangles,
                stats.shadowTriangles, static_cast<unsigned long long>(stats.uploadedBytes));
    ImGui::Text("Deferred resources %zu", retired.size());
    ImGui::Text("Upload CPU %.3f ms | GPU %.3f ms | work %llu | pending %llu bytes", stats.uploadCpuMs,
                stats.gpuUploadMs, (unsigned long long)stats.transferBytes,
                (unsigned long long)stats.pendingUploadBytes);
    float uploadMs = float(uploads.budgetMs);
    if (ImGui::SliderFloat("Upload budget (ms)", &uploadMs, .1f, 4.f))
        uploads.budgetMs = uploadMs;
    ImGui::PlotLines("CPU stages (ms)", cpuHistory.data(), int(cpuHistory.size()), int(historyIndex), nullptr,
                     0, FLT_MAX, ImVec2(0, 55));
    ImGui::PlotLines("GPU (ms)", gpuHistory.data(), int(gpuHistory.size()), int(historyIndex), nullptr, 0,
                     FLT_MAX, ImVec2(0, 55));
    ImGui::Text("Shadow commands %zu for %zu draws | binds %zu | plan %.3f ms", stats.shadowCommands,
                stats.shadowDraws, stats.shadowGeometryBinds, stats.shadowPlanMs);
    ImGui::Text("Shadow LOD %zu | triangles saved %zu | region skips %zu", stats.shadowLodDraws,
                stats.shadowTrianglesSaved, stats.regionRejectedDraws);
    ImGui::Checkbox("Shadow batching", &shadowBatching);
    ImGui::Checkbox("Shadow LOD", &shadowLod);
    ImGui::Checkbox("Spatial regions", &spatialCulling);
    ImGui::Checkbox("Frustum culling", &culling);
    ImGui::Checkbox("Distance LOD", &lodEnabled);
    ImGui::Checkbox("Instance repeated meshes", &instancing);
    ImGui::Checkbox("Distance texture streaming", &textureStreaming);
    ImGui::Text("Texture detail transitions %zu | pressure bias %u", stats.textureTransitions,
                texturePressure);
    ImGui::Text("Instances main %zu | shadow %zu", stats.mainInstances, stats.shadowInstances);
    ImGui::SliderFloat("Shadow distance", &shadowDistance, 20, 500, "%.0f m");
    ImGui::Text("LOD draws %zu | triangles saved %zu | animated culled %zu", stats.lodDraws,
                stats.lodTrianglesSaved, stats.animatedCulled);
    ImGui::Text("Pending draws %zu | staged batches %zu | anisotropy %.0fx", stats.pendingDraws,
                stats.uploadCommands, deviceState.anisotropy);
    ImGui::Text("Asset CPU: mesh %.2f MiB | images %.2f MiB | upload payload %.2f MiB",
                assetMemory.cpuMeshBytes / 1048576.0, assetMemory.cpuImageBytes / 1048576.0,
                assetMemory.pendingCpuBytes / 1048576.0);
    ImGui::Text("Asset GPU: geometry %.2f MiB | textures %.2f MiB | staging %.2f MiB | retiring %.2f MiB",
                assetMemory.gpuGeometryBytes / 1048576.0, assetMemory.gpuTextureBytes / 1048576.0,
                assetMemory.stagingBytes / 1048576.0, assetMemory.retiredGpuBytes / 1048576.0);
    ImGui::Text("Models %zu | unique textures %zu | texture aliases %zu | unused packed geometry %.2f MiB",
                assetMemory.models, assetMemory.textures, assetMemory.textureAliases,
                assetMemory.unusedGeometryBytes / 1048576.0);
    if (ImGui::Button("Release unused asset GPU resources"))
        trimRequested = true;
    ImGui::End();
}
} // namespace one
