#include "mapped_file.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fstream>
#endif
namespace one::asset_cache {
MappedFile::MappedFile(const std::filesystem::path& path) {
#ifdef _WIN32
    auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    file_ = file;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 || length.QuadPart > 512ll * 1024 * 1024 + 4096)
        return;
    mapping_ = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_)
        return;
    data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    if (data_)
        size_ = size_t(length.QuadPart);
#else
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    auto size = in.tellg();
    if (size <= 0 || size > 512ll * 1024 * 1024 + 4096)
        return;
    storage_.resize(size_t(size));
    in.seekg(0);
    if (in.read(reinterpret_cast<char*>(storage_.data()), size)) {
        data_ = storage_.data();
        size_ = storage_.size();
    }
#endif
}
MappedFile::~MappedFile() {
#ifdef _WIN32
    if (data_)
        UnmapViewOfFile(data_);
    if (mapping_)
        CloseHandle(mapping_);
    if (file_)
        CloseHandle(file_);
#endif
}
} // namespace one::asset_cache
