#include "backend.hpp"
#include <unordered_set>
namespace one {
const Renderer::Impl::MaterialGPU& Renderer::Impl::material(const one::Material* source,
                                                            const one::Image* baseImage) {
    auto found = materialsState.materials.find(source);
    if (found != materialsState.materials.end()) {
        if (found->second.textureBindingsDirty[frameIndex])
            refreshMaterialTextures(found->second);
        return found->second;
    }
    vulkan::ScopedTiming totalTiming(uploadStats.materialTotalMs);
    ++stats.materialCreations;
    one::Material fallback;
    const auto& m = source ? *source : fallback;
    auto& gpu = materialsState.materials[source];
    glm::vec4 emissive(m.emissiveFactor, 0);
    createBuffer(sizeof(emissive), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, gpu.uniform, gpu.memory);
    void* ptr = nullptr;
    check(vkMapMemory(deviceState.device, gpu.memory, 0, sizeof(emissive), 0, &ptr), "map material");
    std::memcpy(ptr, &emissive, sizeof(emissive));
    vkUnmapMemory(deviceState.device, gpu.memory);
    std::array<VkDescriptorImageInfo, 6> images{
        {{materialsState.sampler, texture(baseImage).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {materialsState.shadowSampler, frame().scene.shadowView,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
         {materialsState.sampler, texture(m.metallicRoughness, false).view,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {materialsState.sampler, texture(m.normal ? m.normal : &materialsState.flatNormal, false).view,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {materialsState.sampler, texture(m.occlusion, false).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {materialsState.sampler, texture(m.emissive).view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
    std::array<VkDescriptorBufferInfo, 2> buffers{
        {{frame().scene.uniform, 0, sizeof(SceneUniform)}, {gpu.uniform, 0, sizeof(emissive)}}};
    gpu.images = {baseImage, m.metallicRoughness, m.normal, m.occlusion, m.emissive};
    gpu.residencyTextures = {&texture(baseImage), &texture(m.metallicRoughness, false),
                             &texture(m.normal ? m.normal : &materialsState.flatNormal, false),
                             &texture(m.occlusion, false), &texture(m.emissive)};
    for (auto t : gpu.residencyTextures)
        gpu.hasTextureDetail |= t->prepared->mips.size() > 1;
    for (uint32_t slot = 0; slot < vulkan::frameSlots; ++slot) {
        images[1].imageView = frames[slot].scene.shadowView;
        buffers[0].buffer = frames[slot].scene.uniform;
        VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        a.descriptorPool = materialsState.pool;
        a.descriptorSetCount = 1;
        a.pSetLayouts = &materialsState.layout;
        check(vkAllocateDescriptorSets(deviceState.device, &a, &gpu.sets[slot]),
              "allocate material descriptor");
        std::array<VkWriteDescriptorSet, 8> writes{};
        for (uint32_t i = 0; i < 8; ++i) {
            auto& w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = gpu.sets[slot];
            w.dstBinding = i;
            w.descriptorCount = 1;
            if (i == 1 || i == 7) {
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &buffers[i == 1 ? 0 : 1];
            } else {
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &images[i == 0 ? 0 : i - 1];
            }
        }
        vkUpdateDescriptorSets(deviceState.device, 8, writes.data(), 0, nullptr);
    }
    return gpu;
}

void Renderer::Impl::destroyTextures() {
    textureAliases = {};
    destroyShadowMasks();
    ++materialsState.generation;materialsState.references.clear();
    materialsState.pendingUploads.clear();
    materialsState.uploadBytes.clear();
    for (auto& [key, m] : materialsState.materials) {
        (void)key;
        for (auto set : m.sets)
            if (set)
                vkFreeDescriptorSets(deviceState.device, materialsState.pool, 1, &set);
        if (m.uniform)
            vkDestroyBuffer(deviceState.device, m.uniform, nullptr);
        if (m.memory)
            vkFreeMemory(deviceState.device, m.memory, nullptr);
    }
    materialsState.materials.clear();
    std::unordered_set<VkImage> destroyed;
    for (auto* map : {&materialsState.textures, &materialsState.replacements})
        for (auto& [key, t] : *map) {
            if (!destroyed.insert(t.image).second)
                continue;
            if (t.upload)
                t.upload->cancelled = true;
            (void)key;

            if (t.view)
                vkDestroyImageView(deviceState.device, t.view, nullptr);
            if (t.image)
                vkDestroyImage(deviceState.device, t.image, nullptr);
            if (t.memory)
                vkFreeMemory(deviceState.device, t.memory, nullptr);
        }
    materialsState.textures.clear();
    materialsState.replacements.clear();
    ++materialsState.memoryRevision;
    materialsState.demand.clear();
}
} // namespace one
