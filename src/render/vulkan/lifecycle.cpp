#include "backend.hpp"
namespace one {
void Renderer::Impl::initialize(SDL_Window* window, bool validate) {
    deviceState.window = window;
    deviceState.validation = validate;
    initVulkan();
    createSharedResources();
    createSwapchain();
}

void Renderer::Impl::clearAssets() {
    check(vkDeviceWaitIdle(deviceState.device), "wait asset reset");
    completed = submitted;
    collectRetired(true);
    assetOwners = {};
    spatialRegions.clear();
    assetVersions.clear();
    assetLastUsed.clear();
    for (auto& f : frames)
        releaseUploads(f);
    trimStagingPool(true);
    collectRetired(true);
    preparation.images.clear();
    preparation.meshes.clear();
    cancelUploads();
    destroyTextures();
    destroyGeometryMeshes();
    destroyShadowCommands();
    assetMemory = {};
    for (auto& f : frames)
        f.geometry.dynamicCached = false;
}

void Renderer::Impl::close() {
    if (deviceState.device) {
        vkDeviceWaitIdle(deviceState.device);
        completed = submitted;
        collectRetired(true);
        assetOwners = {};
        spatialRegions.clear();
        assetVersions.clear();
        assetLastUsed.clear();
        for (auto& f : frames)
            releaseUploads(f);
        preparation.images.clear();
        preparation.meshes.clear();
        cancelUploads();
        destroySwapchain();
        if(volumes.gpu)retireVolumeBuffers();
        destroySharedResources();
        destroyFrameResources();
        completed=submitted;collectRetired(true);
        trimStagingPool(true);
        if (deviceState.pool)
            vkDestroyCommandPool(deviceState.device, deviceState.pool, nullptr);
        vkDestroyDevice(deviceState.device, nullptr);
        deviceState.device = VK_NULL_HANDLE;
    }
    if (deviceState.surface) {
        vkDestroySurfaceKHR(deviceState.instance, deviceState.surface, nullptr);
        deviceState.surface = VK_NULL_HANDLE;
    }
    if (deviceState.debug) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(deviceState.instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(deviceState.instance, deviceState.debug, nullptr);
        deviceState.debug = VK_NULL_HANDLE;
    }
    if (deviceState.instance) {
        vkDestroyInstance(deviceState.instance, nullptr);
        deviceState.instance = VK_NULL_HANDLE;
    }
}
} // namespace one
