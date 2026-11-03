#pragma once
#include <theone/render/draw.hpp>
#include <theone/render/camera.hpp>
namespace one {
class ParticleSystem {
    struct Particle {
        glm::vec3 position, velocity;
        float life, maxLife;
    };
    std::vector<Particle> particles_;
    float timer_ = 0;
    uint32_t random_ = 0x13579u;
    Image image_;
    Material material_;
    float random() {
        random_ = 1664525u * random_ + 1013904223u;
        return static_cast<float>(random_ & 65535u) / 65535;
    }

  public:
    ParticleSystem();
    void step(float dt);
    void bake(const Camera& camera, std::vector<Vertex>& vertices, std::vector<Draw>& draws) const;
    size_t count() const { return particles_.size(); }
};
} // namespace one
