#include "dependency_manifest.hpp"
#include <fstream>
#include <atomic>
#include <chrono>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace one::asset_cache {
namespace {
constexpr uint64_t format = 0x4f4e454445500001ull;
uint64_t checksum(std::string_view bytes) {
    uint64_t h = 14695981039346656037ull;
    for (auto b : bytes)
        h = (h ^ uint8_t(b)) * 1099511628211ull;
    return h;
}
struct Header {
    uint64_t version, hash, size;
};
} // namespace
uint64_t sourceLocation(const std::filesystem::path& source) {
    return checksum(std::filesystem::weakly_canonical(source).generic_string());
}
std::vector<std::filesystem::path> readDependencies(const std::filesystem::path& source) {
    auto path = cachePath({0, sourceLocation(source)});
    if (path.empty())
        return {};
    path.replace_extension(".deps");
    std::ifstream in(path, std::ios::binary);
    Header h{};
    if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)) || h.version != format || h.size > 1024 * 1024)
        return {};
    std::string bytes(size_t(h.size), '\0');
    if (!in.read(bytes.data(), bytes.size()) || checksum(bytes) != h.hash)
        return {};
    std::vector<std::filesystem::path> paths;
    for (size_t start = 0; start < bytes.size();) {
        auto end = bytes.find('\0', start);
        if (end == std::string::npos || end == start || paths.size() >= 100000)
            return {};
        paths.emplace_back(bytes.substr(start, end - start));
        start = end + 1;
    }
    if (paths.empty() || paths.front() != std::filesystem::weakly_canonical(source))
        return {};
    return paths;
}
void writeDependencies(Key key, std::span<const std::filesystem::path> dependencies) {
    auto path = cachePath(key);
    if (path.empty() || dependencies.empty())
        return;
    path.replace_extension(".deps");
    std::filesystem::path temporary;
    try {
        std::string bytes;
        for (const auto& dep : dependencies) {
            bytes += std::filesystem::weakly_canonical(dep).generic_string();
            bytes += '\0';
        }
        if (bytes.size() > 1024 * 1024)
            return;
        Header h{format, checksum(bytes), bytes.size()};
        static std::atomic<uint64_t> serial{0};
        temporary = path;
        temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
                     std::to_string(serial++) + ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(bytes.data(), bytes.size());
        out.close();
        if (!out)
            throw std::runtime_error("Dependency manifest write failed");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Dependency manifest publish failed");
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code error;
        if (!temporary.empty())
            std::filesystem::remove(temporary, error);
    }
}
} // namespace one::asset_cache
