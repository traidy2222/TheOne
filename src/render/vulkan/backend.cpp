#include "backend.hpp"
#include "presentation_pacing.hpp"
#include "asset_memory_snapshot.hpp"
#include "shadow_submission_state.hpp"
namespace one {
Renderer::Impl::Impl()
    : imageMemory(std::make_unique<vulkan::ImageMemorySnapshot>()),
      shadowSubmission(std::make_unique<vulkan::ShadowSubmission>()) {}
Renderer::Impl::~Impl() = default;
}
