#include <theone/render/renderer.hpp>
#include "vulkan/backend.hpp"
namespace one {
Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() {
    close();
}
void Renderer::initialize(SDL_Window* window, bool validate) {
    impl_->initialize(window, validate);
}
bool Renderer::beginFrame() {
    return impl_->beginFrame();
}
bool Renderer::waitForFrame() {
    return impl_->waitForFrame();
}
void Renderer::enableHdr(bool enabled) {
    impl_->volumes.hdr = enabled;
}
void Renderer::setLighting(const SceneLighting& lighting) {
    if (!std::isfinite(lighting.exposure) || lighting.exposure <= 0 ||
        glm::length(lighting.sunDirection) < .001f)
        throw std::invalid_argument("Invalid lighting");
    for (int a = 0; a < 3; ++a)
        if (!std::isfinite(lighting.sunDirection[a]) || !std::isfinite(lighting.sunRadiance[a]) ||
            lighting.sunRadiance[a] < 0 || !std::isfinite(lighting.skyRadiance[a]) ||
            lighting.skyRadiance[a] < 0)
            throw std::invalid_argument("Invalid lighting");
    impl_->volumes.lighting = lighting;
    impl_->volumes.lighting.sunDirection = glm::normalize(lighting.sunDirection);
}
void Renderer::setVolumeScene(const VolumeScene& scene) {
    validateVolumeScene(scene);
    auto& v = impl_->volumes;
    if (!v.active || v.scene.generation != scene.generation ||
        v.scene.domain.resolution != scene.domain.resolution ||
        v.scene.domain.origin != scene.domain.origin || v.scene.domain.extent != scene.domain.extent ||
        scene.timeSeconds + 1e-6 < v.simulated) {
        v.initialize = true;
        v.simulated = 0;
        v.stats = {};
        for (auto& f : impl_->frames)
            f.volumeQueryWritten = false;
    }
    if (v.scene.obstacles.size() != scene.obstacles.size() ||
        !std::equal(v.scene.obstacles.begin(), v.scene.obstacles.end(), scene.obstacles.begin(),
                    scene.obstacles.end(), [](const auto& a, const auto& b) {
                        return a.minimum == b.minimum && a.maximum == b.maximum && a.velocity == b.velocity;
                    }))
        v.obstaclesDirty = true;
    v.scene = scene;
    v.active = true;
    v.hdr = true;
}
void Renderer::clearVolumes() {
    auto& v = impl_->volumes;
    v.active = false;
    v.initialize = true;
    v.simulated = 0;
    v.stats = {};
    impl_->retireVolumeBuffers();
    for (auto& f : impl_->frames)
        f.volumeQueryWritten = false;
}
const VolumeStats& Renderer::volumeStats() const {
    return impl_->volumes.stats;
}
VolumeReadback Renderer::captureVolumeState() {
    return impl_->captureVolumeState();
}
void Renderer::resize() {
    impl_->swapchainState.resizeRequested = true;
}
bool Renderer::render(const std::vector<Vertex>& vertices, const std::vector<Draw>& draws,
                      const Camera& camera, float time, bool capture, bool shadows, uint64_t selected,
                      bool classicColor) {
    return impl_->draw(vertices, draws, camera, time, capture, shadows, selected, classicColor);
}
bool Renderer::render(const RenderScene& scene, const std::vector<Vertex>& vertices,
                      const std::vector<Draw>& draws, const Camera& camera, float time, bool capture,
                      bool shadows, uint64_t selected, bool classicColor) {
    return impl_->draw(vertices, draws, camera, time, capture, shadows, selected, classicColor, &scene);
}
bool Renderer::renderCached(const std::vector<Vertex>& vertices, const std::vector<Draw>& draws,
                            uint64_t revision, const Camera& camera, float time, bool capture, bool shadows,
                            uint64_t selected, bool classicColor) {
    return impl_->draw(vertices, draws, camera, time, capture, shadows, selected, classicColor, nullptr,
                       &revision);
}
const GeometryStats& Renderer::geometryStats() const {
    return impl_->geometryState.stats;
}
const AssetUploadStats& Renderer::assetUploadStats() const {
    return impl_->uploadStats;
}
const FrameStats& Renderer::frameStats() const {
    return impl_->stats;
}
const AssetMemoryStats& Renderer::assetMemoryStats() const {
    return impl_->assetMemory;
}
void Renderer::setAssetMemoryBudget(uint64_t bytes) {
    impl_->assetMemoryBudget = bytes;
}
void Renderer::trimUnusedAssets() {
    impl_->trimRequested = true;
}
void Renderer::drawProfiler(bool* open) {
    impl_->drawProfiler(open);
}
void Renderer::setVsync(bool enabled) {
    if (impl_->swapchainState.vsync != enabled) {
        impl_->swapchainState.vsync = enabled;
        impl_->swapchainState.resized = true;
    }
}
bool Renderer::vsyncEnabled() const {
    return impl_->swapchainState.presentMode == VK_PRESENT_MODE_FIFO_KHR;
}
void Renderer::setShadowBatching(bool enabled, bool indirect) {
    impl_->shadowBatching = enabled;
    impl_->shadowIndirect = indirect;
}
void Renderer::setSpatialCulling(bool enabled) {
    impl_->spatialCulling = enabled;
}
void Renderer::setShadowLod(bool enabled) {
    impl_->shadowLod = enabled;
}
void Renderer::setCulling(bool enabled) {
    impl_->culling = enabled;
}
void Renderer::setTextureStreaming(bool enabled) {
    impl_->textureStreaming = enabled;
}
void Renderer::setTextureMemoryBudget(uint64_t bytes) {
    if (bytes > impl_->textureBudget)
        impl_->texturePressure = 0;
    impl_->textureBudget = bytes;
}
void Renderer::setInstancing(bool enabled) {
    impl_->instancing = enabled;
}
void Renderer::setLod(bool enabled) {
    impl_->lodEnabled = enabled;
}
void Renderer::setUploadBudget(uint64_t bytes) {
    impl_->uploadBudget = std::max<uint64_t>(4, bytes);
}
void Renderer::setUploadTimeBudget(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds <= 0)
        throw std::invalid_argument("Invalid upload time budget");
    impl_->uploads.budgetMs = milliseconds;
}
void Renderer::setShadowDistance(float distance) {
    if (!std::isfinite(distance) || distance <= 0)
        throw std::invalid_argument("Invalid shadow distance");
    impl_->shadowDistance = distance;
}
void Renderer::setFramesInFlight(unsigned count) {
    if (count < 1 || count > vulkan::frameSlots)
        throw std::invalid_argument("Frames in flight must be 1 or 2");
    if (count == impl_->frameCount)
        return;
    if (impl_->deviceState.device) {
        check(vkDeviceWaitIdle(impl_->deviceState.device), "change frame count");
        impl_->completed = impl_->submitted;
        impl_->collectRetired();
    }
    impl_->frameCount = count;
    impl_->frameIndex = 0;
    impl_->frameReady = false;
}
void Renderer::invalidateAsset(const std::shared_ptr<const Model>& model) {
    impl_->invalidateAsset(model);
}
void Renderer::invalidateAssets(const std::vector<std::shared_ptr<const Model>>& models) {
    impl_->invalidateAssets(models);
}
void Renderer::invalidateImage(const Image* image) {
    impl_->invalidateImage(image);
}
void Renderer::invalidateMaterial(const Material* material) {
    impl_->invalidateMaterial(material);
}
void Renderer::clearAssets() {
    impl_->clearAssets();
}
unsigned Renderer::errors() const {
    return impl_->deviceState.validationErrors;
}
void Renderer::close() {
    impl_->close();
}
} // namespace one
