#include <theone/scene/world.hpp>
#include <set>
#include "core/validation.hpp"
namespace one {
using detail::need;
Entity* World::find(uint64_t id) {
    for (auto& e : entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
const Entity* World::find(uint64_t id) const {
    for (const auto& e : entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
glm::mat4 World::matrix(uint64_t id) const {
    glm::mat4 result(1);
    std::set<uint64_t> seen;
    while (id) {
        need(seen.insert(id).second, "Cyclic entity hierarchy");
        auto e = find(id);
        need(e != nullptr, "Missing parent entity");
        result = e->transform.matrix() * result;
        id = e->parent;
    }
    return result;
}
} // namespace one
