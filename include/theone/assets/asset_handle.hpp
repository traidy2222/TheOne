#pragma once
#include <cstdint>
namespace one {
struct AssetHandle {
    uint64_t id = 0, revision = 1;
    bool operator==(const AssetHandle&) const = default;
};
AssetHandle newAssetHandle();
} // namespace one
