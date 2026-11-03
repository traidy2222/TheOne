#pragma once
#include "runtime.hpp"
#include "shape_cache.hpp"
#include "body_shape.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <theone/physics/physics.hpp>
#include <unordered_map>
#include <stdexcept>
namespace one::physics_detail {

inline JPH::Vec3 v(glm::vec3 p) {
    return {p.x, p.y, p.z};
}
inline glm::vec3 v(JPH::Vec3Arg p) {
    return {p.GetX(), p.GetY(), p.GetZ()};
}
class Broad final : public JPH::BroadPhaseLayerInterface {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(l));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
        return l.GetValue() == 0 ? "Static" : "Moving";
    }
#endif
};
class ObjectBroad final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
        return a != 0 || b.GetValue() != 0;
    }
};
class Pair final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a != 0 || b != 0; }
};
class NotPlayer final : public JPH::BodyFilter {
    JPH::BodyID player_;

  public:
    explicit NotPlayer(JPH::BodyID p) : player_(p) {}
    bool ShouldCollide(const JPH::BodyID& id) const override { return id != player_; }
};
} // namespace one::physics_detail
namespace one {
struct Physics::Impl {
    // First to initialize and last to die, including constructor-failure unwinding.
    physics_detail::RuntimeLease runtime;
    std::shared_ptr<physics_detail::ShapeCache> shapeCache = physics_detail::sharedShapeCache();
    physics_detail::Broad broad;
    physics_detail::ObjectBroad objectBroad;
    physics_detail::Pair pair;
    JPH::TempAllocatorImpl allocator{32 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1};
    JPH::PhysicsSystem system;
    JPH::BodyID player;
    std::unordered_map<uint64_t, JPH::BodyID> bodies;
    std::unordered_map<uint64_t, physics_detail::Collider> colliders;
    std::shared_ptr<const int> editOwner = std::make_shared<const int>(0);
    uint64_t editRevision = 0;
    bool onGround = false;
    glm::vec3 respawn{0, 2, 10};
    Impl() { system.Init(8192, 0, 32768, 8192, broad, objectBroad, pair); }
    void clear() {
        auto& bi = system.GetBodyInterface();
        if (!player.IsInvalid()) {
            bi.RemoveBody(player);
            bi.DestroyBody(player);
            player = JPH::BodyID();
        }
        for (const auto& [id, body] : bodies) {
            (void)id;
            bi.RemoveBody(body);
            bi.DestroyBody(body);
        }
        bodies.clear();
        colliders.clear();
        ++editRevision;
    }
    ~Impl() { clear(); }
};
} // namespace one
