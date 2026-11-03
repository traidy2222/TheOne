#include "backend.hpp"
namespace one {
void Renderer::Impl::createRenderTargets(VkFormat colorFormat) {
    if (!targetsState.renderPass) {
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        for (auto f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(deviceState.physical, f, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = f;
                break;
            }
        }
        if (depthFormat == VK_FORMAT_UNDEFINED)
            throw std::runtime_error("No depth attachment format");
        targetsState.depthFormat = depthFormat;
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = depthFormat;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color;
        sub.pDepthStencilAttachment = &depth;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = dep.srcStageMask;
        dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo r{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        r.attachmentCount = 2;
        r.pAttachments = attachments.data();
        r.subpassCount = 1;
        r.pSubpasses = &sub;
        r.dependencyCount = 1;
        r.pDependencies = &dep;
        check(vkCreateRenderPass(deviceState.device, &r, nullptr, &targetsState.renderPass),
              "create render pass");
        targetsState.pipeline = createGraphicsPipeline(false, false);
        targetsState.blendPipeline = createGraphicsPipeline(false, true);
        targetsState.meshPipeline = createGraphicsPipeline(false, false, true);
        targetsState.meshBlendPipeline = createGraphicsPipeline(false, true, true);
        ImGui_ImplVulkan_InitInfo gui{};
        gui.ApiVersion = VK_API_VERSION_1_1;
        gui.Instance = deviceState.instance;
        gui.PhysicalDevice = deviceState.physical;
        gui.Device = deviceState.device;
        gui.QueueFamily = deviceState.family;
        gui.Queue = deviceState.queue;
        gui.DescriptorPoolSize = 2048;
        gui.RenderPass = targetsState.renderPass;
        gui.MinImageCount = swapchainState.minImages;
        gui.ImageCount = static_cast<uint32_t>(swapchainState.images.size());
        if (!ImGui_ImplVulkan_Init(&gui))
            throw std::runtime_error("Cannot initialize ImGui Vulkan backend");
        targetsState.guiReady = true;
        if (!ImGui_ImplVulkan_CreateFontsTexture())
            throw std::runtime_error("Cannot upload UI fonts");
    }
    createSurfaceTargets();
}

void Renderer::Impl::destroyRenderTargets() {
    destroySurfaceTargets();
    if (targetsState.meshPipeline)
        vkDestroyPipeline(deviceState.device, targetsState.meshPipeline, nullptr);
    targetsState.meshPipeline = VK_NULL_HANDLE;
    if (targetsState.meshBlendPipeline)
        vkDestroyPipeline(deviceState.device, targetsState.meshBlendPipeline, nullptr);
    targetsState.meshBlendPipeline = VK_NULL_HANDLE;
    if (targetsState.guiReady) {
        ImGui_ImplVulkan_Shutdown();
        targetsState.guiReady = false;
    }
    if (targetsState.blendPipeline)
        vkDestroyPipeline(deviceState.device, targetsState.blendPipeline, nullptr);
    targetsState.blendPipeline = VK_NULL_HANDLE;
    if (targetsState.pipeline)
        vkDestroyPipeline(deviceState.device, targetsState.pipeline, nullptr);
    targetsState.pipeline = VK_NULL_HANDLE;
    if (targetsState.renderPass)
        vkDestroyRenderPass(deviceState.device, targetsState.renderPass, nullptr);
    targetsState.renderPass = VK_NULL_HANDLE;
}
} // namespace one
