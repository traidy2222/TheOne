#include "shadow_fixture.hpp"
#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <fstream>
#include <iostream>
void require(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
std::vector<uint8_t> pixels() {
    std::ifstream f("capture.ppm", std::ios::binary);
    std::string magic;
    int w = 0, h = 0, max = 0;
    f >> magic >> w >> h >> max;
    f.get();
    require(magic == "P6" && w == 640 && h == 480 && max == 255, "Invalid image");
    std::vector<uint8_t> result(size_t(w) * h * 3);
    f.read((char*)result.data(), result.size());
    require(bool(f), "Truncated image");
    uint64_t digest=14695981039346656037ull;
    for(auto byte:result)digest=(digest^byte)*1099511628211ull;
    std::cout<<"capture_checksum="<<digest<<'\n';
    return result;
}
void compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, double limit) {
    require(a.size() == b.size(), "Image size changed");
    double error = 0;
    size_t large = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        auto d = std::abs(int(a[i]) - int(b[i]));
        error += d;
        large += d > 8;
    }
    std::cout << "image_error=" << error / a.size() << " large_fraction=" << double(large) / a.size() << '\n';
    require(error / a.size() <= limit && double(large) / a.size() < .005, "Shadow image changed excessively");
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scene");
        auto world = one::World::load(argv[1]);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window =
            SDL_CreateWindow("Focused shadow submission", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        require(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } wg{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplSDL3_InitForVulkan(window);
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        one::Renderer renderer;
        renderer.setVsync(false);
        renderer.initialize(window, true);
        renderer.setInstancing(false);
        renderer.setTextureStreaming(false);
        renderer.setShadowLod(false);
        one::Camera camera;
        one::RenderScene scene;
        bool cpuGeometry = false;
        std::vector<one::Vertex> cpuVertices;
        std::vector<one::Draw> cpuDraws;
        auto render = [&](bool capture = false, bool shadows = true) {
            auto frame = [&](bool readback) {
                world.prepare(.4f, scene);
                SDL_PumpEvents();
                require(renderer.beginFrame(), "No frame");
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                ImGui::Render();
                if (cpuGeometry) {
                    world.bake(.4f, cpuVertices, cpuDraws);
                    require(renderer.render(cpuVertices, cpuDraws, camera, .4f, readback, shadows),
                            "CPU render failed");
                } else
                    require(renderer.render(scene, {}, {}, camera, .4f, readback, shadows), "Render failed");
            };
            for (int attempt = 0; attempt < 500; ++attempt) {
                frame(false);
                if (!renderer.frameStats().pendingDraws) {
                    if (capture)
                        frame(true);
                    return;
                }
            }
            throw std::runtime_error("Upload timeout");
        };
        renderer.setShadowBatching(false);
        renderer.setSpatialCulling(false);
        render(true);
        auto reference = pixels();
        auto original = renderer.frameStats();
        auto memory = renderer.assetMemoryStats();
        renderer.setShadowBatching(true);
        renderer.setSpatialCulling(true);
        for (int slot = 0; slot < 2; ++slot) {
            render(true);
            compare(reference, pixels(), 0);
            require(renderer.frameStats().mainDraws == original.mainDraws &&
                        renderer.frameStats().shadowDraws == original.shadowDraws,
                    "Spatial culling lost geometry");
        }
        require(renderer.frameStats().shadowGeometryBinds < original.shadowGeometryBinds,
                "Batch fixture did not reduce binds");
        require(renderer.frameStats().shadowPlansReused == 3, "Stable cascade plans were rebuilt");
        renderer.setShadowBatching(true, false);
        render(true);
        compare(reference, pixels(), 0);
        renderer.setShadowBatching(true);
        renderer.setInstancing(true);
        render(true);
        compare(reference, pixels(), 0);
        renderer.setInstancing(false);
        require(renderer.assetMemoryStats().gpuTextureBytes == memory.gpuTextureBytes,
                "Shadow batching retained extra textures");
        // Refit a completely changed draw layout, including a dense caster and distant regions.
        world.entities.clear();
        auto caster = shadowGrid(64, 4, true), receiver = shadowGrid(1, 20);
        one::Entity ground;
        ground.id = 1;
        ground.model = receiver;
        ground.transform.rotation = glm::angleAxis(-1.5707963f, glm::vec3(1, 0, 0));
        world.entities.push_back(ground);
        for (uint64_t i = 0; i < 40; ++i) {
            one::Entity e;
            e.id = i + 2;
            e.model = caster;
            e.transform.position = {i ? 100.f + float(i) * 10 : 0.f, 3, 0};
            world.entities.push_back(e);
        }
        camera.position = {0, 6, 12};
        camera.yaw = 3.14159265f;
        camera.pitch = -.35f;
        render(true);
        reference = pixels();
        auto full = renderer.frameStats().shadowTriangles;
        require(renderer.frameStats().regionRejectedDraws > 0, "Region fixture did not reject draws");
        render(true, false);
        auto unshadowed = pixels();
        require(unshadowed != reference, "Shadow fixture was vacuous");
        renderer.setShadowLod(true);
        render(true);
        compare(reference, pixels(), .5);
        auto reduced = renderer.frameStats().shadowTriangles;
        require(reduced < full && renderer.frameStats().shadowLodDraws > 0,
                "Shadow LOD did not reduce triangles");
        std::cout << "shadow_triangles=" << full << " -> " << reduced << '\n';
        // Moved geometry and repeated palettes must remain correct for each cascade.
        world.entities[1].transform.position.x = 3;
        world.entities[1].transform.scale = {1.2f, .8f, 1};
        renderer.setShadowBatching(false);
        renderer.setSpatialCulling(false);
        render(true);
        reference = pixels();
        renderer.setShadowBatching(true);
        renderer.setSpatialCulling(true);
        render(true);
        compare(reference, pixels(), 0);
        renderer.invalidateAsset(caster);
        render(true);
        compare(reference, pixels(), 0);
        renderer.clearAssets();
        require(renderer.assetMemoryStats().shadowCommandBytes == 0, "Clear retained shadow commands");
        render(true);
        compare(reference, pixels(), 0);
        // A transparent masked caster must produce exactly the unshadowed image.
        // An opaque texel must still produce a nonempty shadow on the same receiver.
        auto masked = shadowGrid(1, 4);
        masked->materials[0].mask = true;
        masked->materials[0].image = 0;
        masked->images.emplace_back();
        masked->images[0].rgba = {255, 255, 255, 0};
        world.entities.resize(2);
        world.entities[1].model = masked;
        world.entities[1].transform.position = {0, 3, 0};
        world.entities[1].transform.scale = {1, 1, 1};
        renderer.setShadowLod(false);
        renderer.setInstancing(false);
        for (bool batched : {false, true}) {
            renderer.setShadowBatching(batched);
            render(true, false);
            auto clear = pixels();
            render(true, true);
            compare(clear, pixels(), 0);
        }
        masked->images[0].rgba[3] = 255;
        ++masked->images[0].revision;
        renderer.invalidateImage(&masked->images[0]);
        render(true, false);
        auto clear = pixels();
        render(true, true);
        require(clear != pixels(), "Opaque mask did not cast a shadow");
        reference = pixels();
        renderer.setShadowBatching(false);
        render(true, true);
        compare(reference, pixels(), 0);
        // Base factor alpha and per-vertex alpha participate in the same cutoff.
        masked->materials[0].color.a = 0;
        render(true, false);
        clear = pixels();
        render(true, true);
        compare(clear, pixels(), 0);
        masked->materials[0].color.a = 1;
        for (auto& v : masked->meshes[0][0].vertices)
            v.vertex.color.a = 0;
        masked->meshes[0][0].prepared = one::prepareMesh(masked->meshes[0][0].vertices);
        renderer.invalidateAsset(masked);
        renderer.setShadowBatching(true);
        render(true, false);
        clear = pixels();
        render(true, true);
        compare(clear, pixels(), 0);
        // Check the separate procedural shader, then a skinned masked caster.
        cpuGeometry = true;
        render(true, false);
        clear = pixels();
        render(true, true);
        compare(clear, pixels(), 0);
        cpuGeometry = false;
        one::Node joint;
        masked->nodes.push_back(joint);
        masked->nodes[0].skin = 0;
        masked->skins.push_back({{1}, {glm::mat4(1)}});
        for (auto& v : masked->meshes[0][0].vertices) {
            v.weights = {1, 0, 0, 0};
            v.joints = {0, 0, 0, 0};
        }
        masked->meshes[0][0].prepared = one::prepareMesh(masked->meshes[0][0].vertices);
        renderer.invalidateAsset(masked);
        render(true, false);
        clear = pixels();
        render(true, true);
        compare(clear, pixels(), 0);
        reference = pixels();
        // Cold off-camera mask: only alpha is needed, never normal/occlusion textures.
        renderer.clearAssets();
        auto outside = shadowGrid(1, 4);
        outside->materials[0].mask = true;
        outside->materials[0].image = 0;
        outside->images.resize(2);
        outside->images[0].rgba = {255, 10, 10, 0};
        outside->images[1].width = outside->images[1].height = 256;
        outside->images[1].rgba.assign(256 * 256 * 4, 128);
        outside->materials[0].normal = &outside->images[1];
        world.entities[1].model = outside;
        world.entities[1].transform.position = {-8, 12, 0};
        render(true, true);
        auto transparentOutside = pixels();
        require(renderer.frameStats().mainDraws == 1, "Masked fixture was not outside camera");
        require(renderer.frameStats().shadowDraws > 0, "Off-camera caster missed light frustum");
        require(renderer.assetMemoryStats().gpuTextureBytes < 65536,
                "Off-camera mask loaded unused surface textures");
        render(true, false);
        compare(transparentOutside, pixels(), 0);
        outside->images[0].rgba[3] = 255;
        ++outside->images[0].revision;
        render(true, true);
        reference = pixels();
        render(true, false);
        require(reference != pixels(), "Off-camera opaque texel did not cast a visible shadow");
        render(true, true);
        auto solidOutside = pixels();
        outside->images[0].width = outside->images[0].height = 4;
        outside->images[0].rgba.assign(4 * 4 * 4, 255);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 2; ++x)
                outside->images[0].rgba[(y * 4 + x) * 4 + 3] = 0;
        ++outside->images[0].revision;
        for (auto& v : outside->meshes[0][0].vertices)
            v.vertex.uv = {v.vertex.position.x / 4 + .5f, v.vertex.position.y / 4 + .5f};
        outside->meshes[0][0].prepared = one::prepareMesh(outside->meshes[0][0].vertices);
        renderer.invalidateAsset(outside);
        renderer.setShadowBatching(true);
        render(true, true);
        auto cutoutOutside = pixels();
        render(true, false);
        require(cutoutOutside != pixels() && cutoutOutside != solidOutside,
                "Partial alpha did not make shadow holes");
        renderer.setShadowBatching(false);
        render(true, true);
        compare(cutoutOutside, pixels(), 0);
        renderer.setShadowBatching(true);
        render(true, true);
        compare(cutoutOutside, pixels(), 0);
        reference = pixels();
        renderer.setVsync(true);
        render(true);
        require(renderer.vsyncEnabled(), "FIFO switch failed");
        compare(reference, pixels(), 0);
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation failed");
        std::cout << "PASS shadow batches/direct fallback, both slots, spatial/LOD parity, motion, "
                     "invalidation, presentation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
