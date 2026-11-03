#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <theone/assets/model.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <cstring>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage: one_import source.(fbx|obj|dae|3ds|md2|md3|md5mesh|ply|stl|iqm|gltf) output.glb");
        std::filesystem::path source=std::filesystem::absolute(argv[1]),output=std::filesystem::absolute(argv[2]);
        if(std::filesystem::exists(output))throw std::runtime_error("Output already exists; choose a new path");
        if(output.extension()!=".glb")throw std::runtime_error("Output must use .glb extension");
        Assimp::Importer importer;
        if(!importer.ReadFile(source.string(),aiProcess_Triangulate|aiProcess_GenSmoothNormals|aiProcess_LimitBoneWeights|aiProcess_ValidateDataStructure))throw std::runtime_error(importer.GetErrorString());
        std::unique_ptr<aiScene> scene(importer.GetOrphanedScene());
        std::vector<aiTexture*> textures;for(unsigned i=0;i<scene->mNumTextures;++i)textures.push_back(scene->mTextures[i]);
        std::unordered_map<std::string,unsigned> embedded;std::vector<std::string> warnings;
        for(unsigned i=0;i<scene->mNumMaterials;++i) for(int type=1;type<=static_cast<int>(aiTextureType_UNKNOWN);++type) {
            auto textureType=static_cast<aiTextureType>(type);auto* material=scene->mMaterials[i];
            for(unsigned index=0;index<material->GetTextureCount(textureType);++index) {
                aiString path;if(material->GetTexture(textureType,index,&path)!=AI_SUCCESS || path.C_Str()[0]=='*')continue;
                std::string key=path.C_Str();
                if(!embedded.contains(key)) {
                    auto file=(source.parent_path()/key).lexically_normal();std::ifstream in(file,std::ios::binary|std::ios::ate);
                    if(!in)throw std::runtime_error("Missing material texture: "+file.string());
                    auto size=static_cast<size_t>(in.tellg());if(size>64*1024*1024)throw std::runtime_error("Texture exceeds 64 MiB limit");in.seekg(0);
                    auto* texture=new aiTexture();texture->mWidth=static_cast<unsigned>(size);texture->mHeight=0;texture->pcData=new aiTexel[(size+sizeof(aiTexel)-1)/sizeof(aiTexel)];
                    in.read(reinterpret_cast<char*>(texture->pcData),static_cast<std::streamsize>(size));if(!in){delete texture;throw std::runtime_error("Texture read failed");}
                    auto extension=file.extension().string();if(!extension.empty())extension.erase(0,1);std::strncpy(texture->achFormatHint,extension.c_str(),8);
                    embedded[key]=static_cast<unsigned>(textures.size());textures.push_back(texture);
                }
                aiString ref(("*"+std::to_string(embedded[key])).c_str());material->AddProperty(&ref,AI_MATKEY_TEXTURE(textureType,index));
            }
        }
        delete[] scene->mTextures;scene->mNumTextures=static_cast<unsigned>(textures.size());scene->mTextures=new aiTexture*[textures.size()];std::copy(textures.begin(),textures.end(),scene->mTextures);
        std::filesystem::create_directories(output.parent_path());
        auto temporary=output;temporary+=".pending.glb";
        if(std::filesystem::exists(temporary))throw std::runtime_error("Pending import already exists; choose a new output path");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{temporary};
        Assimp::Exporter exporter;
        if(exporter.Export(scene.get(),"glb2",temporary.string())!=AI_SUCCESS)throw std::runtime_error(exporter.GetErrorString());
        auto model=one::Model::load(temporary);
        std::vector<one::Vertex> vertices;std::vector<one::Draw> draws;model->bake(glm::mat4(1),0,0,false,1,vertices,draws);
        nlohmann::json report={{"source",source.string()},{"output",output.string()},{"triangles",vertices.size()/3},{"nodes",model->nodes.size()},{"skins",model->skins.size()},{"clips",model->animations.size()},{"diagnostics",model->diagnostics},
            {"review",{"Check source units and axis conventions","Compare materials and animation with original","Gameplay, physics settings and scripts require a separate port"}}};
        std::filesystem::rename(temporary,output);
        auto reportPath=output;reportPath+=".report.json";std::ofstream log(reportPath);log<<report.dump(2);
        std::cout<<report.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"Import failed: "<<e.what()<<'\n';return 1;}
}
