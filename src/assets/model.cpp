#include <theone/assets/model.hpp>
#include <theone/animation/pose.hpp>
#include <atomic>
namespace one {
AssetHandle newAssetHandle() {
    static std::atomic<uint64_t> next{1};
    return {next.fetch_add(1, std::memory_order_relaxed), 1};
}
std::vector<glm::mat4> Model::pose(float time, int clip, bool animate, int previousClip, float previousTime,
                                   float blend, bool loop, bool previousLoop) const {
    return samplePose(nodes, animations, time, clip, animate, previousClip, previousTime, blend, loop,
                      previousLoop);
}
} // namespace one
