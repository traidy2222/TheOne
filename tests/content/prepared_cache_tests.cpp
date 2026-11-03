#include "assets/cache/prepared_cache.hpp"
#include "assets/cache/dependency_manifest.hpp"
#include <theone/assets/model.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <future>
#include <cstdlib>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    if (*value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected fixture and isolated cache directory");
        auto root = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(root);
        auto cache = root / "cache";
        setEnvironment("THEONE_ASSET_CACHE_DIR", cache.string().c_str());
        setEnvironment("THEONE_DISABLE_ASSET_CACHE", "");
        // Expand a GLB into glTF with an external buffer and image, including escaped URI names.
        std::ifstream input(std::filesystem::path(argv[1]) / "crate.glb", std::ios::binary);
        uint32_t header[5];
        input.read(reinterpret_cast<char*>(header), sizeof(header));
        std::string json(header[3], ' ');
        input.read(json.data(), json.size());
        uint32_t chunk[2];
        input.read(reinterpret_cast<char*>(chunk), sizeof(chunk));
        std::vector<uint8_t> buffer(chunk[0]);
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        require(bool(input), "Fixture read failed");
        auto document = nlohmann::json::parse(json);
        document["buffers"][0]["uri"] = "geometry%20data.bin";
        auto imageView = document["images"][0]["bufferView"].get<size_t>();
        auto offset = document["bufferViews"][imageView].value("byteOffset", size_t(0));
        auto length = document["bufferViews"][imageView]["byteLength"].get<size_t>();
        {
            std::ofstream out(root / "image.png", std::ios::binary);
            out.write((char*)buffer.data() + offset, length);
        }
        document["images"][0].erase("bufferView");
        document["images"][0]["uri"] = "image.png";
        auto source = root / "model.gltf";
        {
            std::ofstream out(source);
            out << document.dump();
        }
        {
            std::ofstream out(root / "geometry data.bin", std::ios::binary);
            out.write((char*)buffer.data(), buffer.size());
        }
        one::ModelLoadStats cold, warm;
        auto a = one::Model::load(source, &cold), b = one::Model::load(source, &warm);
        require(warm.cacheHits == 1 && warm.parseMs==0, "Warm manifest hit parsed source");
        require(one::asset_cache::encode(*a, {}) == one::asset_cache::encode(*b, {}),
                "Cache changed prepared payloads");
        for (size_t i = 0; i < a->images.size(); ++i)
            require(a->images[i].prepared == b->images[i].prepared, "Cached image lost residency sharing");
        require(a->handle != b->handle, "Cache reused runtime identity");
        auto miss = [&] {
            one::ModelLoadStats stats;
            auto model = one::Model::load(source, &stats);
            require(stats.cacheHits == 0, "Changed dependency used stale cache");
            return model;
        };
        {
            std::ofstream out(root / "geometry data.bin", std::ios::binary | std::ios::app);
            out.put('\0');
        }
        miss();
        {
            std::ofstream out(root / "image.png", std::ios::binary | std::ios::app);
            out.put('\0');
        }
        miss();
        document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {.5, 1., 1., 1.};
        {
            std::ofstream out(source);
            out << document.dump();
        }
        require(miss()->materials[0].color.x == .5f, "Source edit not loaded");
        // Same-length edits with restored timestamps must still invalidate by content.
        auto stamp=std::filesystem::last_write_time(source);
        document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"]={.6,1.,1.,1.};
        {std::ofstream out(source);out<<document.dump();}
        std::filesystem::last_write_time(source,stamp);
        require(miss()->materials[0].color.x==.6f,"Timestamp-preserving source edit was stale");
        auto dependency=root/"geometry data.bin";
        stamp=std::filesystem::last_write_time(dependency);
        {std::fstream out(dependency,std::ios::binary|std::ios::in|std::ios::out);out.seekp(-1,std::ios::end);out.put('x');}
        std::filesystem::last_write_time(dependency,stamp);
        miss();
        for(const auto& file:std::filesystem::directory_iterator(cache))if(file.path().extension()==".deps") {
            std::ofstream out(file.path(),std::ios::binary);out<<"damaged";
        }
        one::ModelLoadStats manifestFallback;
        one::Model::load(source,&manifestFallback);
        require(manifestFallback.cacheHits==1 && manifestFallback.parseMs>0,"Manifest corruption did not use validated slow path");
        one::ModelLoadStats manifestRepaired;
        one::Model::load(source,&manifestRepaired);
        require(manifestRepaired.cacheHits==1 && manifestRepaired.parseMs==0,"Manifest not repaired");
        for (const auto& file : std::filesystem::directory_iterator(cache))
            if (file.path().extension() == ".onecache") {
                std::ofstream out(file.path(), std::ios::binary);
                out << "damaged";
            }
        miss();
        one::ModelLoadStats repaired;
        one::Model::load(source, &repaired);
        require(repaired.cacheHits == 1, "Corrupt cache was not replaced");
        // Animation and skin payload roundtrip, including pointer fixups from all material slots.
        one::ModelLoadStats robotWarm;
        auto robot = one::Model::load(std::filesystem::path(argv[1]) / "robot.glb");
        auto robotHit = one::Model::load(std::filesystem::path(argv[1]) / "robot.glb", &robotWarm);
        require(robotWarm.cacheHits == 1 &&
                    one::asset_cache::encode(*robot, {}) == one::asset_cache::encode(*robotHit, {}),
                "Rig cache roundtrip failed");
        for (auto* model : {a.get(), b.get()})
            for (const auto& m : model->materials)
                for (auto p : {m.normal, m.occlusion, m.emissive, m.metallicRoughness})
                    if (p)
                        require(p >= model->images.data() && p < model->images.data() + model->images.size(),
                                "Invalid cached texture pointer");
        std::stop_source cancelled;
        cancelled.request_stop();
        bool threw = false;
        try {
            one::Model::load(source, nullptr, cancelled.get_token());
        } catch (...) {
            threw = true;
        }
        require(threw, "Cancelled load returned cached model");
        setEnvironment("THEONE_ASSET_CACHE_DIR",
                       (root / "geometry data.bin" / "not-a-directory").string().c_str());
        one::Model::load(source); // Unwritable cache cannot break source import.
        std::cout << "PASS payload/rig roundtrip, dependency changes, corruption recovery, cancellation, "
                     "unwritable fallback\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
