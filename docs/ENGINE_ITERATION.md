# Editing, handoff, upload scheduling and asset memory

Implemented 2026-11-01. This extends [background collision preparation](PHYSICS_LOADING.md) and supersedes the earlier oversized-resource upload policy in [render quality](RENDER_QUALITY.md).

## Responsibilities and controls

| Area | Owning module | Current behavior |
| --- | --- | --- |
| Editor collision replacement | `physics/scene_loading.*`, `physics/body_state.cpp`, consumer `background_loading.*` | Selected-model reload, inspector rebuilds and undo/redo prepare independent collision worlds on a worker. Latest requests supersede earlier ones. Current poses/velocities are restored for unchanged dynamic bodies; explicit transform edits retain their requested pose. Non-collision edits made during preparation are merged at adoption. |
| Cooked shape cache | `physics/shape_cache.*` | Shared, bounded in-memory cache of static mesh shapes. Exact transformed triangle positions are compared after hashing, so hash collisions cannot reuse mismatched shapes. Geometry or transform changes miss the cache; irrelevant material/color changes do not. |
| Scene handoff | Consumer `scene_handoff.*`, renderer `asset_invalidation.cpp` | Timings for batched GPU invalidation, history retirement, ownership swap, game callbacks, first scene preparation/render and frames until uploads finish. Old history containers move to worker retirement. |
| Upload scheduling | `render/vulkan/upload_state.hpp`, `upload_scheduler.cpp`, `upload_queue.cpp`, `texture_upload.cpp` | Large geometry, cached procedural buffers and textures are split into chunks. GPU timestamps feed a throughput estimate; a time target and byte limit control each frame's scheduled work. Incomplete resources are not drawn. |
| Asset memory | `render/vulkan/asset_memory.cpp`, `textures.cpp` | Cache-owned CPU mesh/image payloads, pending CPU payloads, resident GPU mesh/texture allocations, staging allocations, deferred asset allocations, packed geometry slack and texture aliases are exposed. Identical image pixels/dimensions/color-space use one GPU allocation. |

`LoadJob<T, Input>` accepts typed request snapshots. Its worker retires results, superseded queued requests and explicitly transferred resources outside the publication mutex. `retire(shared_ptr<Resource>)` also permits the host to move undo/redo cleanup onto the existing worker.

Editor collision preparation pauses simulation while the collision snapshot is pending; rendering, loading UI and cancellation continue. A successful replacement restores the preceding simulation setting. Explicit synchronous save-game restoration and smoke paths retain synchronous semantics. Cancelling a collision edit leaves simulation paused so an unapplied collider edit is not simulated against stale collision.

`Physics::collisionCacheStats()` reports hits, completed cooks, entries and retained bytes. `setCollisionCacheBudget(bytes)` controls the shared cache, default **64 MiB**. LRU eviction releases the cache reference; existing bodies retain their shapes safely. Registration lifetime remains protected by the Jolt runtime lease. The cache is process-local and does not write cooked files to disk. A cache hit still performs geometry baking/key comparison, body creation and broad-phase setup.

`Renderer::setUploadBudget(bytes)` now limits scheduled work, with a four-byte minimum/alignment and no oversized-resource transfer exception. The default remains **8 MiB**. `setUploadTimeBudget(milliseconds)` defaults to **1 ms** and is exposed in the profiler. The maximum chunk is **256 KiB**. Work bytes count copied payloads plus mip destination bytes; they are not a measurement of memory-bus traffic. Power-of-two textures use tiled GPU mip blits; other dimensions use the existing linear-light CPU mip generator, then chunked transfers, to preserve odd-dimension filtering.

The time setting is a **soft scheduling target**, not a real-time guarantee. Scheduling checks elapsed CPU time between chunks and caps future work using completed GPU timestamps. A driver allocation, command-recording call or OS preemption cannot be interrupted. Initial resource allocation, content comparison/hash, geometry packing and CPU mip generation remain foreground preparation outside the chunk scheduler. In the tiny-budget fixture, a 0.2 ms target kept observed GPU upload samples below 0.115 ms, but occasional CPU scheduling samples reached 1.08 ms. The implementation does not claim a hard 0.2 ms frame bound.

`Renderer::assetMemoryStats()` exposes the asset counters; the F2 profiler displays them and a trim button. `setAssetMemoryBudget(bytes)` defaults to **512 MiB**. Models unused for 120 submitted frames are eligible for release; memory pressure or `trimUnusedAssets()` makes unused models immediately eligible. Cleanup processes at most eight unused models per frame and respects GPU completion serials. Active assets are never evicted to satisfy the budget, so an active scene may exceed it. Shared packed geometry allocations survive until their last live mesh is removed; slack is reported.

