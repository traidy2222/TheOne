#include "transform_cache.hpp"
#include "core/validation.hpp"
#include <functional>
namespace one::detail {
void SceneCache::update(const std::vector<Entity>& source, ScenePreparationStats& stats) {
    bool rebuild = entities.size() != source.size();
    if (!rebuild)
        for (size_t i = 0; i < source.size(); ++i)
            if (entities[i].id != source[i].id || entities[i].parent != source[i].parent) {
                rebuild = true;
                break;
            }
    if (rebuild) {
        indices.clear();
        entities.assign(source.size(), {});
        for (size_t i = 0; i < source.size(); ++i) {
            need(indices.emplace(source[i].id, i).second, "Duplicate entity ID");
            entities[i].id = source[i].id;
            entities[i].parent = source[i].parent;
        }
        stats.hierarchyRebuilt = true;
    }
    visiting.assign(source.size(), 0);
    std::function<void(size_t)> resolve = [&](size_t i) {
        need(visiting[i] != 1, "Cyclic entity hierarchy");
        if (visiting[i] == 2)
            return;
        visiting[i] = 1;
        auto& c = entities[i];
        const auto& e = source[i];
        c.dirty = !c.initialized || c.local.position != e.transform.position ||
                  c.local.scale != e.transform.scale || c.local.rotation != e.transform.rotation;
        size_t parent = 0;
        if (e.parent) {
            auto it = indices.find(e.parent);
            need(it != indices.end(), "Missing parent entity");
            parent = it->second;
            resolve(parent);
            c.dirty = c.dirty || entities[parent].dirty;
        }
        if (c.dirty) {
            c.local = e.transform;
            c.root = c.local.matrix();
            if (e.parent)
                c.root = entities[parent].root * c.root;
            ++stats.rootsRecomputed;
            c.initialized = true;
        }
        visiting[i] = 2;
    };
    for (size_t i = 0; i < source.size(); ++i)
        resolve(i);
}
} // namespace one::detail
