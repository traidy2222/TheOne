#pragma once
#include <theone/assets/model.hpp>
#include <cgltf.h>
#include "core/validation.hpp"
#include <functional>
#include <algorithm>
#include <theone/core/cancellation.hpp>
#include <theone/geometry/prepared_mesh.hpp>
namespace one::gltf {
using detail::need;
inline std::string name(const char* value, const char* fallback) {
    return value ? value : fallback;
}
inline glm::vec4 read(const cgltf_accessor* a, size_t index, glm::vec4 fallback = glm::vec4(0)) {
    if (!a)
        return fallback;
    need(index < a->count, "Accessor index outside buffer");
    need(cgltf_accessor_read_float(a, index, glm::value_ptr(fallback), 4) != 0,
         "Cannot read accessor (unsupported sparse attribute)");
    for (int i = 0; i < 4; ++i)
        need(std::isfinite(fallback[i]), "Non-finite accessor value");
    return fallback;
}

void loadImages(Model& model, cgltf_data* raw, const std::filesystem::path& path,
                const cgltf_options& options, std::stop_token stop = {});
void loadMaterials(Model& model, cgltf_data* raw);
size_t loadMeshes(Model& model, cgltf_data* raw, std::stop_token stop = {});
void loadRig(Model& model, cgltf_data* raw);
void loadAnimations(Model& model, cgltf_data* raw, std::stop_token stop = {});
} // namespace one::gltf
