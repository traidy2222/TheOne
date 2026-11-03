#include <theone/scene/world.hpp>
#include "core/validation.hpp"
namespace one {
std::shared_ptr<Model> World::reloadAsset(const std::filesystem::path& asset) {
    auto path = std::filesystem::absolute(source.parent_path() / asset).lexically_normal();
    auto replacement = Model::load(path);
    replaceAsset(path, replacement);
    return replacement;
}
void World::replaceAsset(const std::filesystem::path& asset, std::shared_ptr<Model> replacement) {
    auto path = std::filesystem::absolute(source.parent_path() / asset).lexically_normal();
    detail::need(replacement && std::filesystem::absolute(replacement->source).lexically_normal() == path,
                 "Replacement source does not match asset");
    std::shared_ptr<Model> previous;
    for (const auto& e : entities)
        if (e.model && std::filesystem::absolute(e.model->source).lexically_normal() == path) {
            previous = e.model;
            break;
        }
    detail::need(bool(previous), "Asset is not referenced by this scene");
    replacement->handle = {previous->handle.id, previous->handle.revision + 1};
    for (auto& e : entities)
        if (e.model && std::filesystem::absolute(e.model->source).lexically_normal() == path)
            e.model = replacement;
}
} // namespace one
