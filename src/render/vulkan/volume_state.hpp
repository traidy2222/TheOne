#pragma once
#include <theone/volumes/volume.hpp>
#include <theone/lighting/stellar.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <memory>
namespace one::vulkan {
struct VolumeParameters {
    glm::mat4 inverseVP;
    glm::vec4 eye, origin, extent;
    glm::uvec4 grid;
    glm::vec4 gas, gravityMax, windClosed, fuel, optics;
    glm::vec4 sunDirection, sunRadiance, skyRadiance, ray;
    std::array<glm::mat4, 3> lightVP;
    glm::vec4 splits, forward;
    glm::uvec4 counts;
    glm::vec4 initial;
    std::array<glm::vec4, 16> sourcePosition, sourceRates, sourceInterval, sourceVelocity;
    std::array<glm::vec4, 16> solidMin, solidMax, solidVelocity;
    glm::vec4 stellarCounts;
    std::array<glm::vec4, 4> starPositionRadius, starRadiance;
    std::array<glm::vec4, 16> occultingSpheres;
    std::array<glm::vec4, 4> starActivity, starMotion, starAtmosphere;
    std::array<glm::vec4, 16> starRegionNormal, starRegionTangent;
    glm::vec4 plasmaNormal, plasmaTangent, plasmaGrid, plasmaExtent;
};
static_assert(sizeof(VolumeCell) == 48);
struct VolumePush {
    uint32_t phase = 0, state = 0, pressure = 0, flags = 0;
    float dt = 0, time = 0;
    uint32_t padding0 = 0, padding1 = 0;
};
struct VolumeBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};
struct VolumeGpu {
    VkDevice device = VK_NULL_HANDLE;
    glm::uvec3 resolution{};
    uint64_t bytes = 0;
    std::array<VolumeBuffer, 2> cells, pressure, parameters, diagnostics, plasma;
    VolumeBuffer rhs, light, ledger;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::array<std::vector<VkDescriptorSet>, 2> sets;
    ~VolumeGpu();
};
struct HdrTarget {
    VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE, resolved = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE, depthMemory = VK_NULL_HANDLE,
                   resolvedMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE, resolvedView = VK_NULL_HANDLE;
    VkFramebuffer opaque = VK_NULL_HANDLE, composite = VK_NULL_HANDLE;
};
struct VolumeBackend {
    bool stellarActive = false;
    StellarScene stellarScene;
    VkPipeline stellarSky = VK_NULL_HANDLE;
    bool hdr = false, active = false, initialize = true, obstaclesDirty = false;
    VolumeScene scene;
    SceneLighting lighting;
    VolumeStats stats;
    uint64_t generation = 0;
    uint32_t state = 0;
    double simulated = 0;
    std::shared_ptr<VolumeGpu> gpu;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout surfaceLayout = VK_NULL_HANDLE;
    std::array<VkPipeline, 14> simulation{};
    std::array<VkPipeline, 2> lightingPipeline{};
    VkPipeline resolve = VK_NULL_HANDLE, output = VK_NULL_HANDLE;
    uint64_t hdrBytes = 0, retiredBytes = 0;
    std::array<VkPipeline, 4> surfaces{};
    VkRenderPass hdrPass = VK_NULL_HANDLE, compositePass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE, depthSampler = VK_NULL_HANDLE;
    std::vector<HdrTarget> targets;
};
} // namespace one::vulkan
