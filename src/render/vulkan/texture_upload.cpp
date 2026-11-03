#include "backend.hpp"
namespace one {
namespace {
void transition(VkCommandBuffer command, VkImage image, uint32_t level, uint32_t count, VkImageLayout from,
                VkImageLayout to, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags source,
                VkPipelineStageFlags target) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, count, 0, 1};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    vkCmdPipelineBarrier(command, source, target, 0, 0, nullptr, 0, nullptr, 1, &b);
}
} // namespace
void Renderer::Impl::flushTextureUploads(bool) {
    if (materialsState.pendingUploads.empty())
        return;

    for (auto upload : materialsState.pendingUploads) {
        auto data = upload.data;
        uint64_t work = 0;
        for (const auto& copy : upload.copies)
            work += uint64_t(copy.imageExtent.width) * copy.imageExtent.height * 4;
        if (upload.blit)
            for (uint32_t level = 1; level < upload.levels; ++level)
                work +=
                    uint64_t(std::max(1u, upload.width >> level)) * std::max(1u, upload.height >> level) * 4;
        uploads.pending.push_back(
            {upload.progress,
             work,
             [this, upload, data, copyIndex = size_t(0), pixel = uint64_t(0), level = 1u, initialized = false,
              mipStarted = false](uint64_t allowance) mutable {

                 bool copying = copyIndex < upload.copies.size();
                 uint32_t width = copying ? upload.copies[copyIndex].imageExtent.width
                                          : std::max(1u, upload.width >> level);
                 uint32_t height = copying ? upload.copies[copyIndex].imageExtent.height
                                           : std::max(1u, upload.height >> level);
                 uint32_t x = uint32_t(pixel % width), y = uint32_t(pixel / width);
                 uint32_t columns = uint32_t(std::min<uint64_t>(width - x, allowance / 4));
                 uint32_t rows = x == 0 && columns == width
                                     ? uint32_t(std::min<uint64_t>(height - y, allowance / (width * 4ull)))
                                     : 1;
                 uint64_t bytes = uint64_t(columns) * rows * 4;
                 bool last = pixel + uint64_t(columns) * rows == uint64_t(width) * height;
                 auto staging=acquireStaging(copying?bytes:0);
                 if(copying) {
                         auto* mapped=staging.mapped;
                         std::memcpy(mapped, data->data() + upload.copies[copyIndex].bufferOffset + pixel * 4,
                                     size_t(bytes));
                     }
                     stageUpload(staging, [&](VkCommandBuffer command) {
                         if (!initialized)
                             transition(command, upload.image, 0, upload.levels, VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                         if (copying) {
                             VkBufferImageCopy copy{};
                             copy.imageSubresource = upload.copies[copyIndex].imageSubresource;
                             copy.imageOffset = {int32_t(x), int32_t(y), 0};
                             copy.imageExtent = {columns, rows, 1};
                             vkCmdCopyBufferToImage(command, staging.buffer, upload.image,
                                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                             if (last && !upload.blit)
                                 transition(command, upload.image, copy.imageSubresource.mipLevel, 1,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                             if (last && upload.blit && upload.levels == 1)
                                 transition(command, upload.image, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                         } else {
                             if (!mipStarted)
                                 transition(command, upload.image, level - 1, 1,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                             uint32_t sx = std::max(1u, upload.width >> (level - 1)) / width,
                                      sy = std::max(1u, upload.height >> (level - 1)) / height;
                             VkImageBlit blit{};
                             blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
                             blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                             blit.srcOffsets[0] = {int32_t(x * sx), int32_t(y * sy), 0};
                             blit.srcOffsets[1] = {int32_t((x + columns) * sx), int32_t((y + rows) * sy), 1};
                             blit.dstOffsets[0] = {int32_t(x), int32_t(y), 0};
                             blit.dstOffsets[1] = {int32_t(x + columns), int32_t(y + rows), 1};
                             vkCmdBlitImage(command, upload.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            upload.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                                            VK_FILTER_LINEAR);
                             if (last) {
                                 transition(command, upload.image, level - 1, 1,
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                                 if (level + 1 == upload.levels)
                                     transition(command, upload.image, level, 1,
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                             }
                         }
                     });
                 initialized = true;
                 pixel += uint64_t(columns) * rows;
                 if (copying) {
                     uploadStats.textureBytes += bytes;
                     if (last) {
                         ++copyIndex;
                         pixel = 0;
                     }
                 } else {
                     mipStarted = true;
                     if (last) {
                         ++level;
                         pixel = 0;
                         mipStarted = false;
                     }
                 }
                 upload.progress->ready =
                     copyIndex == upload.copies.size() && (!upload.blit || level >= upload.levels);
                 return bytes;
             },
             {data}});
    }
    materialsState.pendingUploads.clear();
    materialsState.uploadBytes.clear();
}
} // namespace one
