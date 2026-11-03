#include <theone/volumes/volume.hpp>
#include <iostream>
#include <stdexcept>
#include <cmath>
static void need(bool b, const char* t) {
    if (!b)
        throw std::runtime_error(t);
}
int main() {
    try {
        using namespace one;
        auto slab = integrateMedium({.4, .7, 1.2}, {2, 3, 4}, 4);
        TransportSegment divided;
        for (int i = 0; i < 100; ++i)
            divided = composeMedium(divided, integrateMedium({.4, .7, 1.2}, {2, 3, 4}, .04));
        need(glm::length(slab.radiance - divided.radiance) < 1e-11 &&
                 glm::length(slab.transmittance - divided.transmittance) < 1e-12,
             "Transport changes with step length");
        auto vacuum = integrateMedium({0, 0, 0}, {0, 0, 0}, 10);
        need(vacuum.transmittance == glm::dvec3(1) && vacuum.radiance == glm::dvec3(0),
             "Vacuum changed transport");
        auto zero = integrateMedium({1e-15, 0, 0}, {2, 2, 2}, 3);
        need(glm::length(zero.radiance - glm::dvec3(6)) < 1e-10, "Zero extinction limit");
        double integral = 0;
        for (int i = 0; i < 100000; ++i)
            integral += henyeyGreenstein(-1 + (i + .5) * 2. / 100000, .4) * 2. / 100000 * 6.283185307179586;
        need(std::abs(integral - 1) < 1e-8, "Phase is not normalized");
        FuelMaterial fuel;
        auto r = reactFuel(1, .1, 1200, 1, fuel);
        need(std::abs(r.fuelConsumed - .1 / fuel.oxygenRatio) < 1e-12, "Oxidizer limit");
        need(std::abs(r.fuelConsumed + r.oxygenConsumed - r.productsProduced - r.sootProduced) < 1e-12,
             "Reaction lost mass");
        need(r.heatJ == r.fuelConsumed * fuel.heatRelease, "Unbounded heat");
        need(reactFuel(1, 1, 293, 1, fuel).fuelConsumed == 0 &&
                 reactFuel(0, 1, 1200, 1, fuel).fuelConsumed == 0 &&
                 reactFuel(1, 0, 1200, 1, fuel).fuelConsumed == 0,
             "Invalid ignition/fuel limits");
        VolumeScene scene;
        addFuelExplosion(scene, {0, 2, 0}, .18f);
        need(scene.sources.size() == 2, "Explosion did not separate fuel and ignition");
        auto a = sourceNormalization(scene.domain, scene.sources[0]);
        scene.domain.resolution *= 2u;
        auto b = sourceNormalization(scene.domain, scene.sources[0]);
        need(std::abs(a - b) / a < .02f, "Source integration changes with resolution");
        bool rejected = false;
        try {
            scene.domain.resolution = {256, 256, 256};
            validateVolumeScene(scene);
        } catch (const std::exception&) {
            rejected = true;
        }
        need(rejected, "Unbounded domain accepted");
        std::cout << "PASS analytic radiative transport, phase normalization, reaction mass/energy, source "
                     "normalization and allocation limits\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
