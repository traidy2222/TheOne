#include <theone/physics/scene_loading.hpp>
#include <theone/physics/edit.hpp>
#include <chrono>
#include <thread>
#include <iostream>
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Job> auto ready(Job& job) {
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        auto status = job.status();
        require(status.state != one::LoadState::Failed, status.error.c_str());
        if (auto result = job.take())
            return result;
        require(std::chrono::steady_clock::now() - start < std::chrono::seconds(10), "Worker timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected fixture");
        auto live = one::PhysicsScene::load(argv[1]);
        auto before = live->physics->collisionCacheStats();
        auto same = one::PhysicsScene::prepare(live->world);
        auto reused = live->physics->collisionCacheStats();
        require(reused.hits > before.hits && reused.cooks == before.cooks,
                "Unchanged collision was recooked");
        auto moved = live->world;
        moved.entities.front().transform.position.x += 3;
        auto shifted = one::PhysicsScene::prepare(moved);
        auto changed = live->physics->collisionCacheStats();
        require(changed.cooks == reused.cooks, "Transform change recooked local collision");
        auto edited = live->world;
        edited.entities.front().model = one::Model::load(live->world.entities.front().model->source);
        for (auto& group : edited.entities.front().model->meshes)
            for (auto& p : group)
                for (auto& v : p.vertices)
                    v.vertex.position.y += .2f;
        auto geometry = one::PhysicsScene::prepare(edited);
        require(live->physics->collisionCacheStats().cooks > changed.cooks,
                "Geometry change reused stale collision");
        one::PhysicsRebuildJob job;
        for (int i = 0; i < 3; ++i)
            live->physics->step(live->world, 1.f / 60, {}, false);
        job.request(one::PhysicsRebuildRequest::capture(live->world, *live->physics));
        for (int i = 0; i < 10; ++i)
            live->physics->step(live->world, 1.f / 60, {}, false);
        live->world.find(10)->name = "Edited while cooking";
        auto result = ready(job);
        result->preserveLiveState(*live->physics, &live->world);
        require(result->world.find(10)->name == "Edited while cooking", "Concurrent non-collision edit lost");
        for (const auto& state : live->physics->bodyStates()) {
            require(glm::distance(result->world.find(state.id)->transform.position,
                                  state.transform.position) < .0001f,
                    "Moving body rewound on adoption");
            for (const auto& restored : result->physics->bodyStates())
                if (restored.id == state.id)
                    require(glm::distance(restored.linear, state.linear) < .0001f,
                            "Moving body lost velocity");
        }
        auto desired = live->world;
        auto* entity = desired.find(10);
        require(entity, "Missing dynamic fixture");
        entity->transform.position.x += 5;
        job.request(one::PhysicsRebuildRequest::capture(desired, *live->physics));
        auto movedBody = ready(job);
        movedBody->preserveLiveState(*live->physics);
        require(glm::distance(movedBody->world.find(10)->transform.position, entity->transform.position) <
                    .0001f,
                "Intentional body edit overwritten");
        job.request(one::PhysicsRebuildRequest::capture(live->world, *live->physics));
        job.request(one::PhysicsRebuildRequest::capture(desired, *live->physics));
        auto latest = ready(job);
        require(latest->world.find(10)->transform.position.x == entity->transform.position.x,
                "Stale edit published");
        job.retire(std::move(result));
        job.retire(std::move(movedBody));
        job.retire(std::move(latest));
        one::PhysicsEditJob edits;
        auto desiredEdit = live->world;
        desiredEdit.find(1)->transform.position.x += 3;
        auto staticToken = live->physics->bodyToken(1), dynamicToken = live->physics->bodyToken(10);
        auto cacheBefore = live->physics->collisionCacheStats();
        edits.request(live->physics->captureEdit(desiredEdit));
        auto delta = ready(edits);
        require(delta->stats.moved == 1 && delta->stats.replaced == 0,
                "Move did not produce a single body edit");
        for (int i = 0; i < 4; ++i)
            live->physics->step(live->world, 1.f / 60, {}, false);
        auto motionBefore = live->physics->bodyStates();
        auto playerBefore = live->physics->playerPosition();
        live->world.find(10)->name = "Latest visual edit";
        live->physics->applyEdit(*delta, live->world);
        require(live->physics->bodyToken(1) == staticToken && live->physics->bodyToken(10) == dynamicToken,
                "Move replaced existing bodies");
        require(live->physics->collisionCacheStats().cooks == cacheBefore.cooks,
                "Static move recooked triangles");
        require(live->physics->playerPosition() == playerBefore, "Edit reset player");
        require(live->world.find(10)->name == "Latest visual edit", "Incremental edit lost visual changes");
        for (const auto& b : motionBefore)
            for (const auto& a : live->physics->bodyStates())
                if (a.id == b.id)
                    require(a.transform.position == b.transform.position && a.linear == b.linear,
                            "Unrelated dynamic state changed");
        bool stale = false;
        try {
            live->physics->applyEdit(*delta, live->world);
        } catch (const std::exception&) {
            stale = true;
        }
        require(stale, "Stale plan applied twice");
        edits.retire(std::move(delta));
        desiredEdit = live->world;
        desiredEdit.find(10)->mass += 1;
        desiredEdit.find(11)->body = "none";
        one::Entity extra;
        extra.id = 900;
        extra.body = "dynamic";
        extra.transform.position = {5, 5, 0};
        desiredEdit.entities.push_back(extra);
        edits.request(live->physics->captureEdit(desiredEdit));
        delta = ready(edits);
        require(delta->stats.added == 1 && delta->stats.removed == 1 && delta->stats.replaced == 1,
                "Wrong changed-body set");
        live->physics->applyEdit(*delta, live->world);
        require(live->physics->bodyToken(10) != dynamicToken && live->physics->bodyToken(1) == staticToken,
                "Wrong bodies replaced");
        require(live->physics->bodyToken(900) != uint32_t(-1) && live->physics->bodyToken(11) == uint32_t(-1),
                "Add/remove failed");
        edits.retire(std::move(delta));
        desiredEdit = live->world;
        desiredEdit.find(1)->transform.scale = {1.2f, .8f, 1.1f};
        desiredEdit.find(1)->transform.rotation = glm::angleAxis(.15f, glm::vec3(0, 1, 0));
        auto full = one::PhysicsScene::prepare(desiredEdit);
        full->preserveBodies =
            one::PhysicsRebuildRequest::capture(desiredEdit, *live->physics).preserveBodies;
        full->preserveLiveState(*live->physics);
        edits.request(live->physics->captureEdit(desiredEdit));
        delta = ready(edits);
        live->physics->applyEdit(*delta, live->world);
        for (int i = 0; i < 120; ++i) {
            live->physics->step(live->world, 1.f / 60, {}, false);
            full->physics->step(full->world, 1.f / 60, {}, false);
        }
        require(glm::distance(live->physics->playerPosition(), full->physics->playerPosition()) < .01f,
                "Moved/scaled local collision differs from full rebuild");
        desiredEdit = live->world;
        desiredEdit.find(10)->halfExtent = {-1, 0, 0};
        bool rejected = false;
        try {
            one::PhysicsEdit::prepare(live->physics->captureEdit(desiredEdit));
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid collision edit accepted");
        live->physics->setCollisionCacheBudget(0);
        require(live->physics->collisionCacheStats().bytes == 0, "Cache ignored memory budget");
        live->physics->step(live->world, 1.f / 60, {}, false);
        live->physics->setCollisionCacheBudget(64 * 1024 * 1024);
        std::cout << "PASS incremental move/add/remove/replace, stable bodies, stale/invalid rejection, "
                     "transformed collision parity, cached collision reuse/invalidation/eviction, async edit "
                     "supersession, current "
                     "motion and explicit transform preservation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
