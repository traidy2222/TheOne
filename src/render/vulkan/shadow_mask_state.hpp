#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <theone/assets/material.hpp>
namespace one::vulkan {
struct ShadowMaskRecord {
    VkPipeline pipeline = VK_NULL_HANDLE;
    const Image* image = nullptr;
    const Material* material = nullptr;
    bool bound = false;
};
struct ShadowMaskBinding {
    std::array<VkDescriptorSet, 2> sets{};
    std::array<VkImageView, 2> views{};
};
} // namespace one::vulkan
