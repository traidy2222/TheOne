#include "backend.hpp"
#include "shadow_submission_state.hpp"
#include "asset_cache_invalidation.hpp"
#include "asset_cache_scan_reference.hpp"
#include <theone/assets/model.hpp>
#include <unordered_set>
namespace one {
void Renderer::Impl::invalidateMaterial(const Material* source) {
    auto it = materialsState.materials.find(source);
    if (it == materialsState.materials.end())
        return;
    auto gpu = it->second;
    ++materialsState.generation;
    materialsState.materials.erase(it);
    retire([this, gpu] {
        for (auto set : gpu.sets)
            if (set)
                vkFreeDescriptorSets(deviceState.device, materialsState.pool, 1, &set);
        if (gpu.uniform)
            vkDestroyBuffer(deviceState.device, gpu.uniform, nullptr);
        if (gpu.memory)
            vkFreeMemory(deviceState.device, gpu.memory, nullptr);
    });
}
void Renderer::Impl::invalidateImage(const Image* image) {
    ++materialsState.memoryRevision;
    // Retain representatives for unaffected payloads. A removed representative
    // is repaired lazily against the live texture maps by findTextureAlias.
    invalidateShadowMask(image);
    preparation.images.erase(image);
    invalidateTextureMaterials(image);
    for (bool srgb : {false, true}) {
        vulkan::TextureKey key{image, srgb};
        materialsState.demand.erase(key);
        for (auto* map : {&materialsState.textures, &materialsState.replacements}) {
            auto it = map->find(key);
            if (it == map->end())
                continue;
            auto gpu = it->second;
            map->erase(it);
            retireTexture(std::move(gpu));
        }
    }
}

void Renderer::Impl::invalidateAsset(const std::shared_ptr<const Model>& model) {
    if (model)
        invalidateAssets({model});
}
void Renderer::Impl::invalidateAssets(const std::vector<std::shared_ptr<const Model>>& models) {
    if(models.empty())return;
    ++materialsState.memoryRevision;
    vulkan::ScopedTiming totalTiming(stats.invalidationMs);
    ++stats.invalidationCalls;
    auto phaseStart = std::chrono::steady_clock::now();
    auto checkpoint = [&](double& destination) {
        auto now = std::chrono::steady_clock::now();
        destination += std::chrono::duration<double, std::milli>(now - phaseStart).count();
        phaseStart = now;
    };
    geometryMemory.dirty = true;
    auto retiredCaches = vulkan::invalidateGeometryCaches(models, geometryState, *shadowSubmission, stats);
    static const bool referenceScan = std::getenv("THEONE_REFERENCE_INVALIDATION_SCAN") != nullptr;
    if (referenceScan) vulkan::scanInvalidatedGeometryCaches(models, geometryState, drawOrderCache,
                                                            boundsSnapshots, *shadowSubmission, stats);
    checkpoint(stats.invalidationCachesMs);
    std::unordered_set<const Material*> materials;
    std::unordered_set<const Image*> images;
    for (const auto& model : models)
        if (model) {
            geometryState.owners.erase(model.get());
            assetLastUsed.erase(model.get());
            for (const auto& m : model->materials)
                materials.insert(&m);
            for (const auto& i : model->images)
                images.insert(&i);
        }
    for (auto image : images)
        preparation.images.erase(image);
    for (const auto& model : models) if (model)
        for (const auto& mesh : model->meshes) for (const auto& primitive : mesh)
            preparation.meshes.erase(&primitive);
    std::vector<const Material*> dependents;
    for (const auto& [key, gpu] : materialsState.materials)
        if (materials.contains(key) ||
            std::any_of(gpu.images.begin(), gpu.images.end(), [&](auto p) { return images.contains(p); }))
            dependents.push_back(key);
    for (auto key : dependents)
        invalidateMaterial(key);
    checkpoint(stats.invalidationMaterialsMs);
    std::unordered_map<VkImage, Texture> removed;
    std::erase_if(materialsState.textures, [&](const auto& entry) {
        if (!images.contains(entry.first.image))
            return false;
        removed.emplace(entry.second.image, entry.second);
        return true;
    });
    std::erase_if(materialsState.replacements, [&](const auto& entry) {
        if (!images.contains(entry.first.image))
            return false;
        removed.emplace(entry.second.image, entry.second);
        return true;
    });
    std::erase_if(materialsState.demand,
                  [&](const auto& entry) { return images.contains(entry.first.image); });
    for (const auto& [key, gpu] : materialsState.textures)
        removed.erase(gpu.image);
    for (const auto& [key, gpu] : materialsState.replacements)
        removed.erase(gpu.image);
    for (const auto& [handle, gpu] : removed) {
        textureAliases.sources.erase({gpu.prepared.get(), gpu.firstMip, gpu.srgb});
        if (gpu.upload)
            gpu.upload->cancelled = true;
        retire(
            [this, gpu] {
                if (gpu.view)
                    vkDestroyImageView(deviceState.device, gpu.view, nullptr);
                if (gpu.image)
                    vkDestroyImage(deviceState.device, gpu.image, nullptr);
                if (gpu.memory)
                    vkFreeMemory(deviceState.device, gpu.memory, nullptr);
            },
            gpu.bytes);
    }
    checkpoint(stats.invalidationTexturesMs);
    // Extract nodes without moving/copying MeshGPU's nested hash tables. Their
    // destruction runs with the existing deferred owner release on the worker.
    using MeshNode = decltype(geometryState.meshes)::node_type;
    auto removedMeshes = std::make_shared<std::vector<MeshNode>>();
    std::unordered_set<VkBuffer> deadBuffers;
    for (const auto& model : models) if (model)
        for (const auto& mesh : model->meshes) for (const auto& primitive : mesh) {
            auto node = geometryState.meshes.extract(&primitive);
            if (node.empty()) continue;
            auto users = geometryState.bufferUsers.find(node.mapped().buffer);
            if (users != geometryState.bufferUsers.end() && --users->second == 0) {
                deadBuffers.insert(users->first);
                geometryState.bufferUsers.erase(users);
            }
            removedMeshes->push_back(std::move(node));
        }
    std::erase_if(geometryState.allocations, [&](const auto& allocation) {
        if (!deadBuffers.contains(allocation.buffer))
            return false;
        geometryState.stats.residentGeometryBytes -= allocation.bytes;
        if (allocation.upload)
            allocation.upload->cancelled = true;
        retire(
            [this, allocation] {
                if (allocation.buffer)
                    vkDestroyBuffer(deviceState.device, allocation.buffer, nullptr);
                if (allocation.memory)
                    vkFreeMemory(deviceState.device, allocation.memory, nullptr);
            },
            allocation.allocatedBytes);
        return true;
    });
    geometryState.stats.cachedMeshes = geometryState.meshes.size();
    vulkan::pruneShadowBindings(*shadowSubmission, deadBuffers);
    retire([this, owners=models, meshes=std::move(removedMeshes), caches=std::move(retiredCaches)]() mutable {
        assetReleaseWorker.submit([owners=std::move(owners), meshes=std::move(meshes), caches=std::move(caches)] {});
    });
    checkpoint(stats.invalidationGeometryMs);
}
} // namespace one
