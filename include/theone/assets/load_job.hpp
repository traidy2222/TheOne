#pragma once
#include <theone/core/load_job.hpp>
#include <theone/assets/model.hpp>
namespace one {
class ModelLoadJob : public LoadJob<Model> {
  public:
    ModelLoadJob()
        : LoadJob([](const auto& path, std::stop_token stop) { return Model::load(path, nullptr, stop); }) {}
};
class ImageLoadJob : public LoadJob<Image> {
  public:
    ImageLoadJob()
        : LoadJob([](const auto& path, std::stop_token stop) {
              checkCancellation(stop);
              auto result = std::make_shared<Image>();
              result->reload(path);
              checkCancellation(stop);
              return result;
          }) {}
};
} // namespace one
