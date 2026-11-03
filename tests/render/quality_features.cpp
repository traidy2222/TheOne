#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>
#include <theone/geometry/prepared_mesh.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <fstream>
#include <iostream>
#include <algorithm>
static void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
static std::vector<uint8_t> pixels() {
    std::ifstream f("capture.ppm", std::ios::binary);
    std::string magic;
    int w, h, max;
    f >> magic >> w >> h >> max;
    f.get();
    require(magic == "P6" && w == 640 && h == 480, "Invalid capture");
    std::vector<uint8_t> p(size_t(w) * h * 3);
    f.read(reinterpret_cast<char*>(p.data()), p.size());
    require(bool(f), "Truncated capture");
    return p;
}
static void compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, double tolerance = .5) {
    require(a.size() == b.size(), "Image sizes differ");
    double error = 0;
    for (size_t i = 0; i < a.size(); ++i)
        error += std::abs(int(a[i]) - int(b[i]));
    std::cout << "Image mean error " << error / a.size() << '\n';
    require(error / a.size() < tolerance, "Image differs from reference");
}
static std::shared_ptr<one::Model> plane(int cells, float size, bool checker) {
    auto model = std::make_shared<one::Model>();
    one::Primitive primitive;
    primitive.material = 0;
    auto v = [&](int x, int y) {
        one::SkinVertex p;
        p.vertex.position = {(float(x) / cells - .5f) * size, (float(y) / cells - .5f) * size, 0};
        p.vertex.normal = {0, 0, 1};
        p.vertex.uv = {float(x) / cells * (checker ? 128 : 1), float(y) / cells * (checker ? 128 : 1)};
        return p;
    };
    for (int y = 0; y < cells; ++y)
        for (int x = 0; x < cells; ++x)
            primitive.vertices.insert(primitive.vertices.end(), {v(x, y), v(x + 1, y), v(x + 1, y + 1),
                                                                 v(x, y), v(x + 1, y + 1), v(x, y + 1)});
    primitive.prepared = one::prepareMesh(primitive.vertices);
    model->meshes.push_back({std::move(primitive)});
    one::Node node;
    node.mesh = 0;
    model->nodes.push_back(node);
    one::Material material;
    material.unlit = true;
    if (checker) {
        one::Image image;
        image.width = image.height = 64;
        image.rgba.resize(64 * 64 * 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                auto offset = (y * 64 + x) * 4;
                image.rgba[offset] = image.rgba[offset + 1] = image.rgba[offset + 2] = ((x + y) % 2) * 255;
                image.rgba[offset + 3] = 255;
            }
        model->images.push_back(std::move(image));
        material.image = 0;
        material.metallicRoughness = &model->images[0];
    }
    model->materials.push_back(material);
    return model;
}
int main(int argc, char** argv) {
    try {
        require(argc == 2 || (argc == 3 && std::string(argv[2]) == "--lod-only"),
                "Expected fixture scene [--lod-only]");
        auto world = one::World::load(argv[1]);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window = SDL_CreateWindow("Focused rendering quality", 640, 480,
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
        renderer.initialize(window, true);
        renderer.setTextureStreaming(false);
        one::Camera camera;
        one::RenderScene scene;
        auto begin = [&] {
            SDL_PumpEvents();
            require(renderer.beginFrame(), "No frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::Render();
        };
        uint64_t uploadedMipLevels = 0;
        size_t evaluatedBounds = 0;
        auto render = [&](bool capture = false, bool shadows = true, bool classic = false,
                          bool settle = true) {
            uploadedMipLevels = 0;
            evaluatedBounds = 0;
            for (int attempt = 0; attempt < (settle ? 500 : 1); ++attempt) {
                world.prepare(.4f, scene);
                begin();
                require(renderer.render(scene, {}, {}, camera, .4f, false, shadows, 0, classic),
                        "Render failed");
                uploadedMipLevels += renderer.assetUploadStats().mipLevels;
                evaluatedBounds += renderer.frameStats().boundsEvaluated;
                if (!renderer.frameStats().pendingDraws)
                    break;
                require(!settle || attempt < 499, "Upload did not settle");
            }
            if (capture) {
                begin();
                require(renderer.render(scene, {}, {}, camera, .4f, true, shadows, 0, classic),
                        "Capture failed");
                uploadedMipLevels += renderer.assetUploadStats().mipLevels;
                evaluatedBounds += renderer.frameStats().boundsEvaluated;
            }
        };
        auto checkLod = [&] {
            world.entities.resize(1);
            world.entities[0].transform = {};
            world.entities[0].parent = 0;
            camera.position = {0, 0, 40};
            camera.yaw = 3.14159265f;
            camera.pitch = 0;
            world.entities[0].model = plane(128, 4, false);
            camera.position.z = 40;
            renderer.setLod(false);
            render();
            auto full = renderer.frameStats().mainTriangles;
            renderer.setLod(true);
            render();
            auto reduced = renderer.frameStats().mainTriangles;
            require(renderer.frameStats().lodDraws > 0 && reduced < full,
                    "Distance LOD did not reduce triangles");
            camera.position.z = 5;
            render();
            require(renderer.frameStats().mainTriangles == full, "Near mesh lost full detail");
            return std::pair{full, reduced};
        };
        if (argc == 3) {
            auto animatedFixture = world;
            auto [full, reduced] = checkLod();
            render(true);
            auto nearReference = pixels();
            require(renderer.frameStats().rigidBoundsReused == 1 &&
                        renderer.frameStats().boundsEvaluated == 0,
                    "Unchanged rigid bounds were rebuilt");
            world.entities[0].transform.position.x = 10000;
            render();
            require(renderer.frameStats().mainDraws == 0 && renderer.frameStats().boundsEvaluated == 1,
                    "Moved rigid bounds were not refreshed");
            world.entities[0].transform.position.x = 0;
            render(true);
            compare(nearReference, pixels());
            renderer.invalidateAsset(world.entities[0].model);
            render();
            require(renderer.frameStats().mainTriangles == full, "LOD cache lost after invalidation");
            require(evaluatedBounds > 0, "Invalidated geometry reused an old bound");
            world = std::move(animatedFixture);
            camera = {};
            render();
            render();
            require(renderer.frameStats().boundsEvaluated > 0 && renderer.frameStats().rigidBoundsReused > 0,
                    "Animated bounds must refresh while unchanged rigid bounds reuse");
            renderer.close();
            require(renderer.errors() == 0, "Vulkan validation failed");
            std::cout << "PASS rigid/animated bounds, movement, LOD near/far and invalidation " << full
                      << " -> " << reduced << '\n';
            return 0;
        }
        // Force incremental uploads through both frame slots; no queue-idle upload submissions.
        renderer.setUploadBudget(16 * 1024);
        bool pending = false;
        int stagedFrames = 0;
        for (int i = 0; i < 100; ++i) {
            render(false, true, false, false);
            pending |= renderer.frameStats().pendingDraws > 0;
            stagedFrames += renderer.frameStats().uploadCommands > 0;
            require(renderer.assetUploadStats().immediateSubmissions == 0,
                    "Upload blocked on immediate queue wait");
            if (!renderer.frameStats().pendingDraws)
                break;
        }
        require(pending && stagedFrames > 1 && renderer.frameStats().pendingDraws == 0,
                "Budgeted upload did not converge");
        renderer.setUploadBudget(8 * 1024 * 1024);
        std::vector<one::Vertex> vertices;
        std::vector<one::Draw> draws;
        world.bake(.4f, vertices, draws);
        begin();
        renderer.render(vertices, draws, camera, .4f, true);
        auto reference = pixels();
        renderer.setCulling(false);
        render(true);
        compare(reference, pixels());
        renderer.setCulling(true);
        render(true);
        compare(reference, pixels());
        std::filesystem::copy_file("capture.ppm", "quality-shadows-origin.ppm",
                                   std::filesystem::copy_options::overwrite_existing);
        // A skinned actor wholly outside both views is now safely culled.
        auto actor = *world.find(2);
        actor.id = 9000;
        actor.parent = 0;
        actor.transform.position = {10000, 0, 0};
        world.entities.push_back(actor);
        render();
        require(renderer.frameStats().animatedCulled > 0, "Animated bounds never cull");
        world.entities.pop_back();
        // Camera-following coverage must survive travelling far outside the old origin shadow volume.
        for (auto& e : world.entities)
            if (!e.parent)
                e.transform.position.x += 1000;
        camera.position.x += 1000;
        render(true);
        auto traveled = pixels();
        compare(reference, traveled, 2.0);
        std::filesystem::copy_file("capture.ppm", "quality-shadows-travel.ppm",
                                   std::filesystem::copy_options::overwrite_existing);
        render(true, false);
        auto unshadowed = pixels();
        size_t changes = 0;
        for (size_t i = 0; i < traveled.size(); ++i)
            changes += std::abs(int(traveled[i]) - int(unshadowed[i])) > 5;
        require(changes > 50, "Traveling shadow check was vacuous");
        // Color and data variants of the same source image have distinct, complete mip chains.
        one::World textured;
        one::Entity quad;
        quad.id = 1;
        quad.model = plane(1, 4, true);
        textured.entities.push_back(quad);
        world = std::move(textured);
        camera.position = {0, 0, 5};
        camera.yaw = 3.14159265f;
        camera.pitch = 0;
        render(true, false, true);
        auto minified = pixels();
        auto levelCount = uploadedMipLevels;
        int center = minified[(240 * 640 + 320) * 3];
        require(center >= 182 && center <= 191, "sRGB GPU mip filtering is not linear-light");
        require(levelCount >= 14, "Both color/data mip chains were not uploaded");
        std::filesystem::copy_file("capture.ppm", "quality-mip-checker.ppm",
                                   std::filesystem::copy_options::overwrite_existing);
        // Dense static surfaces simplify at distance, while a near view keeps full detail.
        auto [full, reduced] = checkLod();
        std::cout << "LOD triangles " << full << " -> " << reduced << "; checker center " << center
                  << "; streaming frames " << stagedFrames << '\n';
        // Invalidation with submitted staged commands must respect GPU resource lifetimes.
        renderer.setUploadBudget(16 * 1024);
        renderer.invalidateAsset(world.entities[0].model);
        render();
        renderer.invalidateAsset(world.entities[0].model);
        render();
        renderer.clearAssets();
        SDL_SetWindowSize(window, 800, 600);
        renderer.resize();
        render();
        renderer.close();
        require(renderer.errors() == 0, "Vulkan synchronization validation failed");
        std::cout << "PASS staged uploads, mips, traveling cascades, animated culling, LOD, invalidation and "
                     "resize\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
