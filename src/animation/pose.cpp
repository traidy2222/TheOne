#include <theone/animation/pose.hpp>
#include <algorithm>
#include <functional>
#include "core/validation.hpp"
namespace one {
using detail::need;
std::vector<glm::mat4> samplePose(const std::vector<Node>& nodes, const std::vector<Animation>& animations,
                                  float time, int clip, bool animate, int previousClip, float previousTime,
                                  float blend, bool loop, bool previousLoop) {
    auto samplePose = [&](int clip, float time, bool loop) {
        std::vector<Transform> local;
        for (const auto& n : nodes)
            local.push_back(n.transform);
        if (animate && clip >= 0 && clip < static_cast<int>(animations.size())) {
            const auto& a = animations[clip];
            float t = a.duration > 0 ? (loop ? std::fmod(std::max(time, 0.0f), a.duration)
                                             : std::clamp(time, 0.0f, a.duration))
                                     : 0;
            for (const auto& c : a.channels) {
                auto it = std::upper_bound(c.times.begin(), c.times.end(), t);
                size_t next = std::min(static_cast<size_t>(it - c.times.begin()), c.times.size() - 1),
                       prev = it == c.times.end() ? next : (next ? next - 1 : 0);
                float f = c.step || next == prev
                              ? 0
                              : std::clamp((t - c.times[prev]) / (c.times[next] - c.times[prev]), 0.0f, 1.0f);
                auto value = glm::mix(c.values[prev], c.values[next], f);
                if (c.cubic && next != prev) {
                    float f2 = f * f, f3 = f2 * f, dt = c.times[next] - c.times[prev];
                    value = (2 * f3 - 3 * f2 + 1) * c.values[prev] +
                            (f3 - 2 * f2 + f) * dt * c.outTangents[prev] +
                            (-2 * f3 + 3 * f2) * c.values[next] + (f3 - f2) * dt * c.inTangents[next];
                }
                auto& tr = local[c.node];
                if (c.path == 0)
                    tr.position = value;
                else if (c.path == 2)
                    tr.scale = value;
                else if (c.cubic) {
                    tr.rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
                } else {
                    auto q0 = c.values[prev], q1 = c.values[next];
                    tr.rotation = glm::normalize(
                        glm::slerp(glm::quat(q0.w, q0.x, q0.y, q0.z), glm::quat(q1.w, q1.x, q1.y, q1.z), f));
                }
            }
        }
        return local;
    };
    auto local = samplePose(clip, time, loop);
    if (animate && previousClip >= 0 && previousClip < static_cast<int>(animations.size()) && blend < 1) {
        auto previous = samplePose(previousClip, previousTime, previousLoop);
        float factor = std::clamp(blend, 0.0f, 1.0f);
        for (size_t i = 0; i < local.size(); ++i) {
            local[i].position = glm::mix(previous[i].position, local[i].position, factor);
            local[i].scale = glm::mix(previous[i].scale, local[i].scale, factor);
            local[i].rotation = glm::normalize(glm::slerp(previous[i].rotation, local[i].rotation, factor));
        }
    }
    std::vector<glm::mat4> world(nodes.size(), glm::mat4(1));
    std::vector<int> state(nodes.size(), 0);
    std::function<glm::mat4(int)> resolve = [&](int i) {
        need(state[i] != 1, "Cyclic glTF hierarchy");
        if (state[i] == 2)
            return world[i];
        state[i] = 1;
        auto m = nodes[i].useMatrix ? nodes[i].matrix : local[i].matrix();
        world[i] = nodes[i].parent < 0 ? m : resolve(nodes[i].parent) * m;
        state[i] = 2;
        return world[i];
    };
    for (size_t i = 0; i < nodes.size(); ++i)
        resolve(static_cast<int>(i));
    return world;
}
} // namespace one
