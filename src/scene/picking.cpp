#include <theone/scene/picking.hpp>
#include <theone/assets/model.hpp>
#include <theone/geometry/prepared_mesh.hpp>
#include "picking_index.hpp"
#include <unordered_map>
#include <limits>
namespace one {
struct ScenePicker::Impl {
    struct Entry {
        std::weak_ptr<const Model> owner;
        AssetHandle handle;
        std::unique_ptr<detail::PickingIndex> index;
    };
    std::unordered_map<const Primitive*, Entry> indices;
    std::vector<glm::vec3> posed;
};
ScenePicker::ScenePicker() : impl_(std::make_unique<Impl>()) {}
ScenePicker::~ScenePicker() = default;
void ScenePicker::clear() {
    impl_->indices.clear();
    impl_->posed.clear();
}
PickResult ScenePicker::pick(const RenderScene& scene, glm::vec3 origin, glm::vec3 direction) {
    PickResult result;
    result.distance = std::numeric_limits<float>::max();
    if (glm::length(direction) < 1e-8f)
        return result;
    direction = glm::normalize(direction);
    std::erase_if(impl_->indices, [](const auto& item) { return item.second.owner.expired(); });
    for (const auto& draw : scene.draws) {
        if (!draw.geometry || !draw.count)
            continue;
        const auto& primitive = *draw.geometry;
        const auto& root = scene.transforms.at(draw.palette);
        auto bounds = primitive.prepared
                          ? primitive.prepared->bounds.evaluate(
                                root.position, root.info.x,
                                [&](size_t j) -> const glm::mat4& {
                                    return scene.transforms.at(draw.palette + 1 + j).position;
                                })
                          : (root.info.x ? Bounds{} : transformedBounds(primitive.bounds, root.position));
        if (!detail::rayBounds(bounds, origin, direction, result.distance))
            continue;
        ++result.candidates;
        bool hit = false;
        if (!root.info.x) {
            auto inverse = glm::inverse(root.position);
            auto localOrigin = glm::vec3(inverse * glm::vec4(origin, 1));
            // Do not normalize: ray parameter stays in world units under nonuniform scale.
            auto localDirection = glm::vec3(inverse * glm::vec4(direction, 0));
            if (draw.owner) {
                auto& entry = impl_->indices[draw.geometry];
                if (!entry.index || entry.owner.lock() != draw.owner || entry.handle != draw.owner->handle) {
                    entry.owner = draw.owner;
                    entry.handle = draw.owner->handle;
                    entry.index = std::make_unique<detail::PickingIndex>(primitive.vertices);
                }
                hit = entry.index->intersect(primitive.vertices, localOrigin, localDirection, result.distance,
                                             result.triangles);
            } else {
                detail::PickingIndex index(primitive.vertices);
                hit = index.intersect(primitive.vertices, localOrigin, localDirection, result.distance,
                                      result.triangles);
            }
        } else {
            auto& posed = impl_->posed;
            posed.clear();
            posed.reserve(primitive.vertices.size());
            for (const auto& v : primitive.vertices) {
                float weight = glm::dot(v.weights, glm::vec4(1));
                glm::vec4 position(0);
                if (weight > 1e-5f) {
                    for (int j = 0; j < 4; ++j)
                        if (v.weights[j] > 0) {
                            if (v.joints[j] >= root.info.x)
                                throw std::runtime_error("Picking joint out of range");
                            position += scene.transforms.at(draw.palette + 1 + v.joints[j]).position *
                                        glm::vec4(v.vertex.position, 1) * (v.weights[j] / weight);
                        }
                } else
                    position = root.position * glm::vec4(v.vertex.position, 1);
                posed.push_back(glm::vec3(position));
            }
            result.skinnedVertices += posed.size();
            for (size_t i = 0; i + 2 < posed.size(); i += 3) {
                ++result.triangles;
                hit = detail::rayTriangle(posed[i], posed[i + 1], posed[i + 2], origin, direction,
                                          result.distance) ||
                      hit;
            }
        }
        if (hit)
            result.entity = draw.entity;
    }
    return result;
}
} // namespace one
