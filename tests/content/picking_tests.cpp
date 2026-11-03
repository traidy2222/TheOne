#include <theone/scene/picking.hpp>
#include <theone/scene/world.hpp>
#include <iostream>
#include <limits>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected fixture scene");
        auto world = one::World::load(argv[1]);
        one::ScenePicker picker;
        size_t hits = 0, skinned = 0;
        for (int state = 0; state < 4; ++state) {
            if (state == 1)
                for (auto& e : world.entities)
                    if (e.model) {
                        e.transform.scale = {1.2f, .8f, 1.6f};
                        e.transform.position.x += .6f;
                    }
            if (state == 2)
                for (auto& e : world.entities)
                    if (e.model)
                        e.model = one::Model::load(e.model->source);
            if (state == 3) {
                std::reverse(world.entities.begin(), world.entities.end());
                world.entities.front().visible = false;
            }
            one::RenderScene scene;
            world.prepare(state * .37f, scene);
            std::vector<one::Vertex> vertices;
            std::vector<one::Draw> draws;
            world.bake(state * .37f, vertices, draws);
            for (const auto& target : draws) {
                if (target.count < 3)
                    continue;
                auto aim = (vertices[target.first].position + vertices[target.first + 1].position +
                            vertices[target.first + 2].position) /
                           3.f;
                glm::vec3 origin = aim + glm::vec3(2, 3, 7), direction = glm::normalize(aim - origin);
                float nearest = std::numeric_limits<float>::max();
                uint64_t id = 0;
                for (const auto& d : draws)
                    for (uint32_t i = 0; i + 2 < d.count; i += 3) {
                        auto a = vertices[d.first + i].position, b = vertices[d.first + i + 1].position,
                             c = vertices[d.first + i + 2].position;
                        auto e1 = b - a, e2 = c - a, h = glm::cross(direction, e2);
                        float det = glm::dot(e1, h);
                        if (std::abs(det) < 1e-7f)
                            continue;
                        auto offset = origin - a;
                        float u = glm::dot(offset, h) / det;
                        if (u < 0 || u > 1)
                            continue;
                        auto q = glm::cross(offset, e1);
                        float v = glm::dot(direction, q) / det;
                        if (v < 0 || u + v > 1)
                            continue;
                        float t = glm::dot(e2, q) / det;
                        if (t > 0 && t < nearest) {
                            nearest = t;
                            id = d.entity;
                        }
                    }
                auto result = picker.pick(scene, origin, direction);
                require(result.entity == id, "Picking entity differs from full bake");
                if (id)
                    require(std::abs(result.distance - nearest) < .001f, "Picking distance differs");
                hits += id != 0;
                skinned += result.skinnedVertices;
            }
        }
        require(hits > 0 && skinned > 0, "Picking fixture missed rigid/animated coverage");
        require(picker.pick({}, glm::vec3(0), glm::vec3(0, 0, 1)).entity == 0,
                "Empty scene picked old asset");
        picker.clear();
        std::cout
            << "PASS picking versus bake: animation, scale, movement, replacement, reorder, visibility; hits="
            << hits << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
