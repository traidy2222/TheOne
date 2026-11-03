#include "spatial_regions.hpp"
#include <algorithm>
namespace one::render {
int SpatialRegions::classify(const Bounds& bounds, const Frustum& frustum) {
    auto center = (bounds.minimum + bounds.maximum) * .5f;
    auto extent = (bounds.maximum - bounds.minimum) * .5f;
    bool contained = true;
    for (const auto& plane : frustum.planes) {
        auto n = glm::vec3(plane);
        float distance = glm::dot(n, center) + plane.w, radius = glm::dot(glm::abs(n), extent);
        if (distance + radius < -1e-5f)
            return -1;
        contained &= distance - radius > 1e-5f;
    }
    return contained ? 1 : 0;
}
uint32_t SpatialRegions::build(std::span<const Bounds> bounds, uint32_t begin, uint32_t end) {
    uint32_t index = uint32_t(nodes_.size());
    nodes_.push_back({{}, begin, end - begin});
    if (end - begin > 16) {
        Bounds centers;
        for (uint32_t i = begin; i < end; ++i)
            if (bounds[order_[i]].valid())
                centers.include((bounds[order_[i]].minimum + bounds[order_[i]].maximum) * .5f);
        auto size = centers.valid() ? centers.maximum - centers.minimum : glm::vec3(0);
        int axis = size.y > size.x ? 1 : 0;
        if (size.z > size[axis])
            axis = 2;
        auto center = [&](uint32_t i) {
            return bounds[i].valid() ? (bounds[i].minimum[axis] + bounds[i].maximum[axis]) * .5f : 0.f;
        };
        auto middle = begin + (end - begin) / 2;
        std::nth_element(order_.begin() + begin, order_.begin() + middle, order_.begin() + end,
                         [&](auto a, auto b) { return center(a) < center(b); });
        auto left = build(bounds, begin, middle), right = build(bounds, middle, end);
        nodes_[index].left = left;
        nodes_[index].right = right;
        nodes_[left].parent = nodes_[right].parent = index;
    }
    if (!nodes_[index].left)
        for (uint32_t i = begin; i < end; ++i)
            leaves_[order_[i]] = index;
    return index;
}
void SpatialRegions::refit(std::span<const Bounds> bounds, uint32_t index) {
    auto& node = nodes_[index];
    node.bounds = {};
    node.bounded = true;
    auto include = [&](const Bounds& b, bool valid) {
        node.bounded &= valid;
        if (valid) {
            node.bounds.include(b.minimum);
            node.bounds.include(b.maximum);
        }
    };
    if (node.left) {
        include(nodes_[node.left].bounds, nodes_[node.left].bounded);
        include(nodes_[node.right].bounds, nodes_[node.right].bounded);
    } else
        for (uint32_t i = node.begin; i < node.begin + node.count; ++i)
            include(bounds[order_[i]], bounds[order_[i]].valid());
}
void SpatialRegions::update(std::span<const Bounds> bounds) {
    refitted_ = 0;
    if (order_.size() != bounds.size() || (nodes_.empty() && !bounds.empty())) {
        clear();
        order_.resize(bounds.size());
        leaves_.resize(bounds.size());
        std::iota(order_.begin(), order_.end(), 0u);
        if (!bounds.empty())
            build(bounds, 0, uint32_t(bounds.size()));
        previous_.assign(bounds.begin(), bounds.end());
    } else {
        for (size_t i = 0; i < bounds.size(); ++i) {
            if (previous_[i].minimum == bounds[i].minimum && previous_[i].maximum == bounds[i].maximum)
                continue;
            previous_[i] = bounds[i];
            for (auto n = leaves_[i]; n != UINT32_MAX && !nodes_[n].dirty; n = nodes_[n].parent)
                nodes_[n].dirty = true;
        }
    }
    // Children are allocated after parents. Reverse order evaluates each dirty node once.
    for (size_t i = nodes_.size(); i > 0; --i)
        if (nodes_[i - 1].dirty) {
            refit(bounds, uint32_t(i - 1));
            nodes_[i - 1].dirty = false;
            ++refitted_;
        }
}
} // namespace one::render
