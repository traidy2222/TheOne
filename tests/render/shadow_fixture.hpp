#pragma once
#include <theone/assets/model.hpp>
#include <theone/geometry/prepared_mesh.hpp>
inline std::shared_ptr<one::Model> shadowGrid(unsigned cells, float size, bool ripple = false) {
    auto model = std::make_shared<one::Model>();
    one::Primitive primitive;
    primitive.material = 0;
    auto vertex = [&](unsigned x, unsigned y) {
        one::SkinVertex v;
        float px = (float(x) / cells - .5f) * size, py = (float(y) / cells - .5f) * size;
        v.vertex.position = {px, py, ripple ? .03f * std::sin(px * 3) * std::cos(py * 3) : 0};
        v.vertex.normal = {0, 0, 1};
        return v;
    };
    for (unsigned y = 0; y < cells; ++y)
        for (unsigned x = 0; x < cells; ++x)
            primitive.vertices.insert(primitive.vertices.end(),
                                      {vertex(x, y), vertex(x + 1, y), vertex(x + 1, y + 1), vertex(x, y),
                                       vertex(x + 1, y + 1), vertex(x, y + 1)});
    primitive.prepared = one::prepareMesh(primitive.vertices);
    model->meshes.push_back({std::move(primitive)});
    one::Node node;
    node.mesh = 0;
    model->nodes.push_back(node);
    model->materials.emplace_back();
    return model;
}
