#include "prepared_cache.hpp"
#include "mapped_file.hpp"
#include "dependency_manifest.hpp"
#include <theone/core/cancellation.hpp>
#include <fstream>
#include <cstdlib>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <bit>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace one::asset_cache {
namespace {
constexpr uint64_t version = 0x4f4e454341430002ull; // Bump when archive or preparation algorithms change.
constexpr uint64_t seed = 14695981039346656037ull;
// Non-cryptographic local corruption/content fingerprint. Four independent word
// lanes avoid the byte-at-a-time dependency chain on large prepared payloads.
uint64_t hash(std::span<const uint8_t> bytes, uint64_t value = seed) {
    constexpr uint64_t prime = 0x9e3779b185ebca87ull;
    uint64_t lanes[4] = {value, value ^ prime, value + prime, ~value};
    auto size = bytes.size();
    while (bytes.size() >= 32) {
        for (int lane = 0; lane < 4; ++lane) {
            uint64_t word;
            std::memcpy(&word, bytes.data() + lane * 8, 8);
            lanes[lane] = std::rotl((lanes[lane] ^ word) * prime, 27);
        }
        bytes = bytes.subspan(32);
    }
    value = lanes[0] ^ std::rotl(lanes[1], 13) ^ std::rotl(lanes[2], 29) ^ std::rotl(lanes[3], 43) ^ size;
    while (bytes.size() >= 8) {
        uint64_t word;
        std::memcpy(&word, bytes.data(), 8);
        value = std::rotl((value ^ word) * prime, 27);
        bytes = bytes.subspan(8);
    }
    for (auto b : bytes)
        value = (value ^ b) * prime;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    return value;
}
std::filesystem::path directory() {
    if (std::getenv("THEONE_DISABLE_ASSET_CACHE"))
        return {};
    if (auto overridePath = std::getenv("THEONE_ASSET_CACHE_DIR"))
        return overridePath;
    if (auto local = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(local) / "TheOne" / "prepared-v1";
    return std::filesystem::temp_directory_path() / "theone-prepared-v1";
}
std::filesystem::path internalPath(Key key) {
    auto root = directory();
    return root.empty() ? root : root / (std::to_string(key.location) + ".onecache");
}
struct Header {
    uint64_t format, key, checksum, size, vertexSize, matrixSize;
};
} // namespace
std::filesystem::path cachePath(Key key) { return internalPath(key); }
Key identify(std::span<const std::filesystem::path> dependencies, std::stop_token stop) {
    if (directory().empty())
        return {};
    Key key{seed, seed};
    std::array<uint8_t, 65536> buffer;
    for (size_t i = 0; i < dependencies.size(); ++i) {
        auto name = std::filesystem::weakly_canonical(dependencies[i]).generic_string();
        uint64_t nameHash = seed;
        for (auto b : name)
            nameHash = (nameHash ^ uint8_t(b)) * 1099511628211ull;
        if (!i)
            key.location = nameHash;
        key.content = hash({reinterpret_cast<const uint8_t*>(&nameHash), sizeof(nameHash)}, key.content);
        std::ifstream in(dependencies[i], std::ios::binary);
        if (!in)
            return {};
        while (in) {
            checkCancellation(stop);
            in.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
            key.content = hash({buffer.data(), size_t(in.gcount())}, key.content);
        }
        if (!in.eof())
            return {};
    }
    return key;
}
std::shared_ptr<Model> read(Key key, const std::filesystem::path& source, std::stop_token stop, ModelLoadStats* stats) {
    if (!key.location)
        return {};
    try {
        auto readStart=std::chrono::steady_clock::now();
        MappedFile file(cachePath(key));
        auto view=file.bytes();
        Header h{};
        if(view.size()<sizeof(h))return {};
        std::memcpy(&h,view.data(),sizeof(h));
        if(h.format!=version || h.key!=key.content || h.vertexSize!=sizeof(SkinVertex) ||
           h.matrixSize!=sizeof(glm::mat4) || h.size>512*1024*1024 || view.size()!=sizeof(h)+h.size)return {};
        auto bytes=view.subspan(sizeof(h));
        if(hash(bytes)!=h.checksum)return {};
        checkCancellation(stop);
        auto decodeStart=std::chrono::steady_clock::now();
        if(stats)stats->cacheReadMs+=std::chrono::duration<double,std::milli>(decodeStart-readStart).count();
        auto model = decode(bytes, stop);
        if(stats)stats->cacheDecodeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decodeStart).count();
        model->source = source;
        return model;
    } catch (...) {
        checkCancellation(stop);
        return {};
    }
}
void write(Key key, const Model& model, std::stop_token stop) {
    if (!key.location)
        return;
    std::filesystem::path temporary;
    try {
        auto bytes = encode(model, stop);
        if (bytes.size() > 512 * 1024 * 1024)
            return;
        Header h{version, key.content, hash(bytes), bytes.size(), sizeof(SkinVertex), sizeof(glm::mat4)};
        auto destination = cachePath(key);
        std::filesystem::create_directories(destination.parent_path());
        static std::atomic<uint64_t> serial{0};
        temporary = destination;
        temporary += std::string(".") +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                     std::to_string(serial++) + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(&h), sizeof(h));
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            out.close();
            if (!out)
                throw std::runtime_error("Cache write failed");
        }
        checkCancellation(stop);
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Cache publish failed");
#else
        std::filesystem::rename(temporary, destination);
#endif
    } catch (...) {
        std::error_code error;
        if (!temporary.empty())
            std::filesystem::remove(temporary, error);
        checkCancellation(stop); // Cache failure never prevents a valid source import.
    }
}
} // namespace one::asset_cache
