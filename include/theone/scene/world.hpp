#pragma once
#include <theone/scene/entity.hpp>
#include <theone/scene/load_stats.hpp>
#include <theone/render/render_scene.hpp>
namespace one {
namespace detail {
struct SceneCache;
}
struct World {
    std::filesystem::path source;
    std::vector<Entity> entities;
    std::vector<std::string> diagnostics;
    Entity* find(uint64_t id);
    const Entity* find(uint64_t id) const;
    glm::mat4 matrix(uint64_t id) const;
    static World load(const std::filesystem::path& path, SceneLoadStats* stats = nullptr,
                      std::stop_token stop = {});
    // Reload one model transactionally, preserving its stable asset identity.
    // Existing RenderScenes retain the old model until prepared again.
    std::shared_ptr<Model> reloadAsset(const std::filesystem::path& asset);
    void replaceAsset(const std::filesystem::path& asset, std::shared_ptr<Model> replacement);
    void save(const std::filesystem::path& path) const;
    void bake(float time, std::vector<Vertex>& vertices, std::vector<Draw>& draws) const;
    void prepare(float time, RenderScene& scene) const;

  private:
    mutable std::shared_ptr<detail::SceneCache> cache_;
};
} // namespace one
