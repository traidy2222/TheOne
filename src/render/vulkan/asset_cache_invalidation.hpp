#pragma once
#include <span>
#include <memory>
#include <vulkan/vulkan.h>
#include <unordered_set>
namespace one {
struct Model;
struct FrameStats;
namespace vulkan {
struct GeometryCache;
struct ShadowSubmission;
// Returned CPU-only nodes must be released on the retirement worker, not while applying a reload.
std::shared_ptr<void> invalidateGeometryCaches(std::span<const std::shared_ptr<const Model>> models,
    GeometryCache& geometry, ShadowSubmission& shadow, FrameStats& stats);
void pruneShadowBindings(ShadowSubmission& shadow, const std::unordered_set<VkBuffer>& removed);
}
}
