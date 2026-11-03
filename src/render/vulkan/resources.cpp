#include "backend.hpp"
namespace one {
void Renderer::Impl::createSharedResources() {
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (uint32_t i = 3; i <= 6; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                       nullptr};
    bindings[7] = {7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 8;
    layout.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(deviceState.device, &layout, nullptr, &materialsState.layout),
          "create descriptor layout");
    constexpr uint32_t maxMaterialSets = 8192 * vulkan::frameSlots;
    std::array<VkDescriptorPoolSize, 2> sizes{
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 * maxMaterialSets},
         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * maxMaterialSets}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = maxMaterialSets;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(deviceState.device, &pool, nullptr, &materialsState.pool),
          "create descriptor pool");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.anisotropyEnable = deviceState.anisotropySupported;
    sampler.maxAnisotropy = deviceState.anisotropy;
    check(vkCreateSampler(deviceState.device, &sampler, nullptr, &materialsState.sampler), "create sampler");
    sampler.anisotropyEnable = VK_FALSE;
    sampler.maxLod = 0;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler.compareEnable = VK_TRUE;
    sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    check(vkCreateSampler(deviceState.device, &sampler, nullptr, &materialsState.shadowSampler),
          "create shadow sampler");
    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &ref;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo r{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    r.attachmentCount = 1;
    r.pAttachments = &depth;
    r.subpassCount = 1;
    r.pSubpasses = &sub;
    r.dependencyCount = 2;
    r.pDependencies = deps.data();
    check(vkCreateRenderPass(deviceState.device, &r, nullptr, &sceneState.shadowPass), "create shadow pass");
    createFrameSceneResources();
    createGeometryResources();
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Frame)};
    VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    std::array<VkDescriptorSetLayout, 2> layouts{materialsState.layout, geometryState.layout};
    l.setLayoutCount = 2;
    l.pSetLayouts = layouts.data();
    l.pushConstantRangeCount = 1;
    l.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(deviceState.device, &l, nullptr, &sceneState.pipelineLayout),
          "create pipeline layout");
    sceneState.shadowPipeline = createGraphicsPipeline(true, false);
    sceneState.meshShadowPipeline = createGraphicsPipeline(true, false, true);
    sceneState.shadowMaskPipeline = createGraphicsPipeline(true, false, false, true);
    sceneState.meshShadowMaskPipeline = createGraphicsPipeline(true, false, true, true);
}

void Renderer::Impl::destroySharedResources() {
    destroyTextures();
    destroyGeometryResources();
    if (sceneState.shadowPipeline)
        vkDestroyPipeline(deviceState.device, sceneState.shadowPipeline, nullptr);
    if (sceneState.pipelineLayout)
        vkDestroyPipelineLayout(deviceState.device, sceneState.pipelineLayout, nullptr);
    destroyFrameSceneResources();
    if(volumes.surfaceLayout)vkDestroyPipelineLayout(deviceState.device,volumes.surfaceLayout,nullptr);
    volumes.surfaceLayout=VK_NULL_HANDLE;
    if(volumes.pipelineLayout)vkDestroyPipelineLayout(deviceState.device,volumes.pipelineLayout,nullptr);
    volumes.pipelineLayout=VK_NULL_HANDLE;
    if(volumes.layout)vkDestroyDescriptorSetLayout(deviceState.device,volumes.layout,nullptr);
    volumes.layout=VK_NULL_HANDLE;
    if (sceneState.shadowPass)
        vkDestroyRenderPass(deviceState.device, sceneState.shadowPass, nullptr);
    if (materialsState.sampler)
        vkDestroySampler(deviceState.device, materialsState.sampler, nullptr);
    if (materialsState.shadowSampler)
        vkDestroySampler(deviceState.device, materialsState.shadowSampler, nullptr);
    if (materialsState.pool)
        vkDestroyDescriptorPool(deviceState.device, materialsState.pool, nullptr);
    if (materialsState.layout)
        vkDestroyDescriptorSetLayout(deviceState.device, materialsState.layout, nullptr);
}

} // namespace one
