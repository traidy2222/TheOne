#include "picking_index.hpp"
#include <algorithm>
#include <numeric>
namespace one::detail {
bool rayBounds(const Bounds& b, glm::vec3 origin, glm::vec3 direction, float nearest) {
    if (!b.valid())
        return true;
    float nearHit = 0, farHit = nearest;
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0) {
            if (origin[axis] < b.minimum[axis] || origin[axis] > b.maximum[axis])
                return false;
        } else {
            float a = (b.minimum[axis] - origin[axis]) / direction[axis];
            float c = (b.maximum[axis] - origin[axis]) / direction[axis];
            if (a > c)
                std::swap(a, c);
            nearHit = std::max(nearHit, a);
            farHit = std::min(farHit, c);
            if (nearHit > farHit)
                return false;
        }
    }
    return true;
}
bool rayTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 origin, glm::vec3 direction,
                 float& nearest) {
    auto e1 = b - a, e2 = c - a, h = glm::cross(direction, e2);
    float det = glm::dot(e1, h);
    if (std::abs(det) < 1e-7f)
        return false;
    auto offset = origin - a;
    float u = glm::dot(offset, h) / det;
    if (u < 0 || u > 1)
        return false;
    auto q = glm::cross(offset, e1);
    float v = glm::dot(direction, q) / det;
    if (v < 0 || u + v > 1)
        return false;
    float t = glm::dot(e2, q) / det;
    if (t <= 0 || t >= nearest)
        return false;
    nearest = t;
    return true;
}
PickingIndex::PickingIndex(std::span<const SkinVertex> vertices) {
    triangles_.resize(vertices.size() / 3);
    std::iota(triangles_.begin(), triangles_.end(), 0u);
    if (!triangles_.empty())
        build(vertices, 0, uint32_t(triangles_.size()));
}
uint32_t PickingIndex::build(std::span<const SkinVertex> vertices, uint32_t begin, uint32_t end) {
    auto index = uint32_t(nodes_.size());
    nodes_.push_back({{}, begin, end - begin});
    Bounds bounds;
    for (auto i = begin; i < end; ++i)
        for (uint32_t j = 0; j < 3; ++j)
            bounds.include(vertices[triangles_[i] * 3 + j].vertex.position);
    nodes_[index].bounds = bounds;
    if (end - begin > 8) {
        auto size = bounds.maximum - bounds.minimum;
        int axis = size.y > size.x ? 1 : 0;
        if (size.z > size[axis])
            axis = 2;
        auto center = [&](uint32_t t) {
            return vertices[t * 3].vertex.position[axis] + vertices[t * 3 + 1].vertex.position[axis] +
                   vertices[t * 3 + 2].vertex.position[axis];
        };
        auto mid = begin + (end - begin) / 2;
        std::nth_element(triangles_.begin() + begin, triangles_.begin() + mid, triangles_.begin() + end,
                         [&](auto a, auto b) { return center(a) < center(b); });
        auto left = build(vertices, begin, mid), right = build(vertices, mid, end);
        nodes_[index].left = left;
        nodes_[index].right = right;
    }
    return index;
}
bool PickingIndex::query(std::span<const SkinVertex> vertices, uint32_t index, glm::vec3 origin,
                         glm::vec3 direction, float& nearest, size_t& tested) const {
    const auto& node = nodes_[index];
    if (!rayBounds(node.bounds, origin, direction, nearest))
        return false;
    if (node.left) {
        bool a = query(vertices, node.left, origin, direction, nearest, tested);
        bool b = query(vertices, node.right, origin, direction, nearest, tested);
        return a || b;
    }
    bool hit = false;
    for (auto i = node.begin; i < node.begin + node.count; ++i) {
        auto t = triangles_[i] * 3;
        ++tested;
        hit = rayTriangle(vertices[t].vertex.position, vertices[t + 1].vertex.position,
                          vertices[t + 2].vertex.position, origin, direction, nearest) ||
              hit;
    }
    return hit;
}
bool PickingIndex::intersect(std::span<const SkinVertex> vertices, glm::vec3 origin, glm::vec3 direction,
                             float& nearest, size_t& tested) const {
    return !nodes_.empty() && query(vertices, 0, origin, direction, nearest, tested);
}
} // namespace one::detail
