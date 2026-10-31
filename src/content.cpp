#include "content.hpp"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <algorithm>


namespace one {
using json=nlohmann::json;
static void need(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
static std::string name(const char* value,const char* fallback) { return value?value:fallback; }
static glm::vec4 read(const cgltf_accessor* a,size_t index,glm::vec4 fallback=glm::vec4(0)) {
    if(!a) return fallback;
    need(index<a->count,"Accessor index outside buffer");
    need(cgltf_accessor_read_float(a,index,glm::value_ptr(fallback),4)!=0,"Cannot read accessor (unsupported sparse attribute)");
    for(int i=0;i<4;++i) need(std::isfinite(fallback[i]),"Non-finite accessor value");
    return fallback;
}
std::shared_ptr<Model> Model::load(const std::filesystem::path& path) {
    need(std::filesystem::file_size(path)<256*1024*1024,"Asset exceeds 256 MiB import limit");
    cgltf_options options{}; cgltf_data* raw=nullptr;
    need(cgltf_parse_file(&options,path.string().c_str(),&raw)==cgltf_result_success,"Cannot parse glTF: "+path.string());
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    need(cgltf_load_buffers(&options,raw,path.string().c_str())==cgltf_result_success,"Cannot load glTF buffers");
    need(cgltf_validate(raw)==cgltf_result_success,"glTF validation failed");
    need(raw->nodes_count<100000 && raw->meshes_count<100000,"Asset exceeds node/mesh limits");
    auto model=std::make_shared<Model>(); model->source=path;
    for(size_t i=0;i<raw->extensions_required_count;++i) {
        std::string e=raw->extensions_required[i];
        need(e=="KHR_materials_unlit","Unsupported required glTF extension: "+e);
    }
    for(size_t i=0;i<raw->extensions_used_count;++i) model->diagnostics.push_back("Extension: "+std::string(raw->extensions_used[i])+" (verify conversion semantics)");
    for(size_t i=0;i<raw->images_count;++i) {
        const auto& image=raw->images[i]; Image output; output.name=name(image.name,"image");
        stbi_uc* decoded=nullptr; int channels=0;
        if(image.buffer_view) {
            need(image.buffer_view->size<64*1024*1024,"Encoded texture exceeds import limit");
            decoded=stbi_load_from_memory(cgltf_buffer_view_data(image.buffer_view),static_cast<int>(image.buffer_view->size),&output.width,&output.height,&channels,4);
        } else if(image.uri) {
            std::string uri=image.uri;
            if(uri.starts_with("data:")) {
                auto comma=uri.find(','); need(comma!=std::string::npos && uri.substr(0,comma).ends_with(";base64"),"Unsupported image data URI");
                auto encoded=uri.substr(comma+1); size_t bytes=encoded.size()/4*3;
                if(encoded.ends_with("==")) bytes-=2; else if(encoded.ends_with("=")) --bytes;
                void* memory=nullptr; need(bytes<64*1024*1024 && cgltf_load_buffer_base64(&options,bytes,encoded.c_str(),&memory)==cgltf_result_success,"Invalid texture base64");
                decoded=stbi_load_from_memory(static_cast<stbi_uc*>(memory),static_cast<int>(bytes),&output.width,&output.height,&channels,4); std::free(memory);
            } else {
                std::vector<char> decodedURI(uri.begin(),uri.end()); decodedURI.push_back(0); cgltf_decode_uri(decodedURI.data());
                decoded=stbi_load((path.parent_path()/decodedURI.data()).string().c_str(),&output.width,&output.height,&channels,4);
            }
        }
        std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(decoded,stbi_image_free);
        need(decoded && output.width>0 && output.height>0 && output.width<=8192 && output.height<=8192,"Unsupported or oversized image: "+output.name);
        output.rgba.assign(decoded,decoded+static_cast<size_t>(output.width)*output.height*4); model->images.push_back(std::move(output));
    }
    for(size_t i=0;i<raw->materials_count;++i) {
        const auto& input=raw->materials[i]; Material m; m.name=name(input.name,"material");
        const auto& p=input.pbr_metallic_roughness;
        m.color=glm::make_vec4(p.base_color_factor); m.roughness=p.roughness_factor; m.metallic=p.metallic_factor;
        if(p.base_color_texture.texture && p.base_color_texture.texture->image) m.image=static_cast<int>(p.base_color_texture.texture->image-raw->images);
        m.blend=input.alpha_mode==cgltf_alpha_mode_blend; m.mask=input.alpha_mode==cgltf_alpha_mode_mask; m.cutoff=input.alpha_cutoff;
        if(p.base_color_texture.texcoord!=0) model->diagnostics.push_back(m.name+": base color UV set is not TEXCOORD_0");
        auto imageFor=[&](const cgltf_texture_view& view)->const Image* {
            if(view.texcoord!=0) model->diagnostics.push_back(m.name+": secondary UV sets need conversion to TEXCOORD_0");
            if(view.has_transform) model->diagnostics.push_back(m.name+": texture transforms must be baked into UVs");
            if(view.texture && view.texture->sampler) model->diagnostics.push_back(m.name+": source sampler is approximated by linear/repeat, without mipmaps");
            return view.texture && view.texture->image?&model->images[view.texture->image-raw->images]:nullptr;
        };
        m.metallicRoughness=imageFor(p.metallic_roughness_texture); m.normal=imageFor(input.normal_texture);
        m.occlusion=imageFor(input.occlusion_texture); m.emissive=imageFor(input.emissive_texture); imageFor(p.base_color_texture);
        m.emissiveFactor=glm::make_vec3(input.emissive_factor); m.normalScale=input.normal_texture.scale; m.occlusionStrength=input.occlusion_texture.scale; m.unlit=input.unlit!=0;
        if(m.mask) model->diagnostics.push_back(m.name+": alpha-cutout shadows use solid geometry in this milestone");
        model->materials.push_back(m);
    }
    size_t total=0;
    for(size_t i=0;i<raw->meshes_count;++i) {
        std::vector<Primitive> mesh;
        for(size_t j=0;j<raw->meshes[i].primitives_count;++j) {
            const auto& p=raw->meshes[i].primitives[j]; need(p.type==cgltf_primitive_type_triangles,"Only triangle primitives are supported");
            need(!p.has_draco_mesh_compression,"Draco-compressed geometry needs an external conversion");
            if(p.targets_count) model->diagnostics.push_back("Morph targets are not rendered");
            const cgltf_accessor *pos=nullptr,*normal=nullptr,*uv=nullptr,*color=nullptr,*joints=nullptr,*weights=nullptr;
            for(size_t k=0;k<p.attributes_count;++k) {
                const auto& a=p.attributes[k];
                if(a.type==cgltf_attribute_type_position) pos=a.data;
                if(a.type==cgltf_attribute_type_normal) normal=a.data;
                if(a.type==cgltf_attribute_type_texcoord && a.index==0) uv=a.data;
                if(a.type==cgltf_attribute_type_color && a.index==0) color=a.data;
                if(a.type==cgltf_attribute_type_joints && a.index==0) joints=a.data;
                if(a.type==cgltf_attribute_type_weights && a.index==0) weights=a.data;
            }
            need(pos!=nullptr,"Primitive has no POSITION");
            Primitive output; output.material=p.material?static_cast<int>(p.material-raw->materials):-1;
            size_t count=p.indices?p.indices->count:pos->count; total+=count;
            need(count%3==0 && total<=3000000,"Invalid triangle count or asset exceeds 1 million triangles");
            output.vertices.reserve(count);
            for(size_t k=0;k<count;++k) {
                size_t index=p.indices?cgltf_accessor_read_index(p.indices,k):k; SkinVertex v;
                v.vertex.position=read(pos,index); v.vertex.normal=read(normal,index,{0,1,0,0});
                v.vertex.uv=read(uv,index); v.vertex.color=read(color,index,glm::vec4(1));
                v.weights=read(weights,index);
                if(joints) need(cgltf_accessor_read_uint(joints,index,glm::value_ptr(v.joints),4)!=0,"Cannot read joint indices");
                output.vertices.push_back(v);
            }
            if(!normal) for(size_t k=0;k<count;k+=3) {
                auto n=glm::cross(output.vertices[k+1].vertex.position-output.vertices[k].vertex.position,output.vertices[k+2].vertex.position-output.vertices[k].vertex.position);
                n=glm::length(n)>1e-8f?glm::normalize(n):glm::vec3(0,1,0);
                for(size_t q=0;q<3;++q) output.vertices[k+q].vertex.normal=n;
            }
            mesh.push_back(std::move(output));
        }
        model->meshes.push_back(std::move(mesh));
    }
    for(size_t i=0;i<raw->nodes_count;++i) {
        const auto& n=raw->nodes[i]; Node output; output.name=name(n.name,"node");
        output.parent=n.parent?static_cast<int>(n.parent-raw->nodes):-1; output.mesh=n.mesh?static_cast<int>(n.mesh-raw->meshes):-1; output.skin=n.skin?static_cast<int>(n.skin-raw->skins):-1;
        output.transform.position=glm::make_vec3(n.translation); output.transform.scale=glm::make_vec3(n.scale);
        output.transform.rotation=glm::quat(n.rotation[3],n.rotation[0],n.rotation[1],n.rotation[2]);
        output.useMatrix=n.has_matrix!=0; if(output.useMatrix) output.matrix=glm::make_mat4(n.matrix);
        output.active=raw->scene==nullptr; model->nodes.push_back(output);
    }
    if(raw->scene) {
        std::function<void(cgltf_node*)> activate=[&](cgltf_node* n) { model->nodes[n-raw->nodes].active=true; for(size_t i=0;i<n->children_count;++i) activate(n->children[i]); };
        for(size_t i=0;i<raw->scene->nodes_count;++i) activate(raw->scene->nodes[i]);
    }
    for(size_t i=0;i<raw->skins_count;++i) {
        const auto& s=raw->skins[i]; Skin output;
        for(size_t j=0;j<s.joints_count;++j) {
            output.joints.push_back(static_cast<int>(s.joints[j]-raw->nodes)); glm::mat4 matrix(1);
            if(s.inverse_bind_matrices) need(cgltf_accessor_read_float(s.inverse_bind_matrices,j,glm::value_ptr(matrix),16)!=0,"Cannot read inverse bind matrix");
            output.inverseBind.push_back(matrix);
        }
        model->skins.push_back(std::move(output));
    }
    for(size_t i=0;i<raw->animations_count;++i) {
        const auto& a=raw->animations[i]; Animation output; output.name=name(a.name,"clip");
        for(size_t j=0;j<a.channels_count;++j) {
            const auto& c=a.channels[j]; if(!c.target_node) continue;
            if(c.target_path==cgltf_animation_path_type_weights) { model->diagnostics.push_back("Morph animation omitted"); continue; }
            need(c.sampler->interpolation!=cgltf_interpolation_type_cubic_spline,"CUBICSPLINE animation must be resampled to LINEAR before import");
            Channel ch; ch.node=static_cast<int>(c.target_node-raw->nodes);
            ch.path=c.target_path==cgltf_animation_path_type_translation?0:c.target_path==cgltf_animation_path_type_rotation?1:2;
            ch.step=c.sampler->interpolation==cgltf_interpolation_type_step;
            need(c.sampler->input->count>0 && c.sampler->output->count==c.sampler->input->count,"Invalid animation key count");
            for(size_t k=0;k<c.sampler->input->count;++k) {
                float t=read(c.sampler->input,k).x;
                need(ch.times.empty() || t>ch.times.back(),"Animation key times must increase");
                ch.times.push_back(t); ch.values.push_back(read(c.sampler->output,k)); output.duration=std::max(output.duration,t);
            }
            output.channels.push_back(std::move(ch));
        }
        model->animations.push_back(std::move(output));
    }
    model->diagnostics.push_back("Imported "+std::to_string(total/3)+" triangles, "+std::to_string(model->nodes.size())+" nodes, "+std::to_string(model->animations.size())+" clips");
    return model;
}
void Model::bake(const glm::mat4& root,float time,int clip,bool animate,uint64_t id,std::vector<Vertex>& vertices,std::vector<Draw>& draws) const {
    std::vector<Transform> local; for(const auto& n:nodes) local.push_back(n.transform);
    if(animate && clip>=0 && clip<static_cast<int>(animations.size())) {
        const auto& a=animations[clip]; float t=a.duration>0?std::fmod(std::max(time,0.0f),a.duration):0;
        for(const auto& c:a.channels) {
            auto it=std::upper_bound(c.times.begin(),c.times.end(),t); size_t next=std::min(static_cast<size_t>(it-c.times.begin()),c.times.size()-1), prev=next?next-1:0;
            float f=c.step || next==prev?0:std::clamp((t-c.times[prev])/(c.times[next]-c.times[prev]),0.0f,1.0f);
            auto value=glm::mix(c.values[prev],c.values[next],f); auto& tr=local[c.node];
            if(c.path==0) tr.position=value; else if(c.path==2) tr.scale=value;
            else { auto q0=c.values[prev], q1=c.values[next]; tr.rotation=glm::normalize(glm::slerp(glm::quat(q0.w,q0.x,q0.y,q0.z),glm::quat(q1.w,q1.x,q1.y,q1.z),f)); }
        }
    }
    std::vector<glm::mat4> world(nodes.size(),glm::mat4(1)); std::vector<int> state(nodes.size(),0);
    std::function<glm::mat4(int)> resolve=[&](int i) {
        need(state[i]!=1,"Cyclic glTF hierarchy"); if(state[i]==2) return world[i]; state[i]=1;
        auto m=nodes[i].useMatrix?nodes[i].matrix:local[i].matrix();
        world[i]=nodes[i].parent<0?m:resolve(nodes[i].parent)*m; state[i]=2; return world[i];
    };
    for(size_t i=0;i<nodes.size();++i) resolve(static_cast<int>(i));
    for(size_t i=0;i<nodes.size();++i) {
        const auto& n=nodes[i]; if(!n.active || n.mesh<0) continue;
        const auto meshMatrix=root*world[i];
        need(std::abs(glm::determinant(glm::mat3(meshMatrix)))>1e-10f,"Singular mesh transform");
        const auto meshNormal=glm::transpose(glm::inverse(glm::mat3(meshMatrix)));
        std::vector<glm::mat4> jointMatrices;std::vector<glm::mat3> jointNormals;
        if(n.skin>=0) {
            const auto& skin=skins[n.skin];jointMatrices.reserve(skin.joints.size());jointNormals.reserve(skin.joints.size());
            for(size_t joint=0;joint<skin.joints.size();++joint) {
                auto matrix=root*world[skin.joints[joint]]*skin.inverseBind[joint];
                need(std::abs(glm::determinant(glm::mat3(matrix)))>1e-10f,"Singular joint transform");
                jointMatrices.push_back(matrix);jointNormals.push_back(glm::transpose(glm::inverse(glm::mat3(matrix))));
            }
        }
        for(const auto& p:meshes[n.mesh]) {
            Draw draw;draw.first=static_cast<uint32_t>(vertices.size());draw.count=static_cast<uint32_t>(p.vertices.size());draw.entity=id;
            if(p.material>=0){draw.material=&materials[p.material];if(draw.material->image>=0)draw.image=&images[draw.material->image];}
            for(const auto& input:p.vertices) {
                Vertex v=input.vertex;float weight=glm::dot(input.weights,glm::vec4(1));
                if(n.skin>=0 && weight>0.00001f) {
                    glm::vec4 position(0);glm::vec3 normal(0);
                    for(int j=0;j<4;++j)if(input.weights[j]>0){
                        need(input.joints[j]<jointMatrices.size(),"Skin joint index out of range");float factor=input.weights[j]/weight;
                        position+=jointMatrices[input.joints[j]]*glm::vec4(v.position,1)*factor;normal+=jointNormals[input.joints[j]]*v.normal*factor;
                    }
                    v.position=position;v.normal=glm::length(normal)>1e-8f?glm::normalize(normal):glm::vec3(0,1,0);
                }else{v.position=meshMatrix*glm::vec4(v.position,1);auto normal=meshNormal*v.normal;v.normal=glm::length(normal)>1e-8f?glm::normalize(normal):glm::vec3(0,1,0);}
                vertices.push_back(v);
            }
            draws.push_back(draw);
        }
    }
}
Entity* World::find(uint64_t id) { for(auto& e:entities) if(e.id==id) return &e; return nullptr; }
const Entity* World::find(uint64_t id) const { for(const auto& e:entities) if(e.id==id) return &e; return nullptr; }
glm::mat4 World::matrix(uint64_t id) const {
    glm::mat4 result(1); std::set<uint64_t> seen;
    while(id) { need(seen.insert(id).second,"Cyclic entity hierarchy"); auto e=find(id); need(e!=nullptr,"Missing parent entity"); result=e->transform.matrix()*result; id=e->parent; }
    return result;
}
static glm::vec3 vec3(const json& j) { need(j.is_array() && j.size()==3,"Expected 3-vector"); glm::vec3 v(j[0].get<float>(),j[1].get<float>(),j[2].get<float>()); for(int i=0;i<3;++i) need(std::isfinite(v[i]),"Non-finite transform"); return v; }
World World::load(const std::filesystem::path& path) {
    std::ifstream file(path); need(bool(file),"Cannot open scene: "+path.string()); json j; file>>j;
    need(j.at("version")==2,"Unsupported scene version"); World w; w.source=std::filesystem::absolute(path);
    std::unordered_map<std::string,std::shared_ptr<Model>> cache;
    need(j.at("entities").size()<=100000,"Scene entity limit exceeded");
    for(const auto& item:j.at("entities")) {
        Entity e; e.id=item.at("id").get<uint64_t>(); need(e.id && !w.find(e.id),"Duplicate or zero entity ID");
        e.name=item.at("name").get<std::string>(); e.parent=item.value("parent",uint64_t(0));
        e.transform.position=vec3(item.value("position",json::array({0,0,0}))); e.transform.scale=vec3(item.value("scale",json::array({1,1,1})));
        for(int i=0;i<3;++i) need(e.transform.scale[i]>0.0001f,"Scale must be positive");
        auto r=item.value("rotation",json::array({0,0,0,1})); need(r.is_array() && r.size()==4,"Expected quaternion");
        e.transform.rotation=glm::quat(r[3].get<float>(),r[0].get<float>(),r[1].get<float>(),r[2].get<float>());
        need(std::isfinite(glm::length(e.transform.rotation)) && glm::length(e.transform.rotation)>0.0001f,"Invalid quaternion"); e.transform.rotation=glm::normalize(e.transform.rotation);
        e.asset=item.value("asset",std::string());
        if(!e.asset.empty()) { auto assetPath=(path.parent_path()/e.asset).lexically_normal(); auto key=assetPath.string(); if(!cache.contains(key)) cache[key]=Model::load(assetPath); e.model=cache[key]; }
        e.visible=item.value("visible",true); e.animate=item.value("animate",true); e.clip=item.value("clip",0);
        e.body=item.value("body",std::string("none")); need(e.body=="none" || e.body=="static" || e.body=="dynamic","Unknown physics body type");
        e.halfExtent=vec3(item.value("halfExtent",json::array({0.5,0.5,0.5}))); for(int k=0;k<3;++k) need(e.halfExtent[k]>0,"Invalid collision extents");
        e.mass=item.value("mass",1.0f); need(std::isfinite(e.mass) && e.mass>0,"Invalid mass");
        need(e.parent==0 || e.body=="none","Physics bodies must be scene roots");
        e.behavior=item.value("behavior",std::string()); e.speed=item.value("speed",1.0f); need(std::isfinite(e.speed),"Invalid behavior speed");
        need(e.behavior.empty() || e.behavior=="spin","Unknown gameplay behavior");
        if(e.body!="none") need(e.behavior.empty(),"Scripted transforms require non-physics entities");
        w.entities.push_back(std::move(e));
    }
    for(const auto& e:w.entities) w.matrix(e.id);
    for(const auto& [key,m]:cache) for(const auto& d:m->diagnostics) w.diagnostics.push_back(key+": "+d);
    return w;
}
void World::save(const std::filesystem::path& path) const {
    json j; j["version"]=2; j["entities"]=json::array();
    for(const auto& e:entities) {
        auto vector=[](glm::vec3 v){ return json::array({v.x,v.y,v.z}); };
        std::string asset=e.asset;
        if(!asset.empty()) asset=std::filesystem::relative(source.parent_path()/asset,std::filesystem::absolute(path).parent_path()).generic_string();
        j["entities"].push_back({{"id",e.id},{"name",e.name},{"parent",e.parent},{"position",vector(e.transform.position)},{"scale",vector(e.transform.scale)},
            {"rotation",{e.transform.rotation.x,e.transform.rotation.y,e.transform.rotation.z,e.transform.rotation.w}}, {"asset",asset},{"body",e.body},{"halfExtent",vector(e.halfExtent)},
            {"mass",e.mass},{"visible",e.visible},{"animate",e.animate},{"clip",e.clip},{"behavior",e.behavior},{"speed",e.speed}});
    }
    auto temp=path; temp+=".tmp"; { std::ofstream output(temp); output<<j.dump(2); need(bool(output),"Cannot save scene"); }
    // Replace by same-directory rename; keep the existing file if serialization fails.
    std::error_code ec; std::filesystem::rename(temp,path,ec);
    if(ec) { auto backup=path; backup+=".bak"; if(std::filesystem::exists(path)) std::filesystem::copy_file(path,backup,std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(path); std::filesystem::rename(temp,path); }
}
void World::bake(float time,std::vector<Vertex>& vertices,std::vector<Draw>& draws) const {
    vertices.clear(); draws.clear();
    for(const auto& e:entities) if(e.visible && e.model) {
        auto m=matrix(e.id); if(e.behavior=="spin") m=m*glm::rotate(glm::mat4(1),time*e.speed,glm::vec3(0,1,0));
        e.model->bake(m,time,e.clip,e.animate,e.id,vertices,draws);
    }
}
}
