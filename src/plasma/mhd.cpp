#include <theone/plasma/mhd.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
namespace one {
namespace {
using Q = std::array<double, 8>; // rho, vx/mx, vy/my, vz/mz, p/E, Bx, By, Bz
double limited(double a, double b) {
    return a * b > 0 ? std::copysign(std::min({2 * std::abs(a), 2 * std::abs(b), .5 * std::abs(a + b)}), a)
                     : 0;
}
Q conserved(const Q& w, double gamma) {
    Q u = w;
    double kinetic = 0, magnetic = 0;
    for (int k = 1; k < 4; ++k) {
        u[k] = w[0] * w[k];
        kinetic += w[k] * w[k] * w[0] * .5;
        magnetic += w[k + 4] * w[k + 4] * .5;
    }
    u[4] = w[4] / (gamma - 1) + kinetic + magnetic;
    return u;
}
double wave(const Q& w, int axis, double gamma) {
    double b2 = w[5] * w[5] + w[6] * w[6] + w[7] * w[7], a2 = gamma * w[4] / w[0], v2 = b2 / w[0],
           bn2 = w[5 + axis] * w[5 + axis] / w[0];
    double fast = std::sqrt(.5 * (a2 + v2 + std::sqrt(std::max(0., (a2 + v2) * (a2 + v2) - 4 * a2 * bn2))));
    return std::abs(w[1 + axis]) + fast;
}
Q flux(const Q& w, int axis, double gamma) {
    Q f{}, u = conserved(w, gamma);
    double vn = w[1 + axis], bn = w[5 + axis], b2 = 0, vb = 0;
    for (int k = 0; k < 3; ++k) {
        b2 += w[5 + k] * w[5 + k];
        vb += w[1 + k] * w[5 + k];
    }
    double pt = w[4] + b2 * .5;
    f[0] = w[0] * vn;
    f[4] = (u[4] + pt) * vn - bn * vb;
    for (int k = 0; k < 3; ++k) {
        f[1 + k] = u[1 + k] * vn - bn * w[5 + k] + (axis == k ? pt : 0);
        f[5 + k] = vn * w[5 + k] - w[1 + k] * bn;
    }
    return f;
}
} // namespace
struct PlasmaGrid::Impl {
    PlasmaConfig c;
    int nx, ny, stride;
    double dx, dy, t = 0, injected = 0;
    uint64_t steps = 0, rejected = 0;
    struct State {
        std::vector<Q> u;
        std::vector<double> bx, by;
    };
    State state, stage, trial;
    std::vector<Q> w, sx, sy, fx, fy;
    std::vector<double> electric;
    Impl(const PlasmaConfig& config) : c(config), nx(int(c.nx)), ny(int(c.ny)), stride(nx + 5) {
        auto finite = [](double x) { return std::isfinite(x); };
        if (nx < 4 || ny < 4 || nx > 512 || ny > 512 || !finite(c.gamma) || c.gamma <= 1 || c.gamma > 2 ||
            !finite(c.cfl) || c.cfl <= 0 || c.cfl > .4 || !finite(c.resistivity) || c.resistivity < 0 ||
            !finite(c.gravity) || c.gravity < 0 || !finite(c.stellarRadius) || c.stellarRadius <= 0 ||
            !finite(c.footpointShear) || std::abs(c.footpointShear) > 1 || !finite(c.extent.x) ||
            !finite(c.extent.y) || c.extent.x <= 0 || c.extent.y <= 0 || !finite(c.minimum.x) ||
            !finite(c.minimum.y))
            throw std::invalid_argument("Invalid MHD domain/parameters");
        dx = c.extent.x / nx;
        dy = c.extent.y / ny;
        size_t count = size_t(stride) * (ny + 5);
        for (auto* s : {&state, &stage, &trial}) {
            s->u.resize(count);
            s->bx.resize(count);
            s->by.resize(count);
        }
        w.resize(count);
        sx.resize(count);
        sy.resize(count);
        fx.resize(count);
        fy.resize(count);
        electric.resize(count);
    }
    int at(int i, int j) const { return j * stride + i; }
    glm::dvec2 position(int i, int j) const { return c.minimum + glm::dvec2((i - 1.5) * dx, (j - 1.5) * dy); }
    double gravity(double y) const { return -c.gravity / std::pow(1 + std::max(0., y) / c.stellarRadius, 2); }
    void boundaries(State& s, double time) {
        auto wrap = [](int i, int n) { return 2 + ((i - 2) % n + n) % n; };
        // X is periodic for both test boxes and the bipolar arcade.
        for (int j = 0; j < ny + 5; ++j)
            for (int i = 0; i < nx + 5; ++i) {
                if (i < 2 || i >= nx + 2) {
                    int src = at(wrap(i, nx), j), dst = at(i, j);
                    s.u[dst] = s.u[src];
                    s.bx[dst] = s.bx[src];
                    s.by[dst] = s.by[src];
                }
            }
        if (c.boundary == PlasmaBoundary::Periodic) {
            for (int j = 0; j < ny + 5; ++j)
                if (j < 2 || j >= ny + 2)
                    for (int i = 0; i < nx + 5; ++i) {
                        int dst = at(i, j), src = at(i, wrap(j, ny));
                        s.u[dst] = s.u[src];
                        s.bx[dst] = s.bx[src];
                        s.by[dst] = s.by[src];
                    }
        } else {
            for (int i = 0; i < nx + 5; ++i) {
                for (int j : {0, 1}) {
                    int dst = at(i, j), src = at(i, 3 - j);
                    s.u[dst] = s.u[src];
                    double rho = s.u[src][0];
                    double x = position(i, j).x, drive = c.footpointShear * std::tanh(time * .2) *
                                                         std::sin(2 * 3.141592653589793 * x / c.extent.x) *
                                                         std::exp(-x * x);
                    auto old = s.u[dst];
                    s.u[dst][1] = -old[1];
                    s.u[dst][2] = -old[2];
                    s.u[dst][3] = 2 * rho * drive - old[3];
                    s.u[dst][4] += (s.u[dst][3] * s.u[dst][3] - old[3] * old[3]) / (2 * rho);
                    s.bx[dst] = s.bx[src];
                    s.by[dst] = s.by[at(i, 2)];
                }
                for (int j = ny + 2; j < ny + 5; ++j) {
                    int dst = at(i, j), src = at(i, ny + 1);
                    s.u[dst] = s.u[src];
                    s.bx[dst] = s.bx[src];
                    if (j > ny + 2)
                        s.by[dst] = s.by[at(i, ny + 2)];
                }
            }
        }
    }
    bool primitives(State& s, double time) {
        boundaries(s, time);
        bool valid = true;
        for (int j = 0; j < ny + 4; ++j)
            for (int i = 0; i < nx + 4; ++i) {
                int id = at(i, j);
                Q q = s.u[id];
                double rho = q[0];
                q[5] = .5 * (s.bx[id] + s.bx[at(i + 1, j)]);
                q[6] = .5 * (s.by[id] + s.by[at(i, j + 1)]);
                double kinetic = 0, b2 = 0;
                for (int k = 1; k < 4; ++k) {
                    kinetic += q[k] * q[k] / (2 * rho);
                    q[k] /= rho;
                    b2 += q[k + 4] * q[k + 4];
                }
                q[4] = (c.gamma - 1) * (s.u[id][4] - kinetic - b2 * .5);
                if (c.boundary == PlasmaBoundary::Coronal && (j < 2 || j >= ny + 2)) {
                    int src = at(i, j < 2 ? 3 - j : ny + 1);
                    auto u = s.u[src];
                    double bsx = .5 * (s.bx[src] + s.bx[src + 1]),
                           bsy = .5 * (s.by[src] + s.by[src + stride]);
                    double internal = u[4] - (u[1] * u[1] + u[2] * u[2] + u[3] * u[3]) / (2 * u[0]) -
                                      .5 * (bsx * bsx + bsy * bsy + u[7] * u[7]);
                    q[4] = (c.gamma - 1) * internal;
                    s.u[id][4] = internal + kinetic + b2 * .5;
                }
                w[id] = q;
                if (i >= 2 && i < nx + 2 && j >= 2 && j < ny + 2) {
                    for (double x : q)
                        valid &= std::isfinite(x);
                    valid &= rho > 1e-9 && q[4] > 1e-9;
                }
            }
        return valid;
    }
    Q reconstruct(int id, int axis, double direction) const {
        Q q = w[id];
        const auto& slope = axis == 0 ? sx[id] : sy[id];
        for (int k = 0; k < 8; ++k)
            q[k] += direction * .5 * slope[k];
        return q;
    }
    void slopes() {
        for (int j = 1; j < ny + 3; ++j)
            for (int i = 1; i < nx + 3; ++i) {
                int id = at(i, j);
                for (int k = 0; k < 8; ++k) {
                    sx[id][k] = limited(w[id][k] - w[id - 1][k], w[id + 1][k] - w[id][k]);
                    sy[id][k] = limited(w[id][k] - w[id - stride][k], w[id + stride][k] - w[id][k]);
                }
            }
    }
    glm::dvec3 current(int id) const {
        return {(w[id + stride][7] - w[id - stride][7]) / (2 * dy), -(w[id + 1][7] - w[id - 1][7]) / (2 * dx),
                (w[id + 1][6] - w[id - 1][6]) / (2 * dx) -
                    (w[id + stride][5] - w[id - stride][5]) / (2 * dy)};
    }
    void faceFluxes(State& s) {
        for (int axis = 0; axis < 2; ++axis) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(4) if (nx * ny >= 4096)
#endif
            for (int j = 2; j < ny + 2 + (axis == 1); ++j)
                for (int i = 2; i < nx + 2 + (axis == 0); ++i) {
                    int r = at(i, j), l = r - (axis == 0 ? 1 : stride);
                    Q left = reconstruct(l, axis, 1), right = reconstruct(r, axis, -1);
                    left[5 + axis] = right[5 + axis] = axis == 0 ? s.bx[r] : s.by[r];
                    auto ul = conserved(left, c.gamma), ur = conserved(right, c.gamma),
                         fl = flux(left, axis, c.gamma), fr = flux(right, axis, c.gamma);
                    Q result{};
                    double a = std::max(wave(left, axis, c.gamma), wave(right, axis, c.gamma));
                    for (int k = 0; k < 8; ++k)
                        result[k] = .5 * (fl[k] + fr[k] - a * (ur[k] - ul[k]));
                    if (c.resistivity > 0) {
                        auto jac = .5 * (current(l) + current(r));
                        if (axis == 0) {
                            jac.y = -(w[r][7] - w[l][7]) / dx;
                            jac.z = (w[r][6] - w[l][6]) / dx - .5 *
                                                                   ((w[r + stride][5] - w[r - stride][5]) +
                                                                    (w[l + stride][5] - w[l - stride][5])) /
                                                                   (2 * dy);
                        } else {
                            jac.x = (w[r][7] - w[l][7]) / dy;
                            jac.z =
                                .5 * ((w[r + 1][6] - w[r - 1][6]) + (w[l + 1][6] - w[l - 1][6])) / (2 * dx) -
                                (w[r][5] - w[l][5]) / dy;
                        }
                        glm::dvec3 b{(left[5] + right[5]) * .5, (left[6] + right[6]) * .5,
                                     (left[7] + right[7]) * .5};
                        result[4] += c.resistivity * glm::cross(jac, b)[axis];
                        result[7] -= c.resistivity * (w[r][7] - w[l][7]) / (axis == 0 ? dx : dy);
                    }
                    (axis == 0 ? fx : fy)[r] = result;
                }
        }
    }
    void emf(State& s) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(4) if (nx * ny >= 4096)
#endif
        for (int j = 2; j <= ny + 2; ++j)
            for (int i = 2; i <= nx + 2; ++i) {
                int id = at(i, j);
                auto faceSlope = [&](const std::vector<double>& b, int q, int d) {
                    return limited(b[q] - b[q - d], b[q + d] - b[q]);
                };
                double bx0 = s.bx[id - stride] + .5 * faceSlope(s.bx, id - stride, stride),
                       bx1 = s.bx[id] - .5 * faceSlope(s.bx, id, stride);
                double by0 = s.by[id - 1] + .5 * faceSlope(s.by, id - 1, 1),
                       by1 = s.by[id] - .5 * faceSlope(s.by, id, 1);
                double e = 0, ax = 0, ay = 0;
                for (int oy = 0; oy < 2; ++oy)
                    for (int ox = 0; ox < 2; ++ox) {
                        int q = at(i - 1 + ox, j - 1 + oy);
                        Q corner = w[q];
                        for (int k = 0; k < 8; ++k)
                            corner[k] += (ox ? -.5 : .5) * sx[q][k] + (oy ? -.5 : .5) * sy[q][k];
                        if (corner[0] <= 0 || corner[4] <= 0)
                            corner = w[q];
                        corner[5] = oy ? bx1 : bx0;
                        corner[6] = ox ? by1 : by0;
                        e += .25 * (corner[2] * corner[5] - corner[1] * corner[6]);
                        ax = std::max(ax, wave(corner, 0, c.gamma));
                        ay = std::max(ay, wave(corner, 1, c.gamma));
                    }
                e += .5 * ax * (by1 - by0) - .5 * ay * (bx1 - bx0);
                e += c.resistivity * ((s.by[id] - s.by[id - 1]) / dx - (s.bx[id] - s.bx[id - stride]) / dy);
                if (c.boundary == PlasmaBoundary::Coronal && j == 2)
                    e = 0; // line-tied normal flux
                electric[id] = e;
            }
    }
    bool euler(State& input, State& output, double dt, double time) {
        if (!primitives(input, time))
            return false;
        slopes();
        faceFluxes(input);
        emf(input);
        output = input;
        for (int j = 2; j < ny + 2; ++j)
            for (int i = 2; i < nx + 2; ++i) {
                int id = at(i, j);
                for (int k = 0; k < 8; ++k)
                    output.u[id][k] -=
                        dt * ((fx[id + 1][k] - fx[id][k]) / dx + (fy[id + stride][k] - fy[id][k]) / dy);
                double g = gravity(position(i, j).y);
                output.u[id][2] += dt * input.u[id][0] * g;
                output.u[id][4] += dt * input.u[id][2] * g;
            }
        for (int j = 2; j < ny + 2; ++j)
            for (int i = 2; i <= nx + 2; ++i) {
                int id = at(i, j);
                output.bx[id] -= dt * (electric[id + stride] - electric[id]) / dy;
            }
        for (int j = 2; j <= ny + 2; ++j)
            for (int i = 2; i < nx + 2; ++i) {
                int id = at(i, j);
                output.by[id] += dt * (electric[id + 1] - electric[id]) / dx;
            }
        return primitives(output, time + dt);
    }
};
PlasmaGrid::PlasmaGrid(const PlasmaConfig& c) : impl_(std::make_unique<Impl>(c)) {}
PlasmaGrid::~PlasmaGrid() = default;
PlasmaGrid::PlasmaGrid(PlasmaGrid&&) noexcept = default;
PlasmaGrid& PlasmaGrid::operator=(PlasmaGrid&&) noexcept = default;
void PlasmaGrid::initialize(const std::function<double(glm::dvec2)>& az,
                            const std::function<PlasmaPrimitive(glm::dvec2)>& gas) {
    auto& p = *impl_;
    p.t = p.injected = 0;
    p.steps = p.rejected = 0;
    for (int j = 0; j < p.ny + 5; ++j)
        for (int i = 0; i < p.nx + 5; ++i) {
            int id = p.at(i, j);
            auto corner = p.c.minimum + glm::dvec2((i - 2) * p.dx, (j - 2) * p.dy);
            p.state.bx[id] = (az(corner + glm::dvec2(0, p.dy)) - az(corner)) / p.dy;
            p.state.by[id] = -(az(corner + glm::dvec2(p.dx, 0)) - az(corner)) / p.dx;
        }
    for (int j = 0; j < p.ny + 4; ++j)
        for (int i = 0; i < p.nx + 4; ++i) {
            int id = p.at(i, j);
            auto w = gas(p.position(i, j));
            Q q{w.density,
                w.velocity.x,
                w.velocity.y,
                w.velocity.z,
                w.pressure,
                .5 * (p.state.bx[id] + p.state.bx[id + 1]),
                .5 * (p.state.by[id] + p.state.by[id + p.stride]),
                w.magnetic.z};
            if (q[0] <= 0 || q[4] <= 0)
                throw std::invalid_argument("Nonpositive initial MHD gas");
            p.state.u[id] = conserved(q, p.c.gamma);
        }
    if (!p.primitives(p.state, 0))
        throw std::invalid_argument("Nonfinite initial MHD state");
}
double PlasmaGrid::step(double maximumDt) {
    auto& p = *impl_;
    if (!std::isfinite(maximumDt) || maximumDt <= 0)
        throw std::invalid_argument("Invalid MHD timestep");
    if (!p.primitives(p.state, p.t))
        throw std::runtime_error("Invalid MHD state");
    double rate = 0;
    for (int j = 2; j < p.ny + 2; ++j)
        for (int i = 2; i < p.nx + 2; ++i) {
            auto& q = p.w[p.at(i, j)];
            rate = std::max(rate, wave(q, 0, p.c.gamma) / p.dx + wave(q, 1, p.c.gamma) / p.dy);
        }
    rate += 2 * p.c.resistivity * (1 / (p.dx * p.dx) + 1 / (p.dy * p.dy));
    double dt = std::min(maximumDt, p.c.cfl / std::max(rate, 1e-20));
    for (int retry = 0; retry < 14; ++retry) {
        if (p.euler(p.state, p.stage, dt, p.t) && p.euler(p.stage, p.trial, dt, p.t + dt)) {
            for (size_t i = 0; i < p.state.u.size(); ++i) {
                for (int k = 0; k < 8; ++k)
                    p.trial.u[i][k] = .5 * (p.state.u[i][k] + p.trial.u[i][k]);
                p.trial.bx[i] = .5 * (p.state.bx[i] + p.trial.bx[i]);
                p.trial.by[i] = .5 * (p.state.by[i] + p.trial.by[i]);
            }
            if (p.primitives(p.trial, p.t + dt)) {
                std::swap(p.state, p.trial);
                p.t += dt;
                ++p.steps;
                return dt;
            }
        }
        ++p.rejected;
        dt *= .5;
    }
    throw std::runtime_error("MHD positivity timestep retries exhausted");
}
double PlasmaGrid::time() const {
    return impl_->t;
}
PlasmaPrimitive PlasmaGrid::cell(uint32_t x, uint32_t y) const {
    const auto& p = *impl_;
    if (x >= uint32_t(p.nx) || y >= uint32_t(p.ny))
        throw std::out_of_range("MHD cell");
    int id = p.at(int(x) + 2, int(y) + 2);
    auto u = p.state.u[id];
    PlasmaPrimitive q;
    q.density = u[0];
    q.velocity = {u[1] / u[0], u[2] / u[0], u[3] / u[0]};
    q.magnetic = {.5 * (p.state.bx[id] + p.state.bx[id + 1]),
                  .5 * (p.state.by[id] + p.state.by[id + p.stride]), u[7]};
    q.pressure = (p.c.gamma - 1) * (u[4] - .5 * u[0] * glm::dot(q.velocity, q.velocity) -
                                    .5 * glm::dot(q.magnetic, q.magnetic));
    return q;
}
PlasmaDiagnostics PlasmaGrid::diagnostics() const {
    const auto& p = *impl_;
    PlasmaDiagnostics d;
    d.time = p.t;
    d.steps = p.steps;
    d.rejectedSteps = p.rejected;
    d.injectedEnergy = p.injected;
    d.minimumDensity = d.minimumPressure = std::numeric_limits<double>::max();
    double maxB = 0;
    for (int j = 0; j < p.ny; ++j)
        for (int i = 0; i < p.nx; ++i) {
            int id = p.at(i + 2, j + 2);
            auto q = cell(i, j);
            double area = p.dx * p.dy;
            d.mass += q.density * area;
            d.totalEnergy += p.state.u[id][4] * area;
            d.magneticEnergy += glm::dot(q.magnetic, q.magnetic) * .5 * area;
            d.kineticEnergy += glm::dot(q.velocity, q.velocity) * q.density * .5 * area;
            d.minimumDensity = std::min(d.minimumDensity, q.density);
            d.minimumPressure = std::min(d.minimumPressure, q.pressure);
            maxB = std::max(maxB, glm::length(q.magnetic));
            d.maximumDivergence =
                std::max(d.maximumDivergence, std::abs((p.state.bx[id + 1] - p.state.bx[id]) / p.dx +
                                                       (p.state.by[id + p.stride] - p.state.by[id]) / p.dy));
        }
    d.relativeDivergence = d.maximumDivergence * std::min(p.dx, p.dy) / std::max(maxB, 1e-30);
    return d;
}
void PlasmaGrid::injectGuideField(glm::dvec2 centre, double radius, double amplitude) {
    auto& p = *impl_;
    if (!std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(amplitude))
        throw std::invalid_argument("Invalid magnetic injection");
    for (int j = 2; j < p.ny + 2; ++j)
        for (int i = 2; i < p.nx + 2; ++i) {
            int id = p.at(i, j);
            auto delta = (p.position(i, j) - centre) / radius;
            double old = p.state.u[id][7], b = old + amplitude * std::exp(-glm::dot(delta, delta));
            double work = .5 * (b * b - old * old);
            p.state.u[id][7] = b;
            p.state.u[id][4] += work;
            p.injected += work * p.dx * p.dy;
        }
}
} // namespace one
