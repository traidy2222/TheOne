#include "backend.hpp"
namespace one {
void Renderer::Impl::recordHdrScene(uint32_t index, const std::vector<Draw>& draws, const Camera& camera,
                                    float time, bool shadows, uint64_t selected, const RenderScene* scene) {
    auto command = frame().command;
    auto set = volumes.gpu->sets[frameIndex].at(index);
    auto& target = volumes.targets.at(index);
    auto bindScene = [&] {
        if (scene && !scene->transforms.empty())
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.surfaceLayout, 1, 1,
                                    &frame().geometry.set, 0, nullptr);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.surfaceLayout, 2, 1, &set,
                                0, nullptr);
    };
    if (scene && !scene->transforms.empty())
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneState.pipelineLayout, 1, 1,
                                &frame().geometry.set, 0, nullptr);
    {
        vulkan::ScopedTiming timing(stats.shadowRecordMs);
        recordShadows(draws, shadows, scene);
    }
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 1);
    recordVolumeLighting(index);
    vulkan::ScopedTiming timing(stats.mainRecordMs);
    auto viewport = [&] {
        VkViewport vp{0, 0, float(swapchainState.extent.width), float(swapchainState.extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, swapchainState.extent};
        vkCmdSetViewport(command, 0, 1, &vp);
        vkCmdSetScissor(command, 0, 1, &scissor);
    };
    auto beginPass = [&](VkRenderPass pass, VkFramebuffer fb, bool display) {
        std::array<VkClearValue, 2> clear{};
        auto sky = volumes.lighting.skyRadiance * (.35f / 1024.f);
        clear[0].color = {{display ? 0 : sky.x, display ? 0 : sky.y, display ? 0 : sky.z, 1}};
        clear[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        b.renderPass = pass;
        b.framebuffer = fb;
        b.renderArea = {{0, 0}, swapchainState.extent};
        b.clearValueCount = 2;
        b.pClearValues = clear.data();
        vkCmdBeginRenderPass(command, &b, VK_SUBPASS_CONTENTS_INLINE);
        viewport();
    };
    const auto vp =
        camera.projection(float(swapchainState.extent.width) / swapchainState.extent.height) * camera.view();
    auto surfaces = [&](bool blended) {
        geometryState.boundBuffer = VK_NULL_HANDLE;
        bindScene();
        for (const auto& draw : draws) {
            Material fallback;
            const auto& m = draw.material ? *draw.material : fallback;
            if (m.blend != blended || !draw.count || (!instanceBatchesReady && !draw.cameraVisible))
                continue;
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              volumes.surfaces[(draw.geometry ? 2 : 0) + (blended ? 1 : 0)]);
            const auto& material = resolvedMaterial(draw);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.surfaceLayout, 0, 1,
                                    &material.sets[frameIndex], 0, nullptr);
            Frame f{vp, glm::vec4(camera.position, time), m.color,
                    glm::vec4(m.roughness, m.metallic, m.mask ? m.cutoff : -1.f,
                              selected == draw.entity ? 1.f : 0.f),
                    glm::vec4(1, m.unlit ? 1.f : 0.f, m.normalScale, m.occlusionStrength)};
            vkCmdPushConstants(command, volumes.surfaceLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(f), &f);
            issueGeometryDraw(draw, true);
            ++stats.mainDraws;
            stats.mainInstances += draw.instances;
            stats.mainTriangles +=
                (draw.geometry ? resolvedMesh(draw)->levels.at(draw.lod).count : draw.count) / 3 *
                draw.instances;
        }
    };
    beginPass(volumes.hdrPass, target.opaque, false);
    surfaces(false);
    vkCmdEndRenderPass(command);
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame().queries, 10);
    beginPass(volumes.compositePass, target.composite, false);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.resolve);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.pipelineLayout, 0, 1, &set, 0,
                            nullptr);
    vkCmdDraw(command, 3, 1, 0, 0);
    surfaces(true);
    vkCmdEndRenderPass(command);
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 11);
    beginPass(targetsState.renderPass, targetsState.framebuffers[index], true);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.output);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, volumes.pipelineLayout, 0, 1, &set, 0,
                            nullptr);
    vulkan::VolumePush push{};
    push.flags = swapchainState.colorFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                 swapchainState.colorFormat == VK_FORMAT_R8G8B8A8_SRGB;
    vkCmdPushConstants(command, volumes.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 3, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    vkCmdEndRenderPass(command);
    if (frame().queries)
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 2);
}
} // namespace one
