#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm>
using json = nlohmann::json;
static void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
static std::vector<unsigned char> capture() {
    std::ifstream f("capture.ppm", std::ios::binary);
    std::string magic;
    int w, h, max;
    f >> magic >> w >> h >> max;
    f.get();
    require(magic == "P6" && max == 255, "Invalid capture");
    std::vector<unsigned char> pixels(size_t(w) * h * 3);
    f.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
    require(bool(f), "Truncated capture");
    return pixels;
}
static void compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    require(a.size() == b.size(), "Capture size changed");
    double sum = 0;
    size_t large = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        auto d = std::abs(int(a[i]) - int(b[i]));
        sum += d;
        large += d > 8;
    }
    require(sum / a.size() < .5 && double(large) / a.size() < .005, "Frame output differs from reference");
}
int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected scene");
        bool profile = false, validation = false;
        for (int i = 2; i < argc; ++i) {
            profile |= std::string(argv[i]) == "--profile";
            validation |= std::string(argv[i]) == "--validate";
        }
        auto world = one::World::load(argv[1]);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window = SDL_CreateWindow("Focused frame features", 640, 480,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        require(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } windowGuard{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        require(ImGui_ImplSDL3_InitForVulkan(window), "UI init failed");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        one::Renderer renderer;
        renderer.initialize(window, validation);
        one::Camera camera;
        one::RenderScene scene;
        auto begin = [&](bool panel = false) {
            SDL_PumpEvents();
            require(renderer.beginFrame(), "Frame unavailable");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (panel)
                renderer.drawProfiler();
            ImGui::Render();
        };
        auto render = [&](bool readback = false, float time = .25f) {
            world.prepare(time, scene);
            begin();
            require(renderer.render(scene, {}, {}, camera, time, readback), "Render failed");
        };
        if (profile) {
            // Fixed camera and simulation time: only renderer scheduling/culling changes.
            for (unsigned slots : {1u, 2u})
                for (bool culling : {false, true}) {
                    renderer.setFramesInFlight(slots);
                    renderer.setCulling(culling);
                    for (int i = 0; i < 5; ++i)
                        render();
                    double cpu = 0, record = 0, wait = 0, gpu = 0, shadow = 0, main = 0, prepare = 0;
                    constexpr int samples = 24;
                    for (int i = 0; i < samples; ++i) {
                        auto start = std::chrono::steady_clock::now();
                        render();
                        cpu += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                         start)
                                   .count();
                        const auto& s = renderer.frameStats();
                        record += s.recordMs;
                        wait += s.fenceWaitMs;
                        gpu += s.gpuTotalMs;
                        shadow += s.gpuShadowMs;
                        main += s.gpuMainMs;
                        prepare += s.scenePrepareMs;
                    }
                    auto s = renderer.frameStats();
                    std::cout << "PROFILE "
                              << json({{"slots", slots},
                                       {"culling", culling},
                                       {"frame_ms", cpu / samples},
                                       {"record_ms", record / samples},
                                       {"fence_wait_ms", wait / samples},
                                       {"scene_prepare_ms", prepare / samples},
                                       {"gpu_ms", gpu / samples},
                                       {"shadow_ms", shadow / samples},
                                       {"main_ms", main / samples},
                                       {"gpu_available", s.gpuTimingAvailable},
                                       {"candidates", s.candidateDraws},
                                       {"main_draws", s.mainDraws},
                                       {"shadow_draws", s.shadowDraws},
                                       {"main_culled", s.mainCulled},
                                       {"shadow_culled", s.shadowCulled}})
                                     .dump()
                              << '\n';
                }
        } else {
            // Duplicate a static asset outside both frusta; it shares geometry with the fixture.
            auto outside = world.entities.front();
            outside.id = 900000;
            outside.parent = 0;
            outside.animate = false;
            outside.transform.position = {10000, 10000, 10000};
            world.entities.push_back(outside);
            renderer.setCulling(false);
            renderer.setFramesInFlight(1);
            render(true);
            auto reference = capture();
            renderer.setCulling(true);
            render(true);
            compare(reference, capture());
            require(renderer.frameStats().mainCulled > 0 && renderer.frameStats().shadowCulled > 0,
                    "Culling test was vacuous");
            renderer.setFramesInFlight(2);
            unsigned previousSlot = 99;
            for (int i = 0; i < 6; ++i) {
                render();
                auto s = renderer.frameStats();
                require(s.slot != previousSlot, "Frame slot did not alternate");
                previousSlot = s.slot;
            }
            auto stats = renderer.frameStats();
            require(!stats.gpuTimingSupported ||
                        (stats.gpuTimingAvailable && stats.gpuFrame < stats.frame && stats.gpuTotalMs > 0),
                    "Completed-frame GPU timings missing");
            render(true);
            compare(reference, capture());
            // Exercise independent palettes and CPU vertex buffers on successive in-flight frames.
            std::vector<one::Vertex> vertices;
            std::vector<one::Draw> draws;
            for (int i = 0; i < 6; ++i) {
                float t = .07f * i;
                if (i % 2) {
                    world.bake(t, vertices, draws);
                    begin();
                    renderer.render(vertices, draws, camera, t);
                } else
                    render(false, t);
            }
            world.bake(.4f, vertices, draws);
            begin();
            renderer.render(vertices, draws, camera, .4f, true);
            auto cpu = capture();
            render(true, .4f);
            compare(cpu, capture());
            // Each frame slot warms its procedural buffer once, then reuses unchanged revisions.
            for (int i = 0; i < 3; ++i) {
                begin();
                renderer.renderCached(vertices, draws, 1, camera, .4f);
            }
            require(renderer.geometryStats().uploadedDynamicBytes == 0, "Warm procedural slot reuploaded");
            vertices.front().position.y += .02f;
            begin();
            renderer.renderCached(vertices, draws, 2, camera, .4f);
            require(renderer.geometryStats().uploadedDynamicBytes > 0, "Procedural revision ignored");
            // Submit both slots before replacing an asset. Other models must stay resident.
            render();
            render();
            auto previous = world.entities.front().model;
            auto meshCount = renderer.geometryStats().cachedMeshes;
            size_t changedMeshes = 0;
            for (const auto& m : previous->meshes)
                changedMeshes += m.size();
            auto replacement = world.reloadAsset(world.entities.front().asset);
            render();
            require(renderer.assetUploadStats().meshes == changedMeshes, "Reload uploaded unaffected meshes");
            require(renderer.geometryStats().cachedMeshes == meshCount, "Reload leaked mesh cache entries");
            require(renderer.frameStats().retiredResources > 0, "In-flight asset was not deferred");
            render();
            render();
            render();
            require(renderer.frameStats().retiredResources == 0, "Retired assets were not reclaimed");
            // Texture revision replaces one image and dependent descriptors, preserving meshes.
            require(!replacement->images.empty(), "Fixture texture missing");
            auto& image = replacement->images.front();
            auto oldPixels = image.rgba;
            for (size_t i = 0; i < image.rgba.size(); i += 4)
                image.rgba[i] = 255 - image.rgba[i];
            ++image.revision;
            render();
            require(renderer.assetUploadStats().textures == 1 && renderer.assetUploadStats().meshes == 0,
                    "Texture reload invalidated unrelated assets");
            render(true);
            auto modified = capture();
            require(modified != reference, "Texture replacement did not affect output");
            image.rgba = std::move(oldPixels);
            ++image.revision;
            render(true);
            compare(reference, capture());
            // Resize invalidates swapchain attachments, while model residency survives.
            SDL_SetWindowSize(window, 800, 600);
            renderer.resize();
            render();
            render();
            require(renderer.geometryStats().uploadedGeometryBytes == 0, "Resize reuploaded meshes");
            world.prepare(.25f, scene);
            begin(true);
            renderer.render(scene, {}, {}, camera, .25f);
            renderer.clearAssets();
            render();
            require(renderer.assetUploadStats().meshes > 0, "Full reset stopped working");
            std::cout << "PASS culling parity, independent frame slots, GPU timestamps, selective "
                         "model/texture reload, deferred retirement, resize and profiler panel\n";
        }
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation errors");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
