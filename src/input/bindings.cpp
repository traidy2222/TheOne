#include <theone/input/bindings.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
namespace one {
InputBindings::InputBindings(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Missing input action map: " + path.string());
    nlohmann::json j;
    file >> j;
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto key = SDL_GetScancodeFromName(it.value().get<std::string>().c_str());
        if (key == SDL_SCANCODE_UNKNOWN)
            throw std::runtime_error("Unknown input binding: " + it.key());
        keys[it.key()] = key;
    }
    for (const char* name :
         {"forward", "back", "left", "right", "ascend", "descend", "fast", "jump", "interact"})
        if (!keys.contains(name))
            throw std::runtime_error(std::string("Missing action: ") + name);
}
} // namespace one
