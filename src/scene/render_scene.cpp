#include <theone/scene/world.hpp>
#include <functional>
#include "transform_cache.hpp"
#include <chrono>
#include "core/validation.hpp"
namespace one {
using detail::need;
void World::bake(float time, std::vector<Vertex>& vertices, std::vector<Draw>& draws) const {
    vertices.clear();
    draws.clear();
    for (const auto& e : entities)
        if (e.visible && e.model) {
            auto m = matrix(e.id);
            if (e.behavior == "spin")
                m = m * glm::rotate(glm::mat4(1), time * e.speed, glm::vec3(0, 1, 0));
            e.model->bake(m, e.animationTime >= 0 ? e.animationTime : time, e.clip, e.animate, e.id, vertices,
                          draws, e.previousClip, e.previousAnimationTime, e.animationBlend, e.animationLoop,
                          e.previousAnimationLoop);
        }
}
void World::prepare(float time, RenderScene& scene) const {
    scene.draws.clear();
    scene.transforms.clear();
    scene.vertexCount = 0;
    auto started = std::chrono::steady_clock::now();
    scene.preparation = {};
    if (!cache_)
        cache_ = std::make_shared<detail::SceneCache>();
    else if (cache_.use_count() != 1)
        cache_ = std::make_shared<detail::SceneCache>(*cache_);
    cache_->update(entities, scene.preparation);
    auto append = [&](std::vector<GeometryTransform>& palette, const glm::mat4& matrix, uint32_t joints = 0) {
        need(std::abs(glm::determinant(glm::mat3(matrix))) > 1e-10f, "Singular geometry transform");
        GeometryTransform transform;
        transform.position = matrix;
        transform.normal = glm::mat4(glm::transpose(glm::inverse(glm::mat3(matrix))));
        transform.info.x = joints;
        palette.push_back(transform);
    };
    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        const auto& e = entities[entityIndex];
        if (!e.visible || !e.model)
            continue;
        auto& cached = cache_->entities[entityIndex];
        auto root = cached.root;
        if (e.behavior == "spin")
            root = root * glm::rotate(glm::mat4(1), time * e.speed, glm::vec3(0, 1, 0));
        const auto& model = *e.model;
        bool animated = e.animate && !model.animations.empty();
        if (cached.paletteRoot != root || cached.model != &model || cached.handle != model.handle ||
            animated || cached.animated || e.behavior == "spin" || cached.palette.empty()) {
            auto pose = model.pose(e.animationTime >= 0 ? e.animationTime : time, e.clip, e.animate,
                                   e.previousClip, e.previousAnimationTime, e.animationBlend, e.animationLoop,
                                   e.previousAnimationLoop);
            ++scene.preparation.posesEvaluated;
            ++scene.preparation.palettesRebuilt;
            cached.palette.clear();
            cached.nodePalettes.assign(model.nodes.size(), 0);
            for (size_t n = 0; n < model.nodes.size(); ++n) {
                const auto& node = model.nodes[n];
                if (!node.active || node.mesh < 0)
                    continue;
                const Skin* skin = node.skin >= 0 ? &model.skins.at(node.skin) : nullptr;
                cached.nodePalettes[n] = static_cast<uint32_t>(cached.palette.size());
                append(cached.palette, root * pose[n], skin ? static_cast<uint32_t>(skin->joints.size()) : 0);
                if (skin)
                    for (size_t j = 0; j < skin->joints.size(); ++j)
                        append(cached.palette, root * pose.at(skin->joints[j]) * skin->inverseBind.at(j));
            }
            cached.model = &model;
            cached.handle = model.handle;
            cached.paletteRoot = root;
        }
        cached.animated = animated || e.behavior == "spin";
        need(scene.transforms.size() + cached.palette.size() < UINT32_MAX, "Geometry palette limit exceeded");
        auto base = static_cast<uint32_t>(scene.transforms.size());
        scene.transforms.insert(scene.transforms.end(), cached.palette.begin(), cached.palette.end());
        for (size_t n = 0; n < model.nodes.size(); ++n) {
            const auto& node = model.nodes[n];
            if (!node.active || node.mesh < 0)
                continue;
            auto palette = base + cached.nodePalettes[n];
            for (const auto& primitive : model.meshes.at(node.mesh)) {
                Draw draw;
                draw.count = static_cast<uint32_t>(primitive.vertices.size());
                draw.entity = e.id;
                draw.geometry = &primitive;
                draw.owner = e.model;
                draw.palette = palette;
                if (primitive.material >= 0) {
                    draw.material = &model.materials.at(primitive.material);
                    if (draw.material->image >= 0)
                        draw.image = &model.images.at(draw.material->image);
                }
                scene.vertexCount += draw.count;
                scene.draws.push_back(std::move(draw));
            }
        }
    }
    scene.preparation.cpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}
} // namespace one
