#include "backend.hpp"
namespace one {
void Renderer::Impl::recordShadows(const std::vector<Draw>& draws, bool shadows, const RenderScene* scene) {
    if (shadows && shadowBatching)
        prepareShadowSubmission(draws, scene);
    auto bindsBefore = stats.geometryBinds;
    size_t cullSamples = 0, issueSamples = 0;
    for (uint32_t c = 0; c < shadowCascadeCount; ++c) {
        VkClearValue depth{};
        depth.depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = sceneState.shadowPass;
        begin.framebuffer = frame().scene.shadowFramebuffers[c];
        begin.renderArea = {{0, 0}, {shadowResolution, shadowResolution}};
        begin.clearValueCount = 1;
        begin.pClearValues = &depth;
        vkCmdBeginRenderPass(frame().command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, float(shadowResolution), float(shadowResolution), 0, 1};
        VkRect2D scissor{{0, 0}, {shadowResolution, shadowResolution}};
        vkCmdSetViewport(frame().command, 0, 1, &viewport);
        vkCmdSetScissor(frame().command, 0, 1, &scissor);
        const auto& matrix = sceneState.cascades.matrices[c];
        Frame constants{matrix, glm::vec4(0), glm::vec4(1), glm::vec4(0), glm::vec4(0)};
        vkCmdPushConstants(frame().command, sceneState.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Frame),
                           &constants);
        VkPipeline active = VK_NULL_HANDLE;
        if (shadows && shadowBatching)
            submitShadowCascade(c, draws);
        if (shadows && !shadowBatching)
            for (const auto& draw : (instanceBatchesReady ? shadowBatches[c] : draws))
                if (draw.count && (!draw.material || !draw.material->blend)) {
                    ++stats.shadowCandidates;
                    bool culled;
                    if ((cullSamples++ % 64) == 0) {
                        vulkan::ScopedTiming timing(stats.shadowCullSampleMs);
                        culled = !instanceBatchesReady && !(draw.shadowVisibility & (1u << c));
                    } else
                        culled = !instanceBatchesReady && !(draw.shadowVisibility & (1u << c));
                    if (culled) {
                        ++stats.shadowCulled;
                        continue;
                    }
                    if (draw.material && draw.material->mask) {
                        ++stats.shadowDraws;
                        stats.shadowInstances += draw.instances;
                        stats.shadowTriangles += draw.count / 3 * draw.instances;
                        submitMaskedShadow(draw,c);
                        active=VK_NULL_HANDLE;
                        continue;
                    }
                    auto pipeline = draw.geometry ? sceneState.meshShadowPipeline : sceneState.shadowPipeline;
                    if (pipeline != active) {
                        vkCmdBindPipeline(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                        active = pipeline;
                    }
                    ++stats.shadowDraws;
                    stats.shadowInstances += draw.instances;
                    auto shadowDraw = draw;
                    shadowDraw.lod =
                        draw.geometry ? shadowMeshLod(draw, scene, c, geometryState.meshes.at(draw.geometry))
                                      : 0;
                    auto indices =
                        draw.geometry ? geometryState.meshes.at(draw.geometry).levels.at(shadowDraw.lod).count
                                      : draw.count;
                    stats.shadowTriangles += indices / 3 * draw.instances;
                    if (shadowDraw.lod) {
                        ++stats.shadowLodDraws;
                        stats.shadowTrianglesSaved += (draw.count - indices) / 3 * draw.instances;
                    }
                    ++stats.shadowCommands;
                    if ((issueSamples++ % 64) == 0) {
                        vulkan::ScopedTiming timing(stats.shadowIssueSampleMs);
                        issueGeometryDraw(shadowDraw, true);
                    } else
                        issueGeometryDraw(shadowDraw, true);
                }
        vkCmdEndRenderPass(frame().command);
    }
    stats.shadowGeometryBinds += stats.geometryBinds - bindsBefore;
    stats.shadowCullSampleMs *= 64;
    stats.shadowIssueSampleMs *= 64;
}
} // namespace one
