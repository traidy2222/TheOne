#include "backend.hpp"
namespace one {
uint32_t Renderer::Impl::shadowMeshLod(const Draw& draw, const RenderScene* scene, uint32_t cascade,
                                       vulkan::MeshGPU& mesh) {
    // A merged instance group may span different scales; keep its conservative full mesh.
    if (!shadowLod || !lodEnabled || !scene || !draw.geometry || draw.instances > 1 ||
        (draw.material && (draw.material->mask || draw.material->blend)))
        return 0;
    const auto& transform = scene->transforms.at(draw.palette);
    if (transform.info.x)
        return 0;
    if (mesh.levels.size() < 2)
        return 0;
    auto& history = mesh.shadowLodHistory[draw.entity];
    const glm::mat3 linear(transform.position);
    if (history.transform != linear) {
        history.transform = linear;
        history.scale = std::sqrt(glm::dot(linear[0], linear[0]) + glm::dot(linear[1], linear[1]) +
                                  glm::dot(linear[2], linear[2]));
    }
    const float density = shadowTexelsPerUnit[cascade] * history.scale;
    if (history.density[cascade] != density) {
        history.density[cascade] = density;
        history.level[cascade] = selectMeshLod(mesh.lodErrors, density, history.level[cascade], .5f);
    }
    return uint32_t(history.level[cascade]);
}
} // namespace one
