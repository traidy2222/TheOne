#include <theone/scene/world.hpp>
#include <theone/core/bounds.hpp>
#include <theone/render/camera.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
static void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected fixture directory");
        one::Bounds bounds;
        bounds.include({-1, -1, -1});
        bounds.include({1, 1, 1});
        one::Frustum clip(glm::mat4(1));
        require(clip.intersects(bounds, glm::mat4(1)), "Intersecting bound rejected");
        require(!clip.intersects(bounds, glm::translate(glm::mat4(1), {5, 0, 0})), "Outside bound retained");
        require(!clip.intersects(bounds, glm::translate(glm::mat4(1), {0, 0, -3})),
                "Vulkan near plane incorrect");
        require(!clip.intersects(bounds, glm::translate(glm::mat4(1), {0, 0, 3})), "Far plane incorrect");
        require(clip.intersects(bounds, glm::translate(glm::mat4(1), {2, 0, 0})),
                "Boundary touching bound rejected");
        auto transformed =
            glm::rotate(glm::mat4(1), .8f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {2, .3f, 1});
        require(clip.intersects(bounds, transformed), "Rotated nonuniform bound rejected");
        one::Frustum light(glm::translate(glm::mat4(1), {-5, 0, 0}));
        auto offscreen = glm::translate(glm::mat4(1), {5, 0, 0});
        require(!clip.intersects(bounds, offscreen) && light.intersects(bounds, offscreen),
                "Camera and light visibility coupled");
        auto fixture = std::filesystem::absolute(argv[1]);
        auto model = one::Model::load(fixture / "crate.glb");
        one::World world;
        one::Entity parent;
        parent.id = 1;
        one::Entity child;
        child.id = 2;
        child.parent = 1;
        child.model = model;
        child.animate = false;
        one::Entity independent = child;
        independent.id = 3;
        independent.parent = 0;
        world.entities = {parent, child, independent};
        one::RenderScene scene;
        world.prepare(0, scene);
        require(scene.preparation.rootsRecomputed == 3 && scene.preparation.palettesRebuilt == 2,
                "Initial cache missing");
        auto first = scene.transforms;
        world.prepare(0, scene);
        require(!scene.preparation.hierarchyRebuilt && scene.preparation.rootsRecomputed == 0 &&
                    scene.preparation.posesEvaluated == 0,
                "Static cache missed");
        auto copy = world;
        world.entities[0].transform.position.x = 4;
        world.prepare(0, scene);
        require(scene.preparation.rootsRecomputed == 2 && scene.preparation.palettesRebuilt == 1,
                "Dirty descendants incorrect");
        copy.prepare(0, scene);
        require(scene.transforms.front().position == first.front().position,
                "Copied world shared mutable cache");
        world.entities[1].visible = false;
        world.entities[0].transform.position.x = 7;
        world.prepare(0, scene);
        world.prepare(0, scene);
        world.entities[1].visible = true;
        world.prepare(0, scene);
        require(scene.transforms.front().position[3].x == 7, "Hidden descendant retained stale palette");
        std::swap(world.entities[0], world.entities[2]);
        world.prepare(0, scene);
        require(scene.preparation.hierarchyRebuilt, "Reorder did not rebuild topology");
        world.find(2)->parent = 0;
        world.prepare(0, scene);
        require(scene.preparation.hierarchyRebuilt, "Reparent did not rebuild topology");
        world.find(1)->parent = 2;
        world.find(2)->parent = 1;
        bool rejected = false;
        try {
            world.prepare(0, scene);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Cycle accepted by cached hierarchy");
        auto loaded = one::World::load(fixture / "sandbox.one");
        loaded.prepare(.2f, scene);
        loaded.prepare(.3f, scene);
        require(scene.preparation.posesEvaluated > 0, "Animation incorrectly cached as static");
        auto previous = loaded.entities.front().model;
        auto handle = previous->handle;
        auto replacement = loaded.reloadAsset(loaded.entities.front().asset);
        require(replacement != previous && replacement->handle.id == handle.id &&
                    replacement->handle.revision == handle.revision + 1,
                "Reload identity incorrect");
        for (const auto& e : loaded.entities)
            require(e.model != previous, "Shared reference was not replaced");
        auto snapshot = loaded;
        rejected = false;
        try {
            loaded.reloadAsset("missing.glb");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && loaded.entities.front().model == snapshot.entities.front().model,
                "Failed reload changed live model");
        auto unavailable = std::make_shared<one::Model>(*replacement);
        unavailable->source = fixture / "missing.glb";
        loaded.entities.front().model = unavailable;
        rejected = false;
        try {
            loaded.reloadAsset(unavailable->source);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && loaded.entities.front().model == unavailable,
                "Import failure replaced live model");
        one::Image image;
        image.reload(fixture / "conversion/checker.png");
        auto pixels = image.rgba;
        auto revision = image.revision;
        rejected = false;
        try {
            image.reload(fixture / "missing.png");
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && image.rgba == pixels && image.revision == revision,
                "Failed image reload changed live pixels");
        std::cout << "PASS frustum boundaries, shadow independence, cache invalidation/copies, animation and "
                     "transactional asset/image reload\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
