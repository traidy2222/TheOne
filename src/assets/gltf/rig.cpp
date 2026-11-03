#include "internal.hpp"
namespace one::gltf {
void loadRig(Model& model, cgltf_data* raw) {
    for (size_t i = 0; i < raw->nodes_count; ++i) {
        const auto& n = raw->nodes[i];
        Node output;
        output.name = name(n.name, "node");
        output.parent = n.parent ? static_cast<int>(n.parent - raw->nodes) : -1;
        output.mesh = n.mesh ? static_cast<int>(n.mesh - raw->meshes) : -1;
        output.skin = n.skin ? static_cast<int>(n.skin - raw->skins) : -1;
        output.transform.position = glm::make_vec3(n.translation);
        output.transform.scale = glm::make_vec3(n.scale);
        output.transform.rotation = glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]);
        output.useMatrix = n.has_matrix != 0;
        if (output.useMatrix)
            output.matrix = glm::make_mat4(n.matrix);
        output.active = raw->scene == nullptr;
        model.nodes.push_back(output);
    }
    if (raw->scene) {
        std::function<void(cgltf_node*)> activate = [&](cgltf_node* n) {
            model.nodes[n - raw->nodes].active = true;
            for (size_t i = 0; i < n->children_count; ++i)
                activate(n->children[i]);
        };
        for (size_t i = 0; i < raw->scene->nodes_count; ++i)
            activate(raw->scene->nodes[i]);
    }
    for (size_t i = 0; i < raw->skins_count; ++i) {
        const auto& s = raw->skins[i];
        Skin output;
        for (size_t j = 0; j < s.joints_count; ++j) {
            output.joints.push_back(static_cast<int>(s.joints[j] - raw->nodes));
            glm::mat4 matrix(1);
            if (s.inverse_bind_matrices)
                need(cgltf_accessor_read_float(s.inverse_bind_matrices, j, glm::value_ptr(matrix), 16) != 0,
                     "Cannot read inverse bind matrix");
            output.inverseBind.push_back(matrix);
        }
        model.skins.push_back(std::move(output));
    }
}
} // namespace one::gltf
