#pragma once
#include <theone/assets/mesh.hpp>
#include <span>
namespace one::detail {
bool rayBounds(const Bounds& b, glm::vec3 origin, glm::vec3 direction, float nearest);
bool rayTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 origin, glm::vec3 direction,
                 float& nearest);
class PickingIndex {
    struct Node {
        Bounds bounds;
        uint32_t begin = 0, count = 0, left = 0, right = 0;
    };
    std::vector<Node> nodes_;
    std::vector<uint32_t> triangles_;
    uint32_t build(std::span<const SkinVertex> vertices, uint32_t begin, uint32_t end);
    bool query(std::span<const SkinVertex> vertices, uint32_t node, glm::vec3 origin, glm::vec3 direction,
               float& nearest, size_t& tested) const;

  public:
    explicit PickingIndex(std::span<const SkinVertex> vertices);
    bool intersect(std::span<const SkinVertex> vertices, glm::vec3 origin, glm::vec3 direction,
                   float& nearest, size_t& tested) const;
};
} // namespace one::detail
