#include "jolt_state.hpp"
namespace one {
void Physics::adoptPreparedModelOwners(const World& world) {
    // Collision was cooked from this exact source fingerprint. Only ownership changes.
    for (auto& [id, collider] : impl_->colliders)
        if (auto e = world.find(id); e && e->model && collider.entity.model) {
            const auto& old = collider.entity.model;
            if (old->sourceFingerprint && old->sourceFingerprint == e->model->sourceFingerprint)
                collider.entity.model = e->model;
        }
}
} // namespace one
