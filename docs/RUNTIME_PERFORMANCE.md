# Gameplay profiling and incremental runtime work

> The [shadow submission follow-up](SHADOW_SUBMISSION_PERFORMANCE.md) covers batching, owner caching, spatial regions, cascade LOD and uncapped profiling.

> The [renderer CPU follow-up](RENDERER_CPU_PERFORMANCE.md) profiles the remaining full-frame work and optimizes geometry accounting, draw ordering and LOD preparation.

> The subsequent [texture residency CPU follow-up](TEXTURE_RESIDENCY_PERFORMANCE.md) records the reduced demand/material cost and current descriptor lifetime implementation.

Measured on 2026-11-01, Windows x64 Release, i7-12700F / RTX 3080.

## Method and decisions

The consumer `Games 2/tests/runtime_profile.cpp` loads the actual Robots village,
uses its game module, steps physics, moves/jumps the player, selects Rodney's
animations, and follows with the camera for 128 deterministic frames at 640x480.
It is a bounded gameplay workload, not a complete playthrough. Validation is off
for timing. FIFO presentation remains enabled: fence/presentation waits and loop
wall time are reported separately and do not establish a throughput/FPS gain.

The initial profile measured mean renderer preparation 1.993 ms, recording
1.681 ms, game update 0.041 ms, physics 0.040 ms, scene/animation preparation
0.174 ms, and GPU span 1.656 ms. Cold renderer preparation reached 23.140 ms.
This directed the work toward preparation and draw submission, not simulation.

An intermediate version reduced calls but raised renderer preparation to
5.988 ms. Its per-pass maps and small allocations were replaced with a single
grouping pass plus flat arrays; first-draw order is retained. Packed mesh
segments also share budgeted staging chunks instead of allocating one staging
buffer/command per small primitive.

Further counters caught newly visible textures waiting behind detail changes.
Initial coarse admission now runs first, and resident image aliases consume no
upload admission budget. Intermediate draw-count reductions with missing draws
are excluded from the final comparison. The final profile warms at the gameplay
camera and reports pending draws explicitly.

## Results and remaining cost

The final comparison below uses the same new binary, scene, camera path and
viewport. Only instancing and distance residency are toggled. The profile's main
thread is pinned to logical CPU 0 for both runs to reduce migration noise; worker
threads remain normally scheduled. Earlier historical samples were unpinned, so
absolute times across those samples are not a controlled speedup measurement.

| Metric | New binary, full textures / instancing off | New defaults |
| --- | ---: | ---: |
| Renderer preparation, mean | 2.460 ms | 2.869 ms |
| Command recording, mean | 1.580 ms | 1.662 ms |
| Combined preparation + recording | 4.039 ms | 4.531 ms |
| GPU span, mean | 3.567 ms | 1.867 ms |
| GPU span, p99 | 6.315 ms | 6.539 ms |
| Loop wall time, mean | 16.692 ms | 16.686 ms |
| GPU texture allocations | 16,002,048 B | 5,524,480 B |
| Pending draws during measured gameplay | 0 | 0 |
| Mean main / shadow calls | 1,878 / 9,639 | 1,878 / 9,639 |

Distance residency saves **65.5%** of texture storage against the new full-texture
reference, and **80.8%** against the original 28,760,064-byte baseline. The added
features cost about **0.49 ms of CPU preparation/recording** in this paired sample.
This is a memory/iteration improvement, not an established steady-frame CPU or FPS
speedup. GPU means varied markedly across runs, and p99 did not improve in this
pair; no reliable GPU throughput gain is claimed. Robots has too little repetition
to meet the batching threshold, whereas the repeated-object fixture benefits.

Historical cold preparation p95 was 20.985 ms; final pinned p95 was 5.907 ms.
The final maximum still reached 22.939 ms. Background payload preparation and
coarse-first admission reduce typical cold work, but do not eliminate individual
Vulkan allocation/descriptor stalls. The warm-up now uses the gameplay camera
and visible material admission, so these cold phases do not perform identical
work and are not an end-to-end load-time benchmark.

The final Robots sample imported in 992.556 ms and built initial collision in
130.864 ms. A subsequent one-body pose edit captured in 0.104 ms, waited 1.881 ms
for worker preparation/publication (including polling), and applied in **0.185 ms**.
It moved one body, replaced none, and left 18 unchanged. This is an edit measurement,
not the cost of replacing a complete scene.

## Responsibilities and APIs

* `geometry/prepare.cpp` produces immutable packed vertex/index payloads alongside
  indexed LODs. `geometry/image_prepare.cpp` hashes/interns image content and
  generates both sRGB and linear mip chains. Imported data is prepared during
  background loading. `vulkan/asset_preparation.cpp` uses one `TaskQueue` worker
  for unprepared procedural assets and revised mutable images. A mutable image
  still needs one owned pixel snapshot; Vulkan allocations, descriptor writes,
  palette upload and budgeted staging copies remain on the rendering thread.
