#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
namespace one {
struct Model;
struct PreparedImage;
namespace vulkan {
struct MaterialCache;
struct TextureMemorySnapshot {
    uint64_t revision = 0, residentBytes = 0, totalBytes = 0;
    size_t textures = 0, aliases = 0;
    std::vector<const PreparedImage*> prepared;
    bool update(const MaterialCache& materials);
};
struct ImageMemorySnapshot {
    TextureMemorySnapshot textures;
    std::vector<const PreparedImage*> modelImages;
    uint64_t preparedBytes = 0, countedTextureRevision = 0;
    uint64_t update(std::span<const Model* const> models, const MaterialCache& materials);
};
}
}
