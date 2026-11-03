#include <theone/physics/scene_loading.hpp>
#include <chrono>
namespace one {
std::shared_ptr<PhysicsScene> PhysicsScene::load(const std::filesystem::path& path, std::stop_token stop) {
    using Clock = std::chrono::steady_clock;
    auto result = std::make_shared<PhysicsScene>();
    auto start = Clock::now();
    result->world = World::load(path, nullptr, stop);
    auto imported = Clock::now();
    checkCancellation(stop);
    result->physics = std::make_unique<Physics>();
    result->physics->rebuild(result->world, stop);
    auto ready = Clock::now();
    result->importMs = std::chrono::duration<double, std::milli>(imported - start).count();
    result->collisionMs = std::chrono::duration<double, std::milli>(ready - imported).count();
    return result;
}
std::shared_ptr<PhysicsScene> PhysicsScene::prepare(World world, const std::stop_token& stop) {
    auto result = std::make_shared<PhysicsScene>();
    result->world = std::move(world);
    auto start = std::chrono::steady_clock::now();
    checkCancellation(stop);
    result->physics = std::make_unique<Physics>();
    result->physics->rebuild(result->world, stop);
    result->collisionMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
PhysicsRebuildRequest PhysicsRebuildRequest::capture(const World& world, const Physics& live) {
    PhysicsRebuildRequest result{world, {}};
    for (const auto& body : live.bodyStates())
        if (auto e = world.find(body.id);
            e && e->body == "dynamic" &&
            glm::distance(e->transform.position, body.transform.position) < .0001f &&
            std::abs(glm::dot(e->transform.rotation, body.transform.rotation)) > .99999f)
            result.preserveBodies.push_back(body.id);
    return result;
}
void PhysicsScene::preserveLiveState(const Physics& live, const World* current) {
    // Preserve visual/editor changes made after the collision snapshot was queued.
    // Collider edits queue a new generation; replacement model pointers stay in this result.
    if (current) {
        for (auto& entity : world.entities)
            if (auto now = current->find(entity.id)) {
                entity.name = now->name;
                entity.visible = now->visible;
                entity.animate = now->animate;
                entity.animationTime = now->animationTime;
                entity.clip = now->clip;
                entity.previousClip = now->previousClip;
                entity.previousAnimationTime = now->previousAnimationTime;
                entity.animationBlend = now->animationBlend;
                entity.animationLoop = now->animationLoop;
                entity.previousAnimationLoop = now->previousAnimationLoop;
                entity.behavior = now->behavior;
                entity.speed = now->speed;
                if (entity.body == "none" && now->body == "none")
                    entity.transform = now->transform;
            }
        for (const auto& entity : current->entities)
            if (entity.body == "none" && !world.find(entity.id))
                world.entities.push_back(entity);
    }
    physics->resetPlayer(live.playerPosition());
    physics->setPlayerVelocity(live.playerVelocity());
    physics->restoreBodies(live.bodyStates(), world, preserveBodies);
}
PhysicsRebuildJob::PhysicsRebuildJob()
    : LoadJob([](const PhysicsRebuildRequest& request, std::stop_token stop) {
          auto result = PhysicsScene::prepare(request.world, stop);
          result->preserveBodies = request.preserveBodies;
          return result;
      }) {}
void PhysicsScene::swapInto(World& liveWorld, std::unique_ptr<Physics>& livePhysics) noexcept {
    static_assert(std::is_nothrow_swappable_v<World>);
    std::swap(world, liveWorld);
    physics.swap(livePhysics);
}
PhysicsSceneLoadJob::PhysicsSceneLoadJob() : LoadJob(PhysicsScene::load) {}
} // namespace one
