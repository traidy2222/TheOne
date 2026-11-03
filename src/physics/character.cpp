#include "jolt_state.hpp"
namespace one {
using namespace physics_detail;
glm::vec3 Physics::playerPosition() const {
    return v(impl_->system.GetBodyInterface().GetPosition(impl_->player));
}
bool Physics::grounded() const {
    return impl_->onGround;
}
void Physics::resetPlayer(glm::vec3 position) {
    if (!impl_->player.IsInvalid()) {
        auto& bi = impl_->system.GetBodyInterface();
        bi.SetPosition(impl_->player, v(position), JPH::EActivation::Activate);
        bi.SetLinearVelocity(impl_->player, JPH::Vec3::sZero());
    }
}
void Physics::setRespawnPosition(glm::vec3 position) {
    impl_->respawn = position;
}
glm::vec3 Physics::playerVelocity() const {
    return v(impl_->system.GetBodyInterface().GetLinearVelocity(impl_->player));
}
void Physics::setPlayerVelocity(glm::vec3 velocity) {
    impl_->system.GetBodyInterface().SetLinearVelocity(impl_->player, v(velocity));
}
} // namespace one
