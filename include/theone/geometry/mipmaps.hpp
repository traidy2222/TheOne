#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace one {
struct MipPixels {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
uint32_t mipLevelCount(uint32_t width, uint32_t height);
// CPU fallback for devices without linear image blits; alpha and data textures remain linear.
std::vector<MipPixels> buildMipChain(uint32_t width, uint32_t height, std::span<const uint8_t> rgba,
                                     bool srgb);
} // namespace one
