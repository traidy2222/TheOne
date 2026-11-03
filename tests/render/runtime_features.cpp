#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>
#include <theone/geometry/prepared_mesh.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <fstream>
#include <iostream>
#include <algorithm>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<uint8_t> pixels() {
    std::ifstream f("capture.ppm", std::ios::binary);
    std::string magic;
    int w, h, m;
    f >> magic >> w >> h >> m;
    f.get();
    std::vector<uint8_t> p(size_t(w) * h * 3);
    f.read((char*)p.data(), p.size());
    require(bool(f), "Bad capture");
    return p;
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
        image.width = image.height = 256;
        image.rgba.resize(256 * 256 * 4);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                auto offset = (y * 256 + x) * 4;
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
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window = SDL_CreateWindow("Focused instancing and mip residency", 640, 480,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        require(window, SDL_GetError());
        struct Window {
            SDL_Window* p;
            ~Window() { SDL_DestroyWindow(p); }
        } wg{window};
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        require(ImGui_ImplSDL3_InitForVulkan(window), "UI init");
        struct UI {
            ~UI() {
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
            }
        } ui;
        one::Renderer renderer;
        renderer.initialize(window, true);
        renderer.setTextureStreaming(false);
        renderer.setUploadBudget(256 * 1024);
        renderer.setUploadTimeBudget(2);
        one::World world;
        one::RenderScene scene;
        one::Camera camera;
        camera.position = {0, 0, 9};
        camera.yaw = 3.14159265f;
        camera.pitch = 0;
        auto model = plane(1, .95f, false);
        for (uint64_t i = 0; i < 64; ++i) {
            one::Entity e;
            e.id = i + 1;
            e.model = model;
            e.transform.position = {float(i % 8) - 3.5f, float(i / 8) - 3.5f, 0};
            world.entities.push_back(e);
        }
        bool expectStableMaterials = false;
        auto render = [&](bool capture = false) {
            world.prepare(0, scene);
            SDL_PumpEvents();
            require(renderer.beginFrame(), "No frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::Render();
            require(renderer.render(scene, {}, {}, camera, 0, capture, true, 1), "Render failed");
            if (expectStableMaterials)
                require(renderer.assetUploadStats().materialTotalMs == 0,
                        "Mip change recreated material uniform buffers");
        };
        auto settle = [&]() {
            for (int f = 0; f < 400; ++f) {
                render();
                const auto& s = renderer.frameStats();
                require(s.transferBytes <= 256 * 1024, "Texture upload exceeded chunk budget");
                if (f >= 12 && !s.pendingDraws && !s.pendingUploadBytes && !s.textureTransitions)
                    return;
            }
            throw std::runtime_error("Residency failed to settle");
        };
        std::vector<uint8_t> reference;
        if (argc == 1) {
            renderer.setInstancing(false);
            settle();
            render(true);
            reference = pixels();
            auto unbatched = renderer.frameStats();
            renderer.setInstancing(true);
            render(true);
            auto batched = renderer.frameStats();
            require(reference == pixels(), "Instancing changed image or selected highlight");
            require(batched.mainDraws == 2 && unbatched.mainDraws == 64,
                    "Repeated opaque instances were not batched");
            require(batched.mainInstances == unbatched.mainInstances &&
                        batched.mainTriangles == unbatched.mainTriangles,
                    "Instancing lost visible geometry");
            require(batched.shadowDraws < unbatched.shadowDraws &&
                        batched.shadowInstances == unbatched.shadowInstances,
                    "Cascade instancing lost geometry");
            std::cout << "instancing main=" << unbatched.mainDraws << " -> " << batched.mainDraws
                      << " shadows=" << unbatched.shadowDraws << " -> " << batched.shadowDraws << '\n';
            // Culling happens per instance before batching, including cascade-specific visibility.
            world.entities[2].transform.position.x = 500;
            renderer.setInstancing(false);
            render(true);
            reference = pixels();
            unbatched = renderer.frameStats();
            renderer.setInstancing(true);
            render(true);
            batched = renderer.frameStats();
            require(reference == pixels() && unbatched.mainInstances == batched.mainInstances &&
                        unbatched.shadowInstances == batched.shadowInstances,
                    "Instancing culling differs");
            model->materials[0].blend = true;
            model->materials[0].color.a = .5f;
            renderer.invalidateMaterial(&model->materials[0]);
            renderer.setInstancing(false);
            settle();
            render(true);
            reference = pixels();
            unbatched = renderer.frameStats();
            renderer.setInstancing(true);
            render(true);
            require(reference == pixels() && renderer.frameStats().mainDraws == unbatched.mainDraws,
                    "Blended draw order changed");
        }
        renderer.clearAssets();
        world.entities.clear();
        model = plane(1, 4, true);
        auto& image = model->images[0];
        image.width = image.height = 1024;
        image.rgba.resize(1024 * 1024 * 4);
        for (size_t i = 0; i < image.rgba.size(); i += 4) {
            image.rgba[i] = uint8_t((i / 4) % 251);
            image.rgba[i + 1] = 120;
            image.rgba[i + 2] = 30;
            image.rgba[i + 3] = 255;
        }
        // Exercise background mesh preparation too, without imported prepared data.
        model->meshes[0][0].prepared.reset();
        one::Entity e;
        e.id = 100;
        e.model = model;
        world.entities.push_back(e);
        camera.position = {0, 0, 4};
        renderer.setTextureStreaming(true);
        uint64_t firstBytes = 0;
        for (int f = 0; f < 200; ++f) {
            render();
            if (!renderer.frameStats().pendingDraws) {
                firstBytes = renderer.assetMemoryStats().gpuTextureBytes;
                break;
            }
        }
        require(firstBytes > 0, "Coarse image never became drawable");
        settle();
        auto nearBytes = renderer.assetMemoryStats().gpuTextureBytes;
        require(nearBytes > firstBytes * 4, "Visible texture failed to promote from coarse mip");
        auto duplicate = plane(1, 4, true);
        duplicate->images[0] = image;
        one::Entity aliasEntity;
        aliasEntity.id = 101;
        aliasEntity.model = duplicate;
        world.entities.push_back(aliasEntity);
        settle();
        require(renderer.assetMemoryStats().gpuTextureBytes == nearBytes,
                "Mip promotion duplicated identical resident image storage");
        expectStableMaterials = true;
        renderer.setTextureStreaming(false);
        settle();
        render(true);
        reference = pixels();
        renderer.setTextureStreaming(true);
        settle();
        render(true);
        require(reference == pixels(), "Near mip promotion differs from full-resolution reference");
        // Both frame slots must see refreshed descriptors, including a material
        // that was off-screen while its old texture allocation retired.
        render(true);
        require(reference == pixels(), "Second frame slot retained stale image view");
        for (auto& entity : world.entities)
            entity.transform.position.x = 1000;
        settle();
        require(renderer.assetMemoryStats().gpuTextureBytes < nearBytes / 4,
                "Off-screen detail stayed allocated");
        for (auto& entity : world.entities)
            entity.transform.position.x = 0;
        settle();
        render(true);
        require(reference == pixels(), "Returning material retained retired texture view");
        render(true);
        require(reference == pixels(), "Returning second slot retained retired view");
        camera.position.z = 100;
        settle();
        auto farBytes = renderer.assetMemoryStats().gpuTextureBytes;
        require(farBytes < nearBytes / 4, "Distance did not release detailed mip allocations");
        camera.position.z = 4;
        settle();
        renderer.setTextureMemoryBudget(1);
        settle();
        auto pressureBytes = renderer.assetMemoryStats().gpuTextureBytes;
        require(pressureBytes < nearBytes / 4, "Memory pressure did not demote detail");
        std::cout << "texture bytes coarse=" << firstBytes << " near=" << nearBytes << " distant=" << farBytes
                  << " pressure=" << pressureBytes << '\n';
        renderer.setTextureMemoryBudget(256 * 1024 * 1024);
        renderer.setTextureStreaming(false);
        render();
        expectStableMaterials = false;
        renderer.invalidateImage(&image);
        ++image.revision;
        settle();
        renderer.clearAssets();
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation failed");
        std::cout << (argc == 1 ? "PASS instance parity and " : "PASS residency-only: ")
                  << "worker preparation, stable material uniform buffers, frame-slot view refresh, "
                     "coarse-first/distance/pressure residency, in-flight invalidation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
