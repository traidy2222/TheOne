# Developing TheOne

## Layout and ownership

```text
include/theone/       Public API, grouped by responsibility
include/compat/      Forwarding headers for old consumers
src/core/            Private validation helper and Core interface target
src/animation/       Pose sampling and crossfades
src/assets/          Model API implementation and CPU geometry
  gltf/              Loader orchestration, images, materials, meshes, rig, animation import
src/geometry/        Mesh indexing/LOD, conservative skin bounds, mipmaps and cascade fitting
src/scene/           Hierarchy, scene serialization, GPU render-scene preparation
src/physics/         Jolt runtime/lifecycle, background scene collision, simulation, character, motion snapshots
src/input/           Input binding loading
src/effects/         Particle simulation and geometry
src/render/          Renderer facade
  vulkan/            Device, swapchain, memory, targets, pipelines, passes, frame submission,
                     geometry cache, materials, readback and resource lifetime
cmake/               Pinned dependencies and shader build/deployment
shaders/             Vulkan shader sources
tools/import/        Generic asset conversion executable
tools/inspect/      Model inspection executable
tests/architecture/  Header independence and module link/assembly smoke check
tests/content/       Existing CPU content/scene/physics regression entry point
tests/import/        Existing OBJ conversion check
tests/render/        Existing GPU parity/validation entry point
```

Each module has a CMake source list. Public types and function declarations belong in `include/theone/<responsibility>`; implementation belongs in the matching `src` directory. Do not include `.cpp` or `.inl` implementations inside public classes. Keep helpers private and use explicit includes rather than relying on unrelated headers to import dependencies.

`TheOne::Core` supplies math and data types without a renderer or physics runtime. Animation and Geometry depend on Core; Geometry privately links meshoptimizer; Assets depends on Animation and Geometry; Scene depends on Assets; Physics depends on Scene with Jolt private. Input and Effects depend on Core. Renderer depends on Core, Geometry and its SDL/ImGui backend. `TheOne::Content` exists only as the compatibility aggregate. Asset tools link Assets, so changing physics no longer rebuilds their dependency path.

## Finding renderer code

The current gameplay profiling, background preparation, instancing, incremental
collision and texture residency modules are mapped in [Runtime performance](RUNTIME_PERFORMANCE.md).

The public `one::Renderer` facade owns `Impl` through a unique pointer. `vulkan/backend.hpp` declares backend operations; it contains no renderer method bodies. Resource groups have named state structures in their own private headers, and the backend lifecycle coordinates their destruction.

| Change | Owning files |
| --- | --- |
| Device selection, queues, validation callback | `device.cpp`, `device_state.hpp` |
| Resize and presentation images | `swapchain.cpp`, `swapchain_state.hpp` |
| Buffer/image allocation and staged upload lifetime | `memory.cpp`, `upload_queue.cpp` |
| Render targets and graphics pipelines | `render_targets.cpp`, `pipelines.cpp` |
| Draw filtering/order and frame submission | `draw_order.hpp`, `draw_list.cpp`, `frame.cpp` |
| Frame-slot ownership and deferred retirement | `frame_resources.cpp`, `frame_state.hpp` |
| CPU/GPU timings and debug panel | `frame_profile.cpp` |
| Live asset accounting and immutable geometry totals | `asset_memory.cpp`, `asset_memory_geometry.cpp`, `asset_memory_state.hpp` |
| Camera / shadow visibility | `visibility.cpp`, `render/spatial_regions.*`, public `core/bounds.hpp` |
| Asset ownership, synchronization and invalidation | `asset_owners.*`, `asset_sync.cpp`, `asset_invalidation.cpp` |
| Main/shadow command recording | `passes.cpp`, `shadow_pass.cpp`, `shadow_submission.cpp`, `shadow_lod.cpp`, `shadow_resources.cpp` |
| Resident mesh/index data and joint palettes | `geometry_cache.cpp`, `geometry_resources.cpp`, `geometry_state.hpp` |
| Geometry draw commands and procedural uploads | `draw.cpp` |
| Texture/material descriptors and batched uploads | `materials.cpp`, `textures.cpp`, `texture_upload.cpp`, `materials_state.hpp` |
| Shared scene/shadow resources | `resources.cpp`, `scene_state.hpp` |
| Screenshot/readback | `readback.cpp`, `capture_state.hpp` |
| Startup, asset reset, shutdown | `lifecycle.cpp` |

