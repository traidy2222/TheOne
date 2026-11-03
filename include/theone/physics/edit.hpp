#pragma once
#include <theone/physics/physics.hpp>
#include <theone/core/load_job.hpp>
namespace one {
namespace physics_detail {
struct EditBaseline;
struct EditPlan;
} // namespace physics_detail
struct PhysicsEditRequest {
    World world;
    std::shared_ptr<const physics_detail::EditBaseline> baseline;
};
struct PhysicsEditStats {
    uint32_t added = 0, removed = 0, replaced = 0, moved = 0, unchanged = 0;
};
struct PhysicsEdit {
    World world;
    PhysicsEditStats stats;
    static std::shared_ptr<PhysicsEdit> prepare(const PhysicsEditRequest&, std::stop_token stop = {});

  private:
    friend class Physics;
    std::shared_ptr<physics_detail::EditPlan> plan;
};
class PhysicsEditJob : public LoadJob<PhysicsEdit, PhysicsEditRequest> {
  public:
    PhysicsEditJob() : LoadJob(PhysicsEdit::prepare) {}
};
} // namespace one
