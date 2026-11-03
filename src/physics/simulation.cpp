#include "jolt_state.hpp"
namespace one {
using namespace physics_detail;
void Physics::step(World& world, float dt, glm::vec3 movement, bool jump, bool allowAirJump) {
    auto& bi = impl_->system.GetBodyInterface();
    if (!impl_->player.IsInvalid()) {
        auto p = bi.GetCenterOfMassPosition(impl_->player);
        JPH::RayCastResult hit;
        impl_->onGround = impl_->system.GetNarrowPhaseQuery().CastRay(
            JPH::RRayCast(p, JPH::Vec3(0, -0.92f, 0)), hit, {}, {}, NotPlayer(impl_->player));
        auto velocity = bi.GetLinearVelocity(impl_->player);
        bi.SetLinearVelocity(impl_->player,
                             JPH::Vec3(movement.x,
                                       jump && (impl_->onGround || allowAirJump) ? 5.3f : velocity.GetY(),
                                       movement.z));
    }
    auto result = impl_->system.Update(dt, 1, &impl_->allocator, &impl_->jobs);
    if (result != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Physics update capacity exceeded");
    for (const auto& [id, body] : impl_->bodies)
        if (auto e = world.find(id); e && e->body == "dynamic") {
            e->transform.position = v(bi.GetPosition(body));
            auto q = bi.GetRotation(body);
            e->transform.rotation = {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
        }
    if (playerPosition().y < -30)
        resetPlayer(impl_->respawn);
}
} // namespace one
