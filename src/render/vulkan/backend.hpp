#pragma once
#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>

#include <unordered_map>
#include <functional>
#include "device_state.hpp"
#include "swapchain_state.hpp"
#include "geometry_state.hpp"
#include "materials_state.hpp"
#include "texture_alias_index.hpp"
#include "material_admission.hpp"
#include "scene_state.hpp"
#include "targets_state.hpp"
#include "capture_state.hpp"
#include "profiling.hpp"
#include "frame_state.hpp"
#include "frame_trace.hpp"
#include "presentation_pacing.hpp"
#include "staging_pool.hpp"
#include "upload_state.hpp"
#include "preparation_state.hpp"
#include "instance_state.hpp"
#include "asset_memory_state.hpp"
#include "visibility_state.hpp"
#include "asset_owner_state.hpp"
#include "render/spatial_regions.hpp"
#include "render/draw_order_cache.hpp"
#include "render/draw_scratch.hpp"
#include "shadow_mask_state.hpp"
#include "volume_state.hpp"
namespace one {
namespace vulkan { struct PresentationPacing; struct ImageMemorySnapshot; struct ShadowSubmission; }
inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) +
                                 ")");
}
// Coordinates backend state and lifecycle; function bodies live in responsibility-specific .cpp files.
struct Renderer::Impl {
    vulkan::VolumeBackend volumes;
    void createVolumeLayout();
    void createHdrTargets();
    void destroyHdrTargets(bool pipelines);
    void createVolumeBuffers();
    void retireVolumeBuffers();
    void updateVolumeDescriptors();
    VkPipeline createVolumePostPipeline(VkRenderPass pass,bool output);
    void readVolumeDiagnostics(uint32_t slot);
    void prepareVolumes(uint32_t index,const Camera& camera,bool shadows);
    void recordVolumeSimulation(uint32_t index);
    void recordVolumeLighting(uint32_t index);
    void volumeBarrier();
    VolumeReadback captureVolumeState();
    void recordHdrScene(uint32_t index,const std::vector<Draw>& draws,const Camera& camera,float time,bool shadows,uint64_t selected,const RenderScene* scene);
    void finishSurface(uint32_t index,bool capture);
    Impl();
    ~Impl();
    using Texture = vulkan::Texture;
    using MaterialGPU = vulkan::MaterialGPU;
    using Frame = vulkan::FrameUniform;
    using SceneUniform = vulkan::SceneUniform;
    AssetUploadStats uploadStats;
    vulkan::TextureAliasIndex textureAliases;
    vulkan::MaterialAdmission materialAdmission;
    size_t materialWarmupCursor = 0;
    bool referenceMaterialWarmup = std::getenv("THEONE_REFERENCE_MATERIAL_WARMUP") != nullptr;
    void warmResidentMaterials(const std::vector<Draw>& draws);
    std::optional<vulkan::Texture> findTextureAlias(const std::shared_ptr<const PreparedImage>& prepared, bool srgb, uint32_t mip);
    void rememberTextureAlias(vulkan::TextureKey source, const vulkan::Texture& texture);
    vulkan::PreparationState preparation;
    std::shared_ptr<const PreparedImage> preparedImage(const Image* image);
    std::shared_ptr<const PreparedMesh> preparedMesh(const Draw& draw);
    vulkan::UploadQueue uploads;
    vulkan::StagingPool stagingPool;
    bool referenceStagingPool=std::getenv("THEONE_REFERENCE_STAGING_POOL")!=nullptr;
    vulkan::StagedUpload acquireStaging(uint64_t bytes);
    void destroyStaging(vulkan::StagedUpload upload);
    void trimStagingPool(bool drain);

