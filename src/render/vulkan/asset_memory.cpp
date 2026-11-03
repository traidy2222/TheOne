#include "backend.hpp"
#include "asset_memory_snapshot.hpp"
#include <theone/assets/model.hpp>
#include <unordered_set>
namespace one {
void Renderer::Impl::collectUnusedAssets(bool aggressive) {
    std::unordered_map<const Model*, std::shared_ptr<const Model>> unused;
    for (const auto& [key, owner] : geometryState.owners) {
        auto last = assetLastUsed.find(key);
        uint64_t age = last == assetLastUsed.end() ? stats.frame : stats.frame - last->second;
        if (age && (aggressive || age > 120))
            unused.emplace(key, owner);
    }
    size_t count = 0;
    for (auto& [key, owner] : unused) {
        invalidateAsset(owner);
        if (++count == 8)
            break;
    }
    std::erase_if(assetVersions, [](const auto& entry) { return entry.second.expired(); });
}
void Renderer::Impl::updateAssetMemory() {
    vulkan::refreshGeometryMemory(geometryState, geometryMemory);
    assetMemory = geometryMemory.stats;
    assetMemory.pooledStagingBytes=stagingPool.bytes;
    assetMemory.cpuImageBytes = imageMemory->update(geometryMemory.models, materialsState);
    assetMemory.gpuTextureBytes = imageMemory->textures.totalBytes;
    assetMemory.textures = imageMemory->textures.textures;
    assetMemory.textureAliases = imageMemory->textures.aliases;
    for (const auto& f : frames) {
        assetMemory.shadowCommandBytes += f.geometry.shadowCapacity;
        for (const auto& upload : f.uploads)
            assetMemory.stagingBytes += upload.bytes;
    }
    for (const auto& item : retired)
        assetMemory.retiredGpuBytes += item.bytes;
    std::unordered_set<const void*> payloads;
    for (const auto& task : uploads.pending)
        for (const auto& payload : task.payloads)
            if (payload && payloads.insert(payload.get()).second)
                assetMemory.pendingCpuBytes += payload->capacity();
}
} // namespace one
