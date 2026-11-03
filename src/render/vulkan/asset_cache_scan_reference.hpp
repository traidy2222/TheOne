#pragma once
#include "geometry_state.hpp"
#include "shadow_submission_state.hpp"
#include "visibility_state.hpp"
#include "render/draw_order_cache.hpp"
#include <theone/assets/model.hpp>
#include <theone/render/frame_stats.hpp>
#include <unordered_set>
namespace one::vulkan {
// Diagnostic fallback for measuring eager scene-wide clearing against lazy
// generations in the same executable. Never used by the default renderer path.
inline void scanInvalidatedGeometryCaches(std::span<const std::shared_ptr<const Model>> models,
    GeometryCache& geometry, render::DrawOrderCache& order, std::span<BoundsSnapshot> bounds,
    ShadowSubmission& shadow, FrameStats& stats) {
    std::unordered_set<uint64_t> removed;
    for (const auto& model : models) if (model)
        for (const auto& mesh : model->meshes) for (const auto& primitive : mesh) {
            auto found = geometry.meshes.find(&primitive);
            if (found != geometry.meshes.end()) removed.insert(found->second.generation);
        }
    for (auto& reference : geometry.references) {
        ++stats.invalidationCacheEntriesScanned;
        if (!resident(reference.lifetime)) reference = {};
    }
    for (auto& entry : order.depths) {
        ++stats.invalidationCacheEntriesScanned;
        if (removed.contains(entry.generation)) entry = {};
    }
    for (auto& entry : bounds) {
        ++stats.invalidationCacheEntriesScanned;
        if (removed.contains(entry.generation)) { entry = {}; ++stats.invalidatedBounds; }
    }
    for (auto& cascade : shadow.cascades) for (auto& entry : cascade.templates) {
        ++stats.invalidationCacheEntriesScanned;
        if (entry.geometry && !resident(entry.lifetime)) { entry = {}; ++stats.invalidatedShadowTemplates; }
    }
}
}
