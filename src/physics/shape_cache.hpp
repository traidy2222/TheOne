#pragma once
#include "runtime.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <theone/physics/physics.hpp>
#include <mutex>
namespace one::physics_detail {
class ShapeCache {
    RuntimeLease runtime;
    struct Entry {
        uint64_t hash, touched, bytes;
        std::vector<glm::vec3> positions;
        JPH::RefConst<JPH::Shape> shape;
    };
    std::mutex mutex;
    std::vector<Entry> entries;
    uint64_t clock = 0, budget = 64 * 1024 * 1024;
    CollisionCacheStats counters;
    void trim();

  public:
    JPH::RefConst<JPH::Shape> mesh(const std::vector<Vertex>& vertices, const std::stop_token& stop);
    CollisionCacheStats stats();
    void setBudget(uint64_t bytes);
};
std::shared_ptr<ShapeCache> sharedShapeCache();
} // namespace one::physics_detail
