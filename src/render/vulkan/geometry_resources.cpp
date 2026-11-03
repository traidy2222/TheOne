#include "backend.hpp"
namespace one {
void Renderer::Impl::createGeometryResources() {
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                                         nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(deviceState.device, &layout, nullptr, &geometryState.layout),
          "create geometry layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vulkan::frameSlots};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = vulkan::frameSlots;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(deviceState.device, &pool, nullptr, &geometryState.pool),
          "create geometry pool");
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = geometryState.pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &geometryState.layout;
    for (auto& context : frames)
        check(vkAllocateDescriptorSets(deviceState.device, &alloc, &context.geometry.set),
              "allocate geometry descriptor");
}

void Renderer::Impl::destroyGeometryResources() {
    destroyGeometryMeshes();
    destroyShadowCommands();
    if (sceneState.meshShadowPipeline)
        vkDestroyPipeline(deviceState.device, sceneState.meshShadowPipeline, nullptr);
    if (sceneState.shadowMaskPipeline) vkDestroyPipeline(deviceState.device,sceneState.shadowMaskPipeline,nullptr);
    if (sceneState.meshShadowMaskPipeline) vkDestroyPipeline(deviceState.device,sceneState.meshShadowMaskPipeline,nullptr);
    for (auto& context : frames) {
        if (context.geometry.paletteMapped)
            vkUnmapMemory(deviceState.device, context.geometry.paletteMemory);
        if (context.geometry.paletteBuffer)
            vkDestroyBuffer(deviceState.device, context.geometry.paletteBuffer, nullptr);
        if (context.geometry.paletteMemory)
            vkFreeMemory(deviceState.device, context.geometry.paletteMemory, nullptr);
    }
    if (geometryState.pool)
        vkDestroyDescriptorPool(deviceState.device, geometryState.pool, nullptr);
    if (geometryState.layout)
        vkDestroyDescriptorSetLayout(deviceState.device, geometryState.layout, nullptr);
}
} // namespace one
