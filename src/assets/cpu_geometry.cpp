#include <theone/assets/model.hpp>
#include "core/validation.hpp"
namespace one {
using detail::need;
void Model::bake(const glm::mat4& root, float time, int clip, bool animate, uint64_t id,
                 std::vector<Vertex>& vertices, std::vector<Draw>& draws, int previousClip,
                 float previousTime, float blend, bool loop, bool previousLoop) const {
    auto world = pose(time, clip, animate, previousClip, previousTime, blend, loop, previousLoop);
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!n.active || n.mesh < 0)
            continue;
        const auto meshMatrix = root * world[i];
        need(std::abs(glm::determinant(glm::mat3(meshMatrix))) > 1e-10f, "Singular mesh transform");
        const auto meshNormal = glm::transpose(glm::inverse(glm::mat3(meshMatrix)));
        std::vector<glm::mat4> jointMatrices;
        std::vector<glm::mat3> jointNormals;
        if (n.skin >= 0) {
            const auto& skin = skins[n.skin];
            jointMatrices.reserve(skin.joints.size());
            jointNormals.reserve(skin.joints.size());
            for (size_t joint = 0; joint < skin.joints.size(); ++joint) {
                auto matrix = root * world[skin.joints[joint]] * skin.inverseBind[joint];
                need(std::abs(glm::determinant(glm::mat3(matrix))) > 1e-10f, "Singular joint transform");
                jointMatrices.push_back(matrix);
                jointNormals.push_back(glm::transpose(glm::inverse(glm::mat3(matrix))));
            }
        }
        for (const auto& p : meshes[n.mesh]) {
            Draw draw;
            draw.first = static_cast<uint32_t>(vertices.size());
            draw.count = static_cast<uint32_t>(p.vertices.size());
            draw.entity = id;
            if (p.material >= 0) {
                draw.material = &materials[p.material];
                if (draw.material->image >= 0)
                    draw.image = &images[draw.material->image];
            }
            for (const auto& input : p.vertices) {
                Vertex v = input.vertex;
                float weight = glm::dot(input.weights, glm::vec4(1));
                if (n.skin >= 0 && weight > 0.00001f) {
                    glm::vec4 position(0);
                    glm::vec3 normal(0);
                    for (int j = 0; j < 4; ++j)
                        if (input.weights[j] > 0) {
                            need(input.joints[j] < jointMatrices.size(), "Skin joint index out of range");
                            float factor = input.weights[j] / weight;
                            position += jointMatrices[input.joints[j]] * glm::vec4(v.position, 1) * factor;
                            normal += jointNormals[input.joints[j]] * v.normal * factor;
                        }
                    v.position = position;
                    v.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                } else {
                    v.position = meshMatrix * glm::vec4(v.position, 1);
                    auto normal = meshNormal * v.normal;
                    v.normal = glm::length(normal) > 1e-8f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
                }
                vertices.push_back(v);
            }
            draws.push_back(draw);
        }
    }
}
} // namespace one
