#include "backend.hpp"
namespace one {
bool Renderer::Impl::prepareShadowMask(const Draw& draw) {
    vulkan::TextureKey key{draw.image, true};
    auto found = materialsState.textures.find(key);
    if (found != materialsState.textures.end())
        return found->second.upload->ready;
    auto prepared = preparedImage(draw.image);
    if (!prepared)
        return false;
    uint32_t mip = 0;
    if (textureStreaming)
        while (mip + 1 < prepared->mips.size() &&
               std::max(prepared->mips[mip].width, prepared->mips[mip].height) > 64)
            ++mip;
    bool alias =
        std::any_of(materialsState.textures.begin(), materialsState.textures.end(), [&](const auto& item) {
            return item.first.srgb && item.second.prepared == prepared && item.second.firstMip == mip &&
                   !item.second.upload->cancelled;
        });
    if (!alias && !reserveUpload(uint64_t(prepared->mips[mip].width) * prepared->mips[mip].height * 4))
        return false;
    return texture(draw.image).upload->ready;
}
VkDescriptorSet Renderer::Impl::shadowMaskDescriptor(const Image* image) {
    const auto& texture = materialsState.textures.at({image, true});
    auto& binding = shadowMasks[image];
    auto& set = binding.sets[frameIndex];
    if (!set) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = materialsState.pool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &materialsState.layout;
        check(vkAllocateDescriptorSets(deviceState.device, &info, &set), "allocate alpha shadow descriptor");
    }
    if (binding.views[frameIndex] != texture.view) {
        // Only this fence-complete slot is rewritten. The shader accesses binding zero alone.
        VkDescriptorImageInfo imageInfo{materialsState.sampler, texture.view,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(deviceState.device, 1, &write, 0, nullptr);
        binding.views[frameIndex] = texture.view;
    }
    return set;
}
void Renderer::Impl::invalidateShadowMask(const Image* image) {
    auto it = shadowMasks.find(image);
    if (it == shadowMasks.end())
        return;
    auto binding = it->second;
    shadowMasks.erase(it);
    retire([this, binding] {
        for (auto set : binding.sets)
            if (set)
                vkFreeDescriptorSets(deviceState.device, materialsState.pool, 1, &set);
    });
}
void Renderer::Impl::destroyShadowMasks() {
    for (const auto& [image, binding] : shadowMasks) {
        (void)image;
        for (auto set : binding.sets)
            if (set)
                vkFreeDescriptorSets(deviceState.device, materialsState.pool, 1, &set);
    }
    shadowMasks.clear();
}
void Renderer::Impl::submitMaskedShadow(const Draw& draw, uint32_t cascade, vulkan::ShadowMaskRecord* state) {
    auto pipeline = draw.geometry ? sceneState.meshShadowMaskPipeline : sceneState.shadowMaskPipeline;
    if (!state || state->pipeline != pipeline) {
        vkCmdBindPipeline(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        if (state)
            state->pipeline = pipeline;
    }
    if (!state || !state->bound || state->image != draw.image) {
        auto descriptor = shadowMaskDescriptor(draw.image);
        vkCmdBindDescriptorSets(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneState.pipelineLayout,
                                0, 1, &descriptor, 0, nullptr);
        if (state) {
            state->image = draw.image;
            state->bound = true;
        }
    }
    if (!state || state->material != draw.material) {
        const auto& m = *draw.material;
        Frame constants{sceneState.cascades.matrices[cascade], glm::vec4(0), m.color,
                        glm::vec4(0, 0, m.cutoff, 0), glm::vec4(0)};
        vkCmdPushConstants(frame().command, sceneState.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Frame),
                           &constants);
        if (state)
            state->material = draw.material;
    }
    auto fullDetail = draw;
    fullDetail.lod = 0;
    issueGeometryDraw(fullDetail, true);
    ++stats.shadowCommands;
}
} // namespace one
