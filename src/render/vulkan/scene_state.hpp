#pragma once
#include <vulkan/vulkan.h>
#include <theone/render/renderer.hpp>
#include <theone/render/shadows.hpp>
namespace one::vulkan {
struct FrameUniform {
    glm::mat4 viewProjection;
    glm::vec4 eyeTime, color, params, lighting;
};
struct SceneUniform {
    std::array<glm::mat4, shadowCascadeCount> lightVP;
    glm::vec4 lightDirection, options, splits, cameraForward;
};
struct FrameScene {
    VkBuffer uniform = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
    VkImageView shadowView = VK_NULL_HANDLE;
    std::array<VkFramebuffer, shadowCascadeCount> shadowFramebuffers{};
    std::array<VkImageView, shadowCascadeCount> shadowLayers{};
};
struct SceneResources {
    ShadowCascades cascades;
    VkRenderPass shadowPass = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE, meshShadowPipeline = VK_NULL_HANDLE;
    VkPipeline shadowMaskPipeline = VK_NULL_HANDLE, meshShadowMaskPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};
} // namespace one::vulkan
