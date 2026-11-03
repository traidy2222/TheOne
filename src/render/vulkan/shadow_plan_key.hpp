#pragma once
#include "shadow_plan_cache.hpp"
#include <theone/render/render_scene.hpp>
namespace one::vulkan {
// Kept beside the cache policy, but visible to its two hot traversal loops so
// constructing an exact dependency key does not require a call per draw.
inline ShadowPlanKey shadowPlanKey(const Draw& draw, const RenderScene* scene, uint32_t cascade,
                                   bool instanced, bool lod) {
    // Culled/empty/blended entries emit no command. Preserve their candidate
    // classification, but do not snapshot matrices or bindings that cannot affect
    // this cascade. Visibility changes still force a new plan before submission.
    if (!draw.count || (draw.material && draw.material->blend)) return {};
    if (!instanced && !(draw.shadowVisibility & (1u << cascade))) {
        ShadowPlanKey culled;
        culled.count = 1;
        return culled;
    }
    bool rigidLod = lod && scene && draw.geometry && draw.instances == 1 &&
                    !(draw.material && draw.material->mask) &&
                    !scene->transforms.at(draw.palette).info.x;
    ShadowPlanKey key{draw.geometry, rigidLod ? draw.entity : 0, draw.first, draw.count,
                      draw.palette, draw.instances};
    key.flags = 1 |
                (draw.material && draw.material->mask ? 2 : 0) |
                (draw.material && draw.material->blend ? 4 : 0) | (rigidLod ? 8 : 0);
    if (rigidLod) key.linear = glm::mat3(scene->transforms.at(draw.palette).position);
    return key;
}
}
