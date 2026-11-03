#pragma once
#include <vulkan/vulkan.h>
#include <vector>
namespace one::vulkan {

struct DepthTarget {
    VkImage depth = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
};
struct RenderTargets {
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    bool guiReady = false;
    std::vector<DepthTarget> depths;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE, blendPipeline = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE, meshBlendPipeline = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
};
} // namespace one::vulkan