## Focused build and validation

Select the smallest target and check that cover the edit. A structural refactor needs affected-module compilation, public-header checks and a small link/assembly check. It does not require running complete engine/game suites, gameplay smoke loops or GPU benchmarks. Run rendering tests when rendering behavior changes; broaden validation only for a concrete failure or affected integration.

```powershell
# Change renderer implementation: compile just that module and its dependencies.
.\run.ps1 -Configuration Release -Target one_renderer

# Change import/model code: compile Assets, without physics or game executables.
.\run.ps1 -Configuration Release -Target one_assets

# Public headers / target wiring / mechanical source refactor.
.\run.ps1 -Configuration Release -Target theone_module_smoke -TestRegex '^engine_module_boundaries$'

# When GPU behavior actually changes.
.\run.ps1 -Configuration Release -Target theone_gpu_tests -TestRegex '^engine_gpu_validation$'

# Explicit full engine suite, when warranted.
.\run.ps1 -Configuration Release -AllTests
```

If configuration is unchanged, use `cmake --build build --config Release --target <target> --parallel 8` directly. `run.ps1 -Test` without a test selection is rejected before building. Focused tests require a build target; `-AllTests` is the only helper option that runs the entire suite. No engine helper builds or runs consuming games.

For this modularization, the focused smoke check loads one small fixture, assembles its pose/scene data, checks input/effect linkage, and constructs/destructs the opaque renderer and physics facade. It creates no GPU window and runs no simulation. Every canonical public header also compiles in its own translation unit. A one-time source audit compared all 33 moved renderer method bodies to the pre-refactor snapshot after state qualification/formatting; no rendering algorithm or shader was changed.

## Profiling asset loads

`theone_load_profile` isolates scene import, collision construction, asset invalidation and first-render uploads. Use it for loading/upload changes instead of the full gameplay smoke tests. `Renderer::assetUploadStats()` provides nested stage timings and submission counts. See [loading measurements](LOADING_PERFORMANCE.md) for the measured allocation/submission bottlenecks and focused commands.

For CPU import work, use `theone_scene_load_profile` (links Scene/Assets only). Optional `SceneLoadStats`/`ModelLoadStats` expose JSON, model parse/buffer/validation, image, mesh, rig and animation stages. Run only `engine_scene_loading` and `engine_image_loading` for the importer changes described in [loading measurements](LOADING_PERFORMANCE.md). Single-image decoding belongs in `src/assets/gltf/image_decode.cpp`; batch scheduling belongs in `images.cpp`.


## Frame development APIs

- `World::prepare` owns a copy-on-write scene cache. `src/scene/transform_cache.cpp` compares public entity transforms and topology against snapshots, propagates dirty ancestors and rejects invalid hierarchies. `render_scene.cpp` reuses static node/joint palettes. Public entity editing remains supported; callers need no manual dirty calls. `RenderScene::preparation` reports recomputed roots, evaluated poses, rebuilt palettes and elapsed CPU time.
- `World::reloadAsset(path)` loads one model before replacing all matching references. The new model preserves its runtime `AssetHandle::id` and increments `revision`. Retained `RenderScene`s and copied Worlds still own their old model until prepared/replaced. Handles identify runtime assets, not serialized cross-process IDs.
- `Image::reload(path)` atomically replaces decoded pixels and advances its revision. For programmatic pixel edits, advance `Image::revision` after completing the edit. Renderer detects image revisions and recreates only dependent material descriptors and that image. `invalidateMaterial`, `invalidateImage` and `invalidateAsset` provide explicit invalidation for render-thread callers. Models are immutable snapshots; replace/reload them instead of mutating their geometry in place.
- `Renderer::frameStats()` exposes CPU stages and completed-frame GPU timestamps with separate frame IDs. `drawProfiler()` is called between ImGui NewFrame/Render; it shows timings, histories, culling/draw/triangle/upload counts and deferred resource counts. The importing game host exposes this on F2 and an editor menu button, plus selected asset/image reload controls.
- `Renderer::setCulling(bool)` toggles camera and light-frustum checks independently. Imported static geometry uses conservative transformed AABBs. Skinned meshes use current-palette per-joint influence bounds; unsafe bounds and dynamic CPU/procedural draws without bounds remain visible.
- `Renderer::setFramesInFlight(1 or 2)` supports A/B profiling; the default is two. Each slot owns commands, fence/acquire semaphore, palette descriptor/buffer, dynamic vertex buffer, scene uniform, shadow image and timestamp queries. Materials have immutable descriptors for both slots; main depth targets are per swapchain image. Persistent procedural geometry warms each slot once per revision.

