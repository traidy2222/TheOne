#include <theone/plasma/mhd.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace one;
constexpr double pi = 3.141592653589793;
void need(bool yes, const char* message) {
    if (!yes)
        throw std::runtime_error(message);
}
void run(PlasmaGrid& g, double until) {
    while (g.time() < until - 1e-13)
        g.step(until - g.time());
}
double alfven(int n) {
    PlasmaConfig c;
    c.nx = n;
    c.ny = 4;
    c.minimum = {0, 0};
    c.extent = {1, 1};
    c.boundary = PlasmaBoundary::Periodic;
    c.resistivity = 0;
    PlasmaGrid g(c);
    constexpr double a = .02;
    g.initialize([](glm::dvec2 p) { return p.y + a * std::cos(2 * pi * p.x) / (2 * pi); },
                 [](glm::dvec2 p) {
                     PlasmaPrimitive q;
                     q.pressure = .2;
                     q.magnetic.z = a * std::cos(2 * pi * p.x);
                     q.velocity = {0, -a * std::sin(2 * pi * p.x), -q.magnetic.z};
                     return q;
                 });
    auto initial = g.diagnostics();
    run(g, .2);
    auto d = g.diagnostics();
    double e = 0;
    for (int i = 0; i < n; ++i) {
        auto q = g.cell(i, 0);
        double exact = a * std::sin(2 * pi * ((i + .5) / n - .2));
        e += std::abs(q.magnetic.y - exact) / n;
    }
    need(std::abs(d.totalEnergy / initial.totalEnergy - 1) < 1e-10, "Alfven energy not conserved");
    need(d.relativeDivergence < 1e-12, "CT lost divergence constraint");
    std::cout << "ALFVEN n=" << n << " L1=" << e << " rejected=" << d.rejectedSteps << '\n';
    return e;
}
int main() {
    try {
        PlasmaConfig c;
        c.nx = c.ny = 12;
        c.minimum = {0, 0};
        c.extent = {1, 1};
        c.boundary = PlasmaBoundary::Periodic;
        c.resistivity = 0;
        PlasmaGrid uniform(c);
        uniform.initialize([](glm::dvec2 p) { return .5 * p.y - .3 * p.x; },
                           [](glm::dvec2) {
                               PlasmaPrimitive q;
                               q.magnetic.z = .2;
                               return q;
                           });
        auto old = uniform.diagnostics();
        run(uniform, .2);
        auto d = uniform.diagnostics();
        need(std::abs(d.totalEnergy / old.totalEnergy - 1) < 1e-12 &&
                 glm::length(uniform.cell(6, 6).velocity) < 1e-12,
             "Uniform MHD state changed");
        double coarse = alfven(32), fine = alfven(64);
        need(coarse / fine > 2.3 && fine < .001, "Alfven propagation failed spatial convergence");
        c.nx = c.ny = 40;
        PlasmaGrid vortex(c);
        vortex.initialize(
            [](glm::dvec2 p) {
                return (std::cos(2 * pi * p.y) / (2 * pi) + std::cos(4 * pi * p.x) / (4 * pi)) /
                       std::sqrt(4 * pi);
            },
            [](glm::dvec2 p) {
                PlasmaPrimitive q;
                q.density = 25 / (36 * pi);
                q.pressure = 5 / (12 * pi);
                q.velocity = {-std::sin(2 * pi * p.y), std::sin(2 * pi * p.x), 0};
                return q;
            });
        old = vortex.diagnostics();
        run(vortex, .15);
        d = vortex.diagnostics();
        need(d.minimumDensity > 0 && d.minimumPressure > 0, "Orszag Tang positivity failed");
        need(std::abs(d.mass / old.mass - 1) < 1e-10 && std::abs(d.totalEnergy / old.totalEnergy - 1) < 1e-10,
             "Orszag Tang conservation failed");
        need(d.relativeDivergence < 1e-12, "Orszag Tang divergence failed");
        need(d.kineticEnergy < old.kineticEnergy * .98, "No nonlinear magnetic energy exchange");
        std::cout << "VORTEX steps=" << d.steps << " divB=" << d.relativeDivergence
                  << " minP=" << d.minimumPressure << '\n';
        c.nx = 64;
        c.ny = 4;
        c.resistivity = .01;
        PlasmaGrid resistive(c);
        resistive.initialize([](glm::dvec2 p) { return -std::sin(2 * pi * p.x) / (2 * pi); },
                             [](glm::dvec2 p) {
                                 PlasmaPrimitive q;
                                 q.magnetic.z = std::sin(2 * pi * p.x);
                                 return q;
                             });
        old = resistive.diagnostics();
        run(resistive, .1);
        d = resistive.diagnostics();
        need(d.magneticEnergy < old.magneticEnergy * .95, "Resistivity did not dissipate magnetic energy");
        need(std::abs(d.totalEnergy / old.totalEnergy - 1) < 1e-10,
             "Ohmic energy was lost rather than heated");
        need(resistive.cell(0, 0).pressure > 1, "Ohmic dissipation did not heat gas");
        auto before = resistive.diagnostics();
        resistive.injectGuideField({.5, .5}, .1, 2);
        auto after = resistive.diagnostics();
        need(std::abs((after.totalEnergy - before.totalEnergy) -
                      (after.injectedEnergy - before.injectedEnergy)) < 1e-10,
             "Magnetic work budget mismatch");
        need(after.relativeDivergence < 1e-12, "Guide injection introduced magnetic monopoles");
        std::cout
            << "PASS MHD uniform, Alfven convergence, Orszag Tang, CT, Ohmic heating and injected work\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
