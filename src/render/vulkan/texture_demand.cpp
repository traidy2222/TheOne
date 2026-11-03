#include "backend.hpp"
namespace one {
void Renderer::Impl::gatherTextureDemand(const std::vector<Draw>& draws, const Camera& camera) {
    const auto epoch = ++materialsState.demandEpoch;
    const float projection = float(swapchainState.extent.height) / std::tan(.525f);
    auto request = [&](const Texture& t, float pixels) {
        if (t.prepared->mips.size() == 1)
            return;
        uint32_t mip = 0;
        for (float size = float(std::max(t.width, t.height)); size > pixels * 1.25f && size > 1; size *= .5f)
            ++mip;
        if (t.demandFrame != epoch) {
            t.demandFrame = epoch;
            t.requestedMip = mip;
        } else
            t.requestedMip = std::min(t.requestedMip, mip);
    };
    for (const auto& draw : draws) {
        if(draw.shadowVisibility && draw.material && draw.material->mask && !draw.material->blend)
            if(auto texture=materialsState.textures.find({draw.image,true});texture!=materialsState.textures.end())
                request(texture->second,64); // Bounded alpha detail for off-camera shadow casters.
        if (!draw.cameraVisible)
            continue;
        auto found = materialsState.materials.find(draw.material);
        auto* cached = found == materialsState.materials.end() ? nullptr : &found->second;
        if (cached && !cached->hasTextureDetail)
            continue;
        float pixels = 4096;
        if (draw.worldBounds.valid()) {
            auto center = (draw.worldBounds.minimum + draw.worldBounds.maximum) * .5f;
            float radius = glm::length(draw.worldBounds.maximum - center);
            float distance = std::max(camera.nearPlane, glm::length(center - camera.position) - radius);
            pixels = std::max(1.f, radius * projection / distance);
        }
        if (cached) {
            if (cached->demandFrame == epoch && cached->demandPixels >= pixels)
                continue;
            cached->demandFrame = epoch;
            cached->demandPixels = pixels;
            for (auto texture : cached->residencyTextures)
                request(*texture, pixels);
        } else {
            // Initial coarse uploads can precede creation of the material descriptor.
            Material fallback;
            const auto& m = draw.material ? *draw.material : fallback;
            for (auto key : {vulkan::TextureKey{draw.image, true},
                             {m.metallicRoughness, false},
                             {m.normal ? m.normal : &materialsState.flatNormal, false},
                             {m.occlusion, false},
                             {m.emissive, true}})
                if (auto texture = materialsState.textures.find(key);
                    texture != materialsState.textures.end())
                    request(texture->second, pixels);
        }
    }
}
} // namespace one
