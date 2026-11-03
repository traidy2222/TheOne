#include "backend.hpp"
#include "shadow_submission_state.hpp"
#include "shadow_plan_key.hpp"
#include "indexed_binding.hpp"
namespace one {
void Renderer::Impl::prepareShadowSubmission(const std::vector<Draw>& draws, const RenderScene* scene) {
    vulkan::ScopedTiming timing(stats.shadowPlanMs);
    VkDeviceSize total = 0;
    bool changed=referenceSubmissionCache;
    const bool referenceBindings = shadowSubmission->referenceBindings &&
        (!shadowSubmission->alternateBindings || (bool(stats.frame & 1) != shadowSubmission->evenBindings));
    stats.shadowBindingReference = referenceBindings;
    for (uint32_t c = 0; c < shadowCascadeCount; ++c) {
        auto& output = shadowSubmission->cascades[c];
        const auto& source = instanceBatchesReady ? shadowBatches[c] : draws;
        auto before = vulkan::shadowPlanCounts(stats);
        if (referenceSubmissionCache) output.planValid = false;
        bool reuse = false;
        {
            vulkan::ScopedTiming checkTiming(stats.shadowPlanCheckMs);
            bool reference = shadowSubmission->referencePlan && (!shadowSubmission->alternatePlan || (stats.frame & 1));
            stats.shadowPlanReference = reference;
            if (reference) {
                output.planValid = output.recordPlanKeys = false;
                output.comparedKeys = 0;
            } else reuse = vulkan::reuseShadowPlan(output, source, scene, c, instanceBatchesReady,
                                                  shadowLod && lodEnabled, shadowTexelsPerUnit[c]);
        }
        stats.shadowKeysCompared += output.comparedKeys;
        if (reuse && !referenceSubmissionCache) {
            vulkan::applyShadowPlanCounts(output, stats);
        } else {
            vulkan::ScopedTiming rebuildTiming(stats.shadowPlanRebuildMs);
            ++stats.shadowPlansRebuilt;
            if (output.recordPlanKeys) stats.shadowKeysRecorded += source.size();
            for (auto& group : output.groups)
                group.count=0;
            output.procedural.clear();
            output.masked.clear();
            output.templates.resize(source.size());
            for (size_t i = 0; i < source.size(); ++i) {
                const auto& draw = source[i];
                if (output.recordPlanKeys)
                    output.planKeys[i] = vulkan::shadowPlanKey(draw, scene, c, instanceBatchesReady,
                                                              shadowLod && lodEnabled);
                if (!draw.count || (draw.material && draw.material->blend))
                    continue;
                ++stats.shadowCandidates;
                if (!instanceBatchesReady && !(draw.shadowVisibility & (1u << c))) {
                    ++stats.shadowCulled;
                    continue;
                }
                ++stats.shadowDraws;
                stats.shadowInstances += draw.instances;
                if (draw.material && draw.material->mask) {
                    output.masked.push_back(uint32_t(i));
                    stats.shadowTriangles += draw.count / 3 * draw.instances;
                    continue;
                }
                if (!draw.geometry) {
                    output.procedural.push_back(uint32_t(i));
                    stats.shadowTriangles += draw.count / 3;
                    continue;
                }
                auto& cached = output.templates[i];
                if (cached.geometry != draw.geometry || !vulkan::resident(cached.lifetime)) {
                    ++stats.shadowTemplateRebinds;
                    cached.geometry = draw.geometry;
                    cached.density = -1;
                    auto [geometry, added] = output.geometryTemplates.try_emplace(draw.geometry);
                    auto& shared = geometry->second;
                    if (added) {
                        shared.mesh = &geometryState.meshes.at(draw.geometry);
                        shared.owner = shared.mesh->owner.get();
                        for (const auto& lod : shared.mesh->levels) {
                            auto binding = vulkan::indexedBinding(shared.mesh->vertexOffset, lod.offset, sizeof(SkinVertex));
                            vulkan::ShadowBinding key{shared.mesh->buffer, binding.vertex, binding.index};
                            auto [it, inserted] = output.lookup.emplace(key, output.groups.size());
                            if (inserted) {
                                auto vacant = std::find_if(output.groups.begin(), output.groups.end(),
                                    [](const auto& group) { return !group.binding.buffer; });
                                if (vacant == output.groups.end()) output.groups.push_back({key});
                                else { it->second = size_t(vacant - output.groups.begin()); vacant->binding = key; }
                            }
                            shared.levels.push_back({it->second, {lod.count, 1, binding.firstIndex, binding.baseVertex, 0}});
                        }
                    }
                    cached.binding = &shared;
                    if (cached.lifetime != shared.mesh->lifetime) cached.lifetime = shared.mesh->lifetime;
                }
                const auto& levels = cached.binding->levels;
                if (referenceBindings ? !cached.binding->mesh->indexCount :
                    (levels.empty() || !levels.front().command.indexCount))
                    continue;
                uint32_t level = 0;
                if (shadowLod && lodEnabled && scene && draw.instances == 1 && levels.size() > 1 &&
                    !scene->transforms.at(draw.palette).info.x) {
                    glm::mat3 linear(scene->transforms.at(draw.palette).position);
                    if (cached.entity != draw.entity || cached.linear != linear ||
                        cached.density != shadowTexelsPerUnit[c]) {
                        ++stats.shadowLodSelections;
                        cached.selectedLevel = shadowMeshLod(draw, scene, c, *cached.binding->mesh);
                        cached.entity = draw.entity;
                        cached.linear = linear;
                        cached.density = shadowTexelsPerUnit[c];
                    }
                    level = cached.selectedLevel;
                }
                const auto& entry = levels.at(level);
                // The command template already contains immutable LOD counts.
                // Avoid visiting the separate MeshGPU/LOD vectors for every draw.
                auto count = referenceBindings ? cached.binding->mesh->levels.at(level).count : entry.command.indexCount;
                stats.shadowTriangles += count / 3 * draw.instances;
                if (level) {
                    ++stats.shadowLodDraws;
                    auto full = referenceBindings ? cached.binding->mesh->indexCount : levels.front().command.indexCount;
                    stats.shadowTrianglesSaved += (full - count) / 3 * draw.instances;
                }
                auto command = entry.command;
                command.instanceCount = draw.instances;
                command.firstInstance = draw.palette;
                auto& group=output.groups[entry.group];
                if(group.count==group.commands.size()){group.commands.push_back(command);changed=true;++stats.shadowCommandUpdates;}
                else if(referenceSubmissionCache || std::memcmp(&group.commands[group.count],&command,sizeof(command))) {
                    group.commands[group.count]=command;changed=true;++stats.shadowCommandUpdates;
                } else ++stats.shadowCommandReuses;
                ++group.count;
            }
            vulkan::retainShadowPlanCounts(output, before, stats);
        }
        for (auto& group : output.groups) {
            if(group.commands.size()!=group.count){group.commands.resize(group.count);changed=true;}
            if(group.offset!=total)changed=true;
            group.offset = total;
            total += group.commands.size() * sizeof(VkDrawIndexedIndirectCommand);
        }
    }
    if(changed)++shadowSubmission->revision;
    if (!(deviceState.shadowIndirect && shadowIndirect) || !total)
        return;
    auto& gpu = frame().geometry;
    if (total > gpu.shadowCapacity) {
        if (gpu.shadowMapped)
            vkUnmapMemory(deviceState.device, gpu.shadowMemory);
        if (gpu.shadowBuffer)
            vkDestroyBuffer(deviceState.device, gpu.shadowBuffer, nullptr);
        if (gpu.shadowMemory)
            vkFreeMemory(deviceState.device, gpu.shadowMemory, nullptr);
        gpu.shadowRevision=0;
        gpu.shadowBuffer = VK_NULL_HANDLE;
        gpu.shadowMemory = VK_NULL_HANDLE;
        gpu.shadowMapped = nullptr;
        gpu.shadowCapacity = std::max<VkDeviceSize>(4096, total * 2);
        createBuffer(gpu.shadowCapacity, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, gpu.shadowBuffer,
                     gpu.shadowMemory);
        check(vkMapMemory(deviceState.device, gpu.shadowMemory, 0, gpu.shadowCapacity, 0, &gpu.shadowMapped),
              "map shadow commands");
    }
    if(!referenceSubmissionCache && gpu.shadowRevision==shadowSubmission->revision)return;
    for (const auto& cascade : shadowSubmission->cascades)
        for (const auto& group : cascade.groups)
            if (!group.commands.empty())
                std::memcpy(static_cast<uint8_t*>(gpu.shadowMapped) + group.offset, group.commands.data(),
                            group.commands.size() * sizeof(VkDrawIndexedIndirectCommand));
    gpu.shadowRevision=shadowSubmission->revision;
}
void Renderer::Impl::submitShadowCascade(uint32_t cascade, const std::vector<Draw>& draws) {
    const auto& submission = shadowSubmission->cascades[cascade];
    const auto& source = instanceBatchesReady ? shadowBatches[cascade] : draws;
    bool pipelineBound = false;
    for (const auto& group : submission.groups) {
        if (group.commands.empty())
            continue;
        if (!pipelineBound) {
            vkCmdBindPipeline(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              sceneState.meshShadowPipeline);
            pipelineBound = true;
        }
        vkCmdBindVertexBuffers(frame().command, 0, 1, &group.binding.buffer, &group.binding.vertex);
        vkCmdBindIndexBuffer(frame().command, group.binding.buffer, group.binding.index,
                             VK_INDEX_TYPE_UINT32);
        ++stats.shadowGeometryBinds;
        if (deviceState.shadowIndirect && shadowIndirect) {
            for (size_t first = 0; first < group.commands.size();) {
                auto count =
                    uint32_t(std::min<size_t>(deviceState.maxIndirectDraws, group.commands.size() - first));
                vkCmdDrawIndexedIndirect(frame().command, frame().geometry.shadowBuffer,
                                         group.offset + first * sizeof(VkDrawIndexedIndirectCommand), count,
                                         sizeof(VkDrawIndexedIndirectCommand));
                ++stats.shadowCommands;
                first += count;
            }
        } else
            for (const auto& command : group.commands) {
                vkCmdDrawIndexed(frame().command, command.indexCount, command.instanceCount,
                                 command.firstIndex, command.vertexOffset, command.firstInstance);
                ++stats.shadowCommands;
            }
    }
    // Group binding bypasses issueGeometryDraw's cache. Reset it before procedural or main work.
    geometryState.boundBuffer = VK_NULL_HANDLE;
    if (!submission.procedural.empty()) {
        vkCmdBindPipeline(frame().command, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneState.shadowPipeline);
        for (auto index : submission.procedural) {
            issueGeometryDraw(source[index]);
            ++stats.shadowCommands;
        }
    }
    vulkan::ShadowMaskRecord maskState;
    for (auto index : submission.masked)
        submitMaskedShadow(source[index], cascade, &maskState);
}
void Renderer::Impl::destroyShadowCommands() {
    for (auto& context : frames) {
        auto& gpu = context.geometry;
        if (gpu.shadowMapped)
            vkUnmapMemory(deviceState.device, gpu.shadowMemory);
        if (gpu.shadowBuffer)
            vkDestroyBuffer(deviceState.device, gpu.shadowBuffer, nullptr);
        if (gpu.shadowMemory)
            vkFreeMemory(deviceState.device, gpu.shadowMemory, nullptr);
        gpu.shadowMapped = nullptr;
        gpu.shadowBuffer = VK_NULL_HANDLE;
        gpu.shadowMemory = VK_NULL_HANDLE;
        gpu.shadowCapacity = 0;
        gpu.shadowRevision=0;
    }
}
} // namespace one
