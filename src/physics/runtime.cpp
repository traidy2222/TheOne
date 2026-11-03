#include "runtime.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <mutex>
namespace one::physics_detail {
namespace {
std::mutex runtimeMutex;
unsigned users = 0;
} // namespace
RuntimeLease::RuntimeLease() {
    std::lock_guard lock(runtimeMutex);
    if (users == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        try {
            JPH::RegisterTypes();
        } catch (...) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
            throw;
        }
    }
    ++users;
}
RuntimeLease::~RuntimeLease() {
    std::lock_guard lock(runtimeMutex);
    if (--users == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}
} // namespace one::physics_detail
