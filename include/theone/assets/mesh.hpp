#pragma once
#include <theone/core/math.hpp>
#include <vector>
#include <memory>
#include <theone/core/bounds.hpp>
namespace one {
struct Vertex {
    glm::vec3 position{0}, normal{0, 1, 0};
    glm::vec4 color{1};
    glm::vec2 uv{0};
};
struct SkinVertex {
    Vertex vertex;
    glm::uvec4 joints{0};
    glm::vec4 weights{0};
};
struct PreparedMesh;
struct Primitive {
    std::vector<SkinVertex> vertices;
    int material = -1;
    Bounds bounds;
    std::shared_ptr<const PreparedMesh> prepared;
};
} // namespace one
