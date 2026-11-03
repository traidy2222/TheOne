#include <theone/assets/model.hpp>
#include <theone/animation/pose.hpp>
#include <theone/effects/particles.hpp>
#include <theone/input/bindings.hpp>
#include <theone/physics/physics.hpp>
#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>

#if defined(VK_VERSION_1_0) || defined(IMGUI_VERSION) || defined(JPH_VERSION_MAJOR) ||                       \
    defined(NLOHMANN_JSON_VERSION_MAJOR)
#error Public engine headers must not expose backend implementation dependencies
#endif

#include <iostream>
#include <stdexcept>

void legacyHeadersLinkCheck();

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected fixture directory");
        const std::filesystem::path assets = argv[1];
        auto model = one::Model::load(assets / "robot.glb");
        if (model->meshes.empty() || model->skins.empty() || model->animations.empty())
            throw std::runtime_error("Split importer did not assemble geometry, rig and animation");

        one::World world;
        one::Entity entity;
        entity.id = 1;
        entity.model = model;
        world.entities.push_back(entity);
        one::RenderScene scene;
        std::vector<one::Vertex> vertices;
        std::vector<one::Draw> draws;
        world.prepare(.25f, scene);
        world.bake(.25f, vertices, draws);
        if (vertices.empty() || scene.vertexCount != vertices.size() || scene.draws.size() != draws.size())
            throw std::runtime_error("Scene/asset/animation module boundary lost geometry");

        one::InputBindings input(assets / "input.json");
        one::ParticleSystem particles;
        particles.step(.1f);
        if (!particles.count() || input.key("jump") != SDL_SCANCODE_SPACE)
            throw std::runtime_error("Input/effects module linkage failed");

        one::Physics physics;   // Lifetime only; no simulation or game suite.
        one::Renderer renderer; // Opaque implementation lifetime; no GPU/window initialization.
        if (renderer.errors() || renderer.geometryStats().cachedMeshes)
            throw std::runtime_error("Renderer facade initial state changed");
        legacyHeadersLinkCheck();
        std::cout << "PASS: standalone public headers, canonical/legacy linkage, split import/pose/scene "
                     "assembly\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
