#include <theone/scene/world.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <unordered_set>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#endif
using json = nlohmann::json;
// Hash decoded image bytes and explicit vertex fields, never padding or pointers.
struct Fingerprint {
    uint64_t value = 14695981039346656037ull;
    void bytes(const void* data, size_t size) {
        auto p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            value ^= p[i];
            value *= 1099511628211ull;
        }
    }
    template <class T> void scalar(T v) { bytes(&v, sizeof(v)); }
    template <class T> void vector(const T& v) {
        for (int i = 0; i < v.length(); ++i)
            scalar(v[i]);
    }
};
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected scene path");
        uint64_t expected = 0;
        for (int pass = 0; pass < 3; ++pass) {
            one::SceneLoadStats s;
            auto start = std::chrono::steady_clock::now();
            auto world = one::World::load(argv[1], &s);
            auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            Fingerprint hash;
            size_t images = 0, primitives = 0, vertices = 0;
            std::unordered_set<const one::Model*> seen;
            for (const auto& e : world.entities) {
                if (!e.model || !seen.insert(e.model.get()).second)
                    continue;
                const auto& m = *e.model;
                for (const auto& image : m.images) {
                    ++images;
                    hash.scalar(image.width);
                    hash.scalar(image.height);
                    hash.bytes(image.rgba.data(), image.rgba.size());
                }
                for (const auto& mesh : m.meshes)
                    for (const auto& p : mesh) {
                        ++primitives;
                        vertices += p.vertices.size();
                        hash.scalar(p.material);
                        for (const auto& v : p.vertices) {
                            hash.vector(v.vertex.position);
                            hash.vector(v.vertex.normal);
                            hash.vector(v.vertex.color);
                            hash.vector(v.vertex.uv);
                            hash.vector(v.joints);
                            hash.vector(v.weights);
                        }
                    }
            }
            if (pass && hash.value != expected)
                throw std::runtime_error("Reload import fingerprint changed");
            expected = hash.value;
            const auto& m = s.models;
            json result = {{"pass", pass},
                           {"total_ms", ms},
                           {"json_ms", s.jsonMs},
                           {"entities_ms", s.entitiesMs},
                           {"hierarchy_ms", s.hierarchyMs},
                           {"assets_ms", s.assetsMs},
                           {"parse_ms", m.parseMs},
                           {"cache_ms", m.cacheMs},
                           {"cache_hits", m.cacheHits},
                           {"cache_hash_ms",m.cacheHashMs},{"cache_read_ms",m.cacheReadMs},{"cache_decode_ms",m.cacheDecodeMs},
                           {"asset_workers",s.assetWorkers},{"asset_working_estimate",s.assetWorkingEstimate},
                           {"buffers_ms", m.buffersMs},
                           {"validation_ms", m.validationMs},
                           {"images_ms", m.imagesMs},
                           {"materials_ms", m.materialsMs},
                           {"meshes_ms", m.meshesMs},
                           {"rig_ms", m.rigMs},
                           {"animations_ms", m.animationsMs},
                           {"models", m.models},
                           {"images", images},
                           {"primitives", primitives},
                           {"vertices", vertices},
                           {"fingerprint", hash.value}};
            std::cout << "PROFILE " << result.dump() << '\n';
        }
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
        if(K32GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))std::cout<<"peak_working_set="<<memory.PeakWorkingSetSize<<'\n';
#endif
        std::cout << "PASS scene import and reload consistency\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
