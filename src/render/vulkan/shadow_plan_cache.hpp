#pragma once
#include <theone/render/draw.hpp>
#include <span>
namespace one {
struct RenderScene;
struct FrameStats;
namespace vulkan {
struct ShadowCascadeSubmission;
struct ShadowPlanKey {
    const Primitive* geometry = nullptr;
    uint64_t entity = 0;
    uint32_t first = 0, count = 0, palette = 0, instances = 0;
    uint8_t flags = 0;
    glm::mat3 linear{1};
    bool operator==(const ShadowPlanKey&) const = default;
};
struct ShadowPlanCounts {
    size_t candidates = 0, culled = 0, draws = 0, instances = 0, triangles = 0;
    size_t lodDraws = 0, trianglesSaved = 0, commands = 0;
};
bool reuseShadowPlan(ShadowCascadeSubmission& output, std::span<const Draw> source,
    const RenderScene* scene, uint32_t cascade, bool instanced, bool lod, float density);
ShadowPlanCounts shadowPlanCounts(const FrameStats& stats);
void retainShadowPlanCounts(ShadowCascadeSubmission& output, const ShadowPlanCounts& before,
                           const FrameStats& stats);
void applyShadowPlanCounts(const ShadowCascadeSubmission& output, FrameStats& stats);
}
}
