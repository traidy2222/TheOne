#pragma once
#include <theone/core/math.hpp>
#include <cstdint>
#include <vector>
#include <span>
namespace one {
struct SceneLighting {
    glm::vec3 sunDirection = glm::normalize(glm::vec3(-20, 35, 18));
    glm::vec3 sunRadiance{3.f, 2.85f, 2.65f};
    glm::vec3 skyRadiance{.12f, .17f, .24f};
    float exposure = 1.f;
};
struct FuelMaterial {
    float heatRelease = 43e6f, oxygenRatio = 3.5f, sootYield = .055f;
    float ignitionKelvin = 650.f, reactionRate = 5.f;
    float absorption = 6000.f, scattering = 1200.f, phaseG = .25f;
    float flameSpeed = .45f, lowerFuelFraction = .015f;
};
struct VolumeDomain {
    glm::vec3 origin{-8, 0, -8}, extent{16, 24, 16};
    glm::uvec3 resolution{48, 72, 48};
    glm::vec3 gravity{0, -9.81f, 0}, wind{0};
    // Initial resolved air motion, m/s RMS and metres. Zero keeps still-air references exact.
    float initialTurbulenceRms = 0, initialTurbulenceScale = 1.5f;
    float ambientKelvin = 293.15f, ambientDensity = 1.2f, heatCapacity = 1005.f;
    float maximumVelocity = 12.f, coolingRate = .3f, vorticityConfinement = 2.f;
    uint32_t pressureIterations = 48, maximumSubsteps = 12;
    uint64_t memoryBudget = 256ull * 1024 * 1024;
    bool closedBoundary = false;
};
struct CombustionSource {
    glm::vec3 position{0, 1.5f, 0};
    float radius = 1.25f;
    glm::vec3 velocity{0, 1, 0};
    float fuelKgPerSecond = .75f, sootKgPerSecond = 0;
    float heatWatts = 350000.f;
    float startSeconds = 0, durationSeconds = .2f;
};
struct VolumeObstacle {
    glm::vec3 minimum{}, maximum{}, velocity{};
};
// Sources share one physical gas field. Increment generation to restart/reconfigure.
// Values are copied by Renderer; edits and Renderer calls belong to its owning thread.
struct VolumeScene {
    VolumeDomain domain;
    FuelMaterial fuel;
    std::vector<CombustionSource> sources;
    std::vector<VolumeObstacle> obstacles;
    float initialSootKgPerM3 = 0, initialKelvin = 0;
    double timeSeconds = 0;
    uint64_t generation = 1;
    float rayStepScale = 1.f;
    uint32_t maximumRaySteps = 256;
};
struct VolumeStats {
    uint64_t generation = 0, gpuFrame = 0, allocatedBytes = 0;
    double simulatedSeconds = 0, timeDebtSeconds = 0, gpuSimulationMs = 0;
    double gpuLightingMs = 0, gpuRaymarchMs = 0;
    uint32_t substeps = 0, activeCells = 0, velocityClamps = 0;
    float fuelKg = 0, oxygenKg = 0, sootKg = 0, productsKg = 0, nitrogenKg = 0;
    float enthalpyJ = 0, maximumKelvin = 0, maximumSpeed = 0;
    float maximumDivergenceError = 0, initialDivergenceError = 0;
    double burnedFuelKg = 0, lostHeatJ = 0, boundaryMassKg = 0, boundaryEnthalpyJ = 0;
    glm::dvec4 boundarySpeciesKg{0};
    double injectedFuelKg = 0, injectedSootKg = 0, injectedHeatJ = 0;
};
// Diagnostic snapshot only: explicitly waits for queued work and copies the grid.
struct VolumeCell {
    glm::vec4 species{0}, thermal{0}, velocity{0};
};
struct VolumeReadback {
    VolumeDomain domain;
    std::vector<VolumeCell> cells;
    VolumeStats stats;
};
void addFuelExplosion(VolumeScene& scene, glm::vec3 position, float fuelKg, float radius = 1.25f,
                      float startSeconds = 0);
void validateVolumeScene(const VolumeScene& scene);
float sourceNormalization(const VolumeDomain& domain, const CombustionSource& source,
                          std::span<const VolumeObstacle> obstacles = {});
struct TransportSegment {
    glm::dvec3 radiance{0}, transmittance{1};
};
TransportSegment integrateMedium(glm::dvec3 extinction, glm::dvec3 source, double metres);
TransportSegment composeMedium(const TransportSegment& front, const TransportSegment& back);
double henyeyGreenstein(double cosine, double anisotropy);
struct ReactionResult {
    double fuelConsumed = 0, oxygenConsumed = 0, sootProduced = 0, productsProduced = 0, heatJ = 0;
};
ReactionResult reactFuel(double fuelKg, double oxygenKg, double kelvin, double seconds,
                         const FuelMaterial& material);
} // namespace one
