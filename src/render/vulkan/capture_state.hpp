#pragma once
#include <vulkan/vulkan.h>
namespace one::vulkan {

struct CaptureResources {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
} // namespace one::vulkan
