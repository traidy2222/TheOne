#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <limits>
namespace one::vulkan {
struct IndexedBinding {
    VkDeviceSize vertex = 0, index = 0;
    uint32_t firstIndex = 0;
    int32_t baseVertex = 0;
};
// Keep the packed allocation unchanged. Vertex bindings need only the stride
// remainder; firstIndex/baseVertex address each mesh relative to those shared bindings.
inline IndexedBinding indexedBinding(VkDeviceSize vertex, VkDeviceSize index, uint32_t stride) {
    if (stride && index % 4 == 0 && index / 4 <= UINT32_MAX &&
        vertex / stride <= uint64_t(std::numeric_limits<int32_t>::max()))
        return {vertex % stride, 0, uint32_t(index / 4), int32_t(vertex / stride)};
    return {vertex, index, 0, 0};
}
} // namespace one::vulkan
