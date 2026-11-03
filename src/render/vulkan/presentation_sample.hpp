#pragma once
#include <cstdint>
namespace one::vulkan {
struct PresentationCompletionSample {
    uint64_t id = 0;
    double driverMs = 0;
};
}
