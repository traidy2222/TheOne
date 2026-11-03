#include <theone/effects/particles.hpp>
#include <algorithm>
namespace one {
ParticleSystem::ParticleSystem() {
    image_.name = "Generated soft particle";
    image_.width = image_.height = 32;
    image_.rgba.resize(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            float d = glm::length(glm::vec2((x - 15.5f) / 15.5f, (y - 15.5f) / 15.5f));
            size_t i = (y * 32 + x) * 4;
            image_.rgba[i] = 255;
            image_.rgba[i + 1] = 170;
            image_.rgba[i + 2] = 45;
            image_.rgba[i + 3] = static_cast<uint8_t>(std::max(0.0f, 1 - d) * 200);
        }
    material_.name = "Particle sprite";
    material_.blend = true;
    material_.unlit = true;
    material_.color = {1, 1, 1, .7f};
}
void ParticleSystem::step(float dt) {
    for (auto& p : particles_) {
        p.life -= dt;
        p.position += p.velocity * dt;
        p.velocity.y += dt * .3f;
    }
    std::erase_if(particles_, [](const Particle& p) { return p.life <= 0; });
    timer_ += dt;
    while (timer_ > .04f && particles_.size() < 128) {
        timer_ -= .04f;
        float life = .8f + random() * 1.5f;
        particles_.push_back(
            {{0, .2f, -2}, {(random() - .5f) * .5f, 1 + random(), (random() - .5f) * .5f}, life, life});
    }
}
void ParticleSystem::bake(const Camera& camera, std::vector<Vertex>& vertices,
                          std::vector<Draw>& draws) const {
    auto right = glm::normalize(glm::cross(camera.forward(), glm::vec3(0, 1, 0))),
         up = glm::normalize(glm::cross(right, camera.forward()));
    Draw draw;
    draw.first = static_cast<uint32_t>(vertices.size());
    draw.material = &material_;
    draw.image = &image_;
    std::vector<const Particle*> sorted;
    for (const auto& p : particles_)
        sorted.push_back(&p);
    std::sort(sorted.begin(), sorted.end(), [&](auto a, auto b) {
        return glm::distance(a->position, camera.position) > glm::distance(b->position, camera.position);
    });
    for (const auto* p : sorted) {
        float scale = .13f + .2f * (1 - p->life / p->maxLife);
        glm::vec3 positions[]{
            p->position - right * scale - up * scale, p->position + right * scale - up * scale,
            p->position + right * scale + up * scale, p->position - right * scale + up * scale};
        glm::vec2 uv[]{{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int i : {0, 1, 2, 0, 2, 3})
            vertices.push_back(
                {positions[i], -camera.forward(), glm::vec4(1, 1, 1, p->life / p->maxLife), uv[i]});
    }
    draw.count = static_cast<uint32_t>(vertices.size()) - draw.first;
    if (draw.count)
        draws.push_back(draw);
}
} // namespace one
