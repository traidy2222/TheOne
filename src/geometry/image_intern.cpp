#include "image_intern.hpp"
#include <mutex>
#include <unordered_map>
#include <cstring>
namespace one::detail {
namespace {
std::mutex mutex;
std::unordered_multimap<uint64_t, std::weak_ptr<const PreparedImage>> cache;
std::shared_ptr<const PreparedImage> find(uint64_t hash, uint32_t width, uint32_t height,
                                          std::span<const uint8_t> rgba) {
    auto [first, last] = cache.equal_range(hash);
    for (auto it = first; it != last; ++it)
        if (auto p = it->second.lock(); p && !p->mips.empty() && p->mips[0].width == width &&
                                        p->mips[0].height == height && p->color->size() >= rgba.size() &&
                                        std::memcmp(p->color->data(), rgba.data(), rgba.size()) == 0)
            return p;
    return {};
}
} // namespace
std::shared_ptr<const PreparedImage> findPreparedImage(uint64_t hash, uint32_t width, uint32_t height,
                                                       std::span<const uint8_t> rgba) {
    std::lock_guard lock(mutex);
    return find(hash, width, height, rgba);
}
std::shared_ptr<const PreparedImage> internPreparedImage(std::shared_ptr<const PreparedImage> prepared) {
    if (!prepared || prepared->mips.empty())
        return prepared;
    const auto& base = prepared->mips[0];
    auto size = uint64_t(base.width) * base.height * 4;
    if (!prepared->color || size > prepared->color->size())
        throw std::runtime_error("Invalid cached mip payload");
    std::lock_guard lock(mutex);
    if (auto p = find(prepared->hash, base.width, base.height, {prepared->color->data(), size}))
        return p;
    if (cache.size() % 128 == 0)
        std::erase_if(cache, [](const auto& item) { return item.second.expired(); });
    cache.emplace(prepared->hash, prepared);
    return prepared;
}
} // namespace one::detail
