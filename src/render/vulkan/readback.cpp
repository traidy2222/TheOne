#include "backend.hpp"
namespace one {
void Renderer::Impl::verifyCapture() {
    check(vkWaitForFences(deviceState.device, 1, &frame().fence, VK_TRUE, UINT64_MAX), "wait capture");
    void* mapped = nullptr;
    size_t pixels = static_cast<size_t>(swapchainState.extent.width) * swapchainState.extent.height;
    check(vkMapMemory(deviceState.device, captureState.memory, 0, pixels * 4, 0, &mapped), "map readback");
    auto* bytes = static_cast<unsigned char*>(mapped);
    size_t varied = 0;
    uint32_t start = swapchainState.extent.width / 3;
    for (uint32_t y = swapchainState.extent.height / 3; y < swapchainState.extent.height; ++y)
        for (uint32_t x = start; x < swapchainState.extent.width; ++x) {
            size_t i = (static_cast<size_t>(y) * swapchainState.extent.width + x) * 4;
            if (std::abs(int(bytes[i]) - int(bytes[(swapchainState.extent.width - 1) * 4])) > 24 ||
                std::abs(int(bytes[i + 1]) - int(bytes[(swapchainState.extent.width - 1) * 4 + 1])) > 24)
                ++varied;
        }
    std::ofstream out("capture.ppm", std::ios::binary);
    out << "P6\n" << swapchainState.extent.width << " " << swapchainState.extent.height << "\n255\n";
    bool bgra = swapchainState.colorFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                swapchainState.colorFormat == VK_FORMAT_B8G8R8A8_SRGB;
    for (size_t i = 0; i < pixels; ++i) {
        char rgb[]{static_cast<char>(bytes[i * 4 + (bgra ? 2 : 0)]), static_cast<char>(bytes[i * 4 + 1]),
                   static_cast<char>(bytes[i * 4 + (bgra ? 0 : 2)])};
        out.write(rgb, 3);
    }
    vkUnmapMemory(deviceState.device, captureState.memory);
    if (!out)
        throw std::runtime_error("Capture write failed");
    if (varied < pixels / 10)
        throw std::runtime_error("GPU readback scene coverage below 10%");
    std::cout << "GPU readback: " << varied << " scene pixels; capture.ppm saved" << std::endl;
}
} // namespace one
