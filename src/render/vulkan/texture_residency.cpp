#include "backend.hpp"
#include "asset_memory_snapshot.hpp"
#include "render/residency_policy.hpp"
#include <unordered_set>
namespace one {
void Renderer::Impl::invalidateTextureMaterials(const Image* image) {
    std::vector<const Material*> keys;
    for (const auto& [key, gpu] : materialsState.materials)
        if (std::find(gpu.images.begin(), gpu.images.end(), image) != gpu.images.end())
            keys.push_back(key);
    for (auto key : keys)
        invalidateMaterial(key);
}
void Renderer::Impl::retireTexture(Texture gpu) {
    auto referenced = [&](const auto& map) {
        return std::any_of(map.begin(), map.end(),
                           [&](const auto& entry) { return entry.second.image == gpu.image; });
    };
    if (referenced(materialsState.textures) || referenced(materialsState.replacements))
        return;
    textureAliases.sources.erase({gpu.prepared.get(), gpu.firstMip, gpu.srgb});
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
void Renderer::Impl::updateTextureResidency(const std::vector<Draw>& draws, const Camera& camera) {
    auto phaseStart = std::chrono::steady_clock::now();
    auto phase = [&]() {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration<double, std::milli>(now - phaseStart).count();
        phaseStart = now;
        return ms;
    };
    // A replacement is only visible after its complete mip tail was submitted.
    for (auto it = materialsState.replacements.begin(); it != materialsState.replacements.end();) {
        if (!it->second.upload->ready) {
            ++it;
            continue;
        }
        auto key = it->first;
        auto replacement = it->second;
        it = materialsState.replacements.erase(it);
        auto old = materialsState.textures.at(key);
        materialsState.textures[key] = replacement;
        ++materialsState.memoryRevision;
        ++stats.textureChangesCommitted;
        dirtyTextureMaterials(key.image);
        retireTexture(std::move(old));
    }
    stats.residencyCommitMs = phase();
    if (textureStreaming)
        gatherTextureDemand(draws, camera);
    stats.residencyDemandMs = phase();
    imageMemory->textures.update(materialsState);
    uint64_t bytes = imageMemory->textures.residentBytes;
    if (textureStreaming && materialsState.replacements.empty()) {
        if (bytes > textureBudget && texturePressure < 16)
            ++texturePressure;
        else if (texturePressure && bytes < textureBudget / 2 && stats.frame % 120 == 0)
            --texturePressure;
    }
    for (const auto& [key, t] : materialsState.textures) {
        uint32_t target = 0, max = uint32_t(t.prepared->mips.size() - 1);
        if (!max)
            continue;
        if (textureStreaming) {
            target = t.demandFrame == materialsState.demandEpoch
                         ? std::min(max, t.requestedMip + texturePressure)
                         : max;
        }
        auto [it, inserted] =
            materialsState.demand.emplace(key, vulkan::MaterialCache::Demand{target, stats.frame});
        if (it->second.mip != target) {
            it->second = {target, stats.frame};
        }
        if (t.firstMip == target)
            continue;
        ++stats.textureTransitions;
        if (!t.upload->ready || materialsState.replacements.contains(key))
            continue;
        if (!render::residencyTransitionReady(t.firstMip,target,stats.frame-it->second.changed,
                texturePressure!=0,textureStreaming,referenceResidency))
            continue;
        // A detail transition may already have a shared allocation. Like initial
        // aliases, it needs no transfer budget and must not wait behind real uploads.
        auto alias = findTextureAlias(t.prepared, key.srgb, target);
        auto queued=[&]{++materialsState.memoryRevision;++stats.textureChangesQueued;if(target<t.firstMip)++stats.texturePromotions;else ++stats.textureDemotions;};
        if (alias) {
            alias->revision = t.revision;
            materialsState.replacements.emplace(key, std::move(*alias));
            queued();
            continue;
        }
        auto mip = t.prepared->mips[target];
        if (!reserveUpload(uint64_t(mip.width) * mip.height * 4))
            continue;
        const auto& replacement = materialsState.replacements.emplace(key, createTexture(key.image, key.srgb, target)).first->second;
        rememberTextureAlias(key, replacement);
        queued();
    }
    stats.residencyPlanMs = phase();
}
} // namespace one
