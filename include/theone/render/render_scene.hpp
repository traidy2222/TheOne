#pragma once
#include <theone/render/draw.hpp>
namespace one {
struct alignas(16) GeometryTransform {
    glm::mat4 position{1}, normal{1};
    glm::uvec4 info{0}; // x: joint count; joints immediately follow the mesh transform.
};
static_assert(sizeof(GeometryTransform) == 144);
struct ScenePreparationStats {
    double cpuMs = 0;
    size_t rootsRecomputed = 0, posesEvaluated = 0, palettesRebuilt = 0;
    bool hierarchyRebuilt = false;
};
struct RenderScene {
    ScenePreparationStats preparation;
    std::vector<GeometryTransform> transforms;
    std::vector<Draw> draws;
    size_t vertexCount = 0;
};
} // namespace one
