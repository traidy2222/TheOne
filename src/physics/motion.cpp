#include "jolt_state.hpp"
namespace one {
using namespace physics_detail;
void Physics::impulse(uint64_t id, glm::vec3 impulse) {
    auto it = impl_->bodies.find(id);
    if (it != impl_->bodies.end())
        impl_->system.GetBodyInterface().AddImpulse(it->second, v(impulse));
}
std::vector<BodyMotion> Physics::motions() const {
    std::vector<BodyMotion> result;
    const auto& bi = impl_->system.GetBodyInterface();
    for (const auto& [id, body] : impl_->bodies)
        result.push_back({id, v(bi.GetLinearVelocity(body)), v(bi.GetAngularVelocity(body))});
    return result;
}
void Physics::restoreMotions(const std::vector<BodyMotion>& motions) {
    auto& bi = impl_->system.GetBodyInterface();
    for (const auto& motion : motions) {
        auto it = impl_->bodies.find(motion.id);
        if (it != impl_->bodies.end() && bi.GetMotionType(it->second) == JPH::EMotionType::Dynamic)
            bi.SetLinearAndAngularVelocity(it->second, v(motion.linear), v(motion.angular));
    }
}
} // namespace one
