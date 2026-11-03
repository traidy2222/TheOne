#include "internal.hpp"
namespace one::gltf {
size_t loadMeshes(Model& model, cgltf_data* raw, std::stop_token stop) {
    size_t total = 0;
    for (size_t i = 0; i < raw->meshes_count; ++i) {
        std::vector<Primitive> mesh;
        for (size_t j = 0; j < raw->meshes[i].primitives_count; ++j) {
            checkCancellation(stop);
            const auto& p = raw->meshes[i].primitives[j];
            need(p.type == cgltf_primitive_type_triangles, "Only triangle primitives are supported");
            need(!p.has_draco_mesh_compression, "Draco-compressed geometry needs an external conversion");
            if (p.targets_count)
                model.diagnostics.push_back("Morph targets are not rendered");
            const cgltf_accessor *pos = nullptr, *normal = nullptr, *uv = nullptr, *color = nullptr,
                                 *joints = nullptr, *weights = nullptr;
            for (size_t k = 0; k < p.attributes_count; ++k) {
                const auto& a = p.attributes[k];
                if (a.type == cgltf_attribute_type_position)
                    pos = a.data;
                if (a.type == cgltf_attribute_type_normal)
                    normal = a.data;
                if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
                    uv = a.data;
                if (a.type == cgltf_attribute_type_color && a.index == 0)
                    color = a.data;
                if (a.type == cgltf_attribute_type_joints && a.index == 0)
                    joints = a.data;
                if (a.type == cgltf_attribute_type_weights && a.index == 0)
                    weights = a.data;
            }
            need(pos != nullptr, "Primitive has no POSITION");
            Primitive output;
            output.material = p.material ? static_cast<int>(p.material - raw->materials) : -1;
            size_t count = p.indices ? p.indices->count : pos->count;
            total += count;
            need(count % 3 == 0 && total <= 3000000,
                 "Invalid triangle count or asset exceeds 1 million triangles");
            output.vertices.reserve(count);
            for (size_t k = 0; k < count; ++k) {
                if ((k & 4095) == 0)
                    checkCancellation(stop);
                size_t index = p.indices ? cgltf_accessor_read_index(p.indices, k) : k;
                SkinVertex v;
                v.vertex.position = read(pos, index);
                v.vertex.normal = read(normal, index, {0, 1, 0, 0});
                v.vertex.uv = read(uv, index);
                v.vertex.color = read(color, index, glm::vec4(1));
                v.weights = read(weights, index);
                if (joints)
                    need(cgltf_accessor_read_uint(joints, index, glm::value_ptr(v.joints), 4) != 0,
                         "Cannot read joint indices");
                output.bounds.include(v.vertex.position);
                output.vertices.push_back(v);
            }
            if (!normal)
                for (size_t k = 0; k < count; k += 3) {
                    auto n = glm::cross(
                        output.vertices[k + 1].vertex.position - output.vertices[k].vertex.position,
                        output.vertices[k + 2].vertex.position - output.vertices[k].vertex.position);
                    n = glm::length(n) > 1e-8f ? glm::normalize(n) : glm::vec3(0, 1, 0);
                    for (size_t q = 0; q < 3; ++q)
                        output.vertices[k + q].vertex.normal = n;
                }
            output.prepared = prepareMesh(output.vertices, stop);
            mesh.push_back(std::move(output));
        }
        model.meshes.push_back(std::move(mesh));
    }
    return total;
}
} // namespace one::gltf
