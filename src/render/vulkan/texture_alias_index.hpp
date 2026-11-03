#pragma once
#include "materials_state.hpp"
namespace one::vulkan {
struct TextureAliasKey {
    const PreparedImage* prepared = nullptr;
    uint32_t mip = 0;
    bool srgb = true;
    bool operator==(const TextureAliasKey&) const = default;
};
struct TextureAliasHash {
    size_t operator()(TextureAliasKey key) const {
        return std::hash<const void*>{}(key.prepared) ^ (size_t(key.mip) << 1) ^ size_t(key.srgb);
    }
};
struct TextureAliasIndex {
    // Source keys, not owning Texture copies: this cannot retain image payloads or GPU allocations.
    std::unordered_map<TextureAliasKey, TextureKey, TextureAliasHash> sources;
    bool valid = false;
};
} // namespace one::vulkan
