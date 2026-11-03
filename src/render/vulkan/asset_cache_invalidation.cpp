#include "asset_cache_invalidation.hpp"
#include "geometry_state.hpp"
#include "shadow_submission_state.hpp"
#include <theone/assets/model.hpp>
#include <theone/render/frame_stats.hpp>
#include <unordered_set>
namespace one::vulkan {
std::shared_ptr<void> invalidateGeometryCaches(std::span<const std::shared_ptr<const Model>> models,
    GeometryCache& geometry, ShadowSubmission& shadow, FrameStats& stats) {
    using Node = decltype(ShadowCascadeSubmission::geometryTemplates)::node_type;
    auto retired = std::make_shared<std::vector<Node>>();
    for (auto& cascade : shadow.cascades) cascade.planValid = false;
    for (const auto& model : models) {
        if (!model) continue;
        if (auto token = geometry.lifetimes.find(model.get()); token != geometry.lifetimes.end()) {
            token->second->resident = false;
            geometry.lifetimes.erase(token);
        }
        // Models are immutable snapshots. Their own primitive lists are the
        // dependency index; no per-frame reverse-index maintenance is needed.
        for (const auto& mesh : model->meshes) for (const auto& primitive : mesh) {
            ++stats.invalidationPrimitivesVisited;
            for (auto& cascade : shadow.cascades) {
                auto node = cascade.geometryTemplates.extract(&primitive);
                if (!node.empty()) retired->push_back(std::move(node));
            }
        }
    }
    return retired;
}
void pruneShadowBindings(ShadowSubmission& shadow, const std::unordered_set<VkBuffer>& removed) {
    if (removed.empty()) return;
    for (auto& cascade : shadow.cascades)
        std::erase_if(cascade.lookup, [&](const auto& entry) {
            if (!removed.contains(entry.first.buffer)) return false;
            auto& group = cascade.groups[entry.second];
            group.binding = {};
            group.commands.clear();
            group.count = 0;
            return true;
        });
}
}