Selective GPU deletion is deferred to the last submitted frame serial and collected only after a frame fence completes. Shared geometry batches retain their allocation while any surviving mesh uses it; removing part of a batch does not reclaim its unused bytes immediately. Explicit whole-scene reset and resize still wait for device idle. Budgeted asset uploads are staged into frame submissions; staging resources retire after the slot fence completes. See [render quality and background loading](RENDER_QUALITY.md) for controls and limits. Renderer calls and asset edits are render-thread operations.

Focused checks and profiling commands are documented in [frame performance](FRAME_PERFORMANCE.md). The frame-slot and timestamp design follows the [Vulkan frames-in-flight guidance](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/03_Frames_in_flight.html) and [timestamp-query documentation](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html).


## Background scene collision

Use `PhysicsSceneLoadJob` for whole-scene import and collision preparation, then `PhysicsScene::swapInto` and `LoadJob::retire` to publish the result and destroy the previous world on the worker. Independent physics systems may run concurrently; a single system still has one owning thread at a time. See [collision loading](PHYSICS_LOADING.md) for the focused test/profile and lifetime rules.

## Editing, handoff and memory

`PhysicsRebuildJob` accepts typed editor snapshots; `shape_cache.*` owns reusable cooked collision. `upload_scheduler.cpp` handles chunk budgets and GPU timing feedback; `texture_upload.cpp` handles image regions and mip transitions; `asset_memory.cpp` owns accounting and unused-model collection. The consumer's `scene_handoff.*` owns application-stage profiling. Controls, limits and focused checks are in [editing and memory](ENGINE_ITERATION.md).

`ScenePicker` provides reusable full-detail ray picking over a current `RenderScene`; see [1080p/editor/cache work](CPU_EDITOR_ASSET_PERFORMANCE.md) for module ownership, prepared-cache controls and focused checks.

Visible gameplay/editor replay, entity undo transactions, paced retirement, resident draw references and warm cache dependency manifests are documented in [visible hitch performance](VISIBLE_HITCH_PERFORMANCE.md).

Correlated frame waits, unchanged-model reload reuse, bounded staging pooling,
incremental draw/shadow submission and budgeted parallel asset preparation are
documented in [frame waits and reload performance](FRAME_WAITS_RELOAD_PERFORMANCE.md).

Optional presentation-completion pacing, calibrated CPU/GPU tracing, fallback
behavior and the targeted presentation check are documented in
[FIFO pacing performance](FIFO_PACING_PERFORMANCE.md).

Stage CPU-cycle tracing, the input sampling boundary, compatible surface-state
reuse, bounded material admission/warmup and capture identity/comparison tools
are documented in [CPU, input and readiness performance](CPU_INPUT_READINESS_PERFORMANCE.md).

Present-ID stalls, compositor correlation, bounded sleeping and the focused
wait-policy/presentation checks are documented in
[presentation stall performance](PRESENTATION_STALL_PERFORMANCE.md).

Selective reload invalidation, incremental memory accounting, adaptive shadow
plan reuse, bounded draw scratch and private-state header boundaries are covered
in [reload and renderer caches](RELOAD_CACHE_PERFORMANCE.md).

The follow-up [cache correction](CACHE_CORRECTION_PERFORMANCE.md) documents
same-run shadow comparisons, generation-based invalidation and repeated reloads.


## Shared scene editing and prepared-scene reuse

`TheOne::Scene` owns `EditorHistory` in `theone/scene/editor_history.hpp`.
It stores bounded entity deltas, coalesces an application gesture into one
transaction and reports whether an edit affects collision. Applications own
UI gestures, simulation policy and retirement of old history containers.

`TheOne::Physics` owns `reuseSceneAssets` in `theone/physics/scene_reuse.hpp`.
Call it on a prepared `PhysicsScene` before publication. It returns obsolete
live models for renderer invalidation and keeps discarded fresh models in the
prepared scene for worker retirement. The application chooses whether reuse is
enabled, then coordinates invalidation and `swapInto`.

Focused checks: `engine_editor_history` and `engine_scene_reuse`. Both run
without a GPU window. See [ownership review](ENGINE_GAME_BOUNDARY_REVIEW.md).
