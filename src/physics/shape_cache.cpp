#include "shape_cache.hpp"
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <algorithm>
#include <bit>
#include <cstring>
namespace one::physics_detail {
std::shared_ptr<ShapeCache> sharedShapeCache() {
    static std::mutex mutex;
    static std::weak_ptr<ShapeCache> weak;
    std::lock_guard lock(mutex);
    auto shared = weak.lock();
    if (!shared) {
        shared = std::make_shared<ShapeCache>();
        weak = shared;
    }
    return shared;
}
void ShapeCache::trim() {
    while (counters.bytes > budget && !entries.empty()) {
        auto oldest = std::min_element(entries.begin(), entries.end(),
                                       [](auto& a, auto& b) { return a.touched < b.touched; });
        counters.bytes -= oldest->bytes;
        entries.erase(oldest);
    }
    counters.entries = entries.size();
}
CollisionCacheStats ShapeCache::stats() {
    std::lock_guard lock(mutex);
    return counters;
}
void ShapeCache::setBudget(uint64_t bytes) {
    std::lock_guard lock(mutex);
    budget = bytes;
    trim();
}
JPH::RefConst<JPH::Shape> ShapeCache::mesh(const std::vector<Vertex>& vertices, const std::stop_token& stop) {
    std::vector<glm::vec3> positions;
    positions.reserve(vertices.size());
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < vertices.size(); ++i) {
        if ((i & 4095) == 0)
            checkCancellation(stop);
        positions.push_back(vertices[i].position);
        for (int axis = 0; axis < 3; ++axis) {
            hash ^= std::bit_cast<uint32_t>(vertices[i].position[axis]);
            hash *= 1099511628211ull;
        }
    }
    auto lookup = [&]() -> JPH::RefConst<JPH::Shape> {
        for (auto& entry : entries)
            if (entry.hash == hash && entry.positions.size() == positions.size() &&
                std::memcmp(entry.positions.data(), positions.data(), positions.size() * sizeof(glm::vec3)) ==
                    0) {
                entry.touched = ++clock;
                ++counters.hits;
                return entry.shape;
            }
        return {};
    };
    {
        std::lock_guard lock(mutex);
        if (auto found = lookup())
            return found;
    }
    JPH::TriangleList triangles;
    triangles.reserve(positions.size() / 3);
    for (size_t i = 0; i + 2 < positions.size(); i += 3) {
        if (i % 12288 == 0)
            checkCancellation(stop);
        auto v = [&](size_t j) {
            auto p = positions[j];
            return JPH::Vec3(p.x, p.y, p.z);
        };
        triangles.emplace_back(v(i), v(i + 1), v(i + 2));
    }
    auto result = JPH::MeshShapeSettings(triangles).Create();
    checkCancellation(stop);
    if (result.HasError())
        throw std::runtime_error(result.GetError().c_str());
    auto shape = result.Get();
    std::lock_guard lock(mutex);
    if (auto found = lookup())
        return found;
    ++counters.cooks;
    uint64_t bytes = positions.size() * sizeof(glm::vec3) + shape->GetStats().mSizeBytes;
    if (bytes <= budget) {
        entries.push_back({hash, ++clock, bytes, std::move(positions), shape});
        counters.bytes += bytes;
        trim();
    }
    return shape;
}
} // namespace one::physics_detail
