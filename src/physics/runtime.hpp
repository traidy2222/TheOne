#pragma once
namespace one::physics_detail {
// Keeps shared Jolt registration alive until every independent physics world is destroyed.
class RuntimeLease {
  public:
    RuntimeLease();
    ~RuntimeLease();
    RuntimeLease(const RuntimeLease&) = delete;
    RuntimeLease& operator=(const RuntimeLease&) = delete;
};
} // namespace one::physics_detail
