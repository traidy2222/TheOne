#include "backend.hpp"
#include "presentation_pacing.hpp"
namespace one {
VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::Impl::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                             VkDebugUtilsMessageTypeFlagsEXT,
                                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                             void* user) {
    auto* self = static_cast<Impl*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++self->deviceState.validationErrors;
    std::cerr << "Vulkan validation: " << data->pMessage << std::endl;
    return VK_FALSE;
}

void Renderer::Impl::initVulkan() {
    frameTrace = {};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "The One";
    app.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
    app.pEngineName = "The One";
    app.engineVersion = app.applicationVersion;
    app.apiVersion = VK_API_VERSION_1_1;
    uint32_t extensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!sdlExtensions)
        throw std::runtime_error(SDL_GetError());
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + extensionCount);
    if (deviceState.validation)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    const char* layer = "VK_LAYER_KHRONOS_validation";
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (deviceState.validation) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &layer;
    }
    check(vkCreateInstance(&info, nullptr, &deviceState.instance), "vkCreateInstance");
    if (deviceState.validation) {
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = debugCallback;
        debug.pUserData = this;
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(deviceState.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create)
            throw std::runtime_error("Debug utils unavailable");
        check(create(deviceState.instance, &debug, nullptr, &deviceState.debug),
              "create validation messenger");
    }
    if (!SDL_Vulkan_CreateSurface(deviceState.window, deviceState.instance, nullptr, &deviceState.surface))
        throw std::runtime_error(SDL_GetError());
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(deviceState.instance, &count, nullptr), "enumerate GPUs");
    if (!count)
        throw std::runtime_error("No Vulkan GPU found");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(deviceState.instance, &count, devices.data()), "enumerate GPUs");
    for (auto candidate : devices) {
        uint32_t extCount = 0;
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr),
              "enumerate device extensions");
        std::vector<VkExtensionProperties> exts(extCount);
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, exts.data()),
              "enumerate device extensions");
        if (std::none_of(exts.begin(), exts.end(), [](const auto& e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            }))
            continue;
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
        for (uint32_t i = 0; i < queueCount; ++i) {
            VkBool32 present = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, deviceState.surface, &present),
                  "query surface support");
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                deviceState.physical = candidate;
                deviceState.family = i;
                deviceState.timestampBits = queues[i].timestampValidBits;
                break;
            }
        }
        if (deviceState.physical)
            break;
    }
    if (!deviceState.physical)
        throw std::runtime_error("No GPU with a graphics/present queue and swapchain support found");
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(deviceState.physical, &props);
    deviceState.timestampPeriod = props.limits.timestampPeriod;
    std::cout << "GPU: " << props.deviceName << std::endl;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = deviceState.family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    std::vector<const char*> deviceExt{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t availableCount = 0;
    check(vkEnumerateDeviceExtensionProperties(deviceState.physical, nullptr, &availableCount, nullptr),
          "query device extensions");
    std::vector<VkExtensionProperties> available(availableCount);
    check(vkEnumerateDeviceExtensionProperties(deviceState.physical, nullptr, &availableCount,
                                               available.data()),
          "read device extensions");
    presentationPacing.configure(deviceState.physical, available, deviceExt);
    bool traceEnabled = false;
    if (vulkan::FrameTrace::requested() && deviceState.timestampBits == 64) {
        if (std::any_of(available.begin(), available.end(),
                        [](const auto& e) {
                            return std::strcmp(e.extensionName,
                                               VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0;
                        }) &&
            vulkan::FrameTrace::supported(deviceState.instance, deviceState.physical)) {
            deviceExt.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
            traceEnabled = true;
        }
    }
    VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    if (presentationPacing.enabled)
        d.pNext = &presentationPacing.waitFeatures;
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(deviceState.physical, &supported);
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported.samplerAnisotropy;
    features.multiDrawIndirect = supported.multiDrawIndirect;
    features.drawIndirectFirstInstance = supported.drawIndirectFirstInstance;
    deviceState.shadowIndirect = features.multiDrawIndirect && features.drawIndirectFirstInstance;
    deviceState.maxIndirectDraws = std::max(1u, props.limits.maxDrawIndirectCount);
    deviceState.anisotropySupported = features.samplerAnisotropy;
    deviceState.anisotropy =
        features.samplerAnisotropy ? std::min(8.f, props.limits.maxSamplerAnisotropy) : 1.f;
    d.pEnabledFeatures = &features;
    d.queueCreateInfoCount = 1;
    d.pQueueCreateInfos = &q;
    d.enabledExtensionCount = uint32_t(deviceExt.size());
    d.ppEnabledExtensionNames = deviceExt.data();
    check(vkCreateDevice(deviceState.physical, &d, nullptr, &deviceState.device), "vkCreateDevice");
    if (traceEnabled)
        frameTrace.initialize(deviceState.device);
    presentationPacing.initialize(deviceState.device);
    std::cout << "FIFO presentation pacing: " << (presentationPacing.enabled ? "supported" : "fallback")
              << std::endl;
    vkGetDeviceQueue(deviceState.device, deviceState.family, 0, &deviceState.queue);
    VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    p.queueFamilyIndex = deviceState.family;
    check(vkCreateCommandPool(deviceState.device, &p, nullptr, &deviceState.pool), "vkCreateCommandPool");
    createFrameResources();
}
} // namespace one
