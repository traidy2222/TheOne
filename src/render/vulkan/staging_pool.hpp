#pragma once
#include "frame_state.hpp"
namespace one::vulkan {
struct StagingPool {
    std::vector<StagedUpload> available;
    uint64_t bytes = 0;
    static constexpr uint64_t maxBytes = 4 * 1024 * 1024;
    static constexpr size_t maxEntries = 64;
};
} // namespace one::vulkan