* `vulkan/instance_batches.cpp` groups matching static opaque geometry/material/
  image/LOD draws and uploads contiguous instance transforms. Visibility is
  evaluated independently per instance for the camera and each shadow cascade.
  Selected instances are separated; blended order and skinned palettes remain
  individual. Group plans are cached by draw topology. Batching is used when at
  least one eighth of candidate calls can merge; the measured Robots scene has
  sparse repetition and keeps ordinary calls. `Renderer::setInstancing(false)`
  is the reference path. This threshold is a conservative heuristic, not a
  universal GPU cost model.
* `physics/body_shape.cpp` cooks meshes in local space and uses body poses and
  scaled wrappers. `physics/edit_prepare.cpp` compares a captured baseline and
  prepares changed colliders on the worker. `physics/edit_apply.cpp` publishes
  removals/additions/replacements/moves to the existing Jolt world. Unaffected
  bodies and the player retain identity and live state. `PhysicsEditJob`,
  `Physics::captureEdit` and `applyEdit` are used by editor collision changes;
  full-scene loading still builds an independent physics world.
* `vulkan/texture_residency.cpp` derives mip demand from camera visibility and
  projected object size. Initial allocations start at a maximum dimension of
  64 texels; stable nearby demand promotes detail. Distant/off-screen demand
  and memory pressure allocate smaller mip tails. The old texture remains
  usable until its replacement upload is ready; descriptors and old allocations
  retire behind submission fences. Equal prepared pixels share allocations at
  the same format/mip level. `setTextureStreaming` and `setTextureMemoryBudget`
  control this independently from whole-asset eviction.
* The profiler exposes instance counts, detail transitions, pressure bias,
  batching time and residency time alongside existing CPU/GPU stages.

## Limits

Prepared packed geometry and mip chains use additional CPU memory; counters
include these payloads and deduplicate shared mip storage. This is mip residency,
not sparse virtual texturing or an on-disk streaming format. Full decoded source
pixels/chains remain in CPU memory. Projected object bounds are a heuristic,
not per-triangle UV-density analysis. Eight stable demand frames reduce churn.
Texture budgets are soft: driver allocation granularity and simultaneous old/new
allocations can temporarily exceed them. GPU allocation calls are not preemptible.

Collision requests retain immutable model snapshots; in-place geometry mutation
requires a replacement model/revision. Physics bodies must be scene roots.
Stale or foreign edit plans are rejected. Replacement bodies are allocated before
old bodies are removed, so edits near Jolt's capacity need spare slots and fail
without changing live bodies if allocation fails. The host pauses simulation
while an edit is pending; this change does not introduce concurrent access to a
live Jolt world. Worker cancellation/retirement retains the existing latest-request
publication rules.

## Focused verification

* `engine_physics_editing`: one moved body without recooking/replacement; unaffected
  dynamic identity, pose and velocity; live visual changes; add/remove/mass
  replacement; stale/invalid rejection; translated/rotated/scaled collision parity
  against a full rebuild; existing cache and supersession checks.
* `engine_runtime_features`: instanced/reference capture equality, selection,
  cascade culling and blended ordering; worker geometry/image preparation;
  coarse-first promotion, full-detail capture equality, distance/pressure
  demotion, and invalidation while a replacement is pending.
* `engine_streaming_memory`: chunk budget and capture parity, shared image revision
  isolation, eviction and in-flight invalidation across the changed upload queue.
* `engine_render_quality_gpu`: the changed draw/palette/upload path retains skinning,
  mip, LOD and cascade behavior. GPU checks enable synchronization validation.

The repeated-object fixture reduced 64 main draws to 2 and preserved image output.
Its three shadow cascades reduced 192 calls to 6.
The residency fixture measured 48,640 bytes initially, 11,189,760 bytes at full
nearby detail, 7,680 bytes at distance and 2,560 bytes under an intentionally
unattainable one-byte budget. These are fixture results, not Robots figures.

Only the named focused checks and bounded profile are used. Consumers are rebuilt
for integration; no full game/engine test suite is run.

## Reproduction and evidence

From the engine workspace:

```powershell
cmake --build build --config Release --target theone_physics_editing theone_runtime_features theone_streaming_memory theone_render_quality_gpu --parallel 8
ctest --test-dir build -C Release -R '^engine_(physics_editing|runtime_features|streaming_memory|render_quality_gpu)$' --output-on-failure
```

From the consumer workspace:

```powershell
cmake --build build --config Release --target runtime_profile robots sandbox planets --parallel 8
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --no-instancing --no-streaming
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one
```

Engine evidence: `build/runtime-focused-tests.log` (physics plus initial GPU
checks), `build/runtime-final-tests.log` (final visibility/batching/upload checks),
`build/runtime-alias-tests.log` (latest shared residency check), and build logs.
Consumer evidence: `build/runtime-before.log`, `build/runtime-pinned-reference.log`,
`build/runtime-pinned-after.log`, and `build/runtime-alias-build.log`.
The harness fixes only its own main-thread affinity; normal engine/game scheduling
is unchanged. The sample measures a moving view, not manual play or all game scenes.

