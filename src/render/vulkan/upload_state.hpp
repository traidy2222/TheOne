#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <deque>
#include <vector>
namespace one::vulkan {
struct UploadProgress {
    bool ready = false, cancelled = false;
};
struct PendingUpload {
    std::shared_ptr<UploadProgress> progress;
    uint64_t remaining = 0;
    // Records at most the requested byte quantum and returns work bytes consumed.
    std::function<uint64_t(uint64_t)> step;
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> payloads;
};
struct UploadQueue {
    std::deque<PendingUpload> pending;
    double budgetMs = 1.0, bytesPerMs = 4 * 1024 * 1024;
    uint64_t chunkBytes = 256 * 1024;
};
} // namespace one::vulkan
