#include "backend.hpp"
namespace one {
Renderer::Impl::Texture Renderer::Impl::createTexture(const Image* image, bool srgb, uint32_t firstMip) {
    const auto& source = image ? *image : preparation.white;
    auto data = preparedImage(image);
    if (!data)
        throw std::runtime_error("Texture preparation not ready");
    firstMip = std::min(firstMip, uint32_t(data->mips.size() - 1));
    if (auto shared = findTextureAlias(data, srgb, firstMip)) {
        shared->revision = source.revision;
        return *shared;
    }
    Texture t;
    t.srgb = srgb;
    t.prepared = data;
    t.firstMip = firstMip;
    t.revision = source.revision;
    t.hash = data->hash;
    t.width = source.width;
    t.height = source.height;
    t.levels = uint32_t(data->mips.size()) - firstMip;
    t.upload = std::make_shared<vulkan::UploadProgress>();
    auto size = data->mips[firstMip];
    auto format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    try {
        createImage(size.width, size.height, format,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.image, t.memory,
                    t.levels);
        t.view = imageView(t.image, format, VK_IMAGE_ASPECT_COLOR_BIT, t.levels);
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(deviceState.device, t.image, &requirements);
        t.bytes = requirements.size;
        vulkan::TextureUpload upload;
        upload.image = t.image;
        upload.width = size.width;
        upload.height = size.height;
        upload.levels = t.levels;
        upload.blit = false;
        upload.progress = t.upload;
        upload.data = srgb ? data->color : data->data;
        for (uint32_t level = firstMip; level < data->mips.size(); ++level) {
            auto mip = data->mips[level];
            VkBufferImageCopy copy{};
            copy.bufferOffset = mip.offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - firstMip, 0, 1};
            copy.imageExtent = {mip.width, mip.height, 1};
            upload.copies.push_back(copy);
        }
        materialsState.pendingUploads.push_back(std::move(upload));
    } catch (...) {
        if (t.view)
            vkDestroyImageView(deviceState.device, t.view, nullptr);
        if (t.image)
            vkDestroyImage(deviceState.device, t.image, nullptr);
        if (t.memory)
            vkFreeMemory(deviceState.device, t.memory, nullptr);
        throw;
    }
    ++uploadStats.textures;
    uploadStats.mipLevels += t.levels;
    return t;
}
const Renderer::Impl::Texture& Renderer::Impl::texture(const Image* image, bool srgb) {
    vulkan::TextureKey key{image, srgb};
    auto found = materialsState.textures.find(key);
    if (found != materialsState.textures.end())
        return found->second;
    uint32_t first = 0;
    if (textureStreaming && image)
        for (int size = std::max(image->width, image->height); size > 64; size >>= 1)
            ++first;
    auto& result = materialsState.textures.emplace(key, createTexture(image, srgb, first)).first->second;
    ++materialsState.memoryRevision;
    rememberTextureAlias(key, result);
    return result;
}
} // namespace one
