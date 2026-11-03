#pragma once
#include <theone/render/camera.hpp>
#include <array>
namespace one {
inline constexpr uint32_t shadowCascadeCount = 3, shadowResolution = 2048;
struct ShadowCascades {
    std::array<glm::mat4, shadowCascadeCount> matrices;
    glm::vec4 splits{0};
};
ShadowCascades fitShadowCascades(const Camera& camera, float aspect, float distance = 150,
                                 glm::vec3 direction = glm::normalize(glm::vec3(-20, 35, 18)));
} // namespace one
