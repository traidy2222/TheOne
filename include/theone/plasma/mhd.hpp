#pragma once
#include <theone/core/math.hpp>
#include <cstdint>
#include <functional>
#include <memory>
namespace one {
// Cartesian 2.5D resistive MHD: all vector components, derivatives in x/y.
// Normalised units use mu0=1. Faces carry Bx/By; constrained transport preserves div B.
enum class PlasmaBoundary { Periodic, Coronal };
struct PlasmaConfig {
    uint32_t nx = 64, ny = 96;
    glm::dvec2 minimum{-2, 0}, extent{4, 3};
    double gamma = 5. / 3, cfl = .28, resistivity = .001;
    double gravity = 0, stellarRadius = 34.785, footpointShear = 0;
    PlasmaBoundary boundary = PlasmaBoundary::Coronal;
};
struct PlasmaPrimitive {
    double density = 1, pressure = 1;
    glm::dvec3 velocity{0}, magnetic{0};
};
struct PlasmaDiagnostics {
    double time = 0, mass = 0, totalEnergy = 0, magneticEnergy = 0, kineticEnergy = 0;
    double maximumDivergence = 0, relativeDivergence = 0, minimumDensity = 0, minimumPressure = 0;
    double injectedEnergy = 0;
    uint64_t steps = 0, rejectedSteps = 0;
};
class PlasmaGrid {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit PlasmaGrid(const PlasmaConfig& config);
    ~PlasmaGrid();
    PlasmaGrid(PlasmaGrid&&) noexcept;
    PlasmaGrid& operator=(PlasmaGrid&&) noexcept;
    // Az is sampled at corners; Bx=dAz/dy and By=-dAz/dx are discrete curls.
    void initialize(const std::function<double(glm::dvec2)>& az,
                    const std::function<PlasmaPrimitive(glm::dvec2)>& gas);
    double step(double maximumDt);
    double time() const;
    PlasmaPrimitive cell(uint32_t x, uint32_t y) const;
    PlasmaDiagnostics diagnostics() const;
    // Explicit external magnetic work, not a prescribed eruption trajectory.
    void injectGuideField(glm::dvec2 centre, double radius, double amplitude);
};
} // namespace one
