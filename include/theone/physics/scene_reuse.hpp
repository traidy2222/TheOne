#pragma once
#include <theone/physics/scene_loading.hpp>
namespace one {
struct SceneAssetReuse {
    size_t reused = 0, replaced = 0;
    std::vector<std::shared_ptr<const one::Model>> obsolete;
};
// Rebind a prepared scene to unchanged immutable source models before publication.
// Retired fresh models stay owned by ready for worker retirement; obsolete live
// models are returned for the consumer's renderer invalidation. Does not swap worlds.
SceneAssetReuse reuseSceneAssets(one::PhysicsScene& ready, const one::World& current, bool reuseUnchanged = true);

} // namespace one
