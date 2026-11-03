#include "jolt_state.hpp"
#include <unordered_set>
namespace one {
using namespace physics_detail;
std::vector<BodyState> Physics::bodyStates() const {
    std::vector<BodyState> states;
    auto& bi = impl_->system.GetBodyInterface();
    for (auto [id, body] : impl_->bodies)
        if (bi.GetMotionType(body) == JPH::EMotionType::Dynamic) {
            auto q = bi.GetRotation(body);
            states.push_back({id,
                              {v(bi.GetPosition(body)), {1, 1, 1}, {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}},
                              v(bi.GetLinearVelocity(body)),
                              v(bi.GetAngularVelocity(body))});
        }
    return states;
}
void Physics::restoreBodies(const std::vector<BodyState>& states, World& world,
                            const std::vector<uint64_t>& eligible) {
    std::unordered_set<uint64_t> ids(eligible.begin(), eligible.end());
    auto& bi = impl_->system.GetBodyInterface();
    for (const auto& state : states) {
        auto found = impl_->bodies.find(state.id);
        auto* entity = world.find(state.id);
        if (!ids.contains(state.id) || !entity || entity->body != "dynamic" || found == impl_->bodies.end())
            continue;
        auto p = state.transform.position;
        auto q = state.transform.rotation;
        bi.SetPositionAndRotation(found->second, JPH::RVec3(p.x, p.y, p.z), JPH::Quat(q.x, q.y, q.z, q.w),
                                  JPH::EActivation::Activate);
        bi.SetLinearAndAngularVelocity(found->second, v(state.linear), v(state.angular));
        entity->transform.position = p;
        entity->transform.rotation = q;
    }
}
CollisionCacheStats Physics::collisionCacheStats() const {
    return impl_->shapeCache->stats();
}
void Physics::setCollisionCacheBudget(uint64_t bytes) {
    impl_->shapeCache->setBudget(bytes);
}
} // namespace one
