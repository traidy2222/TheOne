#pragma once
#include "prepared_cache.hpp"
namespace one::asset_cache {
std::filesystem::path cachePath(Key key);
uint64_t sourceLocation(const std::filesystem::path& source);
std::vector<std::filesystem::path> readDependencies(const std::filesystem::path& source);
void writeDependencies(Key key, std::span<const std::filesystem::path> dependencies);
} // namespace one::asset_cache
