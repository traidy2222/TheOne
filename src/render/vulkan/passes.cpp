#include "backend.hpp"
#include "cpu_profiling.hpp"
namespace one {
void Renderer::Impl::recordScene(uint32_t index, const std::vector<one::Draw>& draws,
                                 const one::Camera& camera, float time, bool capture, bool shadows,
                                 uint64_t selected, const RenderScene* scene) {
    geometryState.boundBuffer = VK_NULL_HANDLE;
    const auto cameraVP =
        camera.projection(float(swapchainState.extent.width) / swapchainState.extent.height) * camera.view();
    // Instancing filters draws to camera-visible meshes, but shadow batches can still
    // use the palette when the camera list is empty. Bind for the prepared scene.
    if (scene && !scene->transforms.empty())
        vkCmdBindDescriptorSets(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneState.pipelineLayout,
                                1, 1, &frame().geometry.set, 0, nullptr);
    VkPipeline activePipeline = VK_NULL_HANDLE;
    auto bindPipeline = [&](VkPipeline pipeline) {
        if (activePipeline != pipeline) {
            vkCmdBindPipeline(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            activePipeline = pipeline;
        }
    };
    {
        vulkan::ScopedTiming timing(stats.shadowRecordMs);
        vulkan::CpuStageScope cpuStage(stats, CpuStage::Shadow);
        recordShadows(draws, shadows, scene);
    }
    vulkan::ScopedTiming mainTiming(stats.mainRecordMs);
    vulkan::CpuStageScope cpuStage(stats, CpuStage::Main);
    if (frame().queries)
        vkCmdWriteTimestamp(frame().command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 1);
    std::array<VkClearValue, 2> clear{};
    clear[0].color = {{0.035f, 0.055f, 0.09f, 1}};
    clear[1].depthStencil = {1, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = targetsState.renderPass;
    begin.framebuffer = targetsState.framebuffers[index];
    begin.renderArea = {{0, 0}, swapchainState.extent};
    begin.clearValueCount = 2;
    begin.pClearValues = clear.data();
    vkCmdBeginRenderPass(frame().command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0,
                  0,
                  static_cast<float>(swapchainState.extent.width),
                  static_cast<float>(swapchainState.extent.height),
                  0,
                  1};
    VkRect2D scissor{{0, 0}, swapchainState.extent};
    vkCmdSetViewport(frame().command, 0, 1, &vp);
    vkCmdSetScissor(frame().command, 0, 1, &scissor);
    VkDescriptorSet activeMaterialSet=VK_NULL_HANDLE;
    const Material* constantsMaterial=nullptr;bool haveConstants=false,constantsSelected=false;
    for (const auto& draw : draws) {
            one::Material fallback;
            const auto& m = draw.material ? *draw.material : fallback;
            if (!draw.count)
                continue;
            if (!instanceBatchesReady && !draw.cameraVisible) {
                ++stats.mainCulled;
                if (draw.geometry && scene->transforms.at(draw.palette).info.x)
                    ++stats.animatedCulled;
                continue;
            }
            ++stats.mainDraws;
            stats.mainInstances += draw.instances;
            uint32_t indices =
                draw.geometry ? resolvedMesh(draw)->levels.at(draw.lod).count : draw.count;
            stats.mainTriangles += indices / 3 * draw.instances;
            if (draw.lod) {
                ++stats.lodDraws;
                stats.lodTrianglesSaved += (draw.count - indices) / 3 * draw.instances;
            }
            bindPipeline(draw.geometry ? (m.blend ? targetsState.meshBlendPipeline : targetsState.meshPipeline)
                                       : (m.blend ? targetsState.blendPipeline : targetsState.pipeline));
            const auto& tex = resolvedMaterial(draw);
            if(referenceDrawPreparation || activeMaterialSet!=tex.sets[frameIndex]) {
                vkCmdBindDescriptorSets(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    sceneState.pipelineLayout,0,1,&tex.sets[frameIndex],0,nullptr);
                activeMaterialSet=tex.sets[frameIndex];
            }
            if(referenceDrawPreparation || !haveConstants || constantsMaterial!=draw.material ||
                constantsSelected!=(selected==draw.entity)) {
            Frame f{cameraVP, glm::vec4(camera.position, time), m.color,
                    glm::vec4(m.roughness, m.metallic, m.mask ? m.cutoff : -1.0f,
                              selected == draw.entity ? 1.0f : 0.0f),
                    glm::vec4(swapchainState.colorFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                                      swapchainState.colorFormat == VK_FORMAT_R8G8B8A8_SRGB
                                  ? 1.0f
                                  : 0.0f,
                              m.unlit ? 1.0f : 0.0f, m.normalScale, m.occlusionStrength)};
            vkCmdPushConstants(frame().command, sceneState.pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(f), &f);
            haveConstants=true;constantsMaterial=draw.material;constantsSelected=selected==draw.entity;
            }
            issueGeometryDraw(draw, true);
    }
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame().command);
    vkCmdEndRenderPass(frame().command);
    if (frame().queries)
        vkCmdWriteTimestamp(frame().command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 2);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = swapchainState.images[index];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.newLayout = capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.dstAccessMask = capture ? VK_ACCESS_TRANSFER_READ_BIT : 0;
    vkCmdPipelineBarrier(frame().command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         capture ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    if (capture) {
        if (!captureState.buffer)
            createBuffer(static_cast<VkDeviceSize>(swapchainState.extent.width) *
                             swapchainState.extent.height * 4,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT, captureState.buffer, captureState.memory);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {swapchainState.extent.width, swapchainState.extent.height, 1};
        vkCmdCopyImageToBuffer(frame().command, swapchainState.images[index],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureState.buffer, 1, &copy);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(frame().command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}
} // namespace one
