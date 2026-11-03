#include <theone/lighting/stellar.hpp>
#include <cmath>
#include <stdexcept>
namespace one {
namespace {
double hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return double(x & 0xffffffU) / 16777216.;
}
} // namespace
StellarActivityRegion stellarActivityRegion(const StellarEmitter& star, uint32_t index) {
    if (index >= 4)
        throw std::invalid_argument("Four prescribed bipolar regions per star");
    uint32_t seed = star.activity.seed + index * 71;
    double longitude = index * 2.399963229728653 + star.activity.seed * .017 + .3;
    double latitude = (index % 2 ? 1 : -1) * (.17 + .3 * hash(seed));
    StellarActivityRegion r;
    r.normal = {std::cos(latitude) * std::sin(longitude), std::sin(latitude),
                std::cos(latitude) * std::cos(longitude)};
    glm::dvec3 east{std::cos(longitude), 0, -std::sin(longitude)};
    auto north = glm::cross(r.normal, east);
    double tilt = (hash(seed + 9) - .5) * .6;
    r.tangent = east * std::cos(tilt) + north * std::sin(tilt);
    r.halfSeparation = .065 + .055 * hash(seed + 1);
    r.height = .075 + .115 * hash(seed + 2);
    return r;
}
double stellarThomsonPhase(double cosine) {
    if (!std::isfinite(cosine))
        throw std::invalid_argument("Nonfinite scattering angle");
    cosine = glm::clamp(cosine, -1., 1.);
    return 3 * (1 + cosine * cosine) / (16 * 3.14159265358979323846);
}
} // namespace one
