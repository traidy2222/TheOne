#pragma once
#include "body_shape.hpp"
#include <theone/physics/edit.hpp>
#include <unordered_map>
namespace one::physics_detail {
struct EditBaseline {
    std::shared_ptr<ShapeCache> cache;
    std::shared_ptr<const int> owner;
    uint64_t revision = 0;
    std::unordered_map<uint64_t, Collider> colliders;
};
struct EditPlan {
    std::shared_ptr<const EditBaseline> baseline;
    std::unordered_map<uint64_t, Collider> colliders;
    std::vector<uint64_t> removals, replacements, moves, preserve;
};
} // namespace one::physics_detail
