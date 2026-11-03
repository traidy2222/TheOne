#include "prepared_cache.hpp"
#include "archive.hpp"
#include "geometry/image_intern.hpp"
#include <theone/geometry/prepared_mesh.hpp>
#include <theone/geometry/prepared_image.hpp>
namespace one::asset_cache {
template <class A, class T, class F> void shared(A& a, std::shared_ptr<const T>& value, F element) {
    bool present = bool(value);
    a.pod(present);
    if (!present) {
        if constexpr (A::reading)
            value.reset();
        return;
    }
    if constexpr (A::reading) {
        auto result = std::make_shared<T>();
        element(*result);
        value = std::move(result);
    } else
        element(const_cast<T&>(*value)); // Writer only observes values; no model/payload copy.
}
template <class A> void bytes(A& a, std::shared_ptr<const std::vector<uint8_t>>& value) {
    shared(a, value, [&](auto& v) { a.raw(v); });
}
template <class A> void archive(A& a, Model& model) {
    sequence(a, model.images, [&](Image& i) {
        a.text(i.name);
        fields(a, i.width, i.height, i.revision, i.preparedRevision);
        bool baseInMips = i.prepared && i.prepared->color && i.preparedRevision == i.revision &&
                          i.prepared->color->size() >= i.rgba.size() &&
                          std::equal(i.rgba.begin(), i.rgba.end(), i.prepared->color->begin());
        a.pod(baseInMips);
        if (!baseInMips)
            a.raw(i.rgba);
        shared(a, i.prepared, [&](PreparedImage& p) {
            a.pod(p.hash);
            a.raw(p.mips);
            bytes(a, p.color);
            bool same = p.color == p.data;
            a.pod(same);
            if (same) {
                if constexpr (A::reading)
                    p.data = p.color;
            } else
                bytes(a, p.data);
        });
        if constexpr (A::reading) {
            i.prepared = detail::internPreparedImage(std::move(i.prepared));
            if (baseInMips) {
                if (!i.prepared || i.width <= 0 || i.height <= 0 ||
                    uint64_t(i.width) * i.height * 4 > i.prepared->color->size())
                    throw std::runtime_error("Invalid cached base image");
                i.rgba.assign(i.prepared->color->begin(),
                              i.prepared->color->begin() + size_t(i.width) * i.height * 4);
            }
        }
    });
    sequence(a, model.materials, [&](Material& m) {
        a.text(m.name);
        fields(a, m.color, m.roughness, m.metallic, m.cutoff, m.image, m.blend, m.mask, m.unlit,
               m.emissiveFactor, m.normalScale, m.occlusionStrength);
        for (auto** pointer : {&m.metallicRoughness, &m.normal, &m.occlusion, &m.emissive}) {
            int32_t index = -1;
            if constexpr (!A::reading)
                for (size_t i = 0; i < model.images.size(); ++i)
                    if (*pointer == &model.images[i])
                        index = int32_t(i);
            a.pod(index);
            if constexpr (A::reading)
                *pointer = index < 0 ? nullptr : &model.images.at(index);
        }
    });
    sequence(a, model.meshes, [&](auto& mesh) {
        sequence(a, mesh, [&](Primitive& p) {
            a.raw(p.vertices);
            fields(a, p.material, p.bounds);
            shared(a, p.prepared, [&](PreparedMesh& m) {
                a.raw(m.vertices);
                sequence(a, m.levels, [&](MeshLod& l) {
                    a.raw(l.indices);
                    a.pod(l.error);
                });
                bool packed = bool(m.packed);
                a.pod(packed);
                if constexpr (A::reading)
                    if (packed) {
                        size_t size = m.vertices.size() * sizeof(SkinVertex);
                        for (const auto& lod : m.levels)
                            size += lod.indices.size() * sizeof(uint32_t);
                        auto data = std::make_shared<std::vector<uint8_t>>(size);
                        size_t offset = m.vertices.size() * sizeof(SkinVertex);
                        if (offset)
                            std::memcpy(data->data(), m.vertices.data(), offset);
                        for (const auto& lod : m.levels) {
                            checkCancellation(a.stop);
                            auto count = lod.indices.size() * sizeof(uint32_t);
                            if (count)
                                std::memcpy(data->data() + offset, lod.indices.data(), count);
                            offset += count;
                        }
                        m.packed = std::move(data);
                    }
                fields(a, m.bounds.rigid, m.bounds.unweighted, m.bounds.safe);
                a.raw(m.bounds.joints);
                fields(a, m.centerSum, m.unweightedSum, m.maxJoint);
                a.raw(m.jointSums);
            });
        });
    });
    sequence(a, model.nodes, [&](Node& n) {
        a.text(n.name);
        fields(a, n.parent, n.mesh, n.skin, n.transform.position, n.transform.scale, n.transform.rotation,
               n.matrix, n.useMatrix, n.active);
    });
    sequence(a, model.skins, [&](Skin& s) {
        a.raw(s.joints);
        a.raw(s.inverseBind);
    });
    sequence(a, model.animations, [&](Animation& animation) {
        a.text(animation.name);
        a.pod(animation.duration);
        sequence(a, animation.channels, [&](Channel& c) {
            fields(a, c.node, c.path, c.step, c.cubic);
            a.raw(c.times);
            a.raw(c.values);
            a.raw(c.inTangents);
            a.raw(c.outTangents);
        });
    });
    sequence(a, model.diagnostics, [&](std::string& s) { a.text(s); });
}
std::vector<uint8_t> encode(const Model& model, std::stop_token stop) {
    Writer writer{{}, stop};
    archive(writer, const_cast<Model&>(model));
    return std::move(writer.bytes);
}
std::shared_ptr<Model> decode(std::span<const uint8_t> bytes, std::stop_token stop) {
    Reader reader{bytes, stop};
    auto model = std::make_shared<Model>();
    archive(reader, *model);
    if (!reader.bytes.empty())
        throw std::runtime_error("Trailing prepared cache bytes");
    return model;
}
} // namespace one::asset_cache
