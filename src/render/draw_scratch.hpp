#pragma once
#include <theone/render/draw.hpp>
#include <span>
#include <vector>
namespace one::render {
struct DrawScratch {
    static constexpr size_t retainedBytes = 2 * 1024 * 1024;
    std::vector<Draw> draws;
    bool assign(std::span<const Draw> dynamic, std::span<const Draw> scene) {
        auto count = dynamic.size() + scene.size();
        bool grew = count > draws.capacity();
        if (grew) draws.reserve(count);
        draws.assign(dynamic.begin(), dynamic.end());
        draws.insert(draws.end(), scene.begin(), scene.end());
        return grew;
    }
    void release() {
        draws.clear(); // Do not retain model owners between frames or after reload.
        if (draws.capacity() > retainedBytes / sizeof(Draw)) std::vector<Draw>().swap(draws);
    }
};
struct DrawScratchLease {
    DrawScratch& scratch;
    ~DrawScratchLease() { scratch.release(); }
};
}
