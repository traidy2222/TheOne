#include <theone/scene/world.hpp>
#include <theone/render/camera.hpp>
#include <theone/physics/physics.hpp>
#include <theone/effects/particles.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <chrono>
using json=nlohmann::json;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected assets directory");std::filesystem::path assets=argv[1];
        auto temp=std::filesystem::temp_directory_path()/("theone-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(temp);
        struct Clean {std::filesystem::path p;~Clean(){std::error_code ec;std::filesystem::remove_all(p,ec);}} cleanup{temp};
        one::Camera camera;auto projection=camera.projection(16.0f/9);
        auto near=projection*glm::vec4(0,0,-.1f,1),far=projection*glm::vec4(0,0,-300,1);
        require(std::abs(near.z/near.w)<.0001f && std::abs(far.z/far.w-1)<.0001f,"Vulkan depth projection failed");
        auto origin=camera.view()*glm::vec4(camera.position,1);require(glm::length(glm::vec3(origin))<.0001f,"Camera view transform failed");
        auto world=one::World::load(assets/"sandbox.one");require(world.entities.size()==12,"Fixture entity count mismatch");
        auto robot=world.find(2)->model;require(robot->skins.size()==1 && robot->skins[0].joints.size()==2 && robot->animations.size()==1,"Skin/animation import failed");
        require(!robot->images.empty() && robot->images[0].width==32 && robot->images[0].rgba.size()==4096,"Embedded PNG import failed");
        std::vector<one::Vertex> a,b;std::vector<one::Draw> da,db;
        robot->bake(glm::mat4(1),0,0,true,2,a,da);robot->bake(glm::mat4(1),.5f,0,true,2,b,db);
        require(a.size()==b.size() && a.size()>100,"Skinned geometry missing");size_t moved=0,fixed=0;
        for(size_t i=0;i<a.size();++i) {if(glm::distance(a[i].position,b[i].position)>.01f)++moved;else ++fixed;require(std::abs(glm::length(b[i].normal)-1)<.001f,"Skinned normal invalid");}
        require(moved>30 && fixed>30,"Bone animation did not move upper body while preserving lower body");
        // A real glTF CUBICSPLINE channel with a nonzero tangent and 2-second interval.
        const float cubicData[]={0,0,0, 1,0,0, 0,1,0, 0,2,
            0,0,0, 0,0,0, 4,0,0, 0,0,0, 10,0,0, 0,0,0};
        {std::ofstream f(temp/"cubic.bin",std::ios::binary);f.write(reinterpret_cast<const char*>(cubicData),sizeof(cubicData));}
        json cubic={{"asset",{{"version","2.0"}}},
            {"buffers",json::array({{{"uri","cubic.bin"},{"byteLength",sizeof(cubicData)}}})},
            {"bufferViews",json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",8}},{{"buffer",0},{"byteOffset",44},{"byteLength",72}}})},
            {"accessors",json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",1},{"componentType",5126},{"count",2},{"type","SCALAR"}},{{"bufferView",2},{"componentType",5126},{"count",6},{"type","VEC3"}}})},
            {"meshes",json::array({{{"primitives",json::array({{{"attributes",{{"POSITION",0}}}}})}}})},
            {"nodes",json::array({{{"mesh",0}}})},{"scenes",json::array({{{"nodes",json::array({0})}}})},{"scene",0},
            {"animations",json::array({{{"samplers",json::array({{{"input",1},{"output",2},{"interpolation","CUBICSPLINE"}}})},
                {"channels",json::array({{{"sampler",0},{"target",{{"node",0},{"path","translation"}}}}})}}})}};
        {std::ofstream f(temp/"cubic.gltf");f<<cubic;}
        auto cubicModel=one::Model::load(temp/"cubic.gltf");a.clear();da.clear();
        cubicModel->bake(glm::mat4(1),.5f,0,true,1,a,da);
        require(a.size()==3 && std::abs(a[0].position.x-2.6875f)<.0001f,"CUBICSPLINE import or Hermite tangent scaling failed");
        a.clear();da.clear();cubicModel->bake(glm::mat4(1),3.0f,0,true,1,a,da,-1,0,1,false);
        require(std::abs(a[0].position.x-10.0f)<.0001f,"Non-looping animation did not hold its last frame");
        cubicModel->animations.push_back(one::Animation{});
        a.clear();da.clear();cubicModel->bake(glm::mat4(1),.5f,0,true,1,a,da,1,0,.5f);
        require(std::abs(a[0].position.x-1.34375f)<.0001f,"Animation translation crossfade or default pose failed");
        auto& rotation=cubicModel->animations[0].channels[0];rotation.path=1;
        rotation.values={{0,0,0,1},{0,0,1,0}};rotation.inTangents={{0,0,0,0},{0,0,0,0}};rotation.outTangents=rotation.inTangents;
        a.clear();da.clear();cubicModel->bake(glm::mat4(1),.5f,0,true,1,a,da);
        auto expectedRotation=glm::normalize(glm::quat(.84375f,0,0,.15625f));
        require(glm::distance(a[1].position,expectedRotation*glm::vec3(1,0,0))<.0001f,"CUBICSPLINE quaternion normalization failed");
        a.clear();da.clear();cubicModel->bake(glm::mat4(1),.5f,0,true,1,a,da,1,0,.5f);
        auto blendedRotation=glm::slerp(glm::quat(1,0,0,0),expectedRotation,.5f);
        require(glm::distance(a[1].position,blendedRotation*glm::vec3(1,0,0))<.0001f,"Animation quaternion crossfade failed");
        auto childPosition=glm::vec3(world.matrix(4)[3]);require(glm::distance(childPosition,glm::vec3(3,2,0))<.001f,"Parent transform composition failed");
        world.find(2)->transform.position={4,2,1};world.find(30)->visible=false;world.save(temp/"roundtrip.one");auto loaded=one::World::load(temp/"roundtrip.one");
        require(loaded.entities.size()==world.entities.size() && loaded.find(4)->parent==3 && !loaded.find(30)->visible && loaded.find(2)->transform.position.x==4,"Scene roundtrip lost hierarchy or state");
        require(loaded.find(2)->model->skins.size()==1,"Scene save lost asset path resolution");
        auto bad=world;bad.find(3)->parent=4;bool rejected=false;try{bad.matrix(4);}catch(const std::exception&){rejected=true;}require(rejected,"Cyclic hierarchy must fail");
        json j;{std::ifstream f(assets/"sandbox.one");f>>j;}
        for(auto variant : {0,1,2,3}) {
            auto invalid=j;
            // Absolute asset paths keep these tests focused on validation, not path resolution.
            for(auto& e:invalid["entities"])if(e.contains("asset"))e["asset"]=(assets/e["asset"].get<std::string>()).generic_string();
            if(variant==0)invalid["entities"][1]["id"]=1;
            if(variant==1)invalid["entities"][1]["parent"]=999;
            if(variant==2)invalid["entities"][1]["scale"]={0,1,1};
            if(variant==3)invalid["version"]=99;
            {std::ofstream f(temp/"invalid.one");f<<invalid;}
            rejected=false;try{one::World::load(temp/"invalid.one");}catch(const std::exception&){rejected=true;}require(rejected,"Malformed scene accepted");
        }
        auto physical=one::World::load(assets/"sandbox.one");one::Physics physics;physics.rebuild(physical);
        for(int i=0;i<300;++i)physics.step(physical,1.0f/60,{0,0,0},false);
        require(physics.playerPosition().y>.65f && physics.playerPosition().y<1,"Player must rest on imported ground");
        require(physics.grounded(),"Player grounding query failed");
        for(uint64_t id=10;id<15;++id)require(physical.find(id)->transform.position.y>.3f,"Dynamic crate fell through imported collider");
        auto before=physics.playerPosition();physics.step(physical,1.0f/60,{0,0,0},true);
        for(int i=0;i<12;++i)physics.step(physical,1.0f/60,{0,0,0},false);
        require(physics.playerPosition().y>before.y+.5f,"Jump did not lift character");
        require(!physics.grounded(),"Expected player to be airborne before second jump");
        physics.setPlayerVelocity({0,-1,0});physics.step(physical,1.0f/60,{0,0,0},true,true);
        require(physics.playerVelocity().y>4,"Air jump did not restore upward velocity");
        // Walk into the near side of an imported pillar; capsule must stop rather than pass through.
        physics.resetPlayer({0,1,11});for(int i=0;i<180;++i)physics.step(physical,1.0f/60,{0,0,-3},false);
        require(physics.playerPosition().z>8.65f && physics.playerPosition().z<9.2f,"Player collision failed against imported pillar");
        physics.impulse(10,{0,10,0});auto motions=physics.motions();physics.rebuild(physical);physics.restoreMotions(motions);
        bool restored=false;for(const auto& m:physics.motions())if(m.id==10 && m.linear.y>1)restored=true;require(restored,"Rigid-body velocity restoration failed");
        one::ParticleSystem effects;for(int i=0;i<120;++i)effects.step(1.0f/60);require(effects.count()>0 && effects.count()<=128,"Particle lifetime/spawning failed");
        a.clear();da.clear();effects.bake(camera,a,da);require(!da.empty() && da[0].material->blend && a.size()%6==0,"Billboard particle geometry failed");
        std::cout<<"PASS: glTF texture/hierarchy/skin/animation, camera, scene validation/roundtrip, imported mesh collision, rigid bodies, player jump/wall\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
