#pragma once
#include <theone/core/task_queue.hpp>
#include <theone/geometry/prepared_image.hpp>
#include <theone/geometry/prepared_mesh.hpp>
#include <unordered_map>
namespace one::vulkan {
struct ImagePreparation {
    uint64_t revision = 0;
    std::shared_future<std::shared_ptr<const PreparedImage>> future;
};
struct MeshPreparation {
    std::weak_ptr<const Model> owner;
    std::shared_future<std::shared_ptr<const PreparedMesh>> future;
};
struct PreparationState {
    TaskQueue worker;
    Image white;
    std::unordered_map<const Image*, ImagePreparation> images;
    std::unordered_map<const Primitive*, MeshPreparation> meshes;
};
} // namespace one::vulkan
