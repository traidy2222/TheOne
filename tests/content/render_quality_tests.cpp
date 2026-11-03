#include <theone/geometry/prepared_mesh.hpp>
#include <theone/geometry/mipmaps.hpp>
#include <theone/render/shadows.hpp>
#include <theone/scene/load_job.hpp>
#include <theone/assets/load_job.hpp>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
static void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
static void waitFor(auto predicate) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, "Async job timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected fixture directory");
        std::vector<uint8_t> pixels = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
        auto color = one::buildMipChain(2, 2, pixels, true), data = one::buildMipChain(2, 2, pixels, false);
        require(color.size() == 2 && color.back().rgba[0] >= 187 && color.back().rgba[0] <= 189,
                "sRGB mips averaged encoded colors");
        require(data.back().rgba[0] == 128 && color.back().rgba[3] == 255,
                "Linear/alpha mip filtering incorrect");
        std::vector<uint8_t> odd(7 * 3 * 4, 255);
        auto chain = one::buildMipChain(7, 3, odd, true);
        require(chain.size() == 3 && chain[1].width == 3 && chain[1].height == 1 && chain.back().width == 1,
                "Odd mip dimensions incorrect");
        require(one::mipLevelCount(1, 16) == 5 && one::mipLevelCount(1, 1) == 1, "Mip level count incorrect");
        one::Camera camera;
        auto cascades = one::fitShadowCascades(camera, 4.f / 3);
        require(cascades.splits.x > camera.nearPlane && cascades.splits.y > cascades.splits.x &&
                    cascades.splits.z > cascades.splits.y,
                "Cascade splits invalid");
        float previous = camera.nearPlane;
        for (size_t c = 0; c < 3; ++c) {
            auto view = glm::inverse(camera.view());
            for (float z : {previous, cascades.splits[int(c)]})
                for (float y : {-1.f, 1.f})
                    for (float x : {-1.f, 1.f}) {
                        auto p = view *
                                 glm::vec4(x * z * std::tan(.525f) * 4.f / 3, y * z * std::tan(.525f), -z, 1);
                        auto q = cascades.matrices[c] * p;
                        require(std::abs(q.x) <= 1.001f && std::abs(q.y) <= 1.001f && q.z >= -.001f &&
                                    q.z <= 1.001f,
                                "Cascade excludes its receiver frustum");
                    }
            previous = cascades.splits[int(c)];
        }
        camera.position += glm::vec3(1000, 0, 0);
        auto moved = one::fitShadowCascades(camera, 4.f / 3);
        require(moved.matrices[0] != cascades.matrices[0], "Shadows did not follow the camera");
        std::vector<one::SkinVertex> grid;
        auto vertex = [](int x, int y) {
            one::SkinVertex v;
            v.vertex.position = {x / 16.f, y / 16.f, 0};
            v.vertex.normal = {0, 0, 1};
            v.vertex.uv = {x / 16.f, y / 16.f};
            return v;
        };
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                grid.insert(grid.end(), {vertex(x, y), vertex(x + 1, y), vertex(x + 1, y + 1), vertex(x, y),
                                         vertex(x + 1, y + 1), vertex(x, y + 1)});
            }
        auto prepared = one::prepareMesh(grid);
        require(prepared->levels.size() > 1 &&
                    prepared->levels.back().indices.size() < prepared->levels.front().indices.size(),
                "LOD did not simplify dense geometry");
        for (const auto& level : prepared->levels)
            for (auto index : level.indices)
                require(index < prepared->vertices.size(), "LOD index outside vertices");
        float errors[] = {0, .1f, .5f};
        require(one::selectMeshLod(errors, 100, 0) == 0 && one::selectMeshLod(errors, 1, 0) == 2,
                "Projected-error LOD selection incorrect");
        require(one::selectMeshLod(errors, 14, 1) == 1 && one::selectMeshLod(errors, 14, 0) == 0,
                "LOD hysteresis missing");
        // Positive weighted skinning is a convex combination of transformed joint bounds.
        one::SkinVertex v;
        v.vertex.position = {1, 2, 3};
        v.weights = {.2f, .8f, 0, 0};
        v.joints = {0, 1, 0, 0};
        std::vector<one::SkinVertex> skin = {v, v, v};
        auto skinned = one::prepareMesh(skin);
        std::array<glm::mat4, 2> joints = {glm::translate(glm::mat4(1), {10, 0, 0}) *
                                               glm::scale(glm::mat4(1), {2, .5f, 1}),
                                           glm::rotate(glm::mat4(1), .7f, glm::vec3(0, 1, 0))};
        auto bounds = skinned->bounds.evaluate(glm::mat4(1), 2, [&](size_t i) { return joints[i]; });
        auto p = glm::vec3(joints[0] * glm::vec4(v.vertex.position, 1) * .2f +
                           joints[1] * glm::vec4(v.vertex.position, 1) * .8f);
        require(glm::all(glm::greaterThanEqual(p, bounds.minimum)) &&
                    glm::all(glm::lessThanEqual(p, bounds.maximum)),
                "Animated bound excludes skinned position");
        require(skinned->levels.size() == 1, "Skinned mesh was simplified unsafely");
        skin[0].weights.x = -.1f;
        require(!one::prepareMesh(skin)->bounds.safe, "Non-convex weights not detected");
        std::atomic<bool> started = false;
        one::LoadJob<int> job([&](const auto& path, std::stop_token stop) {
            if (path == "slow") {
                started = true;
                while (!stop.stop_requested())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                one::checkCancellation(stop);
            }
            if (path == "fail")
                throw std::runtime_error("Expected failure");
            return std::make_shared<int>(42);
        });
        job.request("slow");
        waitFor([&] { return started.load(); });
        job.cancel();
        require(job.status().state == one::LoadState::Cancelled, "Cancellation not immediate");
        job.request("latest");
        waitFor([&] { return job.status().state == one::LoadState::Ready; });
        require(*job.take() == 42, "Latest request not published");
        job.request("fail");
        waitFor([&] { return job.status().state == one::LoadState::Failed; });
        require(!job.take(), "Failed load published data");
        one::SceneLoadJob sceneJob;
        sceneJob.request(std::filesystem::path(argv[1]) / "sandbox.one");
        waitFor([&] {
            return sceneJob.status().state == one::LoadState::Ready ||
                   sceneJob.status().state == one::LoadState::Failed;
        });
        auto world = sceneJob.take();
        require(world && !world->entities.empty(), "Background scene load failed");
        one::ModelLoadJob modelJob;
        auto source = world->entities.front().model->source;
        modelJob.request(source);
        waitFor([&] {
            return modelJob.status().state == one::LoadState::Ready ||
                   modelJob.status().state == one::LoadState::Failed;
        });
        auto model = modelJob.take();
        require(bool(model), "Background model load failed");
        world->replaceAsset(source, model);
        std::stop_source cancelled;
        cancelled.request_stop();
        bool rejected = false;
        try {
            one::Model::load(source, nullptr, cancelled.get_token());
        } catch (const one::LoadCancelled&) {
            rejected = true;
        }
        require(rejected, "Importer ignored cancellation");
        std::cout << "PASS color/data mips, odd dimensions, cascade coverage, LOD error/seams, animated "
                     "bounds, cancellation and async publication\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
