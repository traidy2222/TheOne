#include <theone/physics/scene_loading.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <thread>
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void waitFor(F condition) {
    auto start = Clock::now();
    while (!condition()) {
        require(ms(start) < 15000, "Timed out waiting for worker");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void retirementCheck() {
    std::atomic<bool> entered = false, release = false, wrongThread = false;
    std::atomic<int> destroyed = 0;
    auto caller = std::this_thread::get_id();
    // Deliberately hold a destructor: publication/cancellation must stay usable meanwhile.
    struct Probe {
        std::atomic<bool>& entered;
        std::atomic<bool>& release;
        std::atomic<bool>& wrongThread;
        std::atomic<int>& destroyed;
        std::thread::id caller;
        ~Probe() {
            if (std::this_thread::get_id() == caller)
                wrongThread = true;
            entered = true;
            while (!release)
                std::this_thread::yield();
            ++destroyed;
        }
    };
    {
        one::LoadJob<Probe> job([&](const auto&, std::stop_token) {
            return std::shared_ptr<Probe>(new Probe{entered, release, wrongThread, destroyed, caller});
        });
        // Releases the held destructor even if an assertion throws.
        struct Release {
            std::atomic<bool>& flag;
            ~Release() { flag = true; }
        } guard{release};
        job.request("first");
        waitFor([&] { return job.status().state == one::LoadState::Ready; });
        job.cancel();
        waitFor([&] { return entered.load(); });
        require(job.status().state == one::LoadState::Cancelled, "Cancelled result still published");
        job.request("second");
        require(job.status().state == one::LoadState::Queued, "Destructor held publication lock");
        release = true;
        waitFor([&] { return job.status().state == one::LoadState::Ready; });
        job.retire(job.take());
        waitFor([&] { return destroyed == 2; });
        job.request("shutdown");
        waitFor([&] { return job.status().state == one::LoadState::Ready; });
    }
    require(destroyed == 3 && !wrongThread, "Result cleanup escaped the worker");
}
void runtimeCheck() {
    // Concurrent first acquisition and final release, with no pre-existing physics owner.
    std::barrier rendezvous(2);
    std::atomic<bool> failed = false;
    auto exercise = [&] {
        for (int i = 0; i < 4; ++i) {
            rendezvous.arrive_and_wait();
            try {
                one::Physics physics;
                one::World world;
                physics.rebuild(world);
                physics.step(world, 1.f / 60, {}, false);
            } catch (...) {
                failed = true;
            }
            rendezvous.arrive_and_wait();
        }
    };
    std::jthread first(exercise), second(exercise);
    first.join();
    second.join();
    require(!failed, "Concurrent Jolt lifetime failed");
}
void collisionFailureCheck(const std::filesystem::path& path) {
    one::LoadJob<one::PhysicsScene> job([&](const auto& request, std::stop_token stop) {
        if (request != "bad-collision")
            return one::PhysicsScene::load(path, stop);
        auto result = std::make_shared<one::PhysicsScene>();
        one::Entity box;
        box.id = 1;
        box.body = "static";
        result->world.entities.push_back(box);
        one::Entity mesh;
        mesh.id = 2;
        mesh.body = "static";
        mesh.model = one::Model::load(path.parent_path() / "level.glb");
        for (auto& primitives : mesh.model->meshes)
            for (auto& primitive : primitives)
                for (auto& vertex : primitive.vertices)
                    vertex.vertex.position = {};
        result->world.entities.push_back(std::move(mesh));
        result->physics = std::make_unique<one::Physics>();
        // Mesh creation must fail after the first body exists; its private world must clean up.
        result->physics->rebuild(result->world, stop);
        return result;
    });
    job.request("bad-collision");
    waitFor([&] {
        auto state = job.status().state;
        return state == one::LoadState::Failed || state == one::LoadState::Ready;
    });
    require(job.status().state == one::LoadState::Failed && !job.take(), "Broken collision was published");
    job.request("recover");
    waitFor([&] {
        auto state = job.status().state;
        return state == one::LoadState::Failed || state == one::LoadState::Ready;
    });
    auto ready = job.take();
    require(ready && ready->physics, "Collision failure prevented later valid load");
    job.retire(std::move(ready));
}
void sceneCheck(const std::filesystem::path& path, bool profile) {
    auto baseline = one::PhysicsScene::load(path);
    one::World liveWorld;
    std::unique_ptr<one::Physics> livePhysics;
    baseline->swapInto(liveWorld, livePhysics);
    auto* originalPhysics = livePhysics.get();
    one::PhysicsSceneLoadJob job;
    job.request(path);
    size_t liveSteps = 0;
    double maxPollMs = 0;
    auto start = Clock::now();
    for (;;) {
        auto poll = Clock::now();
        auto status = job.status();
        maxPollMs = std::max(maxPollMs, ms(poll));
        require(status.state != one::LoadState::Failed, status.error.c_str());
        if (status.state == one::LoadState::Ready)
            break;
        require(ms(start) < 30000, "Scene preparation timed out");
        livePhysics->step(liveWorld, 1.f / 60, {}, false);
        ++liveSteps;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto ready = job.take();
    require(ready && ready->physics && liveSteps > 0, "No live simulation during preparation");
    require(livePhysics.get() == originalPhysics, "Worker changed active physics before adoption");
    auto importMs = ready->importMs, collisionMs = ready->collisionMs;
    auto swapStart = Clock::now();
    ready->swapInto(liveWorld, livePhysics);
    double swapMs = ms(swapStart);
    require(ready->physics.get() == originalPhysics, "Old world was not retained for retirement");
    auto retireStart = Clock::now();
    job.retire(std::move(ready));
    double retireMs = ms(retireStart);
    require(!ready && livePhysics.get() != originalPhysics, "Handoff did not transfer ownership");
    // Match the old synchronous rebuild path, including dynamic body positions and player contacts.
    auto referenceWorld = liveWorld;
    one::Physics reference;
    reference.rebuild(referenceWorld);
    for (int frame = 0; frame < 120; ++frame) {
        reference.step(referenceWorld, 1.f / 60, {}, false);
        livePhysics->step(liveWorld, 1.f / 60, {}, false);
    }
    require(glm::distance(reference.playerPosition(), livePhysics->playerPosition()) < .0001f,
            "Adopted player collision differs from synchronous rebuild");
    for (const auto& entity : referenceWorld.entities)
        if (entity.body == "dynamic")
            require(glm::distance(entity.transform.position, liveWorld.find(entity.id)->transform.position) <
                        .0001f,
                    "Adopted dynamic body differs from synchronous rebuild");
    auto* active = livePhysics.get();
    job.request(path.string() + ".missing");
    waitFor([&] { return job.status().state == one::LoadState::Failed; });
    require(!job.take() && livePhysics.get() == active, "Failure replaced live collision");
    job.request(path);
    job.cancel();
    require(!job.take() && job.status().state == one::LoadState::Cancelled, "Cancelled scene published");
    job.request(path);
    waitFor([&] { return job.status().state == one::LoadState::Ready; });
    job.retire(job.take());
    std::stop_source stop;
    stop.request_stop();
    bool cancelled = false;
    try {
        livePhysics->rebuild(liveWorld, stop.get_token());
    } catch (const one::LoadCancelled&) {
        cancelled = true;
    }
    require(cancelled, "Collision rebuild ignored cancellation");
    livePhysics->step(liveWorld, 1.f / 60, {}, false);
    std::cout << (profile ? "PROFILE " : "FIXTURE ") << "sync_collision_ms=" << baseline->collisionMs
              << " worker_import_ms=" << importMs << " worker_collision_ms=" << collisionMs
              << " swap_ms=" << swapMs << " retire_enqueue_ms=" << retireMs << " max_poll_ms=" << maxPollMs
              << " live_steps=" << liveSteps << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected scene path");
        if (argc < 3) {
            retirementCheck();
            runtimeCheck();
            collisionFailureCheck(argv[1]);
        }
        sceneCheck(argv[1], argc >= 3);
        std::cout << "PASS background collision, concurrent simulation, adoption parity, cancellation, "
                     "failure and retirement\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
