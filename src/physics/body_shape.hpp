#pragma once
#include "shape_cache.hpp"
#include <theone/scene/entity.hpp>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
namespace one::physics_detail {
struct Collider {
    Entity entity;
    JPH::RefConst<JPH::Shape> shape;
};
bool sameShape(const Entity& a, const Entity& b);
bool samePose(const Transform& a, const Transform& b);
Collider prepareCollider(const Entity&, ShapeCache&, std::stop_token);
JPH::BodyCreationSettings bodySettings(const Collider&);
} // namespace one::physics_detail
