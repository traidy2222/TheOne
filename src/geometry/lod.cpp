#include <theone/geometry/prepared_mesh.hpp>
#include <meshoptimizer.h>
#include <algorithm>
namespace one {
void buildMeshLods(PreparedMesh& mesh, std::stop_token stop) {
    // Skinned geometry requires deformation-aware simplification. Keep its original topology.
    if (mesh.vertices.empty() || !mesh.bounds.joints.empty() || mesh.levels[0].indices.size() < 384)
        return;
    const auto& base = mesh.levels[0].indices;
    const float weights[] = {.5f, .5f, .5f, .25f, .25f, .25f, .25f, 1, 1};
    const auto* positions = glm::value_ptr(mesh.vertices[0].vertex.position);
    float scale = meshopt_simplifyScale(positions, mesh.vertices.size(), sizeof(SkinVertex));
    std::vector<MeshLod> generated;
    for (int tier = 1; tier <= 2; ++tier) {
        checkCancellation(stop);
        MeshLod lod;
        lod.indices.resize(base.size());
        float error = 0;
        size_t target = (base.size() / (tier == 1 ? 2 : 4)) / 3 * 3;
        size_t count = meshopt_simplifyWithAttributes(
            lod.indices.data(), base.data(), base.size(), positions, mesh.vertices.size(), sizeof(SkinVertex),
            glm::value_ptr(mesh.vertices[0].vertex.normal), sizeof(SkinVertex), weights, 9, nullptr, target,
            tier == 1 ? .005f : .02f, meshopt_SimplifyLockBorder, &error);
        size_t previous = generated.empty() ? base.size() : generated.back().indices.size();
        if (count < previous * 9 / 10 && count >= 3) {
            lod.indices.resize(count);
            lod.error = std::max(error * scale, scale * 1e-5f);
            generated.push_back(std::move(lod));
        }
    }
    for (auto& lod : generated)
        mesh.levels.push_back(std::move(lod));
}
size_t selectMeshLod(std::span<const float> errors, float pixelsPerUnit, size_t previous, float threshold) {
    if (errors.empty())
        return 0;
    previous = std::min(previous, errors.size() - 1);
    size_t selected = 0;
    for (size_t i = 1; i < errors.size(); ++i) {
        float limit = threshold * (i > previous ? .8f : 1.2f);
        if (errors[i] * pixelsPerUnit <= limit)
            selected = i;
    }
    return selected;
}
} // namespace one
