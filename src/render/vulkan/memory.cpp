#include "backend.hpp"
namespace one {
uint32_t Renderer::Impl::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(deviceState.physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No compatible GPU memory type");
}

void Renderer::Impl::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                                  VkDeviceMemory& memory, VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    b.size = size;
    b.usage = usage;
    b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(deviceState.device, &b, nullptr, &buffer), "create buffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(deviceState.device, buffer, &req);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = req.size;
    a.memoryTypeIndex = memoryType(req.memoryTypeBits, properties);
    check(vkAllocateMemory(deviceState.device, &a, nullptr, &memory), "allocate buffer");
    check(vkBindBufferMemory(deviceState.device, buffer, memory, 0), "bind buffer");
}

VkImageView Renderer::Impl::imageView(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                                      uint32_t levels, uint32_t layers, uint32_t baseLayer) {
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = image;
    v.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    v.format = format;
    v.subresourceRange = {aspect, 0, levels, baseLayer, layers};
    VkImageView result = VK_NULL_HANDLE;
    check(vkCreateImageView(deviceState.device, &v, nullptr, &result), "create image view");
    return result;
}

void Renderer::Impl::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                                 VkImage& image, VkDeviceMemory& memory, uint32_t levels, uint32_t layers) {
    VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    i.imageType = VK_IMAGE_TYPE_2D;
    i.format = format;
    if (format == VK_FORMAT_R8G8B8A8_UNORM)
        i.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    i.extent = {width, height, 1};
    i.mipLevels = levels;
    i.arrayLayers = layers;
    i.samples = VK_SAMPLE_COUNT_1_BIT;
    i.tiling = VK_IMAGE_TILING_OPTIMAL;
    i.usage = usage;
    i.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(deviceState.device, &i, nullptr, &image), "create image");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(deviceState.device, image, &req);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = req.size;
    a.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(deviceState.device, &a, nullptr, &memory), "allocate image");
    check(vkBindImageMemory(deviceState.device, image, memory, 0), "bind image");
}

VkShaderModule Renderer::Impl::shader(const char* name) {
    auto file = std::filesystem::path(SDL_GetBasePath()) / "shaders" / name;
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open shader: " + file.string());
    size_t size = static_cast<size_t>(input.tellg());
    if (!size || size % 4)
        throw std::runtime_error("Invalid SPIR-V");
    std::vector<uint32_t> code(size / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
    if (!input)
        throw std::runtime_error("Cannot read shader");
    VkShaderModuleCreateInfo s{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    s.codeSize = size;
    s.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(deviceState.device, &s, nullptr, &module), "create shader");
    return module;
}

} // namespace one
