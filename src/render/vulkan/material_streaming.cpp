#include "backend.hpp"
namespace one {
bool Renderer::Impl::prepareMaterial(const Draw& draw) {
    if (materialsState.materials.contains(draw.material))
        return true;
    if (!materialAdmission.available()) { ++stats.materialDeferred; return false; }
    vulkan::MaterialWork work(materialAdmission);
    Material fallback;
    const auto& m = draw.material ? *draw.material : fallback;
    const vulkan::TextureKey keys[] = {{draw.image, true},
                                       {m.metallicRoughness, false},
                                       {m.normal ? m.normal : &materialsState.flatNormal, false},
                                       {m.occlusion, false},
                                       {m.emissive, true}};
    bool ready = true;
    for (auto key : keys) {
        if (!materialsState.textures.contains(key)) {
            auto prepared = preparedImage(key.image);
            if (!prepared)
                return false;
            uint32_t mip = 0;
            if (textureStreaming)
                while (mip + 1 < prepared->mips.size() &&
                       std::max(prepared->mips[mip].width, prepared->mips[mip].height) > 64)
                    ++mip;
            // Aliasing resident data is bookkeeping, not an upload. Do not let detail
            // promotions starve a newly visible material that can share this allocation.
            auto alias = findTextureAlias(prepared, key.srgb, mip);
            if (alias) {
                auto shared = *alias;
                shared.revision = key.image ? key.image->revision : preparation.white.revision;
                materialsState.textures.emplace(key, std::move(shared));
                ++materialsState.memoryRevision;
            } else if (!reserveUpload(uint64_t(prepared->mips[mip].width) * prepared->mips[mip].height * 4))
                return false;
        }
        const auto& t = texture(key.image, key.srgb);
        ready &= t.upload->ready;
    }
    if (ready)
        material(draw.material, draw.image);
    return ready;
}
} // namespace one
