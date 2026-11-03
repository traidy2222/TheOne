#include "render/vulkan/asset_cache_invalidation.hpp"
#include "render/vulkan/asset_memory_snapshot.hpp"
#include "render/vulkan/geometry_state.hpp"
#include "render/vulkan/materials_state.hpp"
#include "render/vulkan/shadow_submission_state.hpp"
#include "render/vulkan/shadow_plan_key.hpp"
#include "render/vulkan/visibility_state.hpp"
#include "render/draw_order_cache.hpp"
#include "render/draw_scratch.hpp"
#include <theone/assets/model.hpp>
#include <theone/render/render_scene.hpp>
#include <theone/render/frame_stats.hpp>
#include <iostream>
#include <stdexcept>
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        using namespace one;
        auto a = std::make_shared<Model>(), b = std::make_shared<Model>();
        a->meshes.resize(1); a->meshes[0].resize(1);
        b->meshes.resize(1); b->meshes[0].resize(1);
        auto* pa = &a->meshes[0][0]; auto* pb = &b->meshes[0][0];
        vulkan::GeometryCache geometry;
        auto tokenA = std::make_shared<vulkan::GeometryLifetime>();
        auto tokenB = std::make_shared<vulkan::GeometryLifetime>();
        geometry.lifetimes = {{a.get(), tokenA}, {b.get(), tokenB}};
        vulkan::MeshGPU meshA, meshB;
        geometry.references = {{pa, &meshA, tokenA}, {pb, &meshB, tokenB}};
        render::DrawOrderCache order;
        order.depths.resize(2); order.depths[0].geometry = pa; order.depths[1].geometry = pb;
        order.depths[0].generation = 1; order.depths[1].generation = 2;
        std::vector<vulkan::BoundsSnapshot> bounds(2);
        bounds[0].geometry = pa; bounds[1].geometry = pb;
        bounds[0].generation = 1; bounds[1].generation = 2;
        vulkan::ShadowSubmission shadow;
        for (auto& cascade : shadow.cascades) {
            cascade.templates.resize(2);
            cascade.templates[0].geometry = pa; cascade.templates[1].geometry = pb;
            cascade.templates[0].lifetime = tokenA; cascade.templates[1].lifetime = tokenB;
            cascade.geometryTemplates[pa].owner = a.get();
            cascade.geometryTemplates[pb].owner = b.get();
            cascade.planValid = true;
        }
        FrameStats stats;
        std::vector<std::shared_ptr<const Model>> removed{a};
        vulkan::invalidateGeometryCaches(removed, geometry, shadow, stats);
        require(!geometry.references[0].matches(pa) && geometry.references[1].matches(pb),
                "Residency invalidation missed stale reference or discarded unrelated reference");
        require(!vulkan::resident(shadow.cascades[0].templates[0].lifetime) &&
                vulkan::resident(shadow.cascades[0].templates[1].lifetime) &&
                !shadow.cascades[0].planValid, "Selective shadow lifetime invalidation failed");
        require(stats.invalidationPrimitivesVisited == 1 &&
                !bounds[0].matches(pa, 3, glm::mat4(1)) && bounds[1].matches(pb, 2, glm::mat4(1)),
                "Reused primitive address accepted a stale bound generation");
        geometry.lifetimes[a.get()] = std::make_shared<vulkan::GeometryLifetime>();
        require(!geometry.references[0].matches(pa), "Re-admission resurrected a retired reference");
        require(!shadow.cascades[0].geometryTemplates.contains(pa) &&
                shadow.cascades[0].geometryTemplates.contains(pb), "Shared binding invalidation failed");

        RenderScene scene; scene.transforms.resize(1);
        Material material;
        Draw draw; draw.geometry = pb; draw.count = 6; draw.material = &material;
        std::vector<Draw> source{draw};
        auto& cascade = shadow.cascades[0];
        auto reuse = [&] {
            auto reused = vulkan::reuseShadowPlan(cascade, source, &scene, 0, false, true, 10);
            if (!reused) {
                if (cascade.recordPlanKeys)
                    for (size_t i = 0; i < source.size(); ++i)
                        cascade.planKeys[i] = vulkan::shadowPlanKey(source[i], &scene, 0, false, true);
                vulkan::retainShadowPlanCounts(cascade, {}, {});
            }
            return reused;
        };
        auto settle = [&] {
            for (int i = 0; i < 12; ++i) if (reuse()) return true;
            return false;
        };
        require(!reuse() && reuse(), "Stable shadow plan was not reusable");
        scene.transforms[0].position[3].x = 5;
        require(reuse(), "Translation changed an otherwise identical command plan");
        scene.transforms[0].position[0].x = 2;
        require(!reuse() && cascade.planRetryFrames == 7 && settle(), "LOD scale change reused a stale plan or did not settle");
        source[0].shadowVisibility = 0; require(!reuse() && settle(), "Visibility change reused a stale plan");
        material.mask = true; require(reuse(), "Culled material edit rebuilt an unchanged command plan");
        source[0].shadowVisibility = 7; require(!reuse() && settle(), "Revealed masked geometry reused stale commands");
        material.mask = false; require(!reuse() && settle(), "Mask change reused a stale plan");
        source[0].instances = 2; require(!reuse() && settle(), "Instance change reused a stale plan");
        source[0].geometry = pa; require(!reuse() && settle(), "Geometry change reused a stale plan");
        require(!vulkan::reuseShadowPlan(cascade, source, &scene, 0, false, true, 11) &&
                !cascade.recordPlanKeys, "Moving density recorded a disposable dependency snapshot");
        vulkan::retainShadowPlanCounts(cascade, {}, {});
        require(!cascade.planValid && !reuse() && settle(),
                "Settled density did not rebuild before reusing commands");

        a->images.resize(1); b->images.resize(1);
        auto payload = std::make_shared<std::vector<uint8_t>>(128);
        auto prepared = std::make_shared<PreparedImage>(); prepared->color = prepared->data = payload;
        a->images[0].prepared = b->images[0].prepared = prepared;
        a->images[0].rgba.resize(16); b->images[0].rgba.resize(16);
        vulkan::MaterialCache materials;
        vulkan::Texture texture; texture.image = reinterpret_cast<VkImage>(uintptr_t(1));
        texture.bytes = 256; texture.prepared = prepared;
        materials.textures.emplace(vulkan::TextureKey{&a->images[0], true}, texture);
        materials.textures.emplace(vulkan::TextureKey{&b->images[0], true}, texture);
        vulkan::ImageMemorySnapshot memory;
        std::vector<const Model*> models{a.get(), b.get()};
        auto expected = payload->capacity() + a->images[0].rgba.capacity() + b->images[0].rgba.capacity();
        require(memory.update(models, materials) == expected && memory.textures.totalBytes == 256 &&
                memory.textures.aliases == 1, "Shared CPU/GPU payload counted twice");
        auto beforeCapacity = a->images[0].rgba.capacity(); a->images[0].rgba.reserve(300);
        expected += a->images[0].rgba.capacity() - beforeCapacity;
        require(memory.update(models, materials) == expected, "Unrevisioned public capacity edit missed");
        texture.image = reinterpret_cast<VkImage>(uintptr_t(2)); texture.bytes = 512;
        materials.replacements.emplace(vulkan::TextureKey{&a->images[0], true}, texture);
        ++materials.memoryRevision;
        require(memory.update(models, materials) == expected && memory.textures.totalBytes == 768 &&
                memory.textures.residentBytes == 256, "Replacement distorted resident pressure or sharing");
        materials.textures.clear(); materials.replacements.clear(); ++materials.memoryRevision;
        models.clear(); require(memory.update(models, materials) == 0 && memory.textures.totalBytes == 0,
                                "Removed resources remained counted");

        render::DrawScratch scratch;
        std::weak_ptr<const Model> owner;
        { auto model = std::make_shared<Model>(); owner = model; Draw d; d.owner = model;
          d.count = 7; d.entity = 88;
          std::vector<Draw> input{d}; require(scratch.assign(input, {}), "Initial scratch did not grow");
          require(scratch.draws[0].owner == model && scratch.draws[0].count == 7 && scratch.draws[0].entity == 88,
                  "Frame scratch changed commands or ownership"); }
        scratch.release(); require(owner.expired(), "Scratch retained a retired model owner");
        std::vector<Draw> small(1);
        require(!scratch.assign(small, {}), "Stable scratch reallocated"); scratch.release();
        std::vector<Draw> large(render::DrawScratch::retainedBytes / sizeof(Draw) + 1);
        scratch.assign(large, {}); scratch.release();
        require(scratch.draws.capacity() == 0, "Oversized scratch exceeded retained-memory cap");
        std::cout << "PASS selective invalidation, shadow dependencies, shared accounting and bounded scratch\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
