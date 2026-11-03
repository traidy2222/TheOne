#pragma once
#include <filesystem>
#include <span>
#include <vector>
#include <cstdint>
namespace one::asset_cache {
// The archive decoder copies into model-owned storage; no view escapes this lifetime.
class MappedFile {
  public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    std::span<const uint8_t> bytes() const { return {data_, size_}; }

  private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
#ifdef _WIN32
    void* file_ = nullptr;
    void* mapping_ = nullptr;
#else
    std::vector<uint8_t> storage_;
#endif
};
} // namespace one::asset_cache
