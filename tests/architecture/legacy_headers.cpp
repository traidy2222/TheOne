#include <content.hpp>
#include <renderer.hpp>
#include <physics.hpp>
#include <effects.hpp>
#include <input.hpp>

// Compile/link the original entry points without rebuilding or running consuming games.
void legacyHeadersLinkCheck() {
    Renderer renderer;
    one::ParticleSystem particles;
    particles.step(0);
}
