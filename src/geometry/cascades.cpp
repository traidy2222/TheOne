#include <theone/render/shadows.hpp>
#include <algorithm>
#include <cmath>
namespace one {
ShadowCascades fitShadowCascades(const Camera& camera, float aspect, float distance, glm::vec3 direction) {
    ShadowCascades result;
    float near = std::max(.001f, camera.nearPlane);
    float far = std::max(near + .01f, std::min(camera.farPlane, distance));
    for (uint32_t c = 0; c < shadowCascadeCount; ++c) {
        float fraction = float(c + 1) / shadowCascadeCount;
        result.splits[c] =
            .65f * near * std::pow(far / near, fraction) + .35f * (near + (far - near) * fraction);
    }
    result.splits.w = near;
    auto inverseView = glm::inverse(camera.view());
    auto lightView = glm::lookAtRH(glm::vec3(0), -direction,
                                   std::abs(direction.y) > .98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0));
    float previous = near, beforePrevious = near;
    for (uint32_t c = 0; c < shadowCascadeCount; ++c) {
        float start = c ? previous - .1f * (previous - beforePrevious) : near, end = result.splits[c];
        std::array<glm::vec3, 8> corners;
        size_t index = 0;
        glm::vec3 center(0);
        for (float z : {start, end})
            for (float y : {-1.f, 1.f})
                for (float x : {-1.f, 1.f}) {
                    auto point = glm::vec3(inverseView * glm::vec4(x * z * std::tan(.525f) * aspect,
                                                                   y * z * std::tan(.525f), -z, 1));
                    corners[index++] = point;
                    center += point / 8.f;
                }
        float radius = 0;
        for (auto p : corners)
            radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius * 16) / 16;
        radius *= float(shadowResolution) / float(shadowResolution - 2);
        auto lightCenter = glm::vec3(lightView * glm::vec4(center, 1));
        float texel = 2 * radius / shadowResolution;
        lightCenter.x = std::round(lightCenter.x / texel) * texel;
        lightCenter.y = std::round(lightCenter.y / texel) * texel;
        // Extend along the light direction so off-camera casters can shadow visible receivers.
        float reach = std::max(80.f, radius);
        auto projection = glm::orthoRH_ZO(lightCenter.x - radius, lightCenter.x + radius,
                                          lightCenter.y - radius, lightCenter.y + radius,
                                          -lightCenter.z - radius - reach, -lightCenter.z + radius + reach);
        projection[1][1] *= -1;
        projection[3][1] *= -1;
        result.matrices[c] = projection * lightView;
        beforePrevious = previous;
        previous = end;
    }
    return result;
}
} // namespace one
