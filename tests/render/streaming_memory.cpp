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

int main() {
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        auto* window = SDL_CreateWindow("Upload and memory check", 640, 480,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
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
        one::World world;
        one::Entity entity;
        entity.id = 1;
        entity.model = plane(64, 4, true);
        world.entities.push_back(entity);
        one::Camera camera;
        camera.position = {0, 0, 5};
        camera.yaw = 3.14159265f;
        camera.pitch = 0;
        one::RenderScene scene;
        size_t reusedStaging=0;
        auto render = [&](bool capture = false) {
            world.prepare(0, scene);
            SDL_PumpEvents();
            require(renderer.beginFrame(), "No frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::Render();
            require(renderer.render(scene, {}, {}, camera, 0, capture, false), "Render failed");
            reusedStaging+=renderer.frameStats().stagingReuses;
            require(renderer.assetMemoryStats().pooledStagingBytes<=4*1024*1024,"Staging cache exceeded bound");
        };
        auto settle = [&](uint64_t budget) {
            size_t frames = 0;
            double maxCpu = 0, maxGpu = 0;
            for (; frames < 600; ++frames) {
                render();
                auto s = renderer.frameStats();
                require(s.transferBytes <= budget, "Upload exceeded byte quantum budget");
                maxCpu = std::max(maxCpu, s.uploadCpuMs);
                maxGpu = std::max(maxGpu, s.gpuUploadMs);
                if (!s.pendingDraws)
                    break;
            }
            require(frames < 600, "Upload never converged");
            std::cout << "upload frames=" << frames + 1 << " max_cpu_ms=" << maxCpu
                      << " max_gpu_ms=" << maxGpu << '\n';
            return frames + 1;
        };
        renderer.setUploadBudget(32 * 1024 * 1024);
        renderer.setUploadTimeBudget(20);
        settle(32 * 1024 * 1024);
        render(true);
        auto reference = pixels();
        renderer.clearAssets();
        renderer.setUploadBudget(64 * 1024);
        renderer.setUploadTimeBudget(.2);
        auto frames = settle(64 * 1024);
        require(frames > 2, "Large asset was not split");
        render(true);
        auto actual = pixels();
        require(reference == actual, "Chunked geometry/mips differ from large-budget upload");
        auto initial = renderer.assetMemoryStats();
        require(initial.cpuImageBytes && initial.cpuMeshBytes && initial.gpuTextureBytes &&
                    initial.gpuGeometryBytes,
                "Missing memory counters");
        render();
        require(renderer.assetMemoryStats().cpuMeshBytes == initial.cpuMeshBytes &&
                    renderer.assetMemoryStats().gpuGeometryBytes == initial.gpuGeometryBytes,
                "Stable geometry accounting changed");
        auto second = entity;
        second.id = 2;
        second.model = plane(64, 4, true);
        second.transform.position.x = 1;
        world.entities.push_back(second);
        settle(64 * 1024);
        auto shared = renderer.assetMemoryStats();
        require(shared.gpuTextureBytes == initial.gpuTextureBytes &&
                    shared.textureAliases > initial.textureAliases,
                "Identical textures did not share GPU storage");
        require(shared.models == 2 && shared.cpuMeshBytes > initial.cpuMeshBytes,
                "Geometry accounting did not observe added model");
        auto oldTextureBytes = shared.gpuTextureBytes;
        auto& image = world.entities[0].model->images[0];
        const auto oldCapacity = image.rgba.capacity();
        image.rgba.reserve(oldCapacity + 4096);
        render();
        require(renderer.assetMemoryStats().cpuImageBytes ==
                    shared.cpuImageBytes + image.rgba.capacity() - oldCapacity,
                "Image capacity edit missed by geometry accounting cache");
        for (size_t i = 0; i < image.rgba.size(); i += 4) {
            image.rgba[i] = 255;
            image.rgba[i + 1] = 0;
            image.rgba[i + 2] = 0;
        }
        ++image.revision;
        settle(64 * 1024);
        require(renderer.assetMemoryStats().gpuTextureBytes > oldTextureBytes,
                "Editing a shared texture did not split storage");
        world.entities.erase(world.entities.begin());
        renderer.setAssetMemoryBudget(0);
        settle(64 * 1024);
        render();
        render();
        require(renderer.assetMemoryStats().models == 1, "Unused model not reclaimed under pressure");
        require(renderer.assetMemoryStats().cpuMeshBytes == initial.cpuMeshBytes,
                "Evicted model retained in geometry accounting");
        require(renderer.assetMemoryStats().gpuTextureBytes == initial.gpuTextureBytes,
                "Orphaned texture allocation retained");
        // Repeated replacement should release both ready resources and unfinished chunks.
        for (int i = 0; i < 3; ++i) {
            auto old = world.entities[0].model;
            world.entities[0].model = plane(64, 4, true);
            renderer.invalidateAsset(old);
            settle(64 * 1024);
        }
        render();
        render();
        require(renderer.assetMemoryStats().models == 1, "Replacement leaked model residency");
        renderer.clearAssets();
        render();
        renderer.invalidateAsset(world.entities[0].model);
        world.entities.clear();
        render();
        render();
        render();
        require(renderer.frameStats().pendingUploadBytes == 0, "Cancelled upload retained work");
        require(renderer.assetMemoryStats().gpuTextureBytes == 0 &&
                    renderer.assetMemoryStats().gpuGeometryBytes == 0,
                "Unused assets survived empty scene");
        // A reload can retire hundreds of descriptors at once. Drain across frames
        // while preserving in-flight lifetimes, then verify explicit reset drains all.
        renderer.setUploadBudget(32*1024*1024);renderer.setUploadTimeBudget(20);
        for(uint64_t id=20;id<100;++id){one::Entity e;e.id=id;e.model=plane(1,1,false);world.entities.push_back(e);}
        settle(32*1024*1024);
        auto packedBytes = renderer.assetMemoryStats().gpuGeometryBytes;
        renderer.invalidateAsset(world.entities.front().model);
        world.entities.erase(world.entities.begin());
        render();
        require(renderer.assetMemoryStats().gpuGeometryBytes == packedBytes &&
                    renderer.frameStats().pendingDraws == 0,
                "Partial model removal freed a geometry batch still used by surviving models");
        std::vector<std::shared_ptr<const one::Model>> retiredModels;
        for(const auto& e:world.entities)retiredModels.push_back(e.model);
        renderer.invalidateAssets(retiredModels);world.entities.clear();retiredModels.clear();
        size_t destroyed=0;bool backlog=false;
        for(int frame=0;frame<100;++frame){
            render();auto stats=renderer.frameStats();
            require(stats.resourcesDestroyed<=32,"Retirement exceeded per-frame work cap");
            destroyed+=stats.resourcesDestroyed;backlog|=stats.retiredResources>0;
            if(frame>2 && !stats.retiredResources)break;
        }
        require(backlog && destroyed>32 && renderer.frameStats().retiredResources==0,"Retirement did not drain incrementally");
        renderer.clearAssets();renderer.setUploadBudget(64*1024);renderer.setUploadTimeBudget(.2);
        for(int idle=0;idle<150;++idle)render();
        require(reusedStaging>0 && renderer.assetMemoryStats().pooledStagingBytes==0,"Staging reuse or idle trim failed");
        auto procedural = plane(32, 4, false);
        // Warm a hidden material from already-resident shared textures, without
        // adding texture memory; its first visible frame needs no material creation.
        one::Entity warmVisible;
        warmVisible.id = 501; warmVisible.model = plane(2, 4, false);
        world.entities.push_back(warmVisible);
        settle(64 * 1024);
        render(true);
        auto warmPixels = pixels();
        auto warmBytes = renderer.assetMemoryStats().gpuTextureBytes;
        one::Entity warmHidden;
        warmHidden.id = 502; warmHidden.model = plane(2, 4, false);
        warmHidden.transform.position.x = 100;
        world.entities.push_back(warmHidden);
        size_t warmed = 0;
        for (int i = 0; i < 20; ++i) { render(); warmed += renderer.frameStats().materialWarmups; }
        require(warmed > 0, "Resident hidden material was not warmed");
        require(renderer.assetMemoryStats().gpuTextureBytes == warmBytes, "Warmup allocated hidden textures");
        world.entities[0].transform.position.x = 100;
        world.entities[1].transform.position.x = 0;
        render(true);
        require(renderer.frameStats().materialCreations == 0 && renderer.frameStats().pendingDraws == 0,
                "Warmed material stalled its first visible frame");
        require(warmPixels == pixels(), "Warmed material changed the image");
        world.entities.clear(); renderer.clearAssets();
        one::World proceduralWorld;
        one::Entity pe;
        pe.id = 10;
        pe.model = procedural;
        proceduralWorld.entities.push_back(pe);
        std::vector<one::Vertex> vertices;
        std::vector<one::Draw> draws;
        proceduralWorld.bake(0, vertices, draws);
        auto cached = [&](uint64_t revision) {
            SDL_PumpEvents();
            require(renderer.beginFrame(), "No procedural frame");
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::Render();
            renderer.renderCached(vertices, draws, revision, camera, 0, false, false);
            require(renderer.frameStats().transferBytes <= 64 * 1024, "Procedural upload exceeded budget");
        };
        cached(1);
        for (auto& vertex : vertices)
            vertex.color = {1, 0, 0, 1};
        cached(2);
        for (int frame = 0; frame < 100; ++frame) {
            cached(2);
            if (frame > 1 && !renderer.frameStats().pendingDraws)
                break;
            require(frame < 99, "Procedural upload never completed");
        }
        renderer.clearAssets();
        renderer.close();
        require(renderer.errors() == 0, "Vulkan validation failed");
        std::cout << "PASS chunk budgets, image parity, sharing, revision isolation, memory pressure and "
                     "in-flight invalidation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
