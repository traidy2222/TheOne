#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>
#include <theone/physics/physics.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <stdexcept>

using Clock = std::chrono::steady_clock;
template <class F> double measure(F&& fn) {
    auto start = Clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
static void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Usage: theone_load_profile scene.one [--validate] [--capture]");
        bool validate = false, capture = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--validate")
                validate = true;
            else if (arg == "--capture")
                capture = true;
            else
                throw std::runtime_error("Unknown argument");
        }
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window =
            SDL_CreateWindow("Asset loading profile", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        struct Window {
            SDL_Window* value;
            ~Window() { SDL_DestroyWindow(value); }
        } guard{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        require(ImGui_ImplSDL3_InitForVulkan(window), "UI initialization failed");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        one::Renderer renderer;
        double startup = measure([&] { renderer.initialize(window, validate); });
        one::World world;
        one::RenderScene scene;
        one::Camera camera;
        std::unique_ptr<one::Physics> physics;
        auto begin = [&] {
            SDL_PumpEvents();
            require(renderer.beginFrame(), "Frame unavailable");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::Render();
        };
        for (int pass = 0; pass < 3; ++pass) {
            nlohmann::json result = {{"pass", pass}, {"validation", validate}};
            if (!pass)
                result["startup_ms"] = startup;
            one::World next;
            result["scene_load_ms"] = measure([&] { next = one::World::load(argv[1]); });
            auto nextPhysics = std::make_unique<one::Physics>();
            result["collision_ms"] = measure([&] { nextPhysics->rebuild(next); });
            result["clear_ms"] = measure([&] { renderer.clearAssets(); });
            result["replace_ms"] = measure([&] {
                world = std::move(next);
                physics = std::move(nextPhysics);
            });
            result["prepare_ms"] = measure([&] { world.prepare(.25f, scene); });
            begin();
            result["first_render_ms"] = measure(
                [&] { require(renderer.render(scene, {}, {}, camera, .25f, capture), "Render failed"); });
            auto s = renderer.assetUploadStats();
            auto geometry = renderer.geometryStats();
            result["geometry_index_ms"] = s.geometryIndexMs;
            result["geometry_allocation_ms"] = s.geometryAllocationMs;
            result["geometry_submit_ms"] = s.geometrySubmitMs;
            result["texture_total_ms"] = s.textureTotalMs;
            result["texture_submit_ms"] = s.textureSubmitMs;
            result["materials_total_ms"] = s.materialTotalMs;
            result["meshes"] = s.meshes;
            result["textures"] = s.textures;
            result["upload_submissions"] = s.immediateSubmissions;
            result["geometry_bytes"] = geometry.uploadedGeometryBytes;
            require(geometry.uploadedGeometryBytes > 0, "Reload did not upload geometry");
            begin();
            result["warm_render_ms"] = measure([&] { renderer.render(scene, {}, {}, camera, .25f); });
            require(renderer.geometryStats().uploadedGeometryBytes == 0 &&
                        renderer.assetUploadStats().textures == 0,
                    "Warm frame unexpectedly uploaded assets");
            std::cout << "PROFILE " << result.dump() << std::endl;
        }
        if (capture) {
            // Specifically exercise adding another allocation without discarding the old one.
            std::filesystem::copy_file("capture.ppm", "reload.ppm",
                                       std::filesystem::copy_options::overwrite_existing);
            auto readCapture = [] {
                std::ifstream f("capture.ppm", std::ios::binary);
                return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
            };
            const auto resident = renderer.geometryStats().residentGeometryBytes;
            auto animated = std::find_if(world.entities.begin(), world.entities.end(),
                                         [](const auto& e) { return e.model && !e.model->skins.empty(); });
            require(animated != world.entities.end(), "Capture check requires an animated fixture");
            auto extra = *animated;
            extra.id = 1;
            for (const auto& e : world.entities)
                extra.id = std::max(extra.id, e.id + 1);
            extra.parent = 0;
            extra.transform.position.x += 2;
            extra.model = std::make_shared<one::Model>(*extra.model);
            world.entities.push_back(std::move(extra));
            world.prepare(.25f, scene);
            begin();
            renderer.render(scene, {}, {}, camera, .25f, true);
            require(renderer.geometryStats().uploadedGeometryBytes > 0 &&
                        renderer.geometryStats().residentGeometryBytes > resident,
                    "Incremental mesh allocation was not exercised");
            require(renderer.geometryStats().uploadedGeometryBytes <
                        renderer.geometryStats().residentGeometryBytes,
                    "Existing geometry was unnecessarily uploaded again");
            auto gpu = readCapture();
            std::vector<one::Vertex> vertices;
            std::vector<one::Draw> draws;
            world.bake(.25f, vertices, draws);
            begin();
            renderer.render(vertices, draws, camera, .25f, true);
            auto cpu = readCapture();
            require(gpu.size() == cpu.size(), "Capture dimensions differ");
            double error = 0;
            size_t differing = 0;
            for (size_t i = 0; i < gpu.size(); ++i) {
                int delta = std::abs(int(gpu[i]) - int(cpu[i]));
                error += delta;
                if (delta > 8)
                    ++differing;
            }
            require(error / gpu.size() < .5 && double(differing) / gpu.size() < .005,
                    "Packed mesh offsets differ from CPU reference");
            renderer.clearAssets();
            begin();
            renderer.render(scene, {}, {}, camera, .25f, true);
            require(readCapture() == gpu, "Asset invalidation changed packed geometry or texture results");
            std::cout << "PASS: incremental allocations, CPU/GPU image parity and complete invalidation\n";
        }
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation errors");
        std::cout << "PASS: initial load, two reloads, cache invalidation and warm-frame residency\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
