#pragma once
#include <theone/assets/material.hpp>
#include <cgltf.h>
#include <filesystem>
namespace one::gltf {
Image decodeImage(const cgltf_image& image, const std::filesystem::path& path, const cgltf_options& options);
} // namespace one::gltf
