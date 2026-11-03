#include "backend.hpp"
namespace one {
void Renderer::Impl::dirtyTextureMaterials(const Image* image) {
    for (auto& [key, gpu] : materialsState.materials)
        if (std::find(gpu.images.begin(), gpu.images.end(), image) != gpu.images.end())
            gpu.textureBindingsDirty.fill(true);
}
void Renderer::Impl::refreshMaterialTextures(MaterialGPU& gpu) {
    vulkan::ScopedTiming timing(stats.residencyBindingsMs);
    // startFrame has waited for this slot's fence. Other slots keep their old
    // descriptors until their own fence completes; retirement protects in-flight use.
    VkDescriptorSet replacement = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = materialsState.pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &materialsState.layout;
    check(vkAllocateDescriptorSets(deviceState.device, &allocation, &replacement),
          "allocate resident material descriptor");
    constexpr std::array<uint32_t, 3> stableBindings{1, 2, 7};
    std::array<VkCopyDescriptorSet, 3> copies{};
    for (size_t i = 0; i < copies.size(); ++i) {
        auto& copy = copies[i];
        copy = {VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
        copy.srcSet = gpu.sets[frameIndex];
        copy.dstSet = replacement;
        copy.srcBinding = copy.dstBinding = stableBindings[i];
        copy.descriptorCount = 1;
    }
    constexpr std::array<uint32_t, 5> bindings{0, 3, 4, 5, 6};
    std::array<VkDescriptorImageInfo, 5> images{};
    std::array<VkWriteDescriptorSet, 5> writes{};
    for (size_t i = 0; i < writes.size(); ++i) {
        images[i] = {materialsState.sampler, gpu.residencyTextures[i]->view,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto& write = writes[i];
        write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = replacement;
        write.dstBinding = bindings[i];
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(deviceState.device, uint32_t(writes.size()), writes.data(),
                           uint32_t(copies.size()), copies.data());
    vkFreeDescriptorSets(deviceState.device, materialsState.pool, 1, &gpu.sets[frameIndex]);
    gpu.sets[frameIndex] = replacement;
    gpu.textureBindingsDirty[frameIndex] = false;
}
} // namespace one
