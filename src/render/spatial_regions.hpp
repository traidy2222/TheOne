#pragma once
#include <theone/core/bounds.hpp>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>
namespace one::render {
// Median-split topology; only changed leaves and their ancestors are refitted. Invalid bounds
// remain visible. Leaves keep original draw indices, so query order cannot reorder rendering.
class SpatialRegions {
    struct Node {
        Bounds bounds;
        uint32_t begin = 0, count = 0, left = 0, right = 0;
        bool bounded = false, dirty = true;
        uint32_t parent = UINT32_MAX;
    };
    std::vector<Node> nodes_;
    std::vector<uint32_t> order_, leaves_;
    std::vector<Bounds> previous_;
    size_t refitted_ = 0;
    uint32_t build(std::span<const Bounds> bounds, uint32_t begin, uint32_t end);
    void refit(std::span<const Bounds> bounds, uint32_t node);
    // -1 outside, 0 crossing, 1 entirely inside.
    static int classify(const Bounds& bounds, const Frustum& frustum);
    template <class Visible>
    void queryNode(std::span<const Bounds> bounds, const Frustum& frustum, uint32_t index, Visible& visible,
                   size_t& tests, size_t& rejected) const {
        const auto& node = nodes_[index];
        ++tests;
        if (node.bounded) {
            auto relation = classify(node.bounds, frustum);
            if (relation < 0) {
                rejected += node.count;
                return;
            }
            if (relation > 0) {
                for (uint32_t i = node.begin; i < node.begin + node.count; ++i)
                    visible(order_[i]);
                return;
            }
        }
        if (node.left) {
            queryNode(bounds, frustum, node.left, visible, tests, rejected);
            queryNode(bounds, frustum, node.right, visible, tests, rejected);
        } else
            for (uint32_t i = node.begin; i < node.begin + node.count; ++i)
                if (frustum.intersects(bounds[order_[i]], glm::mat4(1)))
                    visible(order_[i]);
    }

  public:
    void update(std::span<const Bounds> bounds);
    size_t refittedNodes() const { return refitted_; }
    void clear() {
        nodes_.clear();
        order_.clear();
        leaves_.clear();
        previous_.clear();
    }
    template <class Visible>
    void query(std::span<const Bounds> bounds, const Frustum& frustum, Visible visible, size_t& tests,
               size_t& rejected) const {
        if (!nodes_.empty())
            queryNode(bounds, frustum, 0, visible, tests, rejected);
    }
};
} // namespace one::render
