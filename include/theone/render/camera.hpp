#pragma once
#include <theone/core/math.hpp>
namespace one {
struct Camera {
    glm::vec3 position{11, 8, 15};
    float yaw = -2.51f, pitch = -0.35f;
    float nearPlane = .1f, farPlane = 300.f;
    glm::vec3 forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }
    glm::mat4 view() const { return glm::lookAtRH(position, position + forward(), {0, 1, 0}); }
    glm::mat4 projection(float aspect) const {
        auto m = glm::perspectiveRH_ZO(1.05f, aspect, nearPlane, farPlane);
        m[1][1] *= -1;
        return m;
    }
};
} // namespace one
