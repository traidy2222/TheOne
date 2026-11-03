#include "asset_memory_state.hpp"
#include "geometry_state.hpp"
#include <theone/assets/model.hpp>
#include <unordered_set>
namespace one::vulkan {
void refreshGeometryMemory(const GeometryCache& geometryState, GeometryMemorySnapshot& geometryMemory) {
    if (!geometryMemory.dirty)
        return;
    geometryMemory.stats = {};
    geometryMemory.models.clear();
    auto& result = geometryMemory.stats;
    std::unordered_set<const Model*> models;
    std::unordered_set<const PreparedMesh*> prepared;
    uint64_t liveBytes = 0;
    for (const auto& [key, mesh] : geometryState.meshes) {
        liveBytes += mesh.bytes;
        if (models.insert(mesh.owner.get()).second) {
            geometryMemory.models.push_back(mesh.owner.get());
            for (const auto& primitives : mesh.owner->meshes)
                for (const auto& p : primitives)
                    result.cpuMeshBytes += p.vertices.capacity() * sizeof(SkinVertex);
        }
        if (mesh.prepared && prepared.insert(mesh.prepared.get()).second) {
            result.cpuMeshBytes += mesh.prepared->vertices.capacity() * sizeof(SkinVertex);
            if (mesh.prepared->packed)
                result.cpuMeshBytes += mesh.prepared->packed->capacity();
            for (const auto& lod : mesh.prepared->levels)
                result.cpuMeshBytes += lod.indices.capacity() * sizeof(uint32_t);
        }
    }
    result.models = models.size();
    for (const auto& allocation : geometryState.allocations)
        result.gpuGeometryBytes += allocation.allocatedBytes;
    result.unusedGeometryBytes =
        result.gpuGeometryBytes > liveBytes ? result.gpuGeometryBytes - liveBytes : 0;
    geometryMemory.dirty = false;
}
} // namespace one
