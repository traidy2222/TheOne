#include "backend.hpp"
#include <theone/assets/model.hpp>
namespace one {
void Renderer::Impl::synchronizeAssets(const RenderScene* scene, const std::vector<Draw>& draws) {
    static const std::vector<Draw> empty;
    assetOwners.update(scene ? scene->draws : empty, draws);
    for (const auto& owner : assetOwners.unique) {
        ++stats.assetOwnersChecked;
        // Texture aliases may be admitted before a model's geometry upload. Keep
        // their Image/Material source pointers alive until explicit invalidation or eviction.
        geometryState.owners.try_emplace(owner.get(), owner);
        assetLastUsed[owner.get()] = stats.frame;
        auto& weak = assetVersions[owner->handle.id];
        auto previous = weak.lock();
        if (previous && previous != owner && previous->handle.revision < owner->handle.revision)
            invalidateAsset(previous);
        if (!previous || previous->handle.revision <= owner->handle.revision)
            weak = owner;
    }
    std::vector<const Image*> changed;
    for (const auto& [key, gpu] : materialsState.textures)
        if (key.image && key.image->revision != gpu.revision)
            changed.push_back(key.image);
    for (auto image : changed)
        invalidateImage(image);
    collectUnusedAssets(trimRequested ||
                        assetMemory.gpuGeometryBytes + assetMemory.gpuTextureBytes > assetMemoryBudget);
    if(trimRequested)trimStagingPool(true);
    trimRequested = false;
    collectRetired();
}
} // namespace one
