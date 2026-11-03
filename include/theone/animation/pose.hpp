#pragma once
#include <theone/animation/rig.hpp>
#include <theone/animation/clip.hpp>
namespace one {
std::vector<glm::mat4> samplePose(const std::vector<Node>& nodes, const std::vector<Animation>& animations,
                                  float time, int clip, bool animate, int previousClip = -1,
                                  float previousTime = 0, float blend = 1, bool loop = true,
                                  bool previousLoop = true);
}
