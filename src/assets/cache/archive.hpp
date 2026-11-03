#pragma once
#include <theone/core/cancellation.hpp>
#include <span>
#include <vector>
#include <string>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace one::asset_cache {
// Private, versioned local cache format. Bounded reads fail back to source import.
struct Writer {
    static constexpr bool reading = false;
    std::vector<uint8_t> bytes;
    std::stop_token stop;
    template <class T> void pod(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto p = reinterpret_cast<const uint8_t*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    template <class T> void raw(std::vector<T>& values) {
        checkCancellation(stop);
        uint64_t size = values.size();
        pod(size);
        if (size) {
            auto p = reinterpret_cast<const uint8_t*>(values.data());
            bytes.insert(bytes.end(), p, p + size * sizeof(T));
        }
    }
    void text(std::string& value) {
        uint64_t size = value.size();
        pod(size);
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};
struct Reader {
    static constexpr bool reading = true;
    std::span<const uint8_t> bytes;
    std::stop_token stop;
    void need(size_t size) {
        if (size > bytes.size())
            throw std::runtime_error("Truncated prepared cache");
    }
    template <class T> void pod(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        need(sizeof(T));
        std::memcpy(&value, bytes.data(), sizeof(T));
        bytes = bytes.subspan(sizeof(T));
    }
    template <class T> void raw(std::vector<T>& values) {
        checkCancellation(stop);
        uint64_t size = 0;
        pod(size);
        if (size > bytes.size() / sizeof(T))
            throw std::runtime_error("Oversized cache vector");
        values.resize(size);
        if (size)
            std::memcpy(values.data(), bytes.data(), size * sizeof(T));
        bytes = bytes.subspan(size * sizeof(T));
    }
    void text(std::string& value) {
        uint64_t size = 0;
        pod(size);
        need(size);
        value.assign(reinterpret_cast<const char*>(bytes.data()), size);
        bytes = bytes.subspan(size);
    }
};
template <class A, class... T> void fields(A& a, T&... values) {
    (a.pod(values), ...);
}
template <class A, class T, class F> void sequence(A& a, std::vector<T>& values, F element) {
    checkCancellation(a.stop);
    uint64_t size = values.size();
    a.pod(size);
    if constexpr (A::reading) {
        if (size > 1000000 || size > a.bytes.size())
            throw std::runtime_error("Oversized cache sequence");
        values.resize(size);
    }
    for (auto& value : values)
        element(value);
}
} // namespace one::asset_cache
