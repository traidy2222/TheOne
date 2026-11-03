#pragma once
#include <theone/scene/world.hpp>
#include <theone/core/cancellation.hpp>
namespace one {
struct PhysicsEditRequest;
struct PhysicsEdit;
struct BodyMotion {
    uint64_t id;
    glm::vec3 linear, angular;
};
struct BodyState {
    uint64_t id;
    Transform transform;
    glm::vec3 linear, angular;
};
struct CollisionCacheStats {
    uint64_t hits = 0, cooks = 0, bytes = 0, entries = 0;
};
class Physics {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Physics();
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;
    // Instances are thread-confined; independent instances can build/simulate concurrently.
    // Adopt owners of byte-identical prepared models without changing collision bodies.
    void adoptPreparedModelOwners(const World& world);
    void rebuild(const World& world, const std::stop_token& stop = {});
    PhysicsEditRequest captureEdit(const World& desired) const;
    void applyEdit(PhysicsEdit& edit, World& liveWorld);
    // Stable for a body's lifetime; useful for diagnosing incremental replacement.
    uint32_t bodyToken(uint64_t entity) const;
    void step(World& world, float dt, glm::vec3 movement, bool jump, bool allowAirJump = false);
    glm::vec3 playerPosition() const;
    bool grounded() const;
    void resetPlayer(glm::vec3 position = {0, 2, 10});
    void setRespawnPosition(glm::vec3 position);
    std::vector<BodyMotion> motions() const;
    void restoreMotions(const std::vector<BodyMotion>& motions);
    glm::vec3 playerVelocity() const;
    void setPlayerVelocity(glm::vec3 velocity);
    void impulse(uint64_t id, glm::vec3 impulse);
    std::vector<BodyState> bodyStates() const;
    void restoreBodies(const std::vector<BodyState>& states, World& world,
                       const std::vector<uint64_t>& eligible);
    CollisionCacheStats collisionCacheStats() const;
    void setCollisionCacheBudget(uint64_t bytes);
};
} // namespace one
