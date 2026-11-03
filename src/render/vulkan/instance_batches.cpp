#include "backend.hpp"
#include <numeric>
#include <tuple>
namespace one {
bool Renderer::Impl::batchInstances(std::vector<Draw>& draws, const RenderScene& scene, const Camera& camera,
                                    uint64_t selected) {
    // Group once, then use flat arrays for all four visibility passes. Preserve first-draw
    // order so batching does not turn spatially coherent scene order into pointer order.
    std::vector<vulkan::InstanceKey> keys(draws.size());
    for (uint32_t i = 0; i < draws.size(); ++i) {
        const auto& d = draws[i];
        if (d.geometry && !scene.transforms[d.palette].info.x && !(d.material && d.material->blend))
            keys[i] = {uintptr_t(d.geometry), uintptr_t(d.material), uintptr_t(d.image), d.lod,
                       d.entity == selected};
    }
    if (keys != instancePlan.keys) {
        instancePlan.keys = std::move(keys);
        instancePlan.groups.assign(draws.size(), UINT32_MAX);
        instancePlan.count = 0;
        std::vector<uint32_t> indices;
        indices.reserve(draws.size());
        for (uint32_t i = 0; i < draws.size(); ++i)
            if (instancePlan.keys[i].geometry)
                indices.push_back(i);
        std::sort(indices.begin(), indices.end(),
                  [&](auto a, auto b) { return instancePlan.keys[a] < instancePlan.keys[b]; });
        for (size_t i = 0; i < indices.size(); ++i) {
            if (!i || instancePlan.keys[indices[i]] != instancePlan.keys[indices[i - 1]])
                ++instancePlan.count;
            instancePlan.groups[indices[i]] = instancePlan.count - 1;
        }
        // Sparse repetition did not repay per-pass palette/list work in Robots.
        // Use the ordinary draw path unless at least one eighth of calls can merge.
        instancePlan.worthwhile =
            indices.size() > instancePlan.count && (indices.size() - instancePlan.count) * 8 >= draws.size();
    }
    if (!instancePlan.worthwhile)
        return false;
    instanceTransforms = scene.transforms;
    const auto& group = instancePlan.groups;
    auto groups = instancePlan.count;
    std::vector<uint32_t> count(groups), base(groups), cursor(groups), visibleIndices;
    visibleIndices.reserve(draws.size());
    auto batch = [&](bool shadow, uint32_t cascade) {
        std::fill(count.begin(), count.end(), 0);
        std::fill(cursor.begin(), cursor.end(), 0);
        visibleIndices.clear();
        for (uint32_t i = 0; i < draws.size(); ++i) {
            const auto& d = draws[i];
            if (shadow && d.material && d.material->blend)
                continue;
            if (!(shadow ? bool(d.shadowVisibility & (1u << cascade)) : d.cameraVisible)) {
                if (shadow)
                    ++stats.shadowCulled;
                else {
                    ++stats.mainCulled;
                    if (d.geometry && scene.transforms[d.palette].info.x)
                        ++stats.animatedCulled;
                }
                continue;
            }
            visibleIndices.push_back(i);
            if (group[i] != UINT32_MAX)
                ++count[group[i]];
        }
        for (uint32_t g = 0; g < groups; ++g)
            if (count[g] > 1) {
                base[g] = uint32_t(instanceTransforms.size());
                instanceTransforms.resize(instanceTransforms.size() + count[g]);
            }
        std::vector<Draw> output;
        output.reserve(visibleIndices.size());
        for (auto i : visibleIndices) {
            auto d = draws[i];
            auto g = group[i];
            if (g != UINT32_MAX && count[g] > 1) {
                auto n = cursor[g]++;
                instanceTransforms[base[g] + n] = scene.transforms[d.palette];
                if (n)
                    continue;
                d.palette = base[g];
                d.instances = count[g];
            }
            output.push_back(std::move(d));
        }
        return output;
    };
    for (uint32_t c = 0; c < 3; ++c)
        shadowBatches[c] = batch(true, c);
    draws = batch(false, 0);
    return true;
}
} // namespace one
