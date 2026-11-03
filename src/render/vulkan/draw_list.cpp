#include "backend.hpp"
#include "render/draw_order.hpp"
namespace one {
void Renderer::Impl::finishDrawList(std::vector<Draw>& draws, const std::vector<Vertex>& vertices,
                                    const RenderScene* scene, const Camera& camera, bool cachedDynamic) {
    auto ready = [&](const Draw& draw) {
        bool pending = !draw.geometry && cachedDynamic && frame().geometry.dynamicUpload &&
                       !frame().geometry.dynamicUpload->ready;
        if (draw.geometry) {
            auto* mesh = resolvedMesh(draw);
            pending =
                !mesh || (mesh->upload && !mesh->upload->ready);
        }
        if (pending || (draw.cameraVisible && (referenceDrawPreparation || !draw.materialReady) && !prepareMaterial(draw)) ||
            (draw.shadowVisibility && draw.material && draw.material->mask && !draw.material->blend &&
                !prepareShadowMask(draw))) {
            ++stats.pendingDraws;
            return false;
        }
        return true;
    };
    auto distance = [&](const Draw& draw) {
        glm::vec3 center(0);
        if (draw.geometry)
            center = geometryCenter(draw, *scene);
        else {
            if (uint64_t(draw.first) + draw.count > vertices.size())
                throw std::runtime_error("Draw outside vertex buffer");
            for (uint32_t i = 0; i < draw.count; ++i)
                center += vertices[draw.first + i].position;
            center /= float(std::max(draw.count, 1u));
        }
        auto delta = center - camera.position;
        return glm::dot(delta, delta);
    };
    if(referenceSubmissionCache){render::orderDraws(draws,ready,distance);return;}
    drawOrderCache.depths.resize(draws.size());
    auto cachedDistance=[&](const Draw& draw,size_t index) {
        if(draw.geometry && scene && !scene->transforms[draw.palette].info.x) {
            auto& entry=drawOrderCache.depths[index];
            auto generation=resolvedMesh(draw)->generation;
            const auto& transform=scene->transforms[draw.palette].position;
            if(entry.geometry==draw.geometry && entry.generation==generation && entry.transform==transform && entry.camera==camera.position)return entry.depth;
            auto depth=distance(draw);entry={draw.geometry,transform,camera.position,depth,generation};return depth;
        }
        return distance(draw);
    };
    if(drawOrderCache.order(draws,ready,cachedDistance))++stats.drawOrderReuses;
}
} // namespace one
