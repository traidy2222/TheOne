#pragma once
#include <theone/assets/mesh.hpp>
#include <theone/core/cancellation.hpp>
#include <span>
namespace one {
inline Bounds transformedBounds(const Bounds& b, const glm::mat4& m) {
    Bounds result;
    if (!b.valid())
        return result;
    auto center = glm::vec3(m * glm::vec4((b.minimum + b.maximum) * .5f, 1));
    auto e = (b.maximum - b.minimum) * .5f;
    auto extent =
        glm::abs(glm::vec3(m[0])) * e.x + glm::abs(glm::vec3(m[1])) * e.y + glm::abs(glm::vec3(m[2])) * e.z;
    result.include(center - extent);
    result.include(center + extent);
    return result;
}
struct SkinBounds {
    Bounds rigid, unweighted;
    std::vector<Bounds> joints;
    bool safe = true;
    template <class Matrix>
    Bounds evaluate(const glm::mat4& root, size_t jointCount, Matrix jointMatrix) const {
        if (!safe || (jointCount && joints.size() > jointCount))
            return {};
        if (!jointCount)
            return transformedBounds(rigid, root);
        auto result = transformedBounds(unweighted, root);
        for (size_t j = 0; j < joints.size(); ++j)
            if (joints[j].valid()) {
                auto b = transformedBounds(joints[j], jointMatrix(j));
                result.include(b.minimum);
                result.include(b.maximum);
            }
        return result;
    }
};
struct MeshLod {
    std::vector<uint32_t> indices;
    float error = 0;
};
struct PreparedMesh {
    std::vector<SkinVertex> vertices;
    std::vector<MeshLod> levels;
    std::shared_ptr<const std::vector<uint8_t>> packed;
    SkinBounds bounds;
    glm::vec4 centerSum{0}, unweightedSum{0};
    std::vector<glm::vec4> jointSums;
    uint32_t maxJoint = 0;
};
std::shared_ptr<const PreparedMesh> prepareMesh(std::span<const SkinVertex> vertices,
                                                std::stop_token stop = {});
void buildMeshLods(PreparedMesh& mesh, std::stop_token stop = {});
// Error is projected at the nearest bound surface; hysteresis prevents threshold flicker.
size_t selectMeshLod(std::span<const float> errors, float pixelsPerUnit, size_t previous,
                     float threshold = 1.5f);
} // namespace one
