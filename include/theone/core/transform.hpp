#pragma once
#include <theone/core/math.hpp>
namespace one {
struct Transform {
    glm::vec3 position{0}, scale{1};
    glm::quat rotation{1, 0, 0, 0};
    glm::mat4 matrix() const {
        return glm::translate(glm::mat4(1), position) * glm::mat4_cast(rotation) *
               glm::scale(glm::mat4(1), scale);
    }
};
} // namespace one
