#pragma once
#include <vulkan/vulkan.h>
#include <theone/render/asset_memory_stats.hpp>
#include <theone/assets/mesh.hpp>
#include <unordered_map>
#include <array>
#include <theone/geometry/prepared_mesh.hpp>
#include "upload_state.hpp"
#include "geometry_lifetime.hpp"
namespace one { struct Model; }
namespace one::vulkan {
struct GpuLod {
    VkDeviceSize offset = 0;
    uint32_t count = 0;
    float error = 0;
};
struct ShadowLodHistory {
    glm::mat3 transform{0};
    float scale = 0;
    std::array<float, 3> density{-1, -1, -1};
    std::array<size_t, 3> level{};
};
struct MeshGPU {
    uint64_t generation = 0;
    std::shared_ptr<GeometryLifetime> lifetime;
    std::shared_ptr<UploadProgress> upload;
    std::shared_ptr<const PreparedMesh> prepared;
    std::vector<GpuLod> levels;
    std::vector<float> lodErrors;
    std::unordered_map<uint64_t, size_t> lodHistory;
    std::unordered_map<uint64_t, ShadowLodHistory> shadowLodHistory;
    std::shared_ptr<const one::Model> owner;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize vertexOffset = 0, indexOffset = 0, bytes = 0;
    uint32_t indexCount = 0, maxJoint = 0;
    one::Bounds bounds;
    glm::vec4 centerSum{0}, unweightedSum{0};
    std::vector<glm::vec4> jointSums;
};
struct GeometryAllocation {
    std::shared_ptr<UploadProgress> upload;
    VkDeviceSize bytes = 0, allocatedBytes = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
struct GeometryFrame {
    VkBuffer shadowBuffer = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
    void* shadowMapped = nullptr;
    VkDeviceSize shadowCapacity = 0;
    uint64_t shadowRevision = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkBuffer paletteBuffer = VK_NULL_HANDLE;
    VkDeviceMemory paletteMemory = VK_NULL_HANDLE;
    void* paletteMapped = nullptr;
    VkDeviceSize paletteCapacity = 0;
    uint64_t dynamicRevision = 0;
    const void* dynamicSource = nullptr;
    size_t dynamicSize = 0;
    bool dynamicCached = false;
    std::shared_ptr<UploadProgress> dynamicUpload;
    VkBuffer vertices = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkDeviceSize vertexCapacity = 0;
    bool vertexDeviceLocal = false;
};
struct GeometryReference {
    const Primitive* source = nullptr;
    MeshGPU* mesh = nullptr;
    std::shared_ptr<GeometryLifetime> lifetime;
    bool matches(const Primitive* primitive) const { return source == primitive && mesh && resident(lifetime); }
};
struct GeometryCache {
    uint64_t nextGeneration = 0;
    std::unordered_map<const Model*, std::shared_ptr<GeometryLifetime>> lifetimes;
    std::unordered_map<VkBuffer, size_t> bufferUsers;
    std::vector<GeometryReference> references;
    std::unordered_map<const Model*, std::shared_ptr<const Model>> owners;
    std::vector<GeometryAllocation> allocations;
    std::unordered_map<const one::Primitive*, MeshGPU> meshes;
    one::GeometryStats stats;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkBuffer boundBuffer = VK_NULL_HANDLE;
    VkDeviceSize boundVertexOffset = 0, boundIndexOffset = 0;
};
} // namespace one::vulkan
