#include "jolt_state.hpp"
#include "edit_state.hpp"
#include <unordered_set>
namespace one {
using namespace physics_detail;
void Physics::applyEdit(PhysicsEdit& edit, World& world) {
    if (!edit.plan || edit.plan->baseline->owner != impl_->editOwner ||
        edit.plan->baseline->revision != impl_->editRevision)
        throw std::runtime_error("Stale collision edit; capture a new baseline");
    auto& plan = *edit.plan;
    auto& bi = impl_->system.GetBodyInterface();
    // Allocate publication data before touching the live broadphase.
    auto nextBodies = impl_->bodies;
    auto nextColliders = plan.colliders;
    auto nextWorld = edit.world;
    std::unordered_map<uint64_t, BodyState> preserved;
    std::unordered_set<uint64_t> eligible(plan.preserve.begin(), plan.preserve.end());
    for (auto state : bodyStates())
        if (eligible.contains(state.id))
            preserved.emplace(state.id, state);
    for (auto& e : nextWorld.entities) {
        if (auto now = world.find(e.id)) {
            e.name = now->name;
            e.visible = now->visible;
            e.animate = now->animate;
            e.animationTime = now->animationTime;
            e.previousAnimationTime = now->previousAnimationTime;
            e.animationBlend = now->animationBlend;
            e.animationLoop = now->animationLoop;
            e.previousAnimationLoop = now->previousAnimationLoop;
            e.previousClip = now->previousClip;
            // Static clip is part of collision geometry; keep the prepared clip.
            if (e.body != "static")
                e.clip = now->clip;
            e.behavior = now->behavior;
            e.speed = now->speed;
            if (e.body == "none" && now->body == "none")
                e.transform = now->transform;
        }
        if (auto state = preserved.find(e.id); state != preserved.end()) {
            e.transform.position = state->second.transform.position;
            e.transform.rotation = state->second.transform.rotation;
            nextColliders.at(e.id).entity.transform = e.transform;
        }
    }
    for (const auto& e : world.entities)
        if (e.body == "none" && !nextWorld.find(e.id))
            nextWorld.entities.push_back(e);
    std::vector<JPH::BodyID> created;
    created.reserve(plan.replacements.size());
    try {
        for (auto id : plan.replacements) {
            auto* body = bi.CreateBody(bodySettings(nextColliders.at(id)));
            if (!body)
                throw std::runtime_error("Insufficient spare physics body capacity for edit");
            created.push_back(body->GetID());
            nextBodies[id] = body->GetID();
        }
    } catch (...) {
        for (auto id : created)
            bi.DestroyBody(id);
        throw;
    }
    auto remove = [&](uint64_t id) {
        auto it = impl_->bodies.find(id);
        if (it != impl_->bodies.end()) {
            bi.RemoveBody(it->second);
            bi.DestroyBody(it->second);
        }
    };
    for (auto id : plan.removals) {
        remove(id);
        nextBodies.erase(id);
    }
    for (auto id : plan.replacements) {
        remove(id);
        auto body = nextBodies.at(id);
        bi.AddBody(body, nextColliders.at(id).entity.body == "dynamic" ? JPH::EActivation::Activate
                                                                       : JPH::EActivation::DontActivate);
        if (auto s = preserved.find(id); s != preserved.end())
            bi.SetLinearAndAngularVelocity(body, v(s->second.linear), v(s->second.angular));
    }
    for (auto id : plan.moves) {
        const auto& t = nextColliders.at(id).entity.transform;
        auto p = t.position;
        auto q = t.rotation;
        bi.SetPositionAndRotation(nextBodies.at(id), JPH::RVec3(p.x, p.y, p.z), JPH::Quat(q.x, q.y, q.z, q.w),
                                  JPH::EActivation::Activate);
    }
    impl_->bodies.swap(nextBodies);
    impl_->colliders.swap(nextColliders);
    ++impl_->editRevision;
    std::swap(world, nextWorld);
    std::swap(edit.world, nextWorld);
    // Keep removed model/shape ownership on the result for worker retirement.
    plan.colliders.swap(nextColliders);
}
} // namespace one
