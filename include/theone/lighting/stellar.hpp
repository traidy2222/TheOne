#pragma once
#include <theone/core/math.hpp>
#include <cstdint>
#include <cstddef>
#include <vector>
namespace one {
inline constexpr double astronomicalUnitMetres = 149597870700.;
inline constexpr double solarRadiusMetres = 695700000.;
enum class StellarView : uint32_t { Visible, EuvDiagnostic, Coronagraph };
struct StellarActivity {
    bool enabled = false;
    double granuleDiameterMetres = 1e6, granuleLifetimeSeconds = 600;
    double rotationPeriodSeconds = 25.38 * 86400;
    float granuleContrast = .035f, limbDarkening = .6f, magneticStrength = .8f;
    double baseElectronDensityPerM3 = 4e14;
    float atmosphereRadius = 1.6f;
    uint32_t atmosphereSteps = 64, seed = 17;
};
struct StellarEmitter {
    glm::dvec3 positionMetres{0};
    double radiusMetres = solarRadiusMetres, kelvin = 5772., powerScale = 1.;
    StellarActivity activity;
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
    double timeSeconds = 0;
    StellarView view = StellarView::Visible;
};
void validateStellarScene(const StellarScene& scene);
double stellarLuminosityWatts(const StellarEmitter& star);
double stellarIrradianceWattsPerM2(const StellarEmitter& star, glm::dvec3 positionMetres);
double stellarAngularRadius(const StellarEmitter& star, glm::dvec3 positionMetres);
glm::dvec3 stellarBandRadiance(const StellarEmitter& star);
double stellarLimbFactor(double cosine, double coefficient);
double stellarCoronalElectronDensity(const StellarEmitter& star, double radiusInStellarRadii);
struct StellarActivityRegion {
    glm::dvec3 normal{}, tangent{};
    double halfSeparation = 0, height = 0;
};
StellarActivityRegion stellarActivityRegion(const StellarEmitter& star, uint32_t region);
double stellarThomsonPhase(double cosine);
double stellarVisibility(const StellarScene& scene, std::size_t star, glm::dvec3 pointMetres,
                         uint64_t excludeEntity = 0, uint32_t samples = 64);
} // namespace one
