#include <theone/render/renderer.hpp>
#include "vulkan/backend.hpp"
namespace one {
void Renderer::setStellarScene(const StellarScene& scene) {
    validateStellarScene(scene);
    impl_->volumes.stellarScene = scene;
    impl_->volumes.stellarActive = true;
    impl_->volumes.hdr = true;
}
void Renderer::clearStellarScene() {
    impl_->volumes.stellarScene = {};
    impl_->volumes.stellarActive = false;
}
} // namespace one
