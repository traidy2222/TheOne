#include <theone/render/renderer.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
    need(magic == "P6" && w == 160 && h == 120 && max == 255, "Invalid stellar capture");
    std::vector<unsigned char> data(w * h * 3);
    in.read(reinterpret_cast<char*>(data.data()), data.size());
    need(bool(in), "Truncated stellar capture");
    return data;
}
static double display(double x) {
    x = std::clamp((x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14), 0., 1.);
    return x <= .0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - .055;
}
static double linear(unsigned char p) {
    double lo = 0, hi = 10;
    for (int i = 0; i < 60; ++i) {
        double mid = (lo + hi) * .5;
        if (display(mid) * 255 < p)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) * .5;
}
int main() {
    try {
        need(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window =
            SDL_CreateWindow("Stellar correctness", 160, 120, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        need(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } wg{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        need(ImGui_ImplSDL3_InitForVulkan(window), "UI init failed");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        Renderer renderer;
        renderer.initialize(window, true);
        renderer.setVsync(false);
        SceneLighting lighting;
        // Keep legacy defaults nonzero: stellar mode itself must suppress them.
        lighting.exposure = .00001f;
        renderer.setLighting(lighting);
        Camera camera;
        camera.position = {0, 2, 6};
        camera.yaw = 3.14159265358979f;
        camera.pitch = 0;
        Material matte;
        matte.color = {.4f, .4f, .4f, 1};
        matte.roughness = 1;
        std::vector<Vertex> vertices;
        for (auto p : {glm::vec3(-20, -20, 0), glm::vec3(20, -20, 0), glm::vec3(20, 20, 0),
                       glm::vec3(-20, -20, 0), glm::vec3(20, 20, 0), glm::vec3(-20, 20, 0)})
            vertices.push_back({p, {0, 0, 1}, {1, 1, 1, 1}, {0, 0}});
        std::vector<Draw> draws{{0, 6, &matte, nullptr, 1}};
        uint64_t revision = 1;
        StellarScene scene;
        scene.stars = {StellarEmitter{{0, 2, 12}, 1, 5772, 1}};
        auto capture = [&] {
            renderer.setStellarScene(scene);
            for (int f = 0; f < 6; ++f) {
                SDL_PumpEvents();
                need(renderer.beginFrame(), "Frame unavailable");
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                ImGui::Render();
                need(renderer.renderCached(vertices, draws, revision, camera, 0, f == 5, false),
                     "Stellar render failed");
            }
            return pixels();
        };
        size_t center = (60 * 160 + 80) * 3;
        auto near = capture();
        scene.stars[0].positionMetres.z = 24;
        auto far = capture();
        double ratio = linear(near[center]) / linear(far[center]);
        std::cout << "STELLAR near=" << int(near[center]) << " far=" << int(far[center])
                  << " decoded_ratio=" << ratio << '\n';
        need(ratio > 3.6 && ratio < 4.4, "GPU stellar irradiance does not follow inverse square");
        scene.stars[0].positionMetres.z = 12;
        scene.occluders = {{{0, 2, 6}, 2, 2}};
        auto dark = capture();
        need(dark[center] <= 1, "Eclipsed surface received phantom ambient/directional light");
        scene.occluders[0].positionMetres.x = .5;
        scene.occluders[0].radiusMetres = .5;
        auto partial = capture();
        need(partial[center] > dark[center] + 2 && partial[center] < near[center] - 2,
             "Finite stellar disk did not produce partial shadow");
        draws.clear();
        vertices.clear();
        ++revision;
        scene.occluders.clear();
        scene.stars[0].positionMetres.z = -10;
        auto disk = capture();
        scene.stars[0].positionMetres.z = -26;
        auto small = capture();
        size_t largeCount = 0, smallCount = 0;
        for (size_t p = 0; p < disk.size(); p += 3) {
            largeCount += disk[p] > 220;
            smallCount += small[p] > 220;
        }
        std::cout << "DISK bright_pixels=" << largeCount << " -> " << smallCount << '\n';
        need(largeCount > smallCount && smallCount > 0, "Stellar angular size did not shrink with distance");
        scene.originMetres.x = 1e18;
        scene.stars[0].positionMetres.x = 1e18;
        auto rebased = capture();
        need(rebased == small, "Astronomical origin rebasing changed the rendered star");
        scene.originMetres = {0, 0, 0};
        scene.stars[0].positionMetres = {-4, 2, -10};
        auto offAxis = capture();
        size_t offAxisCount = 0;
        for (size_t p = 0; p < offAxis.size(); p += 3)
            offAxisCount += offAxis[p] > 220;
        need(offAxisCount > 10, "An off-axis star incorrectly occluded its own photosphere");
        scene = {};
        scene.metresPerUnit = 1e6;
        scene.stars = {StellarEmitter{{-.05 * astronomicalUnitMetres, 0, 0}, solarRadiusMetres, 5772, 1}};
        scene.occluders = {{{0, 0, 0}, 6371000, 2}};
        camera.position = {3 * 6.371f, .65f * 6.371f, 2 * 6.371f};
        auto towardBody = glm::normalize(-camera.position);
        camera.yaw = std::atan2(towardBody.x, towardBody.z);
        camera.pitch = std::asin(towardBody.y);
        camera.nearPlane = .02f;
        camera.farPlane = 2000;
        auto astronomical = capture();
        size_t astronomicalCount = 0;
        for (size_t p = 0; p < astronomical.size(); p += 3)
            astronomicalCount += astronomical[p] > 220;
        std::cout<<"ASTRONOMICAL bright_pixels="<<astronomicalCount<<'\n';
        need(astronomicalCount > 100,
             "Astronomical disk vanished with a nonblocking body in the visibility list");
        renderer.clearStellarScene();
        renderer.close();
        need(renderer.errors() == 0, "Stellar Vulkan synchronization validation failed");
        std::cout << "PASS finite-emitter falloff, eclipses, dark space, visible disk and teardown\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
