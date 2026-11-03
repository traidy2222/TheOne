#pragma once
#include <theone/render/draw.hpp>
#include <vector>
namespace one::vulkan {
struct AssetOwners {
    std::vector<const Model*> sequence;
    std::vector<std::shared_ptr<const Model>> unique;
    void update(const std::vector<Draw>& scene, const std::vector<Draw>& dynamic);
};
} // namespace one::vulkan
