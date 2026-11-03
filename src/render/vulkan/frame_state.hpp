#pragma once
#include "geometry_state.hpp"
#include "scene_state.hpp"
#include <array>
#include <functional>
namespace one::vulkan {
inline constexpr uint32_t frameSlots = 2;
struct StagedUpload {
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t bytes = 0, capacity = 0, lastUsed = 0;
    void* mapped = nullptr;
};
struct FrameContext {
    bool volumeQueryWritten=false;
    std::vector<StagedUpload> uploads;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    uint64_t serial = 0;
    double submittedAtMs = 0;
    double submitHostMs = 0, fenceObservedHostMs = 0;
    bool queryWritten = false;
    bool uploadQueryWritten = false;
    uint64_t uploadBytes = 0;
    GeometryFrame geometry;
    FrameScene scene;
};
struct RetiredResource {
    uint64_t after = 0;
    std::function<void()> destroy;
    uint64_t bytes = 0;
};
} // namespace one::vulkan
