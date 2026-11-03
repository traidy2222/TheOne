#include "backend.hpp"
namespace one {
void Renderer::Impl::createSurfaceTargets() {
    for (auto imageHandle : swapchainState.images) {
        targetsState.depths.emplace_back();
        auto& depthTarget = targetsState.depths.back();
        createImage(swapchainState.extent.width, swapchainState.extent.height, targetsState.depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthTarget.depth, depthTarget.depthMemory);
        depthTarget.depthView =
            imageView(depthTarget.depth, targetsState.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        targetsState.views.push_back(
            imageView(imageHandle, swapchainState.colorFormat, VK_IMAGE_ASPECT_COLOR_BIT));
        std::array<VkImageView, 2> targets{targetsState.views.back(), depthTarget.depthView};
        VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        f.renderPass = targetsState.renderPass;
        f.attachmentCount = 2;
        f.pAttachments = targets.data();
        f.width = swapchainState.extent.width;
        f.height = swapchainState.extent.height;
        f.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        check(vkCreateFramebuffer(deviceState.device, &f, nullptr, &framebuffer), "create framebuffer");
        targetsState.framebuffers.push_back(framebuffer);
    }
}
void Renderer::Impl::destroySurfaceTargets() {
    for (auto f : targetsState.framebuffers)
        vkDestroyFramebuffer(deviceState.device, f, nullptr);
    targetsState.framebuffers.clear();
    for (auto v : targetsState.views)
        vkDestroyImageView(deviceState.device, v, nullptr);
    targetsState.views.clear();
    for (auto& depthTarget : targetsState.depths) {
        if (depthTarget.depthView)
            vkDestroyImageView(deviceState.device, depthTarget.depthView, nullptr);
        if (depthTarget.depth)
            vkDestroyImage(deviceState.device, depthTarget.depth, nullptr);
        if (depthTarget.depthMemory)
            vkFreeMemory(deviceState.device, depthTarget.depthMemory, nullptr);
    }
    targetsState.depths.clear();
    if (captureState.buffer)
        vkDestroyBuffer(deviceState.device, captureState.buffer, nullptr);
    captureState.buffer = VK_NULL_HANDLE;
    if (captureState.memory)
        vkFreeMemory(deviceState.device, captureState.memory, nullptr);
    captureState.memory = VK_NULL_HANDLE;
}
} // namespace one
