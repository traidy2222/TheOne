#pragma once
#include <theone/physics/physics.hpp>
#include <theone/core/load_job.hpp>
namespace one {
// A private world and collision system become publishable together, never partially.
struct PhysicsScene {
    World world;
    std::vector<std::shared_ptr<Model>> retiredModels;
    std::vector<uint64_t> preserveBodies;
    std::unique_ptr<Physics> physics;
    double importMs = 0, collisionMs = 0;
    static std::shared_ptr<PhysicsScene> load(const std::filesystem::path& path, std::stop_token stop = {});
    static std::shared_ptr<PhysicsScene> prepare(World world, const std::stop_token& stop = {});
    void preserveLiveState(const Physics& live, const World* current = nullptr);
    // Leaves the old world here so the caller can retire it on the loader thread.
    void swapInto(World& liveWorld, std::unique_ptr<Physics>& livePhysics) noexcept;
};
class PhysicsSceneLoadJob : public LoadJob<PhysicsScene> {
  public:
    PhysicsSceneLoadJob();
};
struct PhysicsRebuildRequest {
    World world;
    std::vector<uint64_t> preserveBodies;
    static PhysicsRebuildRequest capture(const World& world, const Physics& live);
};
class PhysicsRebuildJob : public LoadJob<PhysicsScene, PhysicsRebuildRequest> {
  public:
    PhysicsRebuildJob();
};
} // namespace one
