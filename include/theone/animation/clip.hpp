#pragma once
#include <theone/core/math.hpp>
#include <string>
#include <vector>
namespace one {
struct Channel {
    int node = -1, path = 0;
    bool step = false, cubic = false;
    std::vector<float> times;
    std::vector<glm::vec4> values, inTangents, outTangents;
};
struct Animation {
    std::string name;
    float duration = 0;
    std::vector<Channel> channels;
};
} // namespace one
