#pragma once
#include <theone/scene/world.hpp>
namespace one::detail {
void loadSceneAssets(World& world, SceneLoadStats* stats, std::stop_token stop);
}
