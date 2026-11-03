#pragma once
#include <theone/scene/entity.hpp>
#include <theone/render/render_scene.hpp>
#include <unordered_map>
namespace one::detail {
struct CachedEntity {
    uint64_t id = 0, parent = 0;
    Transform local;
    glm::mat4 root{1}, paletteRoot{1};
    bool initialized = false, dirty = true;
    const Model* model = nullptr;
    AssetHandle handle;
    bool animated = false;
    std::vector<GeometryTransform> palette;
    std::vector<uint32_t> nodePalettes;
};
struct SceneCache {
    std::unordered_map<uint64_t, size_t> indices;
    std::vector<CachedEntity> entities;
    std::vector<uint8_t> visiting;
    void update(const std::vector<Entity>& source, ScenePreparationStats& stats);
};
} // namespace one::detail
