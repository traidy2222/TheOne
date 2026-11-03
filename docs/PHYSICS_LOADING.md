# Background collision preparation

Scene loading now prepares a complete `World` and independent `Physics` instance on the loading worker. The ready result includes triangle baking, Jolt mesh creation, body insertion, player creation and broad-phase optimization. Robots/Sandbox publish both objects together; their scene-application path no longer calls `Physics::rebuild`.

## Ownership

- `physics/scene_loading.hpp` and `src/physics/scene_loading.cpp`: `PhysicsScene` bundles a world and its collision system, with import/collision timings. `PhysicsSceneLoadJob` runs that preparation. `swapInto` exchanges the prepared and active worlds without reconstructing collision or deleting the previous system.
- `src/physics/runtime.*`: a synchronized Jolt registration lease initializes shared types before an instance's Jolt members and releases them after those members are destroyed, including constructor failure. Independent instances can build, simulate and retire concurrently. Each individual instance remains confined to its current owning thread.
- `core/load_job.hpp`: `retire(std::move(result))` transfers cleanup to the loader. Superseded, cancelled and unconsumed ready results also retire there. Destruction runs outside the publication mutex; request/status/take do not wait for collision teardown. Shutdown cancels, drains retirement and joins the worker.
- `src/physics/world.cpp`: optional stop-token checks before construction, between bodies, during triangle collection, after baking/mesh creation and around broad-phase completion. Checkpoints borrow the token rather than making a reference-counted copy.
- Consumer `shared/import-host/background_loading.*`: prepares scene collision along with import and displays the existing loading/cancel interface. `application.cpp` sets the latest respawn position, swaps the result into the session, retires the old scene, resets the simulation accumulator and starts the game module.

The previous scene stays active while preparation runs. Import, collision or cancellation failures cannot publish a partial replacement. Existing GPU invalidation continues on the render thread with its deferred GPU lifetime rules.

```cpp
one::PhysicsSceneLoadJob job;
job.request(path);
// During later frames, on the thread that owns the live world:
if (auto ready = job.take()) {
    ready->physics->setRespawnPosition(respawn);
    ready->physics->resetPlayer(respawn);
    ready->swapInto(world, physics);
    job.retire(std::move(ready)); // old world/collision destroyed on the worker
}
```

## Focused verification and measurement

2026-11-01, Windows x64 Release, Intel Core i7-12700F. `engine_physics_scene_loading` passed in **0.19 seconds**. It checks concurrent first/final Jolt ownership; simulation during worker construction; matching player and dynamic-body behavior against synchronous rebuild; cancellation; failed import; failed collision after a body has already been created; recovery with a valid load; and worker cleanup. A deliberately blocked destructor verifies that publication operations remain accessible while retirement is running.

The same CPU-only executable profiled the actual Robots village scene once. This does not launch a game, renderer or gameplay suite.

| Operation | Measured time |
| --- | ---: |
| Synchronous collision preparation baseline, including `Physics` allocation | 126.510 ms |
| Worker collision preparation | 113.049 ms |
| Worker scene import | 745.543 ms |
| Prepared world/physics swap | 0.0006 ms |
| Old result retirement enqueue | 0.0124 ms |
| Largest observed status poll | 0.0028 ms |

The old world advanced 56 physics steps while the replacement loaded. The benefit is moving roughly 0.1 seconds of collision work off scene application; the difference between baseline and worker construction durations is not evidence of a faster collision algorithm. Swap/enqueue numbers exclude renderer invalidation, editor history cleanup, game callbacks and frame presentation; they are not whole-frame latency measurements.

```powershell
cmake --build build --config Release --target theone_physics_scene_loading --parallel 8
ctest --test-dir build -C Release -R '^engine_physics_scene_loading$' --output-on-failure
.\build\Release\theone_physics_scene_loading.exe '..\Games 2\games\robots\build\robots-village.one' --profile
```

Evidence: `build/physics-loading-build.log`, `build/physics-loading-check.log`, `build/physics-loading-robots-profile.log`, and consumer `Games 2/build/physics-loading-build.log`. Only the focused collision/load check and profile ran; Robots/Sandbox compilation verifies the changed host integration.

## Limits

This applies to asynchronous whole-scene startup, F5 reload and saved-scene loading. Selected-model replacement, inspector collision edits and undo/redo were subsequently moved to background preparation with state preservation; see [editing, handoff and memory](ENGINE_ITERATION.md). Explicit synchronous save-game restoration and smoke execution retain synchronous semantics.

Cancellation is cooperative: an individual model bake, Jolt mesh cook or broad-phase optimization already executing completes before the next checkpoint. Rebuild operates on a private instance; calling cancellable `rebuild` on an active instance can leave it partially rebuilt if cancellation arrives after construction starts. Keep the transactional `PhysicsScene` boundary for live scene loading.

Preparation temporarily retains both worlds and their collision memory. Retirement shares the loading worker, so a previous teardown can delay the next load but does not block the frame thread. Application shutdown may wait for an in-progress uninterruptible stage and cleanup.
## Incremental editor updates

Editor changes now use `PhysicsEditJob` to prepare affected colliders and apply
them to the existing physics world. Static mesh cooking is local-space, so pose
edits reuse shapes. See [runtime performance](RUNTIME_PERFORMANCE.md) for current
ownership, validation and limits. The full-scene job described below remains the
scene replacement path.


