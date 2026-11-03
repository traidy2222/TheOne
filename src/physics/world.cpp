#include "jolt_state.hpp"
namespace one {
using namespace physics_detail;
Physics::Physics() : impl_(std::make_unique<Impl>()) {}
Physics::~Physics() = default;
void Physics::rebuild(const World& world, const std::stop_token& stop) {
    checkCancellation(stop);
    impl_->clear();
    auto& bi = impl_->system.GetBodyInterface();
    for (const auto& e : world.entities)
        if (e.body != "none") {
            checkCancellation(stop);
            auto collider = prepareCollider(e, *impl_->shapeCache, stop);
            if (!collider.shape)
                continue;
            auto id = bi.CreateAndAddBody(bodySettings(collider), e.body == "dynamic"
                                                                      ? JPH::EActivation::Activate
                                                                      : JPH::EActivation::DontActivate);
            if (id.IsInvalid())
                throw std::runtime_error("Physics body capacity exceeded");
            impl_->bodies[e.id] = id;
            impl_->colliders[e.id] = std::move(collider);
        }
    checkCancellation(stop);
    JPH::BodyCreationSettings player(new JPH::CapsuleShape(0.5f, 0.3f), JPH::RVec3(0, 2, 10),
                                     JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, 1);
    player.mAllowedDOFs =
        JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
    player.mFriction = 0;
    player.mLinearDamping = 0;
    player.mMotionQuality = JPH::EMotionQuality::LinearCast;
    impl_->player = bi.CreateAndAddBody(player, JPH::EActivation::Activate);
    if (impl_->player.IsInvalid())
        throw std::runtime_error("Cannot create player body");
    impl_->system.OptimizeBroadPhase();
    checkCancellation(stop);
}
} // namespace one
