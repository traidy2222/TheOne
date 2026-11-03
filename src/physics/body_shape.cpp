#include "jolt_state.hpp"
#include "body_shape.hpp"
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
namespace one::physics_detail {
bool sameShape(const Entity& a, const Entity& b) {
    return a.body == b.body && a.transform.scale == b.transform.scale && a.halfExtent == b.halfExtent &&
           a.mass == b.mass && (a.body != "static" || (a.model == b.model && a.clip == b.clip));
}
bool samePose(const Transform& a, const Transform& b) {
    return glm::distance(a.position, b.position) < .0001f &&
           std::abs(glm::dot(a.rotation, b.rotation)) > .99999f;
}
Collider prepareCollider(const Entity& e, ShapeCache& cache, std::stop_token stop) {
    checkCancellation(stop);
    if (e.parent)
        throw std::runtime_error("Physics bodies must be scene roots");
    if (glm::any(glm::lessThanEqual(glm::abs(e.transform.scale), glm::vec3(.00001f))))
        throw std::runtime_error("Collision scale must be nonzero");
    Collider result{e, {}};
    if (e.body == "static" && e.model) {
        std::vector<Vertex> vertices;
        std::vector<Draw> draws;
        e.model->bake(glm::mat4(1), 0, e.clip, false, e.id, vertices, draws);
        checkCancellation(stop);
        if (vertices.empty())
            return result;
        auto base = cache.mesh(vertices, stop);
        result.shape = e.transform.scale == glm::vec3(1)
                           ? base
                           : JPH::RefConst<JPH::Shape>(new JPH::ScaledShape(base, v(e.transform.scale)));
    } else {
        auto extent = glm::abs(e.halfExtent * e.transform.scale);
        if (glm::any(glm::lessThanEqual(extent, glm::vec3(0))) || e.mass <= 0)
            throw std::runtime_error("Invalid box extent or mass");
        // Small boxes need a correspondingly small convex radius.
        result.shape =
            new JPH::BoxShape(v(extent), std::min(.05f, std::min({extent.x, extent.y, extent.z}) * .5f));
    }
    return result;
}
JPH::BodyCreationSettings bodySettings(const Collider& c) {
    const auto& e = c.entity;
    auto p = e.transform.position;
    auto q = e.transform.rotation;
    bool dynamic = e.body == "dynamic";
    JPH::BodyCreationSettings s(c.shape, JPH::RVec3(p.x, p.y, p.z), JPH::Quat(q.x, q.y, q.z, q.w),
                                dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
                                dynamic ? 1 : 0);
    if (dynamic) {
        s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        s.mMassPropertiesOverride.mMass = e.mass;
    }
    return s;
}
} // namespace one::physics_detail
