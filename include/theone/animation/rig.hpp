#pragma once
#include <theone/core/transform.hpp>
#include <string>
#include <vector>
namespace one {
struct Node {
    std::string name;
    int parent = -1, mesh = -1, skin = -1;
    Transform transform;
    glm::mat4 matrix{1};
    bool useMatrix = false, active = true;
};
struct Skin {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBind;
};
} // namespace one
