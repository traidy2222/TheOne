#include "shadow_plan_cache.hpp"
#include "shadow_plan_key.hpp"
#include "shadow_submission_state.hpp"
#include <theone/render/render_scene.hpp>
#include <theone/render/frame_stats.hpp>
namespace one::vulkan {
bool reuseShadowPlan(ShadowCascadeSubmission& output, std::span<const Draw> source,
    const RenderScene* scene, uint32_t cascade, bool instanced, bool lod, float density) {
    output.comparedKeys = 0;
    if (output.planRetryFrames) {
        --output.planRetryFrames;
        output.recordPlanKeys = output.planValid = false;
        output.planDensity = density;
        return false;
    }
    // A moving light/camera changes density every frame. Avoid writing a full
    // dependency snapshot that is already unlikely to survive the next frame.
    // Once density settles, one rebuild records keys before reuse resumes.
    output.recordPlanKeys = output.planKeys.empty() || output.planDensity == density;
    bool same = output.planValid && output.recordPlanKeys && output.planKeys.size() == source.size();
    if (output.recordPlanKeys) output.planKeys.resize(source.size());
    output.planDensity = density;
    if (same) for (size_t i = 0; i < source.size(); ++i) {
        ++output.comparedKeys;
        if (!(output.planKeys[i] == shadowPlanKey(source[i], scene, cascade, instanced, lod))) {
            same = false;
            break; // Rebuild records the new keys in its existing traversal.
        }
    }
    if (!same) {
        // Changing draw order/visibility can invalidate the snapshot even with
        // stable density. Rebuild normally and retry caching after eight frames,
        // instead of copying every dependency again on each moving frame.
        if (output.planValid) {
            output.planRetryFrames = 7;
            output.recordPlanKeys = false;
        }
        output.planValid = false;
    }
    return same;
}
ShadowPlanCounts shadowPlanCounts(const FrameStats& s) {
    return {s.shadowCandidates, s.shadowCulled, s.shadowDraws, s.shadowInstances,
            s.shadowTriangles, s.shadowLodDraws, s.shadowTrianglesSaved,
            s.shadowCommandUpdates + s.shadowCommandReuses};
}
void retainShadowPlanCounts(ShadowCascadeSubmission& output, const ShadowPlanCounts& before, const FrameStats& s) {
    auto after = shadowPlanCounts(s);
    output.planValid = output.recordPlanKeys;
    output.planCounts = {after.candidates-before.candidates, after.culled-before.culled,
        after.draws-before.draws, after.instances-before.instances, after.triangles-before.triangles,
        after.lodDraws-before.lodDraws, after.trianglesSaved-before.trianglesSaved, after.commands-before.commands};
}
void applyShadowPlanCounts(const ShadowCascadeSubmission& output, FrameStats& s) {
    const auto& p = output.planCounts;
    s.shadowCandidates += p.candidates; s.shadowCulled += p.culled; s.shadowDraws += p.draws;
    s.shadowInstances += p.instances; s.shadowTriangles += p.triangles;
    s.shadowLodDraws += p.lodDraws; s.shadowTrianglesSaved += p.trianglesSaved;
    s.shadowCommandReuses += p.commands; ++s.shadowPlansReused;
}
}
