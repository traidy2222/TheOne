#pragma once
#include <theone/core/math.hpp>
namespace one {
struct Camera {
    glm::vec3 position{11, 8, 15};
    float yaw = -2.51f, pitch = -0.35f;
    float nearPlane = .1f, farPlane = 300.f;
    float roll = 0;
    float verticalFov = 1.05f;
    glm::vec3 forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }
    glm::vec3 up() const {
        auto f = forward(), right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
        return glm::cross(right, f) * std::cos(roll) + right * std::sin(roll);
    }
    glm::mat4 view() const { return glm::lookAtRH(position, position + forward(), up()); }
    glm::mat4 projection(float aspect) const {
        auto m = glm::perspectiveRH_ZO(verticalFov, aspect, nearPlane, farPlane);
        m[1][1] *= -1;
        return m;
    }
};
} // namespace one
