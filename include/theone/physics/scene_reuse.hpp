#pragma once
#include <theone/physics/scene_loading.hpp>
namespace one {
struct SceneAssetReuse {
    size_t reused = 0, replaced = 0;
    std::vector<std::shared_ptr<const one::Model>> obsolete;
};
SceneAssetReuse reuseSceneAssets(one::PhysicsScene& ready, const one::World& current, bool reuseUnchanged = true);

} // namespace one
