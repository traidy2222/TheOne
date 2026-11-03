#include "backend.hpp"
namespace one {
void Renderer::Impl::collectRetired(bool drain) {
    auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    // One shared per-frame allowance covers both frame start and asset synchronization.
    auto end = std::remove_if(retired.begin(), retired.end(), [&](auto& item) {
        if (item.after > completed ||
            (!drain && (stats.resourcesDestroyed >= 32 || stats.retirementMs + elapsed() >= .75)))
            return false;
        item.destroy();
        ++stats.resourcesDestroyed;
        return true;
    });
    retired.erase(end, retired.end());
    stats.retirementMs += elapsed();
}
} // namespace one
