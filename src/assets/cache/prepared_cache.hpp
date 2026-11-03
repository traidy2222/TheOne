#pragma once
#include <theone/assets/model.hpp>
#include <span>
namespace one::asset_cache {
struct Key {
    uint64_t content = 0, location = 0;
};
Key identify(std::span<const std::filesystem::path> dependencies, std::stop_token stop);
std::shared_ptr<Model> read(Key key, const std::filesystem::path& source, std::stop_token stop, ModelLoadStats* stats = nullptr);
void write(Key key, const Model& model, std::stop_token stop);
std::vector<uint8_t> encode(const Model& model, std::stop_token stop);
std::shared_ptr<Model> decode(std::span<const uint8_t> bytes, std::stop_token stop);
} // namespace one::asset_cache
