# Rendering quality and background loading

Implemented 2026-11-01. These changes extend the earlier frame and loading work; previous benchmark tables describe the earlier renderer, not the three-cascade renderer introduced here.

## Responsibilities

| Feature | Owner | Behavior |
| --- | --- | --- |
| Mipmaps and filtering | `geometry/mipmaps.cpp`, `render/vulkan/textures.cpp`, `texture_upload.cpp` | Complete mip chains; linear-light sRGB color filtering; separate linear data textures; trilinear sampling and anisotropy up to 8x when supported. GPU blits have a CPU fallback. |
| Camera-following shadows | `geometry/cascades.cpp`, `render/vulkan/shadow_resources.cpp`, `shadow_pass.cpp`, `shaders/cascades.glsl` | Three 2048-square cascades per frame slot, practical splits, texel-snapped projections, overlapping transitions, 3x3 PCF and far-distance fade. |
| Distance LOD | `geometry/prepare.cpp`, `lod.cpp`, `render/vulkan/geometry_cache.cpp`, `visibility.cpp` | Shared indexed geometry with up to two simplified index levels, projected error selection and hysteresis. Close views retain original topology. |
| Background loading | `core/load_job.hpp`, `assets/load_job.hpp`, `scene/load_job.hpp`; consumer `shared/import-host/background_loading.*` | Cancellable worker jobs, latest-request publication, errors without partial scene mutation, loading/cancel UI, live scene retained during reload/import. |
| Staged GPU uploads | `render/vulkan/upload_queue.cpp`, `material_streaming.cpp`, `texture_upload.cpp` | Transfers recorded before drawing in the frame submission; staging allocations retired by the slot fence; unfinished draws deferred across frames. |
| Animation-aware culling | `geometry/prepared_mesh.hpp`, `prepare.cpp`, `render/vulkan/visibility.cpp` | Per-joint influence bounds transformed by the current palette, including unweighted vertices. Conservative bounds feed both camera and cascade visibility. |

`TheOne::Geometry` depends on Core and privately on pinned meshoptimizer. Assets prepares meshes during import; Renderer consumes prepared geometry without linking the importer. Programmatically created primitives can supply `Primitive::prepared`, or let Renderer prepare them on first use. Models remain immutable geometry snapshots: rebuild preparation when constructing a replacement model.

## Controls and integration

- `Renderer::setLod(bool)` enables distance LOD; the profiler includes a toggle and triangle savings.
- `Renderer::setShadowDistance(float)` controls cascade coverage, default 150 world units. The profiler exposes this setting.
- `Renderer::setUploadBudget(uint64_t)` sets a per-frame transfer-work byte budget, default 8 MiB; `setUploadTimeBudget` adds an adaptive time target. `pendingDraws`, `uploadCommands`, `lodDraws`, `lodTrianglesSaved` and `animatedCulled` expose progress/work counts.
- `SceneLoadJob`, `ModelLoadJob` and `ImageLoadJob` wrap `LoadJob<T>`. Call `request(path)`, poll `status()`, take a ready result on the owning thread, or `cancel()`. A new request cancels/supersedes the old request. Errors and stale results cannot publish over a newer request.
- Synchronous `World::load` and `Model::load` remain available with optional stop tokens. `World::replaceAsset` applies a completed model while preserving runtime asset identity and advancing its revision.
- Robots/Sandbox startup, F5 scene reload, saved-scene loading, model import/reload and image reload use the background host. Planets' custom fragment shader shares the new scene uniform layout. Save-game restoration and explicit smoke execution retain synchronous adoption semantics.

## Boundaries

Cancellation is cooperative at import stages, decode tasks, mesh/animation loops and LOD tiers. A codec or meshoptimizer call already running finishes before observing cancellation; destroying a job joins its worker. Cancellation does not abandon resources or forcibly terminate threads.

Whole-scene collision reconstruction now runs on the loader along with import; scene publication swaps the ready world/physics pair and queues the old pair for worker retirement. See [background collision preparation](PHYSICS_LOADING.md) for ownership and focused measurements; [editing and memory](ENGINE_ITERATION.md) covers the subsequent asynchronous editor/model-reload paths.

GPU transfers use the graphics queue, with no upload queue-idle waits. CPU staging allocation/copy still happens on the rendering thread. Large resources, including cached procedural buffers, now transfer in bounded chunks. See [upload scheduling and memory](ENGINE_ITERATION.md) for time-target limits and memory controls. Geometry can appear progressively; cancellation stops CPU jobs, while already submitted transfers complete normally. GPU shadow/main timestamps exclude the preceding transfer command buffers. Resize, explicit full clear and shutdown still drain outstanding GPU work.

LOD applies to static opaque meshes. Skinned, masked and blended draws keep original indices; shadow passes also use original topology. Simplification preserves original vertices and uses attribute-aware meshoptimizer with locked borders. It may produce no useful lower tier for a small or constrained mesh. Prepared CPU geometry and extra index levels add memory alongside the existing expanded physics/baking geometry. This is geometric LOD, not texture residency management or occlusion culling.

Animated bounds are conservative for the renderer's normalized nonnegative linear blend skinning. Unsafe bounds fall back to visibility rather than risking disappearing geometry. Dynamic CPU/procedural draws without bounds remain visible. Shadow coverage is finite and configurable; it does not provide planet-scale shadows.

## Focused evidence

Windows x64 Release, RTX 3080, 640x480 synthetic rendering fixture. Only the two feature-specific checks below ran for this change; consuming apps were compiled without gameplay suites.

| Check | Result |
| --- | --- |
| `engine_render_quality_cpu` | Passed in 0.12 s: sRGB/data/odd-size mip generation, cascade coverage and movement, LOD indexing/error selection, weighted animated bounds, cancellation/latest-result/errors, small asynchronous model/scene loads and asset replacement. |
| `engine_render_quality_gpu` | Passed in 1.60 s with Khronos synchronization validation and zero reported warnings/errors. |
| Dense static mesh LOD | 32,768 to 8,192 main-pass triangles (75% fewer) at distance; close view restored all triangles. This is a fixture result, not a measured game FPS gain. |
| Mip filtering | Minified sRGB checker center 187; both color/data variants uploaded complete chains. |
| Shadow travel | Scene and camera translated 1,000 units; mean pixel-channel difference 0.0677289. Shadows-on/off comparison confirmed actual shadow coverage. |
| Animated visibility | CPU-baked/GPU and culling-on/off image mean error 0; an offscreen animated actor was culled. |
| Upload scheduling | Deliberately tiny byte budget spread fixture upload over 20 frames; backlog converged with zero immediate upload submissions. Invalidation, resize and teardown also passed. |

```powershell
cmake --build build --config Release --target theone_render_quality_cpu theone_render_quality_gpu --parallel 8
ctest --test-dir build -C Release -R '^engine_render_quality_(cpu|gpu)$' --output-on-failure
```

Evidence: `build/quality-final-build.log`, `build/quality-final-check.log`, `build/Testing/Temporary/LastTest.log`, `build/verification/gpu/quality-*.ppm`, and the consumer `Games 2/build/render-quality-build.log`. The application builds cover Robots, Sandbox and Planets; no full engine/game suite or new full-scene performance benchmark was run.
