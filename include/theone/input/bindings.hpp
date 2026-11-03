#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <unordered_map>
#include <string>
namespace one {
struct InputBindings {
    std::unordered_map<std::string, SDL_Scancode> keys;
    explicit InputBindings(const std::filesystem::path& path);
    SDL_Scancode key(const std::string& action) const { return keys.at(action); }
};
} // namespace one
