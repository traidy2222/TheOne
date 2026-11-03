#include <theone/lighting/stellar.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void need(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    try {
        using namespace one;
        StellarEmitter sun;
        double a = stellarIrradianceWattsPerM2(sun, {astronomicalUnitMetres, 0, 0}),
               b = stellarIrradianceWattsPerM2(sun, {2 * astronomicalUnitMetres, 0, 0});
        need(std::abs(a - 1361) < 1, "Solar irradiance differs from nominal reference");
        need(std::abs(a / b - 4) < 1e-12, "Star does not obey inverse square falloff");
        need(std::abs(stellarLuminosityWatts(sun) / 3.828e26 - 1) < .001,
             "Solar luminosity reference failed");
        auto translated = sun;
        translated.positionMetres = {1e18, 1e18, 1e18};
        auto probe = translated.positionMetres + glm::dvec3(astronomicalUnitMetres, 0, 0);
        need(std::abs(stellarIrradianceWattsPerM2(translated, probe) / a - 1) < 1e-8,
             "Large astronomical origin changed physical irradiance");
        StellarScene scene;
        scene.stars = {sun};
        glm::dvec3 point{astronomicalUnitMetres, 0, 0};
        need(stellarVisibility(scene, 0, point) == 1, "Vacuum attenuated the emitter");
        scene.occluders = {{{astronomicalUnitMetres * .5, 0, 0}, solarRadiusMetres, 7}};
        need(stellarVisibility(scene, 0, point) == 0, "Opaque sphere did not eclipse star");
        need(stellarVisibility(scene, 0, point, 7) == 1, "Self-occluder exclusion failed");
        scene.occluders[0].positionMetres.y = solarRadiusMetres;
        double partial = stellarVisibility(scene, 0, point, 0, 1024);
        need(partial > 0 && partial < 1, "Finite star produced no penumbra");
        scene.occluders[0].positionMetres.x = 2 * astronomicalUnitMetres;
        need(stellarVisibility(scene, 0, point) == 1, "Occluder behind the probe blocked light");
        scene.occluders.clear();
        auto foreground = sun;
        foreground.positionMetres = {astronomicalUnitMetres * .5, 0, 0};
        scene.stars.push_back(foreground);
        need(stellarVisibility(scene, 0, point) == 0, "Foreground photosphere did not occult another star");
        std::cout << "PASS solar_flux=" << a << " W/m2 luminosity=" << stellarLuminosityWatts(sun)
                  << " W penumbra_visibility=" << partial << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
