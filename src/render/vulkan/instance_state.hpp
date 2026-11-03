#pragma once
#include <cstdint>
#include <compare>
#include <vector>
namespace one::vulkan {
struct InstanceKey {
    uintptr_t geometry = 0, material = 0, image = 0;
    uint32_t lod = 0;
    bool selected = false;
    auto operator<=>(const InstanceKey&) const = default;
};
struct InstancePlan {
    std::vector<InstanceKey> keys;
    std::vector<uint32_t> groups;
    uint32_t count = 0;
    bool worthwhile = false;
};
} // namespace one::vulkan
