#pragma once
#include "content.hpp"
namespace one {
struct BodyMotion {uint64_t id;glm::vec3 linear,angular;};
class Physics {
    struct Impl; std::unique_ptr<Impl> impl_;
public:
    Physics(); ~Physics(); Physics(const Physics&)=delete; Physics& operator=(const Physics&)=delete;
    void rebuild(const World& world);
    void step(World& world,float dt,glm::vec3 movement,bool jump);
    glm::vec3 playerPosition() const;
    bool grounded() const;
    void resetPlayer(glm::vec3 position={0,2,10});
    std::vector<BodyMotion> motions() const;
    void restoreMotions(const std::vector<BodyMotion>& motions);
    glm::vec3 playerVelocity() const;
    void setPlayerVelocity(glm::vec3 velocity);
    void impulse(uint64_t id,glm::vec3 impulse);
};
}
