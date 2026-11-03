#pragma once
#include <theone/geometry/mipmaps.hpp>
#include <theone/core/cancellation.hpp>
#include <memory>
namespace one {
struct Image;
struct ImageMip {
    uint32_t width, height;
    uint64_t offset;
};
struct PreparedImage {
    uint64_t hash = 0;
    std::vector<ImageMip> mips;
    std::shared_ptr<const std::vector<uint8_t>> color, data;
};
std::shared_ptr<const PreparedImage> prepareImage(const Image& image, const std::stop_token& stop = {});
} // namespace one