    AssetMemoryStats assetMemory;
    vulkan::GeometryMemorySnapshot geometryMemory;
    std::unique_ptr<vulkan::ImageMemorySnapshot> imageMemory;
    uint64_t assetMemoryBudget = 512 * 1024 * 1024;
    bool trimRequested = false;
    vulkan::AssetOwners assetOwners;
    std::unordered_map<const Model*, uint64_t> assetLastUsed;
    void updateAssetMemory();
    void collectUnusedAssets(bool aggressive);
    void pumpUploads();
    void queueBufferUpload(VkBuffer destination, std::shared_ptr<const std::vector<uint8_t>> data,
                           std::shared_ptr<vulkan::UploadProgress> progress, uint64_t destinationOffset = 0);
    void queueBufferParts(VkBuffer destination, std::vector<std::shared_ptr<const std::vector<uint8_t>>> data,
                          std::shared_ptr<vulkan::UploadProgress> progress, uint64_t destinationOffset = 0);
    void cancelUploads();
    FrameStats stats, lastGpu, displayedStats;
    vulkan::FrameTrace frameTrace;
    vulkan::PresentationPacing presentationPacing;
    std::array<float, 120> cpuHistory{}, gpuHistory{};
    size_t historyIndex = 0;
    void publishFrameStats();
    std::array<vulkan::FrameContext, vulkan::frameSlots> frames;
    uint32_t frameIndex = 0, frameCount = 2;
    uint64_t submitted = 0, completed = 0;
    bool frameReady = false, culling = true;
    std::vector<vulkan::RetiredResource> retired;
    TaskQueue assetReleaseWorker;
    std::unordered_map<uint64_t, std::weak_ptr<const Model>> assetVersions;
    vulkan::FrameContext& frame() { return frames[frameIndex]; }
    void startFrame();
    bool waitForFrame();
    void createFrameResources();
    void destroyFrameResources();
    void readFrameQueries();
    void collectRetired(bool drain = false);
    void retire(std::function<void()> destroy, uint64_t bytes = 0);
    void invalidateAsset(const std::shared_ptr<const Model>& model);
    void invalidateAssets(const std::vector<std::shared_ptr<const Model>>& models);
    void invalidateImage(const Image* image);
    void invalidateMaterial(const Material* material);
    void synchronizeAssets(const RenderScene* scene, const std::vector<Draw>& draws);
    void drawProfiler(bool* open);
    bool visible(const Draw& draw, const RenderScene* scene, const Frustum& frustum) const;
    void createFrameSceneResources();
    void destroyFrameSceneResources();
    vulkan::DeviceState deviceState;
    vulkan::SwapchainState swapchainState;
    void prepareDrawMaterials(std::vector<Draw>& draws);
    const MaterialGPU& resolvedMaterial(const Draw& draw);
    bool referenceResidency = std::getenv("THEONE_REFERENCE_RESIDENCY") != nullptr;
    bool referenceDrawPreparation = std::getenv("THEONE_REFERENCE_DRAW_PREPARATION") != nullptr;
    vulkan::MeshGPU* resolvedMesh(const Draw& draw) {
        if(!referenceDrawPreparation && draw.resolvedGeometry<geometryState.references.size()) {
            auto& entry=geometryState.references[draw.resolvedGeometry];
            if(entry.matches(draw.geometry))return entry.mesh;
        }
        auto found=geometryState.meshes.find(draw.geometry);
        return found==geometryState.meshes.end()?nullptr:&found->second;
    }
    const vulkan::MeshGPU* resolvedMesh(const Draw& draw) const { return const_cast<Impl*>(this)->resolvedMesh(draw); }
    bool referenceSubmissionCache=std::getenv("THEONE_REFERENCE_SUBMISSION_CACHE")!=nullptr;
    render::DrawOrderCache drawOrderCache;
    render::DrawScratch drawScratch;
    vulkan::GeometryCache geometryState;
    vulkan::MaterialCache materialsState;
    vulkan::SceneResources sceneState;
    vulkan::RenderTargets targetsState;
    vulkan::CaptureResources captureState;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                        void* user);
    void initVulkan();
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory,
                      VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkImageView imageView(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t levels = 1,
                          uint32_t layers = 1, uint32_t baseLayer = 0);
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                     VkImage& image, VkDeviceMemory& memory, uint32_t levels = 1, uint32_t layers = 1);
    VkShaderModule shader(const char* name);
    void stageUpload(vulkan::StagedUpload upload, const std::function<void(VkCommandBuffer)>& record);
    void releaseUploads(vulkan::FrameContext& context);
    void submitUploadsOnly();
    uint64_t uploadBudget = 8 * 1024 * 1024, uploadSpent = 0;
    bool reserveUpload(uint64_t bytes);
    bool prepareMaterial(const Draw& draw);
    bool textureStreaming = true;
    uint64_t textureBudget = 256 * 1024 * 1024;
    uint32_t texturePressure = 0;
    std::chrono::steady_clock::time_point assetPrepareStart;
    void updateTextureResidency(const std::vector<Draw>& draws, const Camera& camera);
    void gatherTextureDemand(const std::vector<Draw>& draws, const Camera& camera);
    void dirtyTextureMaterials(const Image* image);
    void refreshMaterialTextures(MaterialGPU& material);
    Texture createTexture(const Image* image, bool srgb, uint32_t firstMip);
    void retireTexture(Texture texture);
    void invalidateTextureMaterials(const Image* image);
    void destroySwapchain(bool preserveRenderState = false);
    bool createSwapchain();
    void createRenderTargets(VkFormat colorFormat);
    void destroyRenderTargets();
    void createSurfaceTargets();
    void destroySurfaceTargets();
    VkPipeline createGraphicsPipeline(bool shadow, bool blend, bool mesh = false, bool mask = false, bool hdr = false);
    void recordScene(uint32_t index, const std::vector<one::Draw>& draws, const one::Camera& camera,
                     float time, bool capture, bool shadows, uint64_t selected, const RenderScene* scene);
    void verifyCapture();
    const Texture& texture(const one::Image* image, bool srgb = true);
    void flushTextureUploads(bool nestedInMaterial = false);
    const MaterialGPU& material(const one::Material* source, const one::Image* baseImage);
    void destroyTextures();
    void createSharedResources();
    void destroySharedResources();
    void updateUniform(bool shadows, bool classicColor, const Camera& camera);
    void recordShadows(const std::vector<Draw>& draws, bool shadows, const RenderScene* scene);
    std::unordered_map<const Image*,vulkan::ShadowMaskBinding> shadowMasks;
    bool prepareShadowMask(const Draw& draw);
    VkDescriptorSet shadowMaskDescriptor(const Image* image);
    void invalidateShadowMask(const Image* image);
    void destroyShadowMasks();
    void submitMaskedShadow(const Draw& draw, uint32_t cascade, vulkan::ShadowMaskRecord* state = nullptr);
    std::unique_ptr<vulkan::ShadowSubmission> shadowSubmission;
    void prepareShadowSubmission(const std::vector<Draw>& draws, const RenderScene* scene);
    void submitShadowCascade(uint32_t cascade, const std::vector<Draw>& draws);
    void destroyShadowCommands();
    std::array<float, 3> shadowTexelsPerUnit{};
    uint32_t shadowMeshLod(const Draw& draw, const RenderScene* scene, uint32_t cascade,
                           vulkan::MeshGPU& mesh);
    bool shadowBatching = true, shadowIndirect = true, spatialCulling = true, shadowLod = true;
    float shadowDistance = 150;
    bool lodEnabled = true, instancing = true;
    bool instanceBatchesReady = false;
    vulkan::InstancePlan instancePlan;
    std::array<std::vector<Draw>, 3> shadowBatches;
    std::vector<GeometryTransform> instanceTransforms;
    void preparePalette(const std::vector<GeometryTransform>& transforms);
    bool batchInstances(std::vector<Draw>& draws, const RenderScene& scene, const Camera& camera,
                        uint64_t selected);
    float lodPixelError = 1.5f;
    render::SpatialRegions spatialRegions;
    std::vector<Bounds> spatialBounds;
    std::vector<vulkan::BoundsSnapshot> boundsSnapshots;
    void prepareVisibility(std::vector<Draw>& draws, const RenderScene* scene, const Camera& camera,
                           bool shadows);
    void prepareDrawVisibility(Draw& draw, const RenderScene& scene, const Camera& camera,
                               vulkan::MeshGPU& mesh, vulkan::BoundsSnapshot& snapshot);
    void finishDrawList(std::vector<Draw>& draws, const std::vector<Vertex>& vertices,
                        const RenderScene* scene, const Camera& camera, bool cachedDynamic);
    void destroyGeometryMeshes();
    void prepareGeometry(const one::RenderScene& scene);
    glm::vec3 geometryCenter(const one::Draw& draw, const one::RenderScene& scene) const;
    void createGeometryResources();
    void destroyGeometryResources();
    void issueGeometryDraw(const one::Draw& draw, bool useLod = false);
    void uploadVertices(const std::vector<one::Vertex>& vertices, bool deviceLocal = false);
    bool draw(const std::vector<one::Vertex>& vertices, const std::vector<one::Draw>& draws,
              const one::Camera& camera, float time, bool capture, bool shadows, uint64_t selected,
              bool classicColor, const one::RenderScene* scene = nullptr, const uint64_t* revision = nullptr);
    bool beginFrame();
    void initialize(SDL_Window* window, bool validate);
    void clearAssets();
    void close();
};
} // namespace one
