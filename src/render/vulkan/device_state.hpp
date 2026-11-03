#pragma once
#include <vulkan/vulkan.h>
struct SDL_Window;
namespace one::vulkan {

struct DeviceState {
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    float anisotropy = 1;
    bool anisotropySupported = false;
    uint32_t timestampBits = 0;
    float timestampPeriod = 0;
    bool shadowIndirect = false;
    uint32_t maxIndirectDraws = 1;
    bool validation = false;
    unsigned validationErrors = 0;
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
};
} // namespace one::vulkan
