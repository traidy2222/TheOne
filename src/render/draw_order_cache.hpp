#pragma once
#include "draw_order.hpp"
#include <numeric>
namespace one::render {
struct DrawDepthSnapshot {
    const Primitive* geometry = nullptr;
    glm::mat4 transform{1};
    glm::vec3 camera{0};
    float depth = 0;
    uint64_t generation = 0;
};
struct DrawOrderCache {
    struct Blend { Draw draw; float depth; };
    std::vector<Blend> blended;
    std::vector<DrawDepthSnapshot> depths;
    std::vector<size_t> blendOrder;
    template <class Ready, class Distance>
    bool order(std::vector<Draw>& draws, Ready ready, Distance distance) {
        blended.clear();
        size_t opaque = 0;
        for (size_t i = 0; i < draws.size(); ++i) {
            auto& draw = draws[i];
            if (!ready(draw))
                continue;
            if (draw.material && draw.material->blend) {
                auto depth = distance(draw, i);
                blended.push_back({std::move(draw), depth});
            } else {
                if (opaque != i)
                    draws[opaque] = std::move(draw);
                ++opaque;
            }
        }
        draws.resize(opaque);
        bool sameSize = blendOrder.size() == blended.size();
        if (!sameSize) {
            blendOrder.resize(blended.size());
            std::iota(blendOrder.begin(), blendOrder.end(), 0);
        }
        // Ties always follow source order, even if depths tied after crossing last frame.
        auto less = [&](size_t a, size_t b) {
            return blended[a].depth == blended[b].depth ? a < b : blended[a].depth > blended[b].depth;
        };
        bool reused = sameSize && std::is_sorted(blendOrder.begin(), blendOrder.end(), less);
        if (!reused)
            std::sort(blendOrder.begin(), blendOrder.end(), less);
        for (auto i : blendOrder)
            draws.push_back(std::move(blended[i].draw));
        blended.clear();
        if (blended.capacity() > 1024 * 1024 / sizeof(Blend)) std::vector<Blend>().swap(blended);
        return reused;
    }
};
} // namespace one::render
