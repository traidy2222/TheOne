#include "backend.hpp"
#include "shadow_submission_state.hpp"
#include "asset_memory_snapshot.hpp"
namespace one {
void Renderer::Impl::destroyGeometryMeshes() {
    geometryMemory = {};
    *imageMemory = {};
    geometryState.references.clear();
    for (const auto& [owner, lifetime] : geometryState.lifetimes) lifetime->resident = false;
    geometryState.lifetimes.clear();
    geometryState.bufferUsers.clear();
    drawOrderCache={};
    boundsSnapshots.clear();
    *shadowSubmission = {};
    for(auto& context:frames)context.geometry.shadowRevision=0;
    for (auto& allocation : geometryState.allocations) {
        if (allocation.upload)
            allocation.upload->cancelled = true;
        if (allocation.buffer)
            vkDestroyBuffer(deviceState.device, allocation.buffer, nullptr);
        if (allocation.memory)
            vkFreeMemory(deviceState.device, allocation.memory, nullptr);
    }
    geometryState.allocations.clear();
    geometryState.meshes.clear();
    geometryState.owners.clear();
    geometryState.stats.residentGeometryBytes = 0;
    geometryState.stats.cachedMeshes = 0;
}

void Renderer::Impl::preparePalette(const std::vector<GeometryTransform>& transforms) {
    // The previous frame fence has completed before any cached allocation is replaced.
    VkDeviceSize bytes = transforms.size() * sizeof(one::GeometryTransform);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(deviceState.physical, &properties);
    if (bytes > properties.limits.maxStorageBufferRange)
        throw std::runtime_error("Geometry palette exceeds device storage buffer limit");
    if (bytes > frame().geometry.paletteCapacity) {
        if (frame().geometry.paletteMapped)
            vkUnmapMemory(deviceState.device, frame().geometry.paletteMemory);
        frame().geometry.paletteMapped = nullptr;
        if (frame().geometry.paletteBuffer)
            vkDestroyBuffer(deviceState.device, frame().geometry.paletteBuffer, nullptr);
        frame().geometry.paletteBuffer = VK_NULL_HANDLE;
        if (frame().geometry.paletteMemory)
            vkFreeMemory(deviceState.device, frame().geometry.paletteMemory, nullptr);
        frame().geometry.paletteMemory = VK_NULL_HANDLE;
        frame().geometry.paletteCapacity = std::min<VkDeviceSize>(std::max<VkDeviceSize>(4096, bytes * 2),
                                                                  properties.limits.maxStorageBufferRange);
        createBuffer(frame().geometry.paletteCapacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     frame().geometry.paletteBuffer, frame().geometry.paletteMemory);
        check(vkMapMemory(deviceState.device, frame().geometry.paletteMemory, 0,
                          frame().geometry.paletteCapacity, 0, &frame().geometry.paletteMapped),
              "map geometry palette");
        VkDescriptorBufferInfo info{frame().geometry.paletteBuffer, 0, frame().geometry.paletteCapacity};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame().geometry.set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(deviceState.device, 1, &write, 0, nullptr);
    }
    if (bytes)
        std::memcpy(frame().geometry.paletteMapped, transforms.data(), static_cast<size_t>(bytes));
    geometryState.stats.uploadedPaletteBytes = bytes;
}

void Renderer::Impl::prepareGeometry(const one::RenderScene& scene) {
    // Pack newly encountered primitives into one allocation; draw offsets preserve independent meshes.
    uint64_t packedBytes = 0;
    std::vector<vulkan::MeshGPU*> pending;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    auto release = [&] {
        if (staging)
            vkDestroyBuffer(deviceState.device, staging, nullptr);
        if (stagingMemory)
            vkFreeMemory(deviceState.device, stagingMemory, nullptr);
    };
    try {
        geometryState.references.resize(scene.draws.size());
        for (size_t i=0;i<scene.draws.size();++i) {
            const auto& draw=scene.draws[i];
            auto& reference=geometryState.references[i];
            if(!reference.matches(draw.geometry)){reference={draw.geometry};}
            if (!draw.geometry || !draw.owner || draw.palette >= scene.transforms.size())
                throw std::runtime_error("Invalid GPU geometry draw");
            const auto& primitive = *draw.geometry;
            uint32_t jointCount = scene.transforms[draw.palette].info.x;
            if (uint64_t(draw.palette) + jointCount >= scene.transforms.size())
                throw std::runtime_error("Incomplete joint palette");
            auto* resident=referenceDrawPreparation?nullptr:reference.mesh;
            if(!resident)if(auto found=geometryState.meshes.find(&primitive);found!=geometryState.meshes.end())resident=&found->second;
            if (!resident) {
                auto prepared = preparedMesh(draw);
                if (!prepared)
                    continue;
                VkDeviceSize required = prepared->vertices.size() * sizeof(SkinVertex);
                for (const auto& lod : prepared->levels)
                    required += lod.indices.size() * sizeof(uint32_t);
                if (!reserveUpload(required))
                    continue;
                geometryMemory.dirty = true;
                auto& mesh = geometryState.meshes[&primitive];
                mesh.owner = draw.owner;
                mesh.generation = ++geometryState.nextGeneration;
                auto& lifetime = geometryState.lifetimes[draw.owner.get()];
                if (!lifetime) lifetime = std::make_shared<vulkan::GeometryLifetime>();
                mesh.lifetime = lifetime;
                geometryState.owners.emplace(draw.owner.get(), draw.owner);
                mesh.prepared = prepared;
                mesh.bounds = prepared->bounds.rigid;
                mesh.centerSum = prepared->centerSum;
                mesh.unweightedSum = prepared->unweightedSum;
                mesh.jointSums = prepared->jointSums;
                mesh.maxJoint = prepared->maxJoint;
                ++uploadStats.meshes;
                mesh.indexCount = uint32_t(prepared->levels[0].indices.size());
                mesh.vertexOffset = packedBytes;
                mesh.indexOffset = mesh.vertexOffset + prepared->vertices.size() * sizeof(SkinVertex);
                mesh.bytes = required;
                if (required) {
                    VkDeviceSize offset = mesh.indexOffset;
                    for (const auto& lod : prepared->levels) {
                        mesh.levels.push_back({offset, uint32_t(lod.indices.size()), lod.error});
                        mesh.lodErrors.push_back(lod.error);
                        offset += lod.indices.size() * sizeof(uint32_t);
                    }
                    packedBytes += required;
                    pending.push_back(&mesh);
                }
                resident=&mesh;
            }
            // Stable references already own the right token. Avoid a random
            // MeshGPU metadata read for every resident draw on every frame.
            if (reference.mesh != resident) {
                reference.mesh = resident;
                reference.lifetime = resident->lifetime;
            }
            if (jointCount && !resident->jointSums.empty() && resident->maxJoint >= jointCount)
                throw std::runtime_error("Skin joint index out of range");
        }
        if (packedBytes) {
            geometryState.allocations.emplace_back();
            auto& allocation = geometryState.allocations.back();
            auto progress = std::make_shared<vulkan::UploadProgress>();
            allocation.upload = progress;
            allocation.bytes = packedBytes;
            {
                vulkan::ScopedTiming timing(uploadStats.geometryAllocationMs);
                createBuffer(packedBytes,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             allocation.buffer, allocation.memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(deviceState.device, allocation.buffer, &requirements);
            allocation.allocatedBytes = requirements.size;
            geometryState.bufferUsers.emplace(allocation.buffer, pending.size());
            std::vector<std::shared_ptr<const std::vector<uint8_t>>> payloads;
            payloads.reserve(pending.size());
            for (auto* mesh : pending) {
                payloads.push_back(mesh->prepared->packed);
                mesh->buffer = allocation.buffer;
                mesh->upload = progress;
            }
            queueBufferParts(allocation.buffer, std::move(payloads), progress);
            geometryState.stats.residentGeometryBytes += packedBytes;
        }
    } catch (...) {
        release();
        check(vkDeviceWaitIdle(deviceState.device), "wait failed upload cleanup");
        releaseUploads(frame());
        cancelUploads();
        destroyGeometryMeshes();
        throw;
    }
    release();
    geometryState.stats.cachedMeshes = geometryState.meshes.size();
}

glm::vec3 Renderer::Impl::geometryCenter(const one::Draw& draw, const one::RenderScene& scene) const {
    const auto& mesh = *resolvedMesh(draw);
    const auto& transform = scene.transforms[draw.palette];
    glm::vec4 sum = transform.position * (transform.info.x ? mesh.unweightedSum : mesh.centerSum);
    if (transform.info.x)
        for (size_t j = 0; j < mesh.jointSums.size(); ++j)
            sum += scene.transforms[draw.palette + 1 + j].position * mesh.jointSums[j];
    return glm::vec3(sum) / float(std::max(mesh.indexCount, 1u));
}
} // namespace one
