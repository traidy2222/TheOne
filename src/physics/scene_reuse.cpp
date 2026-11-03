#include <theone/physics/scene_reuse.hpp>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
namespace one {
SceneAssetReuse reuseSceneAssets(one::PhysicsScene& ready, const one::World& current, bool reuseUnchanged) {
    SceneAssetReuse result;
    std::unordered_map<uint64_t, std::shared_ptr<one::Model>> previous;
    std::unordered_set<const one::Model*> seen, live;
    for (const auto& e : current.entities)
        if (e.model && e.model->sourceFingerprint &&
            std::all_of(e.model->images.begin(), e.model->images.end(),
                        [](const auto& image) { return image.revision == 1; }))
            previous.emplace(e.model->sourceFingerprint, e.model);
    for (auto& e : ready.world.entities)
        if (e.model) {
            auto fresh = e.model;
            auto old = previous.find(fresh->sourceFingerprint);
            if (reuseUnchanged && old != previous.end()) {
                if (seen.insert(fresh.get()).second) {
                    // Keep discarded CPU models off the publication thread.
                    ready.retiredModels.push_back(fresh);
                    ++result.reused;
                }
                e.model = old->second;
            } else if (seen.insert(fresh.get()).second)
                ++result.replaced;
            live.insert(e.model.get());
        }
    ready.physics->adoptPreparedModelOwners(ready.world);
    seen.clear();
    for (const auto& e : current.entities)
        if (e.model && !live.contains(e.model.get()) && seen.insert(e.model.get()).second)
            result.obsolete.push_back(e.model);
    return result;
}

} // namespace one
