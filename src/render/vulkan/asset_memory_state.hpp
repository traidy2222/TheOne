#pragma once
#include <theone/render/asset_memory_stats.hpp>
#include <vector>
namespace one {
struct Model;
namespace vulkan {
struct GeometryCache;
struct GeometryMemorySnapshot;
void refreshGeometryMemory(const GeometryCache& geometry, GeometryMemorySnapshot& snapshot);
// Geometry topology changes invalidate this snapshot before its raw owners are read.
// CPU image capacities and transient GPU resources are still counted each frame.
struct GeometryMemorySnapshot {
    bool dirty = true;
    AssetMemoryStats stats;
    std::vector<const Model*> models;
};
} // namespace vulkan
} // namespace one
