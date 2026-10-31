#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace one {
struct Transform {
    glm::vec3 position{0}, scale{1}; glm::quat rotation{1,0,0,0};
    glm::mat4 matrix() const { return glm::translate(glm::mat4(1),position)*glm::mat4_cast(rotation)*glm::scale(glm::mat4(1),scale); }
};
struct Vertex { glm::vec3 position{0}, normal{0,1,0}; glm::vec4 color{1}; glm::vec2 uv{0}; };
struct Image { std::string name; int width=1,height=1; std::vector<uint8_t> rgba{255,255,255,255}; };
struct Material { std::string name; glm::vec4 color{1}; float roughness=1,metallic=0,cutoff=0.5f; int image=-1; bool blend=false,mask=false,unlit=false;
    const Image *metallicRoughness=nullptr,*normal=nullptr,*occlusion=nullptr,*emissive=nullptr;
    glm::vec3 emissiveFactor{0}; float normalScale=1,occlusionStrength=1; };
struct SkinVertex { Vertex vertex; glm::uvec4 joints{0}; glm::vec4 weights{0}; };
struct Primitive { std::vector<SkinVertex> vertices; int material=-1; };
struct Node { std::string name; int parent=-1,mesh=-1,skin=-1; Transform transform; glm::mat4 matrix{1}; bool useMatrix=false,active=true; };
struct Skin { std::vector<int> joints; std::vector<glm::mat4> inverseBind; };
struct Channel { int node=-1,path=0; bool step=false; std::vector<float> times; std::vector<glm::vec4> values; };
struct Animation { std::string name; float duration=0; std::vector<Channel> channels; };
struct Draw { uint32_t first=0,count=0; const Material* material=nullptr; const Image* image=nullptr; uint64_t entity=0; };
struct Model {
    std::filesystem::path source;
    std::vector<Image> images; std::vector<Material> materials;
    std::vector<std::vector<Primitive>> meshes; std::vector<Node> nodes; std::vector<Skin> skins; std::vector<Animation> animations;
    std::vector<std::string> diagnostics;
    static std::shared_ptr<Model> load(const std::filesystem::path& path);
    void bake(const glm::mat4& root,float time,int clip,bool animate,uint64_t id,std::vector<Vertex>& vertices,std::vector<Draw>& draws) const;
};
struct Entity {
    uint64_t id=0,parent=0; std::string name; Transform transform; std::string asset;
    std::shared_ptr<Model> model;
    bool visible=true,animate=true; int clip=0;
    std::string body="none"; glm::vec3 halfExtent{0.5f}; float mass=1;
    std::string behavior; float speed=1;
};
struct World {
    std::filesystem::path source;
    std::vector<Entity> entities;
    std::vector<std::string> diagnostics;
    Entity* find(uint64_t id);
    const Entity* find(uint64_t id) const;
    glm::mat4 matrix(uint64_t id) const;
    static World load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    void bake(float time,std::vector<Vertex>& vertices,std::vector<Draw>& draws) const;
};
struct Camera {
    glm::vec3 position{11,8,15}; float yaw=-2.51f,pitch=-0.35f;
    glm::vec3 forward() const { return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)}; }
    glm::mat4 view() const { return glm::lookAtRH(position,position+forward(),{0,1,0}); }
    glm::mat4 projection(float aspect) const { auto m=glm::perspectiveRH_ZO(1.05f,aspect,0.1f,300.0f); m[1][1]*=-1; return m; }
};
}
