#pragma once
#include <cstddef>
#include <cstdint>
#include <theone/render/cpu_profile.hpp>
namespace one {
struct FrameStats {
    size_t invalidationCalls = 0, invalidatedBounds = 0, invalidatedShadowTemplates = 0;
    double invalidationMs = 0, invalidationCachesMs = 0, invalidationMaterialsMs = 0;
    double invalidationTexturesMs = 0, invalidationGeometryMs = 0;
    size_t shadowPlansReused = 0, drawScratchGrowths = 0;
    size_t shadowKeysCompared = 0, shadowKeysRecorded = 0, shadowPlansRebuilt = 0;
    double shadowPlanCheckMs = 0, shadowPlanRebuildMs = 0;
    bool shadowPlanReference = false;
    bool shadowBindingReference = false;
    size_t shadowTemplateRebinds = 0, shadowLodSelections = 0, invalidationPrimitivesVisited = 0;
    size_t invalidationCacheEntriesScanned = 0;
    std::array<CpuStageSample, size_t(CpuStage::Count)> cpuStages{};
    uint32_t width = 0, height = 0, presentMode = 0, swapchainImages = 0;
    uint64_t swapchainGeneration = 0;
    double swapchainRebuildMs = 0, swapchainIdleMs = 0, swapchainTargetsMs = 0;
    size_t materialCreations = 0, materialDeferred = 0, materialWarmups = 0;
    size_t textureAliasLookups = 0, textureAliasRepairs = 0;
    double materialWorkMs = 0;
    uint64_t frame = 0, gpuFrame = 0, waitedFrame = 0;
    double fenceThreadCpuMs = 0, submitAgeMs = 0;
    bool fencePending = false;
    bool calibratedTimingAvailable = false;
    double gpuStartHostMs = 0, gpuEndHostMs = 0, submitToGpuMs = 0, gpuDoneToFenceMs = 0;
    double calibrationDeviationMs = 0, submitHostMs = 0, presentHostMs = 0;
    double presentationWaitMs = 0;
    uint64_t presentationWaitId = 0;
    uint32_t presentationWaitPolls = 0;
    double presentationCheckMs = 0, presentationSleepMs = 0;
    uint64_t presentationWorkerId = 0;
    double presentationWorkerMs = 0;
    double presentationWaitBudgetMs = 0, presentationWaitHostMs = 0, presentationWaitThreadCpuMs = 0;
    bool presentationPacing = false, presentationWaitTimedOut = false;
    uint32_t slot = 0, framesInFlight = 2;
    double scenePrepareMs = 0, fenceWaitMs = 0, acquireWaitMs = 0, prepareMs = 0;
    double recordMs = 0, submitMs = 0, presentMs = 0;
    double assetSyncMs = 0, geometryPrepareMs = 0, visibilityMs = 0, materialPrepareMs = 0;
    double drawListMs = 0, uniformPaletteMs = 0, shadowRecordMs = 0, mainRecordMs = 0, memoryStatsMs = 0;
    double shadowCullSampleMs = 0, shadowIssueSampleMs = 0, shadowPlanMs = 0;
    size_t geometryBinds = 0, shadowGeometryBinds = 0, shadowCommands = 0;
    size_t shadowCandidates = 0, shadowLodDraws = 0, shadowTrianglesSaved = 0;
    size_t regionTests = 0, regionRejectedDraws = 0, assetOwnersChecked = 0;
    double batchMs = 0, residencyMs = 0;
    double residencyCommitMs = 0, residencyDemandMs = 0, residencyPlanMs = 0, residencyBindingsMs = 0;
    double gpuShadowMs = 0, gpuMainMs = 0, gpuTotalMs = 0;
    double gpuUploadMs = 0, uploadCpuMs = 0;
    uint64_t transferBytes = 0, pendingUploadBytes = 0;
    bool gpuTimingAvailable = false, gpuTimingSupported = false;
    size_t rigidBoundsReused = 0, boundsEvaluated = 0;
    size_t textureTransitions = 0;
    size_t textureChangesQueued=0, textureChangesCommitted=0, texturePromotions=0, textureDemotions=0;
    double retirementMs=0, uploadReleaseMs=0;
    size_t resourcesDestroyed=0, stagingReuses=0, stagingAllocations=0;
    size_t drawOrderReuses=0, shadowCommandReuses=0, shadowCommandUpdates=0;
    size_t mainInstances = 0, shadowInstances = 0;
    size_t candidateDraws = 0, mainDraws = 0, shadowDraws = 0;
    size_t mainTriangles = 0, shadowTriangles = 0, mainCulled = 0, shadowCulled = 0;
    uint64_t uploadedBytes = 0;
    size_t retiredResources = 0, pendingDraws = 0, uploadCommands = 0, lodDraws = 0, lodTrianglesSaved = 0,
           animatedCulled = 0;
};
} // namespace one
