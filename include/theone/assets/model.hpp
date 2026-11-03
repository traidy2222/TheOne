#pragma once
#include <theone/animation/rig.hpp>
#include <theone/animation/clip.hpp>
#include <theone/assets/mesh.hpp>
#include <theone/assets/material.hpp>
#include <theone/assets/load_stats.hpp>
#include <theone/render/draw.hpp>
#include <filesystem>
#include <stop_token>
#include <theone/assets/asset_handle.hpp>
namespace one {
struct Model {
    AssetHandle handle = newAssetHandle();
    std::filesystem::path source;
    // Validated source/dependency fingerprint; zero for procedural or uncached models.
    uint64_t sourceFingerprint = 0;
    std::vector<Image> images;
    std::vector<Material> materials;
    std::vector<std::vector<Primitive>> meshes;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Animation> animations;
    std::vector<std::string> diagnostics;
    static std::shared_ptr<Model> load(const std::filesystem::path& path, ModelLoadStats* stats = nullptr,
                                       std::stop_token stop = {});
    std::vector<glm::mat4> pose(float time, int clip, bool animate, int previousClip = -1,
                                float previousTime = 0, float blend = 1, bool loop = true,
                                bool previousLoop = true) const;
    void bake(const glm::mat4& root, float time, int clip, bool animate, uint64_t id,
              std::vector<Vertex>& vertices, std::vector<Draw>& draws, int previousClip = -1,
              float previousTime = 0, float blend = 1, bool loop = true, bool previousLoop = true) const;
};
} // namespace one
