#pragma once
#include <theone/render/draw.hpp>
namespace one::vulkan {
// A rigid bound depends on immutable geometry and the exact root matrix, not the camera.
// Residency generations reject stale snapshots even when geometry addresses are reused.
struct BoundsSnapshot {
    const Primitive* geometry = nullptr;
    glm::mat4 transform{1};
    Bounds bounds;
    uint64_t generation = 0;
    bool matches(const Primitive* source, uint64_t version, const glm::mat4& matrix) const {
        return geometry == source && generation == version && transform == matrix;
    }
};
} // namespace one::vulkan
