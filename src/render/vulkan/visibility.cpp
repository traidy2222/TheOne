#include "backend.hpp"
namespace one {
void Renderer::Impl::prepareVisibility(std::vector<Draw>& draws, const RenderScene* scene,
                                       const Camera& camera, bool shadows) {
    const Frustum frustum(
        camera.projection(float(swapchainState.extent.width) / swapchainState.extent.height) * camera.view());
    boundsSnapshots.resize(draws.size());
    spatialBounds.resize(draws.size());
    for (size_t i = 0; i < draws.size(); ++i) {
        auto& draw = draws[i];
        auto& snapshot = boundsSnapshots[i];
        auto* mesh = resolvedMesh(draw);
        if (draw.geometry && scene && mesh != nullptr)
            prepareDrawVisibility(draw, *scene, camera, *mesh, snapshot);
        else
            snapshot = {};
        spatialBounds[i] = draw.worldBounds;
        draw.cameraVisible = !culling;
        draw.shadowVisibility = shadows && !culling ? 7 : 0;
    }
    if (!culling)
        return;
    auto query = [&](const Frustum& view, uint8_t mask) {
        auto mark = [&](uint32_t i) {
            if (mask)
                draws[i].shadowVisibility |= mask;
            else
                draws[i].cameraVisible = true;
        };
        if (spatialCulling)
            spatialRegions.query(spatialBounds, view, mark, stats.regionTests, stats.regionRejectedDraws);
        else
            for (uint32_t i = 0; i < draws.size(); ++i)
                if (view.intersects(spatialBounds[i], glm::mat4(1)))
                    mark(i);
    };
    if (spatialCulling)
        spatialRegions.update(spatialBounds);
    query(frustum, 0);
    if (shadows)
        for (uint32_t c = 0; c < shadowCascadeCount; ++c)
            query(Frustum(sceneState.cascades.matrices[c]), uint8_t(1u << c));
}
void Renderer::Impl::prepareDrawVisibility(Draw& draw, const RenderScene& scene, const Camera& camera,
                                           vulkan::MeshGPU& mesh, vulkan::BoundsSnapshot& snapshot) {
    const auto& transform = scene.transforms.at(draw.palette);
    if (!transform.info.x && snapshot.matches(draw.geometry, mesh.generation, transform.position)) {
        draw.worldBounds = snapshot.bounds;
        ++stats.rigidBoundsReused;
    } else {
        draw.worldBounds = mesh.prepared->bounds.evaluate(
            transform.position, transform.info.x,
            [&](size_t j) -> const glm::mat4& { return scene.transforms.at(draw.palette + 1 + j).position; });
        ++stats.boundsEvaluated;
        if (!transform.info.x)
            snapshot = {draw.geometry, transform.position, draw.worldBounds, mesh.generation};
        else
            snapshot = {};
    }
    if (!lodEnabled || transform.info.x || mesh.levels.size() < 2 || !draw.worldBounds.valid() ||
        (draw.material && (draw.material->mask || draw.material->blend)))
        return;
    auto center = (draw.worldBounds.minimum + draw.worldBounds.maximum) * .5f;
    float radius = glm::length(draw.worldBounds.maximum - center);
    float distance = std::max(glm::length(center - camera.position) - radius, camera.nearPlane);
    float pixelsPerUnit = float(swapchainState.extent.height) / (2 * std::tan(.525f) * distance);
    float scale = std::sqrt(glm::dot(glm::vec3(transform.position[0]), glm::vec3(transform.position[0])) +
                            glm::dot(glm::vec3(transform.position[1]), glm::vec3(transform.position[1])) +
                            glm::dot(glm::vec3(transform.position[2]), glm::vec3(transform.position[2])));
    auto& previous = mesh.lodHistory[draw.entity];
    auto selected = selectMeshLod(mesh.lodErrors, pixelsPerUnit * scale, previous, lodPixelError);
    float size = radius * pixelsPerUnit;
    if (size > 180)
        selected = 0;
    else if (size > 60)
        selected = std::min<size_t>(selected, 1);
    previous = selected;
    draw.lod = uint32_t(selected);
}
bool Renderer::Impl::visible(const Draw& draw, const RenderScene*, const Frustum& frustum) const {
    return !culling || frustum.intersects(draw.worldBounds, glm::mat4(1));
}
} // namespace one
