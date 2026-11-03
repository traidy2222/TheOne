#pragma once
#include <theone/assets/model.hpp>
namespace one {
struct Entity {
    uint64_t id = 0, parent = 0;
    std::string name;
    Transform transform;
    std::string asset;
    std::shared_ptr<Model> model;
    bool visible = true, animate = true;
    int clip = 0;
    // Negative uses the world's shared clock; non-negative enables per-entity playback.
    float animationTime = -1;
    int previousClip = -1;
    float previousAnimationTime = 0, animationBlend = 1;
    bool animationLoop = true, previousAnimationLoop = true;
    std::string body = "none";
    glm::vec3 halfExtent{0.5f};
    float mass = 1;
    std::string behavior;
    float speed = 1;
};
} // namespace one
