#include "jolt_state.hpp"
#include "edit_state.hpp"
#include <unordered_set>
namespace one {
using namespace physics_detail;
PhysicsEditRequest Physics::captureEdit(const World& desired) const {
    auto baseline = std::make_shared<EditBaseline>();
    baseline->cache = impl_->shapeCache;
    baseline->owner = impl_->editOwner;
    baseline->revision = impl_->editRevision;
    baseline->colliders = impl_->colliders;
    auto& bi = impl_->system.GetBodyInterface();
    for (auto& [id, c] : baseline->colliders) {
        auto body = impl_->bodies.at(id);
        auto q = bi.GetRotation(body);
        c.entity.transform.position = v(bi.GetPosition(body));
        c.entity.transform.rotation = {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
    }
    return {desired, std::move(baseline)};
}
std::shared_ptr<PhysicsEdit> PhysicsEdit::prepare(const PhysicsEditRequest& request, std::stop_token stop) {
    if (!request.baseline)
        throw std::runtime_error("Missing collision edit baseline");
    auto result = std::make_shared<PhysicsEdit>();
    result->world = request.world;
    result->plan = std::make_shared<EditPlan>();
    auto& plan = *result->plan;
    plan.baseline = request.baseline;
    const auto& old = plan.baseline->colliders;
    std::unordered_set<uint64_t> seen;
    for (const auto& e : result->world.entities) {
        checkCancellation(stop);
        if (!seen.insert(e.id).second)
            throw std::runtime_error("Duplicate entity id in collision edit");
        if (e.body == "none")
            continue;
        if (e.parent)
            throw std::runtime_error("Physics bodies must be scene roots");
        auto before = old.find(e.id);
        bool existing = before != old.end();
        bool pose = existing && samePose(e.transform, before->second.entity.transform);
        if (existing && pose && e.body == "dynamic" && before->second.entity.body == "dynamic")
            plan.preserve.push_back(e.id);
        if (existing && sameShape(e, before->second.entity)) {
            plan.colliders.emplace(e.id, Collider{e, before->second.shape});
            if (pose)
                ++result->stats.unchanged;
            else {
                plan.moves.push_back(e.id);
                ++result->stats.moved;
            }
        } else {
            auto c = prepareCollider(e, *plan.baseline->cache, stop);
            if (!c.shape)
                continue;
            plan.colliders.emplace(e.id, std::move(c));
            plan.replacements.push_back(e.id);
            if (existing)
                ++result->stats.replaced;
            else
                ++result->stats.added;
        }
    }
    for (const auto& [id, c] : old)
        if (!plan.colliders.contains(id)) {
            plan.removals.push_back(id);
            ++result->stats.removed;
        }
    checkCancellation(stop);
    return result;
}
uint32_t Physics::bodyToken(uint64_t entity) const {
    auto it = impl_->bodies.find(entity);
    return it == impl_->bodies.end() ? JPH::BodyID().GetIndexAndSequenceNumber()
                                     : it->second.GetIndexAndSequenceNumber();
}
} // namespace one
