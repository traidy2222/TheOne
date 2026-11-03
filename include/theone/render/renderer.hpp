#pragma once
#include <theone/render/camera.hpp>
#include <theone/render/render_scene.hpp>
#include <memory>
#include <theone/render/frame_stats.hpp>
#include <theone/render/asset_upload_stats.hpp>
#include <theone/render/asset_memory_stats.hpp>
struct SDL_Window;
namespace one {
// Public rendering API. Vulkan resources and implementation are private to the library.
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void initialize(SDL_Window* window, bool validate);
    bool beginFrame();
    // Safe resource/presentation boundary without starting an ImGui frame.
    // Call before polling input; beginFrame remains compatible and idempotent.
    bool waitForFrame();
    void resize();
    bool render(const std::vector<Vertex>& vertices, const std::vector<Draw>& draws, const Camera& camera,
                float time, bool capture = false, bool shadows = true, uint64_t selected = 0,
                bool classicColor = false);
    bool render(const RenderScene& scene, const std::vector<Vertex>& dynamicVertices,
                const std::vector<Draw>& dynamicDraws, const Camera& camera, float time, bool capture = false,
                bool shadows = true, uint64_t selected = 0, bool classicColor = false);
    bool renderCached(const std::vector<Vertex>& vertices, const std::vector<Draw>& draws, uint64_t revision,
                      const Camera& camera, float time, bool capture = false, bool shadows = true,
                      uint64_t selected = 0, bool classicColor = false);
    const GeometryStats& geometryStats() const;
    const AssetUploadStats& assetUploadStats() const;
    const FrameStats& frameStats() const;
    const AssetMemoryStats& assetMemoryStats() const;
    void setAssetMemoryBudget(uint64_t bytes);
    void trimUnusedAssets();
    // Call between ImGui::NewFrame and ImGui::Render; no ImGui types leak into this API.
    void drawProfiler(bool* open = nullptr);
    // Uncapped requests immediate/mailbox presentation, falling back to FIFO if unavailable.
    void setVsync(bool enabled);
    bool vsyncEnabled() const;
    void setShadowBatching(bool enabled, bool indirect = true);
    void setSpatialCulling(bool enabled);
    void setShadowLod(bool enabled);
    void setCulling(bool enabled);
    void setLod(bool enabled);
    void setInstancing(bool enabled);
    void setTextureStreaming(bool enabled);
    void setTextureMemoryBudget(uint64_t bytes);
    void setUploadBudget(uint64_t bytes);
    void setUploadTimeBudget(double milliseconds);
    void setShadowDistance(float distance);
    // One or two; switching waits for outstanding work and is intended for profiling.
    void setFramesInFlight(unsigned count);
    void invalidateAsset(const std::shared_ptr<const Model>& model);
    void invalidateAssets(const std::vector<std::shared_ptr<const Model>>& models);
    void invalidateImage(const Image* image);
    void invalidateMaterial(const Material* material);
    void clearAssets();
    unsigned errors() const;
    void close();
};
} // namespace one
