#include <theone/lighting/stellar.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace one {
namespace {
constexpr double pi = 3.14159265358979323846, sigma = 5.670374419e-8;
void need(bool good, const char* text) {
    if (!good)
        throw std::invalid_argument(text);
}
bool finite(glm::dvec3 x) {
    return std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z);
}
void validate(const StellarEmitter& s) {
    need(finite(s.positionMetres) && std::isfinite(s.radiusMetres) && s.radiusMetres > 0 &&
             s.radiusMetres <= 1e15 && std::isfinite(s.kelvin) && s.kelvin >= 1000 && s.kelvin <= 10000 &&
             std::isfinite(s.powerScale) && s.powerScale >= 0 && s.powerScale <= 10,
         "Invalid stellar emitter");
    if (s.activity.enabled) {
        const auto& a = s.activity;
        need(std::isfinite(a.granuleDiameterMetres) && a.granuleDiameterMetres >= s.radiusMetres / 1e7 &&
                 a.granuleDiameterMetres <= s.radiusMetres && std::isfinite(a.granuleLifetimeSeconds) &&
                 a.granuleLifetimeSeconds >= 1 && std::isfinite(a.rotationPeriodSeconds) &&
                 a.rotationPeriodSeconds >= 1 && std::isfinite(a.granuleContrast) && a.granuleContrast >= 0 &&
                 a.granuleContrast <= .15f && std::isfinite(a.limbDarkening) && a.limbDarkening >= 0 &&
                 a.limbDarkening <= 1 && std::isfinite(a.magneticStrength) && a.magneticStrength >= 0 &&
                 a.magneticStrength <= 2 && std::isfinite(a.baseElectronDensityPerM3) &&
                 a.baseElectronDensityPerM3 >= 0 && a.baseElectronDensityPerM3 <= 1e16 &&
                 std::isfinite(a.atmosphereRadius) && a.atmosphereRadius >= 1.05f &&
                 a.atmosphereRadius <= 2.5f && a.atmosphereSteps >= 16 && a.atmosphereSteps <= 192 &&
                 a.seed < 16777216,
             "Invalid stellar activity model");
    }
}
} // namespace
void validateStellarScene(const StellarScene& scene) {
    need(std::isfinite(scene.timeSeconds) && scene.timeSeconds >= 0 && scene.timeSeconds <= 1e9 &&
             uint32_t(scene.view) <= 3,
         "Invalid stellar activity clock/view");
    need(finite(scene.originMetres) && std::isfinite(scene.metresPerUnit) && scene.metresPerUnit > 0,
         "Invalid astronomical render origin/scale");
    need(scene.stars.size() <= 4 && scene.occluders.size() <= 16,
         "Stellar scene supports four emitters and sixteen opaque spheres");
    auto renderable = [&](glm::dvec3 position, double radius) {
        auto p = (position - scene.originMetres) / scene.metresPerUnit;
        need(finite(p) && glm::all(glm::lessThanEqual(glm::abs(p), glm::dvec3(1e18))) &&
                 radius / scene.metresPerUnit <= 1e18,
             "Stellar coordinates exceed render range; choose an appropriate local origin and unit");
    };
    for (const auto& star : scene.stars) {
        validate(star);
        renderable(star.positionMetres, star.radiusMetres);
    }
    for (const auto& b : scene.occluders) {
        need(finite(b.positionMetres) && std::isfinite(b.radiusMetres) && b.radiusMetres > 0,
             "Invalid stellar occluder");
        renderable(b.positionMetres, b.radiusMetres);
    }
    if (scene.plasma) {
        const auto& p = *scene.plasma;
        need(p.emitter < scene.stars.size() && scene.stars[p.emitter].activity.enabled &&
                 finite(glm::dvec3(p.normal)) && finite(glm::dvec3(p.tangent)) &&
                 std::abs(glm::length(p.normal) - 1) < 1e-4 && std::abs(glm::length(p.tangent) - 1) < 1e-4 &&
                 std::abs(glm::dot(p.normal, p.tangent)) < 1e-4 && std::isfinite(p.lengthMetres) &&
                 p.lengthMetres >= scene.stars[p.emitter].radiusMetres * 1e-6 &&
                 std::isfinite(p.halfDepthMetres) && p.halfDepthMetres >= p.lengthMetres * .001 &&
                 p.lengthMetres / scene.stars[p.emitter].radiusMetres < .2 &&
                 p.halfDepthMetres / p.lengthMetres < 2 && p.resolution.x >= 4 && p.resolution.y >= 4 &&
                 p.resolution.x <= 128 && p.resolution.y <= 128 &&
                 p.cells.size() == size_t(p.resolution.x) * p.resolution.y && std::isfinite(p.minimum.x) &&
                 std::isfinite(p.minimum.y) && std::isfinite(p.extent.x) && std::isfinite(p.extent.y) &&
                 p.extent.x > 0 && p.extent.y > 0 && p.extent.x <= 20 && p.extent.y <= 20 &&
                 std::abs(p.minimum.x) <= 20 && std::abs(p.minimum.y) <= 20,
             "Invalid stellar plasma patch");
        for (const auto& cell : p.cells)
            need(std::isfinite(cell.x) && cell.x >= 0 && cell.x <= 1e20 && std::isfinite(cell.y) &&
                     cell.y >= 100 && cell.y <= 1e9 && std::isfinite(cell.z) && cell.z >= 0 &&
                     cell.z <= 1e20 && std::isfinite(cell.w) && cell.w >= 0 && cell.w <= 1e9 &&
                     (cell.z == 0 || cell.w >= 100),
                 "Invalid plasma density/temperature");
    }
}
double stellarLuminosityWatts(const StellarEmitter& s) {
    validate(s);
    return 4 * pi * s.radiusMetres * s.radiusMetres * sigma * std::pow(s.kelvin, 4) * s.powerScale;
}
double stellarIrradianceWattsPerM2(const StellarEmitter& s, glm::dvec3 p) {
    validate(s);
    double distance = glm::length(p - s.positionMetres);
    need(finite(p) && distance >= s.radiusMetres, "Irradiance probe must be outside the stellar surface");
    double ratio = s.radiusMetres / distance;
    return sigma * std::pow(s.kelvin, 4) * ratio * ratio * s.powerScale;
}
double stellarAngularRadius(const StellarEmitter& s, glm::dvec3 p) {
    validate(s);
    return std::asin(std::clamp(s.radiusMetres / glm::length(p - s.positionMetres), 0., 1.));
}
glm::dvec3 stellarBandRadiance(const StellarEmitter& s) {
    validate(s);
    glm::dvec3 result;
    const double wavelengths[]{650e-9, 550e-9, 450e-9};
    for (int a = 0; a < 3; ++a) {
        double l = wavelengths[a];
        result[a] = s.powerScale * 1.191042972e-16 /
                    (std::pow(l, 5) * std::expm1(.01438776877 / (l * s.kelvin))) * 1e-7;
    }
    return result;
}
double stellarLimbFactor(double cosine, double coefficient) {
    need(std::isfinite(cosine) && std::isfinite(coefficient) && coefficient >= 0 && coefficient <= 1,
         "Invalid limb darkening input");
    return (1 - coefficient + coefficient * std::clamp(cosine, 0., 1.)) / (1 - coefficient / 3);
}
double stellarCoronalElectronDensity(const StellarEmitter& star, double radius) {
    validate(star);
    need(std::isfinite(radius) && radius >= 1, "Coronal radius must be outside photosphere");
    return star.activity.enabled ? star.activity.baseElectronDensityPerM3 * std::pow(radius, -6) : 0;
}
double stellarVisibility(const StellarScene& scene, size_t index, glm::dvec3 p, uint64_t exclude,
                         uint32_t samples) {
    validateStellarScene(scene);
    need(index < scene.stars.size() && samples > 0 && samples <= 4096, "Invalid stellar visibility query");
    const auto& star = scene.stars[index];
    glm::dvec3 delta = star.positionMetres - p;
    double distance = glm::length(delta);
    need(distance > star.radiusMetres, "Visibility probe must be outside star");
    auto axis = delta / distance;
    auto tangent =
        glm::normalize(glm::cross(axis, std::abs(axis.y) < .9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0)));
    auto bitangent = glm::cross(axis, tangent);
    double r = star.radiusMetres / distance, cosine = std::sqrt(std::max(0., 1 - r * r)), visible = 0;
    for (uint32_t i = 0; i < samples; ++i) {
        double c = 1 - ((i + .5) / samples) * (r * r / (1 + cosine)), angle = i * 2.399963229728653;
        auto direction = axis * c + (tangent * std::cos(angle) + bitangent * std::sin(angle)) *
                                        std::sqrt(std::max(0., 1 - c * c));
        auto perpendicularStar = glm::cross(delta, direction);
        double end = glm::dot(delta, direction) -
                     std::sqrt(std::max(0., star.radiusMetres * star.radiusMetres -
                                                glm::dot(perpendicularStar, perpendicularStar)));
        bool blocked = false;
        for (size_t other = 0; other < scene.stars.size(); ++other) {
            if (other == index)
                continue;
            const auto& occluder = scene.stars[other];
            auto d = occluder.positionMetres - p;
            double t = glm::dot(d, direction);
            auto perpendicular = glm::cross(d, direction);
            double discriminant =
                occluder.radiusMetres * occluder.radiusMetres - glm::dot(perpendicular, perpendicular);
            if (discriminant >= 0) {
                double half = std::sqrt(discriminant);
                if (t + half > 1e-8 && t - half < end) {
                    blocked = true;
                    break;
                }
            }
        }
        for (const auto& b : scene.occluders) {
            if (exclude && b.entity == exclude)
                continue;
            auto d = b.positionMetres - p;
            double t = glm::dot(d, direction);
            auto perpendicular = glm::cross(d, direction);
            double discriminant = b.radiusMetres * b.radiusMetres - glm::dot(perpendicular, perpendicular);
            if (discriminant < 0)
                continue;
            double half = std::sqrt(discriminant);
            if (t + half > 1e-8 && t - half < end) {
                blocked = true;
                break;
            }
        }
        if (!blocked)
            visible++;
    }
    return visible / samples;
}
} // namespace one
