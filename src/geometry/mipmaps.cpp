#include <theone/geometry/mipmaps.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace one {
uint32_t mipLevelCount(uint32_t width, uint32_t height) {
    uint32_t n = 0;
    for (auto size = std::max(width, height); size; size >>= 1)
        ++n;
    return n;
}
std::vector<MipPixels> buildMipChain(uint32_t width, uint32_t height, std::span<const uint8_t> rgba,
                                     bool srgb) {
    if (!width || !height || rgba.size() != size_t(width) * height * 4)
        throw std::invalid_argument("Invalid mip source");
    std::vector<MipPixels> levels;
    levels.push_back({width, height, {rgba.begin(), rgba.end()}});
    auto linear = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
    auto encode = [](float v) {
        return v <= .0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
    };
    while (width > 1 || height > 1) {
        auto w = std::max(1u, width / 2), h = std::max(1u, height / 2);
        MipPixels dst{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
        const auto& source = levels.back().rgba;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                for (int c = 0; c < 4; ++c) {
                    // Area box includes the last row/column of odd dimensions.
                    uint32_t x0 = x * width / w, x1 = (x + 1) * width / w, y0 = y * height / h,
                             y1 = (y + 1) * height / h;
                    float sum = 0;
                    for (uint32_t sy = y0; sy < y1; ++sy)
                        for (uint32_t sx = x0; sx < x1; ++sx) {
                            float v = source[(size_t(sy) * width + sx) * 4 + c] / 255.f;
                            sum += srgb && c < 3 ? linear(v) : v;
                        }
                    sum /= float((x1 - x0) * (y1 - y0));
                    if (srgb && c < 3)
                        sum = encode(sum);
                    dst.rgba[(size_t(y) * w + x) * 4 + c] =
                        uint8_t(std::clamp(std::round(sum * 255), 0.f, 255.f));
                }
        levels.push_back(std::move(dst));
        width = w;
        height = h;
    }
    return levels;
}
} // namespace one
