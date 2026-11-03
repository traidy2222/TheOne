#pragma once
#include <theone/core/math.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
namespace one {
struct PreparedImage;
struct Image {
    std::string name;
    int width = 1, height = 1;
    std::vector<uint8_t> rgba{255, 255, 255, 255};
    uint64_t revision = 1, preparedRevision = 0;
    std::shared_ptr<const PreparedImage> prepared;
    void reload(const std::filesystem::path& path);
};
struct Material {
    std::string name;
    glm::vec4 color{1};
    float roughness = 1, metallic = 0, cutoff = 0.5f;
    int image = -1;
    bool blend = false, mask = false, unlit = false;
    const Image *metallicRoughness = nullptr, *normal = nullptr, *occlusion = nullptr, *emissive = nullptr;
    glm::vec3 emissiveFactor{0};
    float normalScale = 1, occlusionStrength = 1;
};
} // namespace one
