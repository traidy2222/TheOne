#pragma once
#include <theone/core/math.hpp>
#include <array>
#include <limits>
namespace one {
struct Bounds {
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    bool valid() const { return glm::all(glm::lessThanEqual(minimum, maximum)); }
    void include(glm::vec3 p) {
        minimum = glm::min(minimum, p);
        maximum = glm::max(maximum, p);
    }
};
// Vulkan clip volume: -w <= x,y <= w and 0 <= z <= w.
struct Frustum {
    std::array<glm::vec4, 6> planes;
    explicit Frustum(const glm::mat4& vp) {
        auto m = glm::transpose(vp);
        planes = {m[3] + m[0], m[3] - m[0], m[3] + m[1], m[3] - m[1], m[2], m[3] - m[2]};
    }
    bool intersects(const Bounds& bounds, const glm::mat4& transform) const {
        if (!bounds.valid())
            return true;
        auto center = glm::vec3(transform * glm::vec4((bounds.minimum + bounds.maximum) * .5f, 1));
        auto localExtent = (bounds.maximum - bounds.minimum) * .5f;
        auto extent = glm::abs(glm::vec3(transform[0])) * localExtent.x +
                      glm::abs(glm::vec3(transform[1])) * localExtent.y +
                      glm::abs(glm::vec3(transform[2])) * localExtent.z;
        for (auto plane : planes) {
            auto n = glm::vec3(plane);
            if (glm::dot(n, center) + plane.w + glm::dot(glm::abs(n), extent) < -1e-5f)
                return false;
        }
        return true;
    }
};
} // namespace one
