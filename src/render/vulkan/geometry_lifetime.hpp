#pragma once
#include <memory>
namespace one::vulkan {
// One token per resident model. Cached raw pointers may outlive retirement, but
// must never be dereferenced after this token is invalidated on the render thread.
struct GeometryLifetime { bool resident = true; };
inline bool resident(const std::shared_ptr<GeometryLifetime>& lifetime) {
    return lifetime && lifetime->resident;
}
}
