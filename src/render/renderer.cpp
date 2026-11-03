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
