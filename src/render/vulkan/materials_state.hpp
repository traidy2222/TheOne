#pragma once
#include <vulkan/vulkan.h>
#include <theone/assets/material.hpp>
#include <unordered_map>
#include <array>
#include "upload_state.hpp"
#include <theone/geometry/prepared_image.hpp>
namespace one::vulkan {
struct TextureKey {
    const one::Image* image = nullptr;
    bool srgb = true;
    bool operator==(const TextureKey&) const = default;
};
struct TextureKeyHash {
    size_t operator()(TextureKey k) const { return std::hash<const void*>{}(k.image) ^ size_t(k.srgb); }
};
struct Texture {
    bool srgb = true;
    std::shared_ptr<UploadProgress> upload;
    uint64_t hash = 0, bytes = 0;
    uint32_t width = 0, height = 0;
    std::shared_ptr<const PreparedImage> prepared;
    uint32_t firstMip = 0;
    mutable uint64_t demandFrame = 0;
    mutable uint32_t requestedMip = 0;
    uint64_t revision = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t levels = 1;
};
struct MaterialGPU {
    std::array<VkDescriptorSet, 2> sets{};
    std::array<const one::Image*, 5> images{};
    // Cache nodes stay stable across rehash and mip replacement. Actual image
    // invalidation destroys dependent materials before erasing texture nodes.
    std::array<const Texture*, 5> residencyTextures{};
    std::array<bool, 2> textureBindingsDirty{};
    uint64_t demandFrame = 0;
    float demandPixels = 0;
    bool hasTextureDetail = false;
    VkBuffer uniform = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
struct TextureUpload {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint32_t width = 0, height = 0, levels = 1;
    bool blit = true;
    std::vector<VkBufferImageCopy> copies;
    std::shared_ptr<UploadProgress> progress;
    std::shared_ptr<const std::vector<uint8_t>> data;
};
struct MaterialReference {
    const one::Material* source=nullptr;
    const one::Image* image=nullptr;
    MaterialGPU* gpu=nullptr;
    uint64_t generation=0;
};
struct MaterialCache {
    uint64_t memoryRevision = 1;
    uint64_t generation=1;
    std::vector<MaterialReference> references;
    uint64_t demandEpoch = 0;
    std::vector<TextureUpload> pendingUploads;
    std::vector<uint8_t> uploadBytes;
    std::unordered_map<TextureKey, Texture, TextureKeyHash> textures, replacements;
    struct Demand {
        uint32_t mip = 0;
        uint64_t changed = 0;
    };
    std::unordered_map<TextureKey, Demand, TextureKeyHash> demand;
    std::unordered_map<const one::Material*, MaterialGPU> materials;
    one::Image flatNormal{"Flat normal", 1, 1, {128, 128, 255, 255}};
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE, shadowSampler = VK_NULL_HANDLE;
};
} // namespace one::vulkan
