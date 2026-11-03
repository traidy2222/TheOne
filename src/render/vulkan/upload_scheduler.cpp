#include "backend.hpp"
namespace one {
void Renderer::Impl::cancelUploads() {
    for (auto& task : uploads.pending)
        task.progress->cancelled = true;
    uploads.pending.clear();
}
void Renderer::Impl::queueBufferUpload(VkBuffer destination, std::shared_ptr<const std::vector<uint8_t>> data,
                                       std::shared_ptr<vulkan::UploadProgress> progress,
                                       uint64_t destinationOffset) {
    queueBufferParts(destination, {std::move(data)}, std::move(progress), destinationOffset);
}
void Renderer::Impl::queueBufferParts(VkBuffer destination,
                                      std::vector<std::shared_ptr<const std::vector<uint8_t>>> data,
                                      std::shared_ptr<vulkan::UploadProgress> progress,
                                      uint64_t destinationOffset) {
    uint64_t total = 0;
    for (const auto& part : data)
        total += part->size();
    if (!total) {
        progress->ready = true;
        return;
    }
    uploads.pending.push_back(
        {progress, total,
         [this, destination, data, progress, total, destinationOffset, part = size_t(0), inPart = size_t(0),
          offset = uint64_t(0)](uint64_t allowance) mutable {
             auto count = std::min<uint64_t>(allowance, total - offset);
             auto staging=acquireStaging(count);
             auto* mapped=staging.mapped;
                 size_t copied = 0;
                 while (copied < count) {
                     auto n = std::min<size_t>(count - copied, data[part]->size() - inPart);
                     std::memcpy(static_cast<uint8_t*>(mapped) + copied, data[part]->data() + inPart, n);
                     copied += n;
                     inPart += n;
                     if (inPart == data[part]->size()) {
                         ++part;
                         inPart = 0;
                     }
                 }
                 stageUpload(staging, [&](VkCommandBuffer command) {
                     VkBufferCopy copy{0, destinationOffset + offset, count};
                     vkCmdCopyBuffer(command, staging.buffer, destination, 1, &copy);
                     VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                     barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                     barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
                     vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                          VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0,
                                          nullptr);
                 });
             offset += count;
             progress->ready = offset == total;
             geometryState.stats.uploadedGeometryBytes += count;
             return count;
         },
         data});
}
void Renderer::Impl::pumpUploads() {
    auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    uint64_t limit =
        std::max<uint64_t>(4, std::min(uploadBudget, uint64_t(uploads.bytesPerMs * uploads.budgetMs))) &
        ~uint64_t(3);
    uint64_t spent = 0;
    bool timestamps = false;
    while (!uploads.pending.empty() && spent < limit && (spent == 0 || elapsed() < uploads.budgetMs)) {
        auto& task = uploads.pending.front();
        if (task.progress->cancelled) {
            uploads.pending.pop_front();
            continue;
        }
        if (!timestamps && frame().queries) {
            stageUpload(acquireStaging(0), [&](VkCommandBuffer command) {
                vkCmdResetQueryPool(command, frame().queries, 4, 2);
                vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame().queries, 4);
            });
            timestamps = true;
        }
        uint64_t count = task.step(std::min(uploads.chunkBytes, limit - spent));
        spent += count;
        task.remaining -= std::min(task.remaining, count);
        if (!task.remaining)
            uploads.pending.pop_front();
        if (!count)
            break;
    }
    if (timestamps) {
        stageUpload(acquireStaging(0), [&](VkCommandBuffer command) {
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame().queries, 5);
        });
    }
    frame().uploadQueryWritten = timestamps;
    frame().uploadBytes = spent;
    stats.transferBytes = spent;
    stats.uploadCpuMs = elapsed();
    for (auto& task : uploads.pending)
        if (!task.progress->cancelled)
            stats.pendingUploadBytes += task.remaining;
}
} // namespace one
