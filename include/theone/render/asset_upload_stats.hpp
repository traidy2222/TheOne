#pragma once
#include <cstdint>
namespace one {
// CPU wall times for cache misses in the last render call (including synchronous GPU waits).
// Material time includes texture time; texture time includes texture-submit time.
struct AssetUploadStats {
    double geometryIndexMs = 0, geometryAllocationMs = 0, geometrySubmitMs = 0;
    double textureTotalMs = 0, textureSubmitMs = 0, materialTotalMs = 0;
    uint64_t textureBytes = 0, mipLevels = 0;
    uint64_t meshes = 0, textures = 0, immediateSubmissions = 0;
};
} // namespace one
