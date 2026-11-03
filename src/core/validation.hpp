#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace one::detail {
inline void need(bool ok, std::string_view message) {
    if (!ok)
        throw std::runtime_error(std::string(message));
}
} // namespace one::detail
