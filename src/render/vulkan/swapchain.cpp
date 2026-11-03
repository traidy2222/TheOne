#include "backend.hpp"
#include "presentation_pacing.hpp"
#include "presentation.hpp"
namespace one {
void Renderer::Impl::destroySwapchain(bool preserveRenderState) {
    presentationPacing.resetSwapchain();
    if (preserveRenderState)
        destroySurfaceTargets();
    else
        destroyRenderTargets();
    for (auto semaphore : swapchainState.presentReady)
        vkDestroySemaphore(deviceState.device, semaphore, nullptr);
    swapchainState.presentReady.clear();
    swapchainState.images.clear();
    if (swapchainState.handle)
        vkDestroySwapchainKHR(deviceState.device, swapchainState.handle, nullptr);
    swapchainState.handle = VK_NULL_HANDLE;
}

bool Renderer::Impl::createSwapchain() {
    static const bool referenceReuse = std::getenv("THEONE_REFERENCE_SURFACE_REUSE") != nullptr;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(deviceState.window, &width, &height);
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(deviceState.window) & SDL_WINDOW_MINIMIZED))
        return false;
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceState.physical, deviceState.surface, &caps),
          "query surface capabilities");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        throw std::runtime_error("Surface cannot be a color attachment");
    swapchainState.captureSupported = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(deviceState.physical, deviceState.surface, &count, nullptr),
          "query surface formats");
    if (!count)
        throw std::runtime_error("Surface has no formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(deviceState.physical, deviceState.surface, &count,
                                               formats.data()),
          "query surface formats");
    auto format = formats[0];
    if (count == 1 && format.format == VK_FORMAT_UNDEFINED)
        format.format = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = f;
            break;
        }
    auto nextExtent = caps.currentExtent;
    if (nextExtent.width == std::numeric_limits<uint32_t>::max()) {
        nextExtent.width =
            std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        nextExtent.height =
            std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!referenceReuse && swapchainState.handle && !swapchainState.resized && nextExtent.width == swapchainState.extent.width &&
        nextExtent.height == swapchainState.extent.height && format.format == swapchainState.colorFormat) {
        swapchainState.resizeRequested = false;
        return true;
    }
    auto previousImageCount = swapchainState.images.size();
    auto previousMinImages = swapchainState.minImages;
    bool preserveRenderState = !referenceReuse && targetsState.guiReady && format.format == swapchainState.colorFormat;
    swapchainState.rebuildMs = swapchainState.idleMs = swapchainState.targetsMs = 0;
    vulkan::ScopedTiming rebuildTiming(swapchainState.rebuildMs);
    {
        vulkan::ScopedTiming idleTiming(swapchainState.idleMs);
        check(vkDeviceWaitIdle(deviceState.device), "wait swapchain recreation");
    }
    destroySwapchain(preserveRenderState);
    swapchainState.extent = nextExtent;
    swapchainState.minImages = std::max(2u, caps.minImageCount);
    uint32_t imageCount = swapchainState.minImages + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                   VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (caps.supportedCompositeAlpha & a) {
            alpha = a;
            break;
        }
    VkSwapchainCreateInfoKHR s{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    s.surface = deviceState.surface;
    s.minImageCount = imageCount;
    s.imageFormat = format.format;
    s.imageColorSpace = format.colorSpace;
    s.imageExtent = swapchainState.extent;
    s.imageArrayLayers = 1;
    s.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   (swapchainState.captureSupported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    s.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    s.preTransform = caps.currentTransform;
    s.compositeAlpha = alpha;
    uint32_t modeCount = 0;
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(deviceState.physical, deviceState.surface, &modeCount,
                                                    nullptr),
          "query present modes");
    std::vector<VkPresentModeKHR> modes(modeCount);
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(deviceState.physical, deviceState.surface, &modeCount,
                                                    modes.data()),
          "read present modes");
    swapchainState.presentMode = vulkan::choosePresentMode(modes, swapchainState.vsync);
    s.presentMode = swapchainState.presentMode;
    s.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(deviceState.device, &s, nullptr, &swapchainState.handle),
          "vkCreateSwapchainKHR");
    check(vkGetSwapchainImagesKHR(deviceState.device, swapchainState.handle, &count, nullptr),
          "query swapchain images");
    swapchainState.images.resize(count);
    check(vkGetSwapchainImagesKHR(deviceState.device, swapchainState.handle, &count,
                                  swapchainState.images.data()),
          "query swapchain images");
    swapchainState.presentReady.resize(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& ready : swapchainState.presentReady)
        check(vkCreateSemaphore(deviceState.device, &sem, nullptr, &ready), "create present semaphore");
    if (preserveRenderState && (previousImageCount != count || previousMinImages != swapchainState.minImages))
        destroyRenderTargets();
    swapchainState.colorFormat = format.format;
    {
        vulkan::ScopedTiming targetsTiming(swapchainState.targetsMs);
        createRenderTargets(format.format);
    }
    ++swapchainState.generation;
    swapchainState.resized = false;
    swapchainState.resizeRequested = false;
    std::cout << "Swapchain: " << swapchainState.extent.width << "x" << swapchainState.extent.height << ", "
              << count << " images; present mode " << int(s.presentMode) << std::endl;
    return true;
}
} // namespace one
