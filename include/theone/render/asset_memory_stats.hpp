#pragma once
#include <cstddef>
#include <cstdint>
namespace one {
struct AssetMemoryStats {
    uint64_t cpuMeshBytes = 0, cpuImageBytes = 0, pendingCpuBytes = 0;
    uint64_t gpuGeometryBytes = 0, gpuTextureBytes = 0, stagingBytes = 0, retiredGpuBytes = 0;
    uint64_t pooledStagingBytes = 0;
    uint64_t unusedGeometryBytes = 0, shadowCommandBytes = 0;
    size_t models = 0, textures = 0, textureAliases = 0;
};
struct GeometryStats {
    uint64_t uploadedGeometryBytes = 0, uploadedPaletteBytes = 0, uploadedDynamicBytes = 0;
    uint64_t residentGeometryBytes = 0;
    size_t cachedMeshes = 0;
};
}
