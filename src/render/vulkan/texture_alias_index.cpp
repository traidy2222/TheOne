#include "backend.hpp"
namespace one {
void Renderer::Impl::rememberTextureAlias(vulkan::TextureKey source, const Texture& texture) {
    if (textureAliases.valid)
        textureAliases.sources.try_emplace({texture.prepared.get(), texture.firstMip, source.srgb}, source);
}
std::optional<Renderer::Impl::Texture>
Renderer::Impl::findTextureAlias(const std::shared_ptr<const PreparedImage>& prepared, bool srgb,
                                 uint32_t mip) {
    if (!textureAliases.valid) {
        textureAliases.sources.clear();
        textureAliases.valid = true;
        for (auto* map : {&materialsState.textures, &materialsState.replacements})
            for (const auto& [source, texture] : *map)
                if (!texture.upload->cancelled)
                    rememberTextureAlias(source, texture);
    }
    ++stats.textureAliasLookups;
    vulkan::TextureAliasKey key{prepared.get(), mip, srgb};
    auto found = textureAliases.sources.find(key);
    if (found == textureAliases.sources.end())
        return {};
    auto matches = [&](const Texture& texture) {
        return texture.prepared == prepared && texture.firstMip == mip && !texture.upload->cancelled;
    };
    for (auto* map : {&materialsState.textures, &materialsState.replacements}) {
        auto candidate = map->find(found->second);
        if (candidate != map->end() && matches(candidate->second))
            return candidate->second;
    }
    // The representative may have been replaced while another alias remains live.
    ++stats.textureAliasRepairs;
    for (auto* map : {&materialsState.textures, &materialsState.replacements})
        for (const auto& [source, texture] : *map)
            if (source.srgb == srgb && matches(texture)) {
                found->second = source;
                return texture;
            }
    textureAliases.sources.erase(found);
    return {};
}
} // namespace one