Memory counters describe renderer-cache asset payloads and selected Vulkan allocations, not total process RSS or total device VRAM. They exclude animation/container overhead, application-owned history, descriptor/pipeline/driver memory, shadow/render targets and frame palette/dynamic-buffer capacity. CPU image vectors remain per model; texture sharing applies to GPU storage. Color and linear-data variants remain separate. Pixel edits must advance `Image::revision`; changing one alias does not change another model's texture.

## Measured results

Windows x64 Release, Intel Core i7-12700F, RTX 3080, actual Robots village scene, 640x480 hidden Vulkan window. The handoff fixture calls the same host handoff module and actual game-start callback, with 32 undo and eight redo snapshots. It runs no gameplay loop. Validation is disabled for this timing profile.

| Stage | Before batching/worker history retirement | After |
| --- | ---: | ---: |
| Scene application total | 8.8791 ms | 2.7598 ms |
| GPU asset invalidation | 6.5783 ms | 2.7466 ms |
| History cleanup / retirement enqueue | 2.2962 ms | 0.0083 ms |
| World/physics ownership swap | 0.0006 ms | 0.0006 ms |
| Game callbacks | 0.0039 ms | 0.0042 ms |
| First scene preparation | 0.2967 ms | 0.2798 ms |
| First renderer call, including presentation | 9.4864 ms | 11.4591 ms |
| Frames until upload backlog clears | 47 | 47 |

Application time decreased about **69%**. This is not a measured FPS increase: first-render latency did not improve in this sample and includes presentation scheduling.

In the final process, initial collision preparation took **111.696 ms**, and the subsequent cached rebuild took **24.8391 ms**. The cache recorded 19 cooks and 19 hits, retaining 8,277,624 bytes. Renderer residency reported 21,477,440 bytes of geometry allocations, 28,760,064 bytes of textures and **951 texture aliases** sharing allocations. These are single-process measurements with no OS-cache flush.

## Focused checks

Only the checks below ran; no complete engine/game suite ran. Robots, Sandbox and Planets were rebuilt.

- `engine_physics_scene_loading`: worker construction/retirement, concurrent Jolt ownership, active simulation, synchronous collision parity, cancellation and failure recovery. Passed in 0.18 s.
- `engine_physics_editing`: unchanged shape reuse, geometry/transform invalidation, bounded eviction with live bodies, superseding typed requests, latest motion/velocity, intentional transform preservation and concurrent non-collision edits. Passed in 0.07 s after the final edit.
- `engine_streaming_memory`: 64 KiB per-frame work cap; identical captured pixels with large versus small budgets; texture sharing and revision isolation; memory-pressure eviction; repeated replacement; invalidation with unfinished transfers; and cached procedural revision supersession. Passed in 3.13 s with zero reported Vulkan validation errors/warnings. The small-budget fixture took 25 frames to settle.
- `engine_render_quality_gpu`: updated to wait for chunked-upload residency rather than assuming an oversized resource completes in one frame. Passed in 7.39 s, retaining CPU/GPU image parity, sRGB mip filtering, cascade travel, animated visibility and LOD results with no reported validation warnings/errors.
- Consumer `scene_handoff_focused`: the shared handoff module, history retirement, actual game-start callback, first frame and upload convergence. Passed in 0.58 s with Vulkan synchronization validation enabled.

```powershell
# Engine: only affected collision/loading and streaming/memory checks.
cmake --build build --config Release --target theone_physics_editing theone_physics_scene_loading theone_streaming_memory --parallel 8
ctest --test-dir build -C Release -R '^engine_(physics_editing|physics_scene_loading|streaming_memory)$' --output-on-failure

# From Games 2: handoff fixture and optional real-scene profile.
cmake --build build --config Release --target scene_handoff_profile --parallel 8
ctest --test-dir build -C Release -R '^scene_handoff_focused$' --output-on-failure
.\build\Release\scene_handoff_profile.exe games/robots/build/robots-village.one --profile
```

Evidence: engine `build/iteration-final-build.log`, `build/iteration-final-check.log`, `build/physics-editing-final-check.log`, `build/quality-streaming-build.log` and `build/quality-streaming-check.log`; consumer `build/iteration-final-build.log`, `build/handoff-final-check.log`, `build/handoff-robots-profile.log`, `build/handoff-robots-final-profile.log` and `build/editing-final-build.log`.
