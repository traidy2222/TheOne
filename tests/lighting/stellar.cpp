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
        sun.activity.enabled = true;
        need(stellarIrradianceWattsPerM2(sun, {astronomicalUnitMetres, 0, 0}) == a,
             "Activity changed mean luminosity");
        double limbIntegral = 0, phaseIntegral = 0;
        for (int i = 0; i < 10000; ++i) {
            double mu = (i + .5) / 10000.;
            limbIntegral += 2 * mu * stellarLimbFactor(mu, .6) / 10000.;
            phaseIntegral += 4 * 3.141592653589793 * stellarThomsonPhase(2 * mu - 1) / 10000.;
        }
        need(std::abs(limbIntegral - 1) < 1e-7, "Limb law is not flux normalized");
        need(std::abs(phaseIntegral - 1) < 1e-7, "Thomson phase is not normalized");
        need(stellarLimbFactor(1, .6) > stellarLimbFactor(0, .6), "Photospheric limb does not darken");
        need(std::abs(stellarCoronalElectronDensity(sun, 2) / stellarCoronalElectronDensity(sun, 1) -
                      1. / 64) < 1e-12,
             "Quiet corona density does not fall with radius");
        for (uint32_t i = 0; i < 4; ++i) {
            auto r = stellarActivityRegion(sun, i);
            need(std::abs(glm::length(r.normal) - 1) < 1e-12 &&
                     std::abs(glm::dot(r.normal, r.tangent)) < 1e-12,
                 "Magnetic region basis is invalid");
            auto foot =
                r.normal * std::sqrt(1 - r.halfSeparation * r.halfSeparation) + r.tangent * r.halfSeparation;
            need(std::abs(glm::length(foot) - 1) < 1e-12,
                 "Magnetic arch footpoint detached from photosphere");
        }
        scene = {};
        scene.stars = {sun};
        validateStellarScene(scene);
        scene.stars[0].activity.granuleLifetimeSeconds = 0;
        bool rejected = false;
        try {
            validateStellarScene(scene);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        need(rejected, "Invalid activity lifetime accepted");
        scene.stars[0] = sun;
        scene.timeSeconds = -1;
        rejected = false;
        try {
            validateStellarScene(scene);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        need(rejected, "Invalid activity clock accepted");
        std::cout << "PASS solar_flux=" << a << " W/m2 luminosity=" << stellarLuminosityWatts(sun)
                  << " W penumbra_visibility=" << partial << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
