#pragma once
#include <chrono>
#include <cstdlib>
namespace one::vulkan {
struct MaterialAdmission {
    double usedMs = 0;
    size_t attempts = 0;
    bool reference = std::getenv("THEONE_REFERENCE_MATERIAL_ADMISSION") != nullptr;
    void reset() {
        usedMs = 0;
        attempts = 0;
    }
    bool available() const { return reference || !attempts || usedMs < 1.0; }
};
class MaterialWork {
    MaterialAdmission& admission;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

  public:
    explicit MaterialWork(MaterialAdmission& value) : admission(value) { ++admission.attempts; }
    ~MaterialWork() {
        admission.usedMs +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
};
} // namespace one::vulkan
