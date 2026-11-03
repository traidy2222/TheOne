#include <theone/geometry/prepared_mesh.hpp>
#include <unordered_map>
#include <string_view>
#include <cstring>
namespace one {
std::shared_ptr<const PreparedMesh> prepareMesh(std::span<const SkinVertex> vertices, std::stop_token stop) {
    auto out = std::make_shared<PreparedMesh>();
    out->levels.emplace_back();
    auto& indices = out->levels[0].indices;
    indices.reserve(vertices.size());
    std::unordered_map<std::string_view, uint32_t> lookup;
    lookup.reserve(vertices.size());
    static_assert(sizeof(SkinVertex) == 80);
    size_t count = 0;
    for (const auto& v : vertices) {
        if ((count++ & 4095) == 0)
            checkCancellation(stop);
        auto [it, inserted] = lookup.emplace(std::string_view(reinterpret_cast<const char*>(&v), sizeof(v)),
                                             uint32_t(out->vertices.size()));
        if (inserted)
            out->vertices.push_back(v);
        indices.push_back(it->second);
        auto p = glm::vec4(v.vertex.position, 1);
        out->centerSum += p;
        out->bounds.rigid.include(v.vertex.position);
        float weight = glm::dot(v.weights, glm::vec4(1));
        if (glm::any(glm::lessThan(v.weights, glm::vec4(0))))
            out->bounds.safe = false;
        if (weight > 1e-5f) {
            for (int j = 0; j < 4; ++j)
                if (v.weights[j] > 0) {
                    auto joint = v.joints[j];
                    if (joint >= 65536)
                        throw std::runtime_error("Joint index exceeds geometry limit");
                    out->maxJoint = std::max(out->maxJoint, joint);
                    if (out->jointSums.size() <= joint) {
                        out->jointSums.resize(size_t(joint) + 1, glm::vec4(0));
                        out->bounds.joints.resize(size_t(joint) + 1);
                    }
                    out->jointSums[joint] += p * (v.weights[j] / weight);
                    out->bounds.joints[joint].include(v.vertex.position);
                }
        } else {
            out->unweightedSum += p;
            out->bounds.unweighted.include(v.vertex.position);
        }
    }
    buildMeshLods(*out, stop);
    size_t bytes = out->vertices.size() * sizeof(SkinVertex);
    for (const auto& lod : out->levels)
        bytes += lod.indices.size() * sizeof(uint32_t);
    auto packed = std::make_shared<std::vector<uint8_t>>(bytes);
    size_t offset = out->vertices.size() * sizeof(SkinVertex);
    if (offset)
        std::memcpy(packed->data(), out->vertices.data(), offset);
    for (const auto& lod : out->levels) {
        checkCancellation(stop);
        auto count = lod.indices.size() * sizeof(uint32_t);
        if (count)
            std::memcpy(packed->data() + offset, lod.indices.data(), count);
        offset += count;
    }
    out->packed = std::move(packed);
    return out;
}
} // namespace one
