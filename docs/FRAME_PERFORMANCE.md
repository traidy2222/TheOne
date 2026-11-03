# Frame performance and asset iteration

> Historical baseline: this page records the earlier renderer. [Render quality and background loading](RENDER_QUALITY.md) documents the subsequent cascaded shadows, mipmaps, LOD, animated culling and staged uploads. Those changes supersede the synchronous-upload and skinned-culling limitations below; the benchmark table has not been rerun.

Measured 2026-11-01 in Windows x64 Release on an Intel Core i7-12700F / NVIDIA GeForce RTX 3080. The focused executable loads the real Robots village scene but runs no gameplay or physics. The camera, simulation time and 640x480 viewport are fixed. Validation is disabled for timings. Each configuration warms for five frames, then measures 24 frames; FIFO presentation remains enabled. GPU results come from completed frame slots and include a separate GPU frame ID.

## Observations

| Frames in flight | Culling | Main draws | Shadow draws | CPU recording | GPU span | Loop wall time |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Off | 6,628 | 5,646 | 1.486 ms | 1.050 ms | 17.095 ms |
| 1 | On | 1,318 | 1,342 | 0.815 ms | 0.230 ms | 16.684 ms |
| 2 | Off | 6,628 | 5,646 | 1.245 ms | 1.086 ms | 17.383 ms |
| 2 | On | 1,318 | 1,342 | 0.927 ms | 0.821 ms | 16.191 ms |

The same view submits **6,628 -> 1,318 main draws (80% fewer)** and **5,646 -> 1,342 shadow draws (76% fewer)** with culling. CPU scene preparation averages 0.16-0.17 ms in this run. GPU culling savings are visible in both configurations, but absolute GPU timings vary with scheduling. The loop remains near the 16.7 ms presentation interval; these samples do **not** demonstrate a reliable FPS improvement from two frames in flight. One-frame mode remains available for comparison/latency-sensitive use.

GPU span covers shadow, main/UI and any capture commands, excluding presentation; CPU loop timing here covers prepare, UI setup and renderer calls only. The debug panel's CPU history sums the reported stages, not unrelated gameplay/physics work. Queries are read only after their slot fence completes and never request an additional GPU wait. Unsupported timestamp queues report timing unavailable.

## Implemented responsibilities

- **Profiling:** `frame_profile.cpp`, `frame_stats.hpp`. CPU scene/render preparation, command recording, fence/acquire waits, submit/present timings; GPU shadow/main/span queries, frame IDs, counts, upload bytes and history plots. The importer host exposes the panel with F2 or its menu button.
- **Visibility:** `core/bounds.hpp`, `visibility.cpp`. Conservative transformed AABBs, Vulkan zero-to-one near/far planes, independent camera and light frusta. Skinned geometry stays visible because bind-pose bounds are unsafe for deformation; dynamic/procedural CPU draws also remain visible. A profiler checkbox permits reference comparisons. Culled meshes remain resident; this is draw culling, not streaming or occlusion culling.
- **Scene cache:** `transform_cache.cpp`, `render_scene.cpp`. Persistent entity lookup and parent-resolved transforms; static palettes and inverse-normal matrices are reused. Direct public transform edits, reparenting, reordered entities, hidden descendants, model replacement, animation and spin transitions invalidate the appropriate work. Copied worlds detach cache state before updating it.
- **Selective reload:** `scene/asset_reload.cpp`, `assets/image.cpp`, `asset_invalidation.cpp`. Stable runtime model handles plus revisions; transactional model/image imports; only affected meshes/images/material descriptors replaced. Old GPU resources are deferred to completed submission serials. Shared mesh allocations are freed after their last surviving mesh is removed; dead portions of a still-used shared allocation remain allocated. Upload transfers still complete synchronously. Full-scene clear remains available.
- **Frame scheduling:** `frame_state.hpp`, `frame_resources.cpp`. Two slots by default; `setFramesInFlight(1)` provides comparison mode. Independent command buffers, acquire semaphores, fences, palettes, dynamic vertices, scene uniforms, shadow targets and query pools; per-swapchain-image main depth and presentation semaphores. Each material has descriptors for both frame slots. Procedural revision caches warm each slot once. Resize/shutdown drain outstanding work.

Robots and Sandbox inspector controls reload a selected model or a selected image from a file; the model command also rebuilds collision while preserving player/body motion. Import failures keep the live model/image. Whole-scene F5 reload and undo retain their explicit full-reset behavior. Robots, Sandbox and Planets were rebuilt against the changes; gameplay suites were not run.

## Focused verification

Only two focused checks ran:

- `engine_scene_cache`: frustum near/far/boundary checks, nonuniform transforms, independent light visibility, cache reuse and ancestor invalidation, hidden descendants, copied worlds, reordering/reparenting, cycle rejection, animation updates, model identity/revision replacement and failed model/image import preservation.
- `engine_frame_features`: rendered culling-on/off parity; one/two-slot parity and alternating slots; completed GPU timestamps; alternating animated palettes/CPU geometry; procedural revisions; model reload while slots are outstanding without reuploading other meshes; deferred cleanup; exactly one texture upload with no geometry upload and visibly changed pixels; restored image parity; resize and profiler panel. Passed with the Khronos synchronization validation feature enabled and zero validation errors/warnings.

```powershell
cmake --build build --config Release --target theone_scene_cache_tests theone_frame_features --parallel 8
ctest --test-dir build -C Release -R '^engine_(scene_cache|frame_features)$' --output-on-failure
# Runs only the renderer/scene profile, not a game loop or gameplay suite.
.\build\Release\theone_frame_features.exe 'path/to/scene.one' --profile
```

The GPU test requires an installed Khronos validation layer. CMake supplies the SDK layer path and enables synchronization validation when the SDK manifest is present. Local evidence: `build/frame-focused-build.log`, `build/frame-focused-check.log`, `build/Testing/Temporary/LastTest.log`, `build/frame-profile-robots.log`, `build/frame-comparison.json`, plus the consumer `Games 2/build/frame-features-build.log`.

