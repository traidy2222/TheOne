#include "content.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
    try {
        if(argc<2)throw std::runtime_error("Usage: one_inspect asset.glb [report.json]");
        auto model=one::Model::load(argv[1]);std::vector<one::Vertex> vertices;std::vector<one::Draw> draws;
        model->bake(glm::mat4(1),0,0,true,1,vertices,draws);glm::vec3 min(1e20f),max(-1e20f);
        for(const auto& v:vertices){min=glm::min(min,v.position);max=glm::max(max,v.position);}
        nlohmann::json clips=nlohmann::json::array();
        for(size_t i=0;i<model->animations.size();++i){vertices.clear();draws.clear();model->bake(glm::mat4(1),model->animations[i].duration*.25f,static_cast<int>(i),true,1,vertices,draws);clips.push_back({{"name",model->animations[i].name},{"duration",model->animations[i].duration}});}
        nlohmann::json report={{"source",std::filesystem::absolute(argv[1]).string()},{"triangles",vertices.size()/3},{"nodes",model->nodes.size()},{"skins",model->skins.size()},{"images",model->images.size()},{"clips",clips},
            {"bounds",{{min.x,min.y,min.z},{max.x,max.y,max.z}}},{"diagnostics",model->diagnostics}};
        std::cout<<report.dump(2)<<'\n';if(argc>2){std::ofstream file(argv[2]);file<<report.dump(2);if(!file)throw std::runtime_error("Report write failed");}return 0;
    }catch(const std::exception& e){std::cerr<<"Inspection failed: "<<e.what()<<'\n';return 1;}
}
