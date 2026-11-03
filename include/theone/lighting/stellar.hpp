#pragma once
#include <theone/core/math.hpp>
#include <cstdint>
#include <cstddef>
#include <vector>
namespace one {
inline constexpr double astronomicalUnitMetres = 149597870700.;
inline constexpr double solarRadiusMetres = 695700000.;
struct StellarEmitter {
    glm::dvec3 positionMetres{0};
    double radiusMetres = solarRadiusMetres, kelvin = 5772., powerScale = 1.;
};
struct StellarOccluder {
    glm::dvec3 positionMetres{0};
    double radiusMetres = 1;
    uint64_t entity = 0;
};
struct StellarScene {
    glm::dvec3 originMetres{0};
    double metresPerUnit = 1;
    std::vector<StellarEmitter> stars;
    std::vector<StellarOccluder> occluders;
};
void validateStellarScene(const StellarScene& scene);
double stellarLuminosityWatts(const StellarEmitter& star);
double stellarIrradianceWattsPerM2(const StellarEmitter& star, glm::dvec3 positionMetres);
double stellarAngularRadius(const StellarEmitter& star, glm::dvec3 positionMetres);
glm::dvec3 stellarBandRadiance(const StellarEmitter& star);
double stellarVisibility(const StellarScene& scene, std::size_t star, glm::dvec3 pointMetres,
                         uint64_t excludeEntity = 0, uint32_t samples = 64);
} // namespace one
