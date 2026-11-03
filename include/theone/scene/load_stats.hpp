#pragma once
#include <theone/assets/load_stats.hpp>
#include <cstdint>
namespace one {
struct SceneLoadStats {
    double jsonMs = 0, entitiesMs = 0, hierarchyMs = 0, assetsMs = 0;
    size_t assetWorkers=0;
    uint64_t assetWorkingEstimate=0;
    ModelLoadStats models;
};
} // namespace one
