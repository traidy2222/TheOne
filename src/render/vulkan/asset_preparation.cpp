#include "backend.hpp"
namespace one {
std::shared_ptr<const PreparedImage> Renderer::Impl::preparedImage(const Image* image) {
    const auto& source = image ? *image : preparation.white;
    if (source.prepared && source.preparedRevision == source.revision)
        return source.prepared;
    auto& pending = preparation.images[image];
    if (!pending.future.valid() || pending.revision != source.revision) {
        // Public mutable pixel vectors require one owned snapshot. Hashing/mips run on the worker.
        Image snapshot = source;
        snapshot.prepared.reset();
        pending.revision = source.revision;
        pending.future =
            preparation.worker.submit([snapshot = std::move(snapshot)] { return prepareImage(snapshot); });
    }
    if (pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return {};
    return pending.future.get();
}
std::shared_ptr<const PreparedMesh> Renderer::Impl::preparedMesh(const Draw& draw) {
    if (draw.geometry->prepared && draw.geometry->prepared->packed)
        return draw.geometry->prepared;
    auto& pending = preparation.meshes[draw.geometry];
    if (!pending.future.valid() || pending.owner.lock() != draw.owner) {
        pending.owner = draw.owner;
        pending.future = preparation.worker.submit(
            [owner = draw.owner, primitive = draw.geometry] { return prepareMesh(primitive->vertices); });
    }
    if (pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return {};
    return pending.future.get();
}
} // namespace one
