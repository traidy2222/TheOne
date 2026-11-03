#pragma once
#include <theone/core/load_job.hpp>
#include <theone/scene/world.hpp>
namespace one {
class SceneLoadJob : public LoadJob<World> {
  public:
    SceneLoadJob()
        : LoadJob([](const auto& path, std::stop_token stop) {
              return std::make_shared<World>(World::load(path, nullptr, stop));
          }) {}
};
} // namespace one
