#pragma once
#include <theone/render/render_scene.hpp>
#include <memory>
namespace one {
struct PickResult {
    uint64_t entity = 0;
    float distance = 0;
    size_t candidates = 0, triangles = 0, skinnedVertices = 0;
};
// Exact full-detail geometry picking. Bounds and poses come from the current prepared scene.
// Cached local triangle trees use weak asset ownership and never retain unloaded models.
class ScenePicker {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    ScenePicker();
    ~ScenePicker();
    PickResult pick(const RenderScene& scene, glm::vec3 origin, glm::vec3 direction);
    void clear();
};
} // namespace one
