#define CGLTF_IMPLEMENTATION
#include "internal.hpp"
#include "core/load_timing.hpp"
#include "assets/cache/prepared_cache.hpp"
#include "assets/cache/dependency_manifest.hpp"
#include <chrono>
namespace one {
using detail::need;
std::shared_ptr<Model> Model::load(const std::filesystem::path& path, ModelLoadStats* stats,
                                   std::stop_token stop) {
    checkCancellation(stop);
    detail::LoadTiming timing(stats != nullptr);
    need(std::filesystem::file_size(path) < 256 * 1024 * 1024, "Asset exceeds 256 MiB import limit");
    auto identify=[&](const auto& dependencies) {
        auto start=std::chrono::steady_clock::now();
        auto key=asset_cache::identify(dependencies,stop);
        if(stats)stats->cacheHashMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return key;
    };
    const auto sourceStamp=std::filesystem::last_write_time(path);
    // A manifest only saves parsing: every source/dependency byte is still hashed.
    try {
        auto dependencies=asset_cache::readDependencies(path);
        if(!dependencies.empty()) {
            auto key=identify(dependencies);
            if(std::filesystem::last_write_time(path)!=sourceStamp)key={};
            if(auto cached=asset_cache::read(key,path,stop,stats)) {
                timing.record(stats ? &stats->cacheMs : nullptr);
                if(stats){++stats->models;++stats->cacheHits;}
                cached->sourceFingerprint=key.content;
                return cached;
            }
        }
    } catch(...) {checkCancellation(stop);}
    timing.record(stats ? &stats->cacheMs : nullptr);

    cgltf_options options{};
    cgltf_data* raw = nullptr;
    need(cgltf_parse_file(&options, path.string().c_str(), &raw) == cgltf_result_success,
         "Cannot parse glTF: " + path.string());
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    checkCancellation(stop);
    timing.record(stats ? &stats->parseMs : nullptr);
    std::vector<std::filesystem::path> dependencies{path};
    auto dependency=[&](const char* uri) {
        if(!uri || std::string_view(uri).starts_with("data:"))return;
        std::string decoded=uri;cgltf_decode_uri(decoded.data());decoded.resize(std::strlen(decoded.c_str()));
        dependencies.push_back(path.parent_path()/decoded);
    };
    for(size_t i=0;i<raw->buffers_count;++i)dependency(raw->buffers[i].uri);
    for(size_t i=0;i<raw->images_count;++i)dependency(raw->images[i].uri);
    asset_cache::Key cacheKey;
    try { cacheKey=identify(dependencies); }
    catch(...) {checkCancellation(stop);}
    if(std::filesystem::last_write_time(path)!=sourceStamp)cacheKey={};
    if(auto cached=asset_cache::read(cacheKey,path,stop,stats)) {
        asset_cache::writeDependencies(cacheKey,dependencies);
        timing.record(stats ? &stats->cacheMs : nullptr);
        if(stats){++stats->models;++stats->cacheHits;}
        cached->sourceFingerprint=cacheKey.content;
        return cached;
    }
    timing.record(stats ? &stats->cacheMs : nullptr);
    need(cgltf_load_buffers(&options, raw, path.string().c_str()) == cgltf_result_success,
         "Cannot load glTF buffers");
    checkCancellation(stop);
    timing.record(stats ? &stats->buffersMs : nullptr);
    need(cgltf_validate(raw) == cgltf_result_success, "glTF validation failed");
    need(raw->nodes_count < 100000 && raw->meshes_count < 100000, "Asset exceeds node/mesh limits");
    auto model = std::make_shared<Model>();
    model->source = path;
    for (size_t i = 0; i < raw->extensions_required_count; ++i) {
        std::string e = raw->extensions_required[i];
        need(e == "KHR_materials_unlit", "Unsupported required glTF extension: " + e);
    }
    for (size_t i = 0; i < raw->extensions_used_count; ++i)
        model->diagnostics.push_back("Extension: " + std::string(raw->extensions_used[i]) +
                                     " (verify conversion semantics)");
    checkCancellation(stop);
    timing.record(stats ? &stats->validationMs : nullptr);
    gltf::loadImages(*model, raw, path, options, stop);
    checkCancellation(stop);
    timing.record(stats ? &stats->imagesMs : nullptr);
    gltf::loadMaterials(*model, raw);
    checkCancellation(stop);
    timing.record(stats ? &stats->materialsMs : nullptr);
    size_t total = gltf::loadMeshes(*model, raw, stop);
    checkCancellation(stop);
    timing.record(stats ? &stats->meshesMs : nullptr);
    gltf::loadRig(*model, raw);
    checkCancellation(stop);
    timing.record(stats ? &stats->rigMs : nullptr);
    gltf::loadAnimations(*model, raw, stop);
    checkCancellation(stop);
    timing.record(stats ? &stats->animationsMs : nullptr);
    model->diagnostics.push_back("Imported " + std::to_string(total / 3) + " triangles, " +
                                 std::to_string(model->nodes.size()) + " nodes, " +
                                 std::to_string(model->animations.size()) + " clips");
    // Do not publish an import if a source dependency changed while it was being prepared.
    if(cacheKey.location) {
        try {
            auto current=identify(dependencies);
            if(current.content==cacheKey.content && std::filesystem::last_write_time(path)==sourceStamp)
            {
                model->sourceFingerprint=cacheKey.content;
                asset_cache::write(cacheKey,*model,stop);
                asset_cache::writeDependencies(cacheKey,dependencies);
            }
        } catch(...) {checkCancellation(stop);}
    }
    timing.record(stats ? &stats->cacheMs : nullptr);
    if (stats)
        ++stats->models;
    return model;
}
} // namespace one
