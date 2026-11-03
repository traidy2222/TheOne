#include <theone/volumes/volume.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace one {
namespace {
void need(bool ok, const char* text) {
    if (!ok)
        throw std::invalid_argument(text);
}
} // namespace
void addFuelExplosion(VolumeScene& scene, glm::vec3 position, float fuelKg, float radius, float start) {
    validateVolumeScene(scene);
    need(fuelKg > 0 && std::isfinite(fuelKg) && radius > 0 && std::isfinite(radius) && start >= 0 &&
             std::isfinite(start),
         "Invalid fuel explosion source");
    for (int a = 0; a < 3; ++a)
        need(std::isfinite(position[a]), "Invalid explosion position");
    CombustionSource fuel;
    fuel.position = position;
    fuel.radius = radius;
    fuel.fuelKgPerSecond = fuelKg / .24f;
    fuel.heatWatts = 0;
    fuel.durationSeconds = .24f;
    fuel.startSeconds = start;
    fuel.velocity = {0, .7f, 0};
    CombustionSource igniter;
    igniter.position = position;
    auto h = scene.domain.extent / glm::vec3(scene.domain.resolution);
    igniter.radius = std::max({.5f, radius * .35f, 1.5f * std::max(h.x, std::max(h.y, h.z))});
    igniter.fuelKgPerSecond = 0;
    igniter.velocity = {0, 0, 0};
    igniter.startSeconds = start + .025f;
    igniter.durationSeconds = .08f;
    auto nearest = glm::clamp(glm::floor((position - scene.domain.origin) / h), glm::vec3(0),
                              glm::vec3(scene.domain.resolution) - 1.f);
    auto q = (scene.domain.origin + (nearest + .5f) * h - position) / igniter.radius;
    float peak = std::pow(std::max(.001f, 1 - glm::dot(q, q)), 2.f);
    igniter.heatWatts = sourceNormalization(scene.domain, igniter, scene.obstacles) *
                        scene.domain.ambientDensity * scene.domain.heatCapacity *
                        (1150 - scene.domain.ambientKelvin) / (igniter.durationSeconds * peak);
    auto candidate = scene;
    candidate.sources.push_back(fuel);
    candidate.sources.push_back(igniter);
    validateVolumeScene(candidate);
    scene = std::move(candidate);
}
void validateVolumeScene(const VolumeScene& s) {
    const auto& d = s.domain;
    for (int a = 0; a < 3; ++a) {
        need(std::isfinite(d.origin[a]) && std::isfinite(d.extent[a]) && d.extent[a] > 0,
             "Invalid volume bounds");
        need(d.resolution[a] >= 4 && d.resolution[a] <= 256, "Volume resolution outside 4..256");
        need(std::isfinite(d.gravity[a]) && std::isfinite(d.wind[a]), "Invalid volume forces");
    }
    const uint64_t n = uint64_t(d.resolution.x) * d.resolution.y * d.resolution.z;
    need(std::isfinite(d.initialTurbulenceRms) && d.initialTurbulenceRms >= 0 &&
             d.initialTurbulenceRms <= d.maximumVelocity * .5f && std::isfinite(d.initialTurbulenceScale) &&
             d.initialTurbulenceScale > 0,
         "Invalid initial turbulence");
    need(n <= 4 * 1024 * 1024 && n * 172 + 65536 <= d.memoryBudget,
         "Volume exceeds bounded grid memory budget");
    need(d.ambientKelvin >= 150 && d.ambientKelvin <= 1000 && d.ambientDensity > .01f &&
             d.ambientDensity < 100,
         "Invalid ambient gas");
    need(d.heatCapacity >= 100 && d.heatCapacity <= 10000 && d.maximumVelocity > 0 &&
             d.maximumVelocity <= 200,
         "Invalid gas solver configuration");
    need(d.vorticityConfinement >= 0 && d.vorticityConfinement <= 20 &&
             std::isfinite(d.vorticityConfinement) && d.coolingRate >= 0 && std::isfinite(d.coolingRate) &&
             d.pressureIterations >= 4 && d.pressureIterations <= 512 && d.maximumSubsteps >= 1 &&
             d.maximumSubsteps <= 128,
         "Invalid volume stepping limits");
    const auto& f = s.fuel;
    need(f.heatRelease >= 0 && std::isfinite(f.heatRelease) && f.oxygenRatio > 0 &&
             std::isfinite(f.oxygenRatio) && f.sootYield >= 0 && f.sootYield <= 1,
         "Invalid fuel composition");
    need(f.ignitionKelvin > d.ambientKelvin && f.ignitionKelvin <= 5000 && f.reactionRate >= 0 &&
             f.reactionRate <= 100,
         "Invalid reaction kinetics");
    need(std::isfinite(f.flameSpeed) && f.flameSpeed >= 0 && f.flameSpeed <= 20 &&
             std::isfinite(f.lowerFuelFraction) && f.lowerFuelFraction >= 0 && f.lowerFuelFraction < .5f,
         "Invalid flame propagation");
    need(f.absorption >= 0 && std::isfinite(f.absorption) && f.scattering >= 0 &&
             std::isfinite(f.scattering) && std::abs(f.phaseG) < .95f,
         "Invalid optical material");
    need(std::isfinite(s.initialSootKgPerM3) && s.initialSootKgPerM3 >= 0 && s.initialSootKgPerM3 <= 10 &&
             std::isfinite(s.initialKelvin) &&
             (s.initialKelvin == 0 || (s.initialKelvin >= 150 && s.initialKelvin <= 6000)),
         "Invalid initial medium");
    need(s.sources.size() <= 16 && s.obstacles.size() <= 16,
         "At most 16 sources and 16 solid boxes per domain");
    need(std::isfinite(s.timeSeconds) && s.timeSeconds >= 0 && s.timeSeconds < 1e7, "Invalid volume clock");
    need(s.rayStepScale >= .25f && s.rayStepScale <= 4 && s.maximumRaySteps >= 16 &&
             s.maximumRaySteps <= 1024,
         "Invalid volume integration budget");
    for (const auto& source : s.sources) {
        need(source.radius > 0 && std::isfinite(source.radius) && source.fuelKgPerSecond >= 0 &&
                 std::isfinite(source.fuelKgPerSecond) && source.sootKgPerSecond >= 0 &&
                 std::isfinite(source.sootKgPerSecond) && source.heatWatts >= 0 &&
                 std::isfinite(source.heatWatts),
             "Invalid mass/energy source");
        need(std::isfinite(source.startSeconds) && source.startSeconds >= 0 &&
                 std::isfinite(source.durationSeconds) && source.durationSeconds > 0,
             "Invalid source interval");
        for (int a = 0; a < 3; ++a)
            need(std::isfinite(source.position[a]) && std::isfinite(source.velocity[a]),
                 "Invalid source position/velocity");
        need(sourceNormalization(d, source, s.obstacles) > 0, "Source footprint misses all gas cells");
    }
    for (const auto& obstacle : s.obstacles)
        for (int a = 0; a < 3; ++a)
            need(std::isfinite(obstacle.minimum[a]) && std::isfinite(obstacle.maximum[a]) &&
                     obstacle.minimum[a] < obstacle.maximum[a] && std::isfinite(obstacle.velocity[a]),
                 "Invalid solid boundary");
}
float sourceNormalization(const VolumeDomain& d, const CombustionSource& s,
                          std::span<const VolumeObstacle> obstacles) {
    need(s.radius > 0 && std::isfinite(s.radius), "Invalid source radius");
    for (int a = 0; a < 3; ++a)
        need(std::isfinite(s.position[a]) && std::isfinite(d.origin[a]) && std::isfinite(d.extent[a]) &&
                 d.extent[a] > 0 && d.resolution[a] >= 4 && d.resolution[a] <= 256,
             "Invalid source integration domain");
    const auto h = d.extent / glm::vec3(d.resolution);
    double sum = 0;
    // Clamp in double before integer conversion, including sources far outside the domain.
    auto lower =
        glm::floor((glm::dvec3(s.position) - double(s.radius) - glm::dvec3(d.origin)) / glm::dvec3(h));
    auto upper =
        glm::floor((glm::dvec3(s.position) + double(s.radius) - glm::dvec3(d.origin)) / glm::dvec3(h));
    auto lo = glm::ivec3(glm::clamp(lower, glm::dvec3(0), glm::dvec3(d.resolution)));
    auto hi = glm::ivec3(glm::clamp(upper, glm::dvec3(-1), glm::dvec3(d.resolution) - 1.));
    for (int z = lo.z; z <= hi.z; ++z)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int x = lo.x; x <= hi.x; ++x) {
                auto position = d.origin + (glm::vec3(x, y, z) + .5f) * h;
                bool blocked = false;
                for (const auto& b : obstacles)
                    if (glm::all(glm::greaterThanEqual(position + h * .5f, b.minimum)) &&
                        glm::all(glm::lessThanEqual(position - h * .5f, b.maximum))) {
                        blocked = true;
                        break;
                    }
                if (blocked)
                    continue;
                auto q = (position - s.position) / s.radius;
                auto w = std::max(0.f, 1 - glm::dot(q, q));
                sum += double(w) * w;
            }
    return float(sum * double(h.x) * h.y * h.z);
}
TransportSegment integrateMedium(glm::dvec3 sigma, glm::dvec3 source, double ds) {
    need(std::isfinite(ds) && ds >= 0, "Invalid segment length");
    TransportSegment r;
    for (int a = 0; a < 3; ++a) {
        need(std::isfinite(sigma[a]) && sigma[a] >= 0 && std::isfinite(source[a]) && source[a] >= 0,
             "Invalid medium coefficient");
        r.transmittance[a] = std::exp(-sigma[a] * ds);
        r.radiance[a] = source[a] * (sigma[a] > 1e-12 ? -std::expm1(-sigma[a] * ds) / sigma[a] : ds);
    }
    return r;
}
TransportSegment composeMedium(const TransportSegment& a, const TransportSegment& b) {
    return {a.radiance + a.transmittance * b.radiance, a.transmittance * b.transmittance};
}
double henyeyGreenstein(double c, double g) {
    need(std::abs(g) < 1 && std::isfinite(c), "Invalid phase function");
    return (1 - g * g) / (12.566370614359172 * std::pow(1 + g * g - 2 * g * std::clamp(c, -1., 1.), 1.5));
}
ReactionResult reactFuel(double fuel, double oxygen, double t, double dt, const FuelMaterial& m) {
    need(fuel >= 0 && oxygen >= 0 && std::isfinite(fuel) && std::isfinite(oxygen) && std::isfinite(t) &&
             dt >= 0 && std::isfinite(dt),
         "Invalid reaction state");
    ReactionResult r;
    if (t < m.ignitionKelvin)
        return r;
    r.fuelConsumed = std::min(fuel * -std::expm1(-double(m.reactionRate) * dt), oxygen / m.oxygenRatio);
    r.oxygenConsumed = r.fuelConsumed * m.oxygenRatio;
    r.sootProduced = r.fuelConsumed * m.sootYield;
    r.productsProduced = r.fuelConsumed + r.oxygenConsumed - r.sootProduced;
    r.heatJ = r.fuelConsumed * m.heatRelease;
    return r;
}
} // namespace one
