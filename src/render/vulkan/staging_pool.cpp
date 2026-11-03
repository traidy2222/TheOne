#include "backend.hpp"
#include <bit>
namespace one {
vulkan::StagedUpload Renderer::Impl::acquireStaging(uint64_t bytes) {
    auto& pool = stagingPool;
    auto best = pool.available.end();
    if (!referenceStagingPool)
        for (auto it = pool.available.begin(); it != pool.available.end(); ++it)
            if ((bytes == 0) == (it->buffer == VK_NULL_HANDLE) && it->capacity >= bytes &&
                (best == pool.available.end() || it->bytes < best->bytes))
                best = it;
    vulkan::StagedUpload upload;
    if (best != pool.available.end()) {
        upload = *best;
        pool.bytes -= upload.bytes;
        pool.available.erase(best);
        ++stats.stagingReuses;
    } else {
        try {
            if (bytes) {
                upload.capacity =
                    referenceStagingPool ? bytes : std::bit_ceil(std::max<uint64_t>(bytes, 4096));
                createBuffer(upload.capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, upload.buffer, upload.memory);
                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(deviceState.device, upload.buffer, &requirements);
                upload.bytes = requirements.size;
                check(vkMapMemory(deviceState.device, upload.memory, 0, VK_WHOLE_SIZE, 0, &upload.mapped),
                      "map staging pool");
            }
            VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            info.commandPool = deviceState.pool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(deviceState.device, &info, &upload.command),
                  "allocate staging command");
            ++stats.stagingAllocations;
        } catch (...) {
            destroyStaging(upload);
            throw;
        }
    }
    upload.lastUsed = stats.frame;
    return upload;
}
void Renderer::Impl::destroyStaging(vulkan::StagedUpload upload) {
    if (upload.mapped)
        vkUnmapMemory(deviceState.device, upload.memory);
    if (upload.buffer)
        vkDestroyBuffer(deviceState.device, upload.buffer, nullptr);
    if (upload.memory)
        vkFreeMemory(deviceState.device, upload.memory, nullptr);
    if (upload.command)
        vkFreeCommandBuffers(deviceState.device, deviceState.pool, 1, &upload.command);
}
void Renderer::Impl::trimStagingPool(bool drain) {
    std::erase_if(stagingPool.available, [&](const auto& upload) {
        if (!drain && stats.frame - upload.lastUsed <= 120)
            return false;
        stagingPool.bytes -= upload.bytes;
        if (drain)
            destroyStaging(upload);
        else
            retire([this, upload] { destroyStaging(upload); }, upload.bytes);
        return true;
    });
}
void Renderer::Impl::releaseUploads(vulkan::FrameContext& context) {
    for (auto upload : context.uploads) {
        if (referenceStagingPool)
            destroyStaging(upload);
        else if (stagingPool.available.size() < vulkan::StagingPool::maxEntries &&
                 upload.bytes <= vulkan::StagingPool::maxBytes - stagingPool.bytes) {
            stagingPool.available.push_back(upload);
            stagingPool.bytes += upload.bytes;
        } else
            retire([this, upload] { destroyStaging(upload); }, upload.bytes);
    }
    context.uploads.clear();
    trimStagingPool(false);
}
} // namespace one
