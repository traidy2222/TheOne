#pragma once
#include <stop_token>
#include <stdexcept>
namespace one {
struct LoadCancelled : std::runtime_error {
    LoadCancelled() : std::runtime_error("Load cancelled") {}
};
inline void checkCancellation(const std::stop_token& stop) {
    if (stop.stop_requested())
        throw LoadCancelled();
}
} // namespace one
