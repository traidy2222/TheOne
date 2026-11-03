#pragma once
#include <theone/geometry/prepared_image.hpp>
#include <span>
namespace one::detail {
std::shared_ptr<const PreparedImage> findPreparedImage(uint64_t hash, uint32_t width, uint32_t height,
                                                       std::span<const uint8_t> rgba);
std::shared_ptr<const PreparedImage> internPreparedImage(std::shared_ptr<const PreparedImage> prepared);
} // namespace one::detail
