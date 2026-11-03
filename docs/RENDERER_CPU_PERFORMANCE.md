# Renderer CPU follow-up

> The [shadow submission follow-up](SHADOW_SUBMISSION_PERFORMANCE.md) covers batching, owner caching, spatial regions, cascade LOD and uncapped profiling.

2026-11-02, Windows Release, RTX 3080 / i7-12700F. The existing Robots gameplay
profile uses 128 frames at 640x480, logical CPU 0 affinity, FIFO presentation,
the moving player/camera and animated actor. Instancing and texture streaming
remain enabled in both versions.

## What kept the earlier total flat

The first instrumented run found memory accounting at 1.787 ms/frame,
visibility/LOD preparation at 1.222 ms, draw-list preparation at 1.136 ms and
shadow recording at 1.677 ms. Texture residency was only 0.202 ms.
Memory accounting ran after presentation, outside the old prepare-plus-record
total. The profile now reports it separately and includes it in renderer CPU
work. Timings are CPU wall durations, so scheduling noise remains possible.

## Changes and ownership

- `vulkan/asset_memory_geometry.cpp` caches totals for immutable mesh payloads
  and geometry allocations. Mesh admission, asset invalidation/eviction and
  geometry destruction invalidate the snapshot. Its raw model references are
  only read after refreshing a dirty snapshot on the renderer thread.
- `vulkan/asset_memory.cpp` still counts current model image capacities,
  shared prepared image payloads, unique GPU images, replacements, staging,
  pending payloads and retired allocations every frame. Memory-pressure
  decisions retain the same cadence and accounting semantics.
- `render/draw_order.hpp` compacts ready opaque draws in source order and sorts
  only transparent draws, retaining descending distance and stable ties.
  `vulkan/draw_list.cpp` owns upload readiness and depth calculation. Offscreen
  shadow casters remain in the list. There is no persistent draw cache that
  could retain stale materials or scene owners.
- `vulkan/geometry_cache.cpp` stores immutable LOD error tables when admitting
  a mesh; `visibility.cpp` reuses them instead of allocating/copying a vector
  per object per frame. Bounds, hysteresis and near-distance protection stay
  unchanged.
- `vulkan/visibility_state.hpp` stores per-draw rigid-bound snapshots. Exact root
  matrix and geometry identity must match to reuse a bound; asset invalidation
  and geometry destruction clear the snapshots. Skin bounds always evaluate
  the current joint palette. Camera frustum checks and LOD selection still run
  every frame. This also removes a redundant geometry-map lookup per draw.
- Frame statistics and the profiler panel expose asset synchronization,
  geometry preparation, visibility, material admission, draw ordering,
  palette/uniform preparation, shadow/main recording and memory accounting.
  Batch time is nested inside palette/uniform time; descriptor refresh is
  nested inside main recording. These nested values must not be double-counted.

Texture policy, formats, upload limits, eight-frame stability, retirement
rules and memory budgets are unchanged. The new CPU metadata consists of LOD
error arrays, rigid-bound snapshots and a model pointer list; no extra GPU mip allocations are retained.

## Final bounded measurements

Four final runs in before/after/after/before order, after builds and tests had
finished. Each child profiling process used Windows AboveNormal priority;
thread affinity, scene, camera path, streaming, instancing and FIFO were identical.
The table averages two 128-frame runs per implementation (256 frames each).

| Mean CPU work | Before | Final |
| --- | ---: | ---: |
| Visibility / LOD | 0.846 ms | 0.600 ms |
| Draw-list preparation | 0.835 ms | 0.400 ms |
| Memory accounting, outside preparation | 1.380 ms | 0.180 ms |
| All renderer preparation | 2.649 ms | 2.224 ms |
| Command recording | 1.603 ms | 1.909 ms |
| Previous metric: preparation + recording | **4.252 ms** | **4.132 ms** |
| CPU work: preparation + recording + accounting | **5.632 ms** | **4.313 ms** |
| GPU texture allocation | **5,524,480 B** | **5,524,480 B** |

The expanded CPU-work metric fell **1.319 ms/frame (23.4%)**. The previous
prepare-plus-record metric fell only **0.120 ms (2.8%)**. Command recording was
higher in these final samples; this work does not establish a recording gain.
The earlier normal-priority comparisons were noisy, including an intermediate
optimized run slower overall than its adjacent baseline. All final runs are
included above, without selecting the fastest samples.

Per-run CPU-work means were before 5.746/5.518 ms and final 4.284/4.341 ms.
The final renderer reused 6,154 rigid bounds and evaluated 474 bounds per frame
on average. Main/shadow draw means matched at 1,878.055 / 9,638.85; mean pending
draws matched at 0.00390625. All four runs ended at the same texture bytes and
50 resident models. FIFO wall time stayed around 16.6 ms; no FPS gain is claimed.
Command recording (1.91 ms) is now the largest measured individual CPU stage.

## Focused verification

Commands from the engine directory:

```powershell
cmake --build build --config Release --target theone_draw_order theone_streaming_memory theone_render_quality_gpu theone_runtime_features --parallel 8
ctest --test-dir build -C Release -R '^engine_(draw_order|streaming_memory|lod_preparation|texture_residency)$' --output-on-failure
```

The draw-order check covers opaque order, transparent ordering/ties, pending
filtering, empty lists and draw payload preservation. Streaming-memory checks
cover unchanged counters, added/evicted models, image capacity edits, asset
replacement, upload filtering and cleanup. The LOD-only mode checks far/near
selection, rigid-bound reuse, movement offscreen and back with image parity,
rebuilding after invalidation and continued animated-bound evaluation. Residency-only checks retain
image parity and memory savings through distance/pressure transitions.
All four focused checks passed. After adding rigid-bound reuse, only the three
affected GPU checks were repeated. The invalidation assertion was adjusted to
count evaluations across upload-settling frames, rather than requiring a
rebuild on the final already-settled frame; its focused rerun passed. GPU checks
used Khronos synchronization validation. Residency fixture allocation stayed
48,640 B coarse, 11,189,760 B near, 7,680 B distant and 2,560 B under pressure.
No full engine or game test suites ran.

## Reproduce the bounded comparison

From `Games 2`:

```powershell
cmake --build build --config Release --target runtime_profile --parallel 8
./build/Release/runtime_renderer_before.exe ./games/robots/build/robots-village.one
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one
```

`runtime_renderer_before.exe` is the saved, instrumented pre-optimization
binary from this task. It has the same new phase counters as the final binary.
Renderer CPU work is prepare + record + memory accounting; scene/physics,
fence/acquire waits, submit/present and frame-slot maintenance are outside this
metric. Prepare + record is also retained for comparison with the previous task.
FIFO wall frame time is not a CPU-throughput or FPS benchmark.

Evidence: engine `build/renderer-focused-tests.log`,
`build/renderer-bounds-tests.log`, `build/renderer-bounds-focused-retest.log`,
`build/renderer-final-build.log` and `build/renderer-bounds-build.log`;
consumer `build/renderer-final-0-before.log`, `renderer-final-1-after.log`,
`renderer-final-2-after.log`, `renderer-final-3-before.log` (all in `build/`).
Initial and intermediate samples remain in `renderer-before.log`,
`renderer-{before,after}-paired.log` and `renderer-{before,after}-repeat.log`.
