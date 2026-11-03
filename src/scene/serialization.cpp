#include <theone/scene/world.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_set>
#include "core/validation.hpp"
#include "core/load_timing.hpp"
#include <theone/core/cancellation.hpp>
#include "asset_loading.hpp"
namespace one {
using detail::need;
using json = nlohmann::json;
static glm::vec3 vec3(const json& j) {
    need(j.is_array() && j.size() == 3, "Expected 3-vector");
    glm::vec3 v(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    for (int i = 0; i < 3; ++i)
        need(std::isfinite(v[i]), "Non-finite transform");
    return v;
}
World World::load(const std::filesystem::path& path, SceneLoadStats* stats, std::stop_token stop) {
    checkCancellation(stop);
    detail::LoadTiming timing(stats != nullptr);
    std::ifstream file(path);
    need(bool(file), "Cannot open scene: " + path.string());
    json j;
    file >> j;
    need(j.at("version") == 2, "Unsupported scene version");
    timing.record(stats ? &stats->jsonMs : nullptr);
    World w;
    w.source = std::filesystem::absolute(path);
    std::unordered_set<uint64_t> ids;
    need(j.at("entities").size() <= 100000, "Scene entity limit exceeded");
    for (const auto& item : j.at("entities")) {
        checkCancellation(stop);
        Entity e;
        e.id = item.at("id").get<uint64_t>();
        need(e.id && ids.insert(e.id).second, "Duplicate or zero entity ID");
        e.name = item.at("name").get<std::string>();
        e.parent = item.value("parent", uint64_t(0));
        e.transform.position = vec3(item.value("position", json::array({0, 0, 0})));
        e.transform.scale = vec3(item.value("scale", json::array({1, 1, 1})));
        for (int i = 0; i < 3; ++i)
            need(e.transform.scale[i] > 0.0001f, "Scale must be positive");
        auto r = item.value("rotation", json::array({0, 0, 0, 1}));
        need(r.is_array() && r.size() == 4, "Expected quaternion");
        e.transform.rotation =
            glm::quat(r[3].get<float>(), r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
        need(std::isfinite(glm::length(e.transform.rotation)) && glm::length(e.transform.rotation) > 0.0001f,
             "Invalid quaternion");
        e.transform.rotation = glm::normalize(e.transform.rotation);
        e.asset = item.value("asset", std::string());
        e.visible = item.value("visible", true);
        e.animate = item.value("animate", true);
        e.clip = item.value("clip", 0);
        e.body = item.value("body", std::string("none"));
        need(e.body == "none" || e.body == "static" || e.body == "dynamic", "Unknown physics body type");
        e.halfExtent = vec3(item.value("halfExtent", json::array({0.5, 0.5, 0.5})));
        for (int k = 0; k < 3; ++k)
            need(e.halfExtent[k] > 0, "Invalid collision extents");
        e.mass = item.value("mass", 1.0f);
        need(std::isfinite(e.mass) && e.mass > 0, "Invalid mass");
        need(e.parent == 0 || e.body == "none", "Physics bodies must be scene roots");
        e.behavior = item.value("behavior", std::string());
        e.speed = item.value("speed", 1.0f);
        need(std::isfinite(e.speed), "Invalid behavior speed");
        need(e.behavior.empty() || e.behavior == "spin", "Unknown gameplay behavior");
        if (e.body != "none")
            need(e.behavior.empty(), "Scripted transforms require non-physics entities");
        w.entities.push_back(std::move(e));
    }
    timing.record(stats ? &stats->entitiesMs : nullptr);
    for (const auto& e : w.entities)
        w.matrix(e.id);
    timing.record(stats ? &stats->hierarchyMs : nullptr);
    detail::loadSceneAssets(w,stats,stop);
    timing.record(stats ? &stats->assetsMs : nullptr);
    return w;
}
void World::save(const std::filesystem::path& path) const {
    json j;
    j["version"] = 2;
    j["entities"] = json::array();
    for (const auto& e : entities) {
        auto vector = [](glm::vec3 v) { return json::array({v.x, v.y, v.z}); };
        std::string asset = e.asset;
        if (!asset.empty())
            asset = std::filesystem::relative(source.parent_path() / asset,
                                              std::filesystem::absolute(path).parent_path())
                        .generic_string();
        j["entities"].push_back({{"id", e.id},
                                 {"name", e.name},
                                 {"parent", e.parent},
                                 {"position", vector(e.transform.position)},
                                 {"scale", vector(e.transform.scale)},
                                 {"rotation",
                                  {e.transform.rotation.x, e.transform.rotation.y, e.transform.rotation.z,
                                   e.transform.rotation.w}},
                                 {"asset", asset},
                                 {"body", e.body},
                                 {"halfExtent", vector(e.halfExtent)},
                                 {"mass", e.mass},
                                 {"visible", e.visible},
                                 {"animate", e.animate},
                                 {"clip", e.clip},
                                 {"behavior", e.behavior},
                                 {"speed", e.speed}});
    }
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream output(temp);
        output << j.dump(2);
        need(bool(output), "Cannot save scene");
    }
    // Replace by same-directory rename; keep the existing file if serialization fails.
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        auto backup = path;
        backup += ".bak";
        if (std::filesystem::exists(path))
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(path);
        std::filesystem::rename(temp, path);
    }
}
} // namespace one
