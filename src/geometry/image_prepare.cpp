#include <theone/geometry/prepared_image.hpp>
#include <theone/assets/material.hpp>
#include "image_intern.hpp"
#include <unordered_map>
#include <cstring>
namespace one {
std::shared_ptr<const PreparedImage> prepareImage(const Image& image, const std::stop_token& stop) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != uint64_t(image.width) * image.height * 4)
        throw std::runtime_error("Invalid image payload");
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < image.rgba.size(); ++i) {
        if ((i & 65535) == 0)
            checkCancellation(stop);
        hash ^= image.rgba[i];
        hash *= 1099511628211ull;
    }
    if (auto found =
            detail::findPreparedImage(hash, uint32_t(image.width), uint32_t(image.height), image.rgba))
        return found;
    auto prepared = std::make_shared<PreparedImage>();
    prepared->hash = hash;
    for (bool srgb : {true, false}) {
        checkCancellation(stop);
        auto levels = buildMipChain(image.width, image.height, image.rgba, srgb);
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        for (auto& mip : levels) {
            checkCancellation(stop);
            if (srgb)
                prepared->mips.push_back({mip.width, mip.height, bytes->size()});
            bytes->insert(bytes->end(), mip.rgba.begin(), mip.rgba.end());
        }
        if (srgb)
            prepared->color = bytes;
        else
            prepared->data = (*bytes == *prepared->color) ? prepared->color : bytes;
    }
    return detail::internPreparedImage(std::move(prepared));
}
} // namespace one
