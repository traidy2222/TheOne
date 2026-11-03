#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace one;
static void need(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
static std::vector<unsigned char> pixels() {
    std::ifstream in("capture.ppm", std::ios::binary);
    std::string magic;
    int w, h, max;
    in >> magic >> w >> h >> max;
    in.get();
    need(magic == "P6" && w == 160 && h == 120 && max == 255, "Invalid volume capture");
    std::vector<unsigned char> p(size_t(w) * h * 3);
    in.read(reinterpret_cast<char*>(p.data()), p.size());
    need(bool(in), "Truncated capture");
    return p;
}
static double display(double x) {
    x = std::clamp((x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14), 0., 1.);
    return x <= .0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - .055;
}
int main() {
    try {
        need(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window = SDL_CreateWindow("Volume correctness", 160, 120,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        need(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } wg{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        need(ImGui_ImplSDL3_InitForVulkan(window), "UI init");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        Renderer renderer;
        renderer.initialize(window, true);
        renderer.enableHdr(true);
        renderer.setVsync(false);
        renderer.setShadowDistance(30);
        Camera camera;
        camera.position = {0, 2, 6};
        camera.yaw = 3.14159265358979f;
        camera.pitch = 0;
        Material white;
        white.unlit = true;
        white.color = {1, 1, 1, 1};
        std::vector<Vertex> vertices;
        std::vector<Draw> draws;
        uint64_t revision = 0;
        auto plane = [&](float z) {
            ++revision;
            vertices.clear();
            for (auto p : {glm::vec3(-20, -20, z), glm::vec3(20, -20, z), glm::vec3(20, 20, z),
                           glm::vec3(-20, -20, z), glm::vec3(20, 20, z), glm::vec3(-20, 20, z)})
                vertices.push_back({p, {0, 0, 1}, {1, 1, 1, 1}, {0, 0}});
            draws = {{0, 6, &white, nullptr, 1}};
        };
        plane(-6);
        auto render = [&](bool capture, bool marker = false) {
            SDL_PumpEvents();
            need(renderer.beginFrame(), "No volume frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (marker)
                ImGui::GetForegroundDrawList()->AddRectFilled({2, 2}, {14, 14}, IM_COL32(30, 180, 60, 255));
            ImGui::Render();
            need(renderer.renderCached(vertices, draws, revision, camera, 0, capture, false),
                 "Volume render failed");
        };
        auto settle = [&](bool marker = false) {
            for (int i = 0; i < 5; ++i)
                render(false, marker);
            render(true, marker);
            return pixels();
        };
        auto vacuum = settle(true);
        VolumeScene fog;
        fog.domain.origin = {-2, 0, -2};
        fog.domain.extent = {4, 4, 4};
        fog.domain.resolution = {16, 16, 16};
        fog.initialSootKgPerM3 = .4f / fog.fuel.absorption;
        fog.fuel.scattering = 0;
        renderer.setVolumeScene(fog);
        auto slab = settle(true);
        size_t center = (60 * 160 + 80) * 3;
        double expected = display(std::exp(-.4 * 4)) * 255;
        std::cout << "SLAB pixel=" << int(slab[center]) << " expected=" << expected
                  << " vacuum=" << int(vacuum[center]) << '\n';
        need(std::abs(slab[center] - expected) < 3, "GPU homogeneous slab disagrees with analytic transport");
        for (int a = 0; a < 3; ++a)
            need(slab[(5 * 160 + 5) * 3 + a] == vacuum[(5 * 160 + 5) * 3 + a], "HDR volume altered UI");
        fog.rayStepScale = .5f;
        renderer.setVolumeScene(fog);
        auto fine = settle();
        need(std::abs(int(fine[center]) - int(slab[center])) <= 1,
             "Volume opacity changes with sample spacing");
        Material transparent = white;
        transparent.blend = true;
        transparent.color.a = .5f;
        auto transparentPlane = [&](float z) {
            uint32_t start = uint32_t(vertices.size());
            for (auto p : {glm::vec3(-20, -20, z), glm::vec3(20, -20, z), glm::vec3(20, 20, z),
                           glm::vec3(-20, -20, z), glm::vec3(20, 20, z), glm::vec3(-20, 20, z)})
                vertices.push_back({p, {0, 0, 1}, {1, 1, 1, 1}, {0, 0}});
            draws.push_back({start, 6, &transparent, nullptr, 2});
            ++revision;
        };
        plane(-6);
        transparentPlane(0);
        auto alpha = settle();
        expected = display(.5 * std::exp(-.4 * 2) + .5 * std::exp(-.4 * 4)) * 255;
        std::cout << "TRANSPARENT pixel=" << int(alpha[center]) << " expected=" << expected << '\n';
        need(std::abs(alpha[center] - expected) < 3,
             "Transparent surface did not integrate its volume prefix");
        plane(3);
        auto foreground = settle();
        need(std::abs(int(foreground[center]) - int(vacuum[center])) <= 1,
             "Foreground opaque surface did not stop the volume ray");
        plane(-6);
        camera.position.z = 0;
        auto inside = settle();
        expected = display(std::exp(-.4 * 2)) * 255;
        std::cout << "INSIDE pixel=" << int(inside[center]) << " expected=" << expected << '\n';
        need(std::abs(inside[center] - expected) < 3, "Camera-inside path length is incorrect");
        camera.position.z = 6;

        plane(-6);
        white.color = {.02f, .02f, .02f, 1};
        VolumeScene lit = fog;
        lit.generation = 20;
        lit.fuel.scattering = lit.fuel.absorption * .3f;
        lit.fuel.phaseG = .65f;
        SceneLighting sunlight;
        sunlight.skyRadiance = {0, 0, 0};
        sunlight.sunRadiance = {2, 2, 2};
        sunlight.sunDirection = {0, 0, 1};
        renderer.setLighting(sunlight);
        renderer.setVolumeScene(lit);
        auto frontlight = settle();
        sunlight.sunDirection = {0, 0, -1};
        renderer.setLighting(sunlight);
        auto backlight = settle();
        std::cout << "PHASE front=" << int(frontlight[center]) << " back=" << int(backlight[center]) << '\n';
        need(backlight[center] > frontlight[center] + 10, "Directional phase/self-shadow response failed");
        sunlight.sunRadiance = {0, 0, 0};
        renderer.setLighting(sunlight);
        auto cold = settle();
        lit.generation++;
        lit.initialKelvin = 1400;
        renderer.setVolumeScene(lit);
        auto hot = settle();
        std::cout << "THERMAL cold=" << int(cold[center]) << " hot=" << int(hot[center]) << '\n';
        need(hot[center] > cold[center] + 30, "Temperature did not drive visible emission");
        white.color = {1, 1, 1, 1};
        renderer.setLighting(SceneLighting{});
        renderer.setVolumeScene(fog);
        auto before = renderer.captureVolumeState();
        SDL_SetWindowSize(window, 176, 128);
        renderer.resize();
        render(false);
        SDL_SetWindowSize(window, 160, 120);
        renderer.resize();
        settle();
        auto after = renderer.captureVolumeState();
        need(std::abs(after.stats.sootKg - before.stats.sootKg) < 1e-6,
             "Resize restarted or lost the medium");
        VolumeScene reactive;
        reactive.generation = 2;
        reactive.domain.origin = {-3, 0, -3};
        reactive.domain.extent = {6, 6, 6};
        reactive.domain.resolution = {12, 12, 12};
        reactive.domain.closedBoundary = true;
        reactive.domain.gravity = {0, 0, 0};
        reactive.domain.coolingRate = 0;
        reactive.domain.maximumVelocity = 2;
        reactive.initialKelvin = 1000;
        CombustionSource source;
        source.position = {0, 2, 0};
        source.radius = 1.25f;
        source.fuelKgPerSecond = .1f / .12f;
        source.durationSeconds = .12f;
        source.heatWatts = 0;
        source.velocity = {0, 0, 0};
        reactive.sources = {source};
        renderer.setVolumeScene(reactive);
        render(false);
        auto initial = renderer.captureVolumeState();
        for (int frame = 1; frame <= 30; ++frame) {
            reactive.timeSeconds = frame / 60.;
            renderer.setVolumeScene(reactive);
            render(false);
        }
        auto result = renderer.captureVolumeState();
        auto s = result.stats;
        double consumed = s.burnedFuelKg;
        double initialMass = initial.stats.oxygenKg + initial.stats.nitrogenKg;
        double finalMass = s.fuelKg + s.oxygenKg + s.sootKg + s.productsKg + s.nitrogenKg;
        double massError = std::abs(finalMass - initialMass - s.injectedFuelKg);
        double fuelError = std::abs(s.fuelKg + consumed - s.injectedFuelKg);
        double oxygenError =
            std::abs(s.oxygenKg + consumed * reactive.fuel.oxygenRatio - initial.stats.oxygenKg);
        double heatExpected =
            s.injectedHeatJ +
            s.injectedFuelKg * reactive.domain.heatCapacity * reactive.domain.ambientKelvin +
            consumed * reactive.fuel.heatRelease - s.lostHeatJ - s.boundaryEnthalpyJ;
        double heatError = std::abs(s.enthalpyJ - initial.stats.enthalpyJ - heatExpected);
        std::cout << "BUDGET injected=" << s.injectedFuelKg << " consumed=" << consumed
                  << " soot=" << s.sootKg << " mass_error=" << massError << " fuel_error=" << fuelError
                  << " oxygen_error=" << oxygenError << " heat_error=" << heatError
                  << " heat_expected=" << heatExpected << " clamps=" << s.velocityClamps << '\n';
        need(consumed > .001 && s.sootKg > 0, "Fuel did not react into smoke");
        need(massError < .005 && fuelError < .00005 && oxygenError < .001, "GPU mass/species budgets failed");
        need(std::abs(s.sootKg - consumed * reactive.fuel.sootYield) < .00005, "Soot yield is inconsistent");
        need(heatError < std::max(1000., std::abs(heatExpected) * .01), "GPU enthalpy ledger failed");

        auto centroid = [](const VolumeReadback& field) {
            glm::dvec3 sum(0);
            double mass = 0;
            auto d = field.domain;
            auto h = d.extent / glm::vec3(d.resolution);
            for (uint32_t z = 0; z < d.resolution.z; ++z)
                for (uint32_t y = 0; y < d.resolution.y; ++y)
                    for (uint32_t x = 0; x < d.resolution.x; ++x) {
                        const auto& c = field.cells[(z * d.resolution.y + y) * d.resolution.x + x];
                        double w = c.species.z;
                        sum += glm::dvec3(d.origin + (glm::vec3(x, y, z) + .5f) * h) * w;
                        mass += w;
                    }
            return sum / std::max(1e-20, mass);
        };
        auto evolve = [&](VolumeScene scene, int frames, double duration) {
            renderer.setVolumeScene(scene);
            render(false);
            for (int frame = 1; frame <= frames; ++frame) {
                scene.timeSeconds = duration * frame / frames;
                renderer.setVolumeScene(scene);
                render(false);
            }
            return renderer.captureVolumeState();
        };
        VolumeScene flow;
        flow.generation = 10;
        flow.domain.origin = {-3, 0, -3};
        flow.domain.extent = {6, 6, 6};
        flow.domain.resolution = {16, 16, 16};
        flow.domain.maximumVelocity = 4;
        flow.domain.maximumSubsteps = 24;
        flow.domain.gravity = {0, 0, 0};
        flow.domain.coolingRate = 0;
        flow.domain.vorticityConfinement = 0;
        CombustionSource smoke;
        smoke.position = {-1.2f, 2.5f, 0};
        smoke.radius = .7f;
        smoke.fuelKgPerSecond = 0;
        smoke.sootKgPerSecond = .1f;
        smoke.heatWatts = 0;
        smoke.durationSeconds = .1f;
        smoke.velocity = {0, 0, 0};
        flow.sources = {smoke};
        auto still = evolve(flow, 18, .6);
        flow.generation++;
        flow.domain.wind = {1.5f, 0, 0};
        auto wind = evolve(flow, 18, .6);
        auto stillCenter = centroid(still), windCenter = centroid(wind);
        std::cout << "WIND still=" << stillCenter.x << " wind=" << windCenter.x
                  << " displacement=" << windCenter.x - stillCenter.x << '\n';
        need(windCenter.x - stillCenter.x > .6 && windCenter.x - stillCenter.x < 1.1,
             "Wind did not transport smoke at the physical speed");
        flow.generation++;
        auto cadence = evolve(flow, 36, .6);
        double cadenceError = 0, weight = 0;
        for (size_t i = 0; i < wind.cells.size(); ++i) {
            cadenceError += std::abs(wind.cells[i].species.z - cadence.cells[i].species.z);
            weight += std::abs(wind.cells[i].species.z);
        }
        std::cout << "CADENCE relative_soot_error=" << cadenceError / std::max(1e-20, weight) << '\n';
        need(cadenceError / std::max(1e-20, weight) < .03,
             "Render cadence changed the simulation excessively");
        flow.generation++;
        flow.obstacles = {{{0, 0, -3}, {.3f, 6, 3}, {0, 0, 0}}};
        auto wall = evolve(flow, 30, 1.0);
        double leaked = 0;
        for (uint32_t z = 0; z < 16; ++z)
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 9; x < 16; ++x)
                    leaked += wall.cells[(z * 16 + y) * 16 + x].species.z;
        std::cout << "WALL leaked_density=" << leaked << '\n';
        need(leaked < 1e-9, "Conservative transport crossed a solid wall");

        VolumeScene turbulent = flow;
        turbulent.generation = 24;
        turbulent.obstacles.clear();
        turbulent.sources.clear();
        turbulent.timeSeconds = 0;
        turbulent.domain.wind = {0, 0, 0};
        turbulent.domain.initialTurbulenceRms = .8f;
        renderer.setVolumeScene(turbulent);
        render(false);
        auto air = renderer.captureVolumeState();
        double initialDiv = 0, kinetic = 0;
        auto cell = [&](int x, int y, int z) -> const VolumeCell& {
            return air.cells[(z * 16 + y) * 16 + x];
        };
        for (int z = 2; z < 14; ++z)
            for (int y = 2; y < 14; ++y)
                for (int x = 2; x < 14; ++x) {
                    const auto& c = cell(x, y, z);
                    double div =
                        (c.velocity.x - cell(x - 1, y, z).velocity.x + c.velocity.y -
                         cell(x, y - 1, z).velocity.y + c.velocity.z - cell(x, y, z - 1).velocity.z) /
                        .375;
                    initialDiv = std::max(initialDiv, std::abs(div));
                    kinetic += glm::dot(glm::vec3(c.velocity), glm::vec3(c.velocity));
                }
        std::cout << "INITIAL_AIR interior_divergence=" << initialDiv << " kinetic_sum=" << kinetic << '\n';
        need(initialDiv < 1e-5 && kinetic > 100,
             "Initial air motion is not resolved divergence-free velocity");

        VolumeScene buoyant = flow;
        buoyant.generation = 25;
        buoyant.obstacles.clear();
        buoyant.domain.wind = {0, 0, 0};
        buoyant.sources[0].position = {0, 1.5f, 0};
        buoyant.sources[0].heatWatts = 400000;
        buoyant.sources[0].durationSeconds = .3f;
        auto weightless = evolve(buoyant, 36, 1.2);
        buoyant.generation++;
        buoyant.domain.gravity = {0, -9.81f, 0};
        auto rising = evolve(buoyant, 36, 1.2);
        double rise = centroid(rising).y - centroid(weightless).y;
        double outflowError =
            std::abs(rising.stats.sootKg + rising.stats.boundarySpeciesKg.z - rising.stats.injectedSootKg);
        std::cout << "BUOYANCY rise=" << rise << " soot_outflow_error=" << outflowError
                  << " divergence=" << rising.stats.initialDivergenceError << " -> "
                  << rising.stats.maximumDivergenceError << '\n';
        need(rise > .1, "Gravity did not change the hot-smoke trajectory");
        need(outflowError < .00001, "Open-boundary soot ledger did not close");
        for (const auto& cell : rising.cells)
            need(glm::all(glm::greaterThanEqual(cell.species, glm::vec4(0))) && std::isfinite(cell.thermal.y),
                 "Nonfinite or negative transported field");

        VolumeScene front;
        front.generation = 30;
        front.domain = reactive.domain;
        front.domain.maximumVelocity = 6;
        front.domain.resolution = {24, 24, 24};
        front.initialKelvin = 0;
        front.domain.vorticityConfinement = 0;
        addFuelExplosion(front, {0, 2, 0}, .12f, 1.f);
        for (auto& source : front.sources)
            source.velocity = {0, 0, 0};
        front.fuel.flameSpeed = 0;
        auto pocket = evolve(front, 36, 1.2);
        front.generation++;
        front.fuel.flameSpeed = .6f;
        auto propagated = evolve(front, 36, 1.2);
        std::cout << "FRONT pocket_burn=" << pocket.stats.burnedFuelKg
                  << " propagated_burn=" << propagated.stats.burnedFuelKg << '\n';
        need(propagated.stats.burnedFuelKg > pocket.stats.burnedFuelKg + .005,
             "Combustion front did not propagate through fuel");
        VolumeScene moving = flow;
        moving.generation = 40;
        moving.sources.clear();
        moving.domain.wind = {0, 0, 0};
        moving.domain.closedBoundary = true;
        moving.initialSootKgPerM3 = .0001f;
        moving.obstacles = {{{-.5f, 1, -.5f}, {.5f, 2, .5f}, {0, 0, 0}}};
        renderer.setVolumeScene(moving);
        render(false);
        auto solidBefore = renderer.captureVolumeState();
        moving.obstacles[0].minimum.x += .4f;
        moving.obstacles[0].maximum.x += .4f;
        moving.obstacles[0].velocity.x = .5f;
        renderer.setVolumeScene(moving);
        render(false);
        auto solidAfter = renderer.captureVolumeState();
        std::cout << "MOVING soot_before=" << solidBefore.stats.sootKg
                  << " soot_after=" << solidAfter.stats.sootKg << '\n';
        need(std::abs(solidBefore.stats.sootKg - solidAfter.stats.sootKg) < 1e-7,
             "Moving voxel obstacle discarded smoke mass");
        renderer.clearVolumes();
        renderer.enableHdr(false);
        settle();
        renderer.enableHdr(true);
        renderer.setVolumeScene(fog);
        auto twoSlots = settle();
        renderer.setFramesInFlight(1);
        auto oneSlot = settle();
        need(std::abs(int(twoSlots[center]) - int(oneSlot[center])) <= 1,
             "Frame-slot count changed static volume transport");
        renderer.close();
        need(renderer.errors() == 0, "Volume synchronization validation failed");
        std::cout
            << "PASS HDR/slab/depth/camera-inside/UI/resize, chemistry budgets, both slots and teardown\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
