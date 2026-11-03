#include "asset_memory_snapshot.hpp"
#include "materials_state.hpp"
#include <theone/assets/model.hpp>
#include <unordered_set>
namespace one::vulkan {
bool TextureMemorySnapshot::update(const MaterialCache& materials) {
    if (revision == materials.memoryRevision) return false;
    residentBytes = totalBytes = 0;
    prepared.clear();
    std::unordered_set<VkImage> unique;
    for (const auto& [key, texture] : materials.textures) {
        if (unique.insert(texture.image).second) residentBytes += texture.bytes;
        prepared.push_back(texture.prepared.get());
    }
    totalBytes = residentBytes;
    for (const auto& [key, texture] : materials.replacements) {
        if (unique.insert(texture.image).second) totalBytes += texture.bytes;
        prepared.push_back(texture.prepared.get());
    }
    textures = unique.size();
    aliases = materials.textures.size() + materials.replacements.size() - textures;
    revision = materials.memoryRevision;
    return true;
}
uint64_t ImageMemorySnapshot::update(std::span<const Model* const> models, const MaterialCache& materials) {
    textures.update(materials);
    bool dirty = countedTextureRevision != textures.revision;
    uint64_t rawBytes = 0;
    size_t index = 0;
    // rgba is public: observe capacity edits even without a revision increment.
    // Prepared payloads are immutable; only their identity changes invalidate totals.
    for (const auto* model : models)
        for (const auto& image : model->images) {
            rawBytes += image.rgba.capacity();
            auto* prepared = image.prepared.get();
            if (index == modelImages.size()) { modelImages.push_back(prepared); dirty = true; }
            else if (modelImages[index] != prepared) { modelImages[index] = prepared; dirty = true; }
            ++index;
        }
    if (modelImages.size() != index) { modelImages.resize(index); dirty = true; }
    if (dirty) {
        preparedBytes = 0;
        std::unordered_set<const void*> payloads;
        auto count = [&](const PreparedImage* image) {
            if (image) for (const auto& bytes : {image->color, image->data})
                if (bytes && payloads.insert(bytes.get()).second) preparedBytes += bytes->capacity();
        };
        for (auto* image : modelImages) count(image);
        for (auto* image : textures.prepared) count(image);
        countedTextureRevision = textures.revision;
    }
    return rawBytes + preparedBytes;
}
}
