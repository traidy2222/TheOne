#pragma once
#include <theone/render/draw.hpp>
#include <algorithm>
#include <vector>
namespace one::render {
// Compact opaque draws in their original order; only transparent draws need sorting.
// Stable ties retain the old blend order. Readiness is evaluated before moving owners.
template <class Ready, class Distance>
void orderDraws(std::vector<Draw>& draws, Ready ready, Distance distance) {
    struct Transparent {
        Draw draw;
        float distance;
    };
    std::vector<Transparent> blended;
    size_t opaqueCount = 0;
    for (size_t i = 0; i < draws.size(); ++i) {
        auto& draw = draws[i];
        if (!ready(draw))
            continue;
        if (draw.material && draw.material->blend) {
            const auto depth = distance(draw);
            blended.push_back({std::move(draw), depth});
        } else {
            if (opaqueCount != i)
                draws[opaqueCount] = std::move(draw);
            ++opaqueCount;
        }
    }
    draws.resize(opaqueCount);
    std::stable_sort(blended.begin(), blended.end(),
                     [](const auto& a, const auto& b) { return a.distance > b.distance; });
    for (auto& item : blended)
        draws.push_back(std::move(item.draw));
}
} // namespace one::render
