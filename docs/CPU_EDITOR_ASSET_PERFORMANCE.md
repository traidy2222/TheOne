# 1080p renderer, editor picking, and prepared assets

2026-11-02. Windows Release, RTX 3080 / i7-12700F.

## Scope and profiling

This iteration implements the five follow-ups to SHADOW_SUBMISSION_PERFORMANCE.md.
The consumer runtime profiler now accepts `--1080p`, `--frames=N`, and
`--pick-profile`. The longer workload runs 1,800 deterministic fixed steps
(30 seconds of simulated gameplay) at 1920x1080, uncapped, with its main thread
on logical CPU 0. Real elapsed time is shorter than simulated time. Picking is
measured separately after rendering with a 25-ray viewport grid. The updated
profiler checks every accelerated result against the old full-bake intersection.
It does not measure all ImGui editor interactions or visible-window responsiveness.

The initial profile measured 3.866 ms renderer CPU, 0.885 ms shadow planning,
0.983 ms visibility and 21.117 ms full-bake picking. Texture allocation was
8,003,072 bytes at 1080p, which is the appropriate memory comparison for this
iteration rather than the previous 640x480 texture demand.

## Responsibilities

- `src/render/vulkan/shadow_submission_state.hpp` stores per-draw immutable
  mesh/LOD command templates. Geometry lookup, rebasing and group lookup occur
  when the geometry slot changes. Visibility, LOD, palette and instance count
  remain current every frame. Unchanged entity/linear-transform/cascade-density
  decisions also bypass repeated LOD-history lookup. Existing asset invalidation/eviction clears all
  templates before GPU mesh references can become stale. Command buffers retain
  the existing per-frame fence ownership.
- `src/render/spatial_regions.*` maps draws to leaves, detects changed bounds,
  and refits only dirty leaves and ancestors in reverse allocation order.
  Unchanged static trees refit zero nodes. Queries still run for the current
  camera/cascades. Fully contained bounded regions are accepted in one step,
  avoiding redundant leaf plane tests. Reordering, movement and unknown bounds remain conservative.
  Topology still rebuilds on size changes; extensive rearrangement can reduce
  query quality even though refitting preserves correctness.
- `include/theone/scene/picking.hpp` and `src/scene/picking*` own exact geometry
  picking. Current prepared poses supply broad-phase bounds. Rigid candidates
  use cached local triangle trees; animated candidates skin positions only.
  Ray parameters retain world distance under nonuniform scaling. Cached trees
  use weak model ownership and model revisions; replacing a model cannot retain
  stale geometry. Picking remains geometric, including transparent triangles,
  matching the previous editor selection semantics.
- `src/assets/cache/` owns disk identification, binary model/payload archives,
  checksums, bounded reads and atomic publication. `gltf/loader.cpp` coordinates
  lookup before buffer/image/mesh preparation and publishes completed imports.
  Source and all external buffer/image contents contribute to the key, including
  decoded URI paths. Materials restore image pointers by index; model handles
  are newly allocated on every load. CPU preparation payloads are restored;
  GPU texture residency and upload budgets are unchanged. `geometry/image_intern.*`
  is shared by fresh imports and cache restores, preserving prepared-image identity
  and GPU aliases across assets. Base image pixels and packed mesh upload bytes
  are reconstructed from already stored mip/vertex/index data rather than stored
  twice. A word-based noncryptographic fingerprint validates cache contents.
- `src/render/vulkan/shadow_mask.cpp` and the three `*shadow_mask*` shaders handle
  alpha-tested depth writes for rigid, skinned and procedural geometry. Cutoff
  combines resident base-texture alpha, vertex alpha and material alpha. Masked
  geometry uses full detail; opaque indirect groups retain their fast path.
  The alpha descriptor reuses the resident base texture. Off-camera casters admit
  only that texture, with bounded 64-texel demand subject to existing pressure and
  upload budgets; normal, occlusion and emissive textures stay unrequested.
  Consecutive masked draws reuse pipeline, descriptor and material constants.
  Descriptor writes affect only the fence-complete frame slot; invalidation retires
  old bindings with the existing submission-serial mechanism.

## Cache operation

Default directory: `%LOCALAPPDATA%/TheOne/prepared-v1` on Windows; temporary
`theone-prepared-v1` directory where LOCALAPPDATA is unavailable. Set
`THEONE_ASSET_CACHE_DIR` to choose a directory, or set `THEONE_DISABLE_ASSET_CACHE`
to bypass it. One latest cache entry is kept per canonical source path. Cache
files may be removed when the engine is closed; source assets are authoritative.

This is a private, native-layout cache, not a portable distribution format.
Bump the archive version when model serialization, preparation algorithms or
relevant native layouts change. Header layout checks and checksums reject
incompatible/damaged entries; missing or unwritable caches fall back to import.
Cancellation remains checked during preparation/archive work. Cache publication
uses a temporary sibling and atomic replacement, and changed dependencies prevent
publication of an in-progress import. First import pays serialization/write cost;
subsequent loads avoid decoding, mesh preparation and mip generation.

The cache does not change the engine's existing decoded/prepared CPU payload
retention policy. Picking trees, shadow templates and spatial snapshots add CPU
metadata, not duplicate GPU mesh or texture payloads. Disk entries for source
paths no longer used are not automatically garbage-collected.

## Focused verification

Only five selected CTests ran, all passing:

- `engine_submission_cpu`: visibility parity, static zero-refit and bounded dirty
  refits, reordering/resizing and invalid bounds.
- `engine_picking`: full-bake parity under animation, nonuniform scale, movement,
  asset replacement, reorder, visibility and an empty scene.
- `engine_prepared_cache`: prepared payload and rig round trips, fresh runtime
  identities, external buffer/image and source invalidation, corruption repair,
  cancellation and unwritable cache fallback.
- `engine_shadow_submission`: direct/indirect and spatial parity, both frame slots,
  geometry invalidation, plus transparent/opaque texel, base factor and vertex
  alpha behavior. Includes procedural/skinned masked paths and a cold off-camera
  caster with an intentionally unused normal map. A partially transparent texture
  must cast holes, differing from both solid and absent shadows, with exact
  direct/batched parity. Vulkan
  synchronization validation enabled.
- `engine_texture_residency`: existing targeted memory/residency behavior under
  Vulkan synchronization validation.

Robots and the runtime profiler were rebuilt. No full engine/game suite ran.
Scene-loading profiling additionally compares source/persistent-cache payload
fingerprints on the actual Robots scene.

## Reproduction

From Games 2, after all builds finish:

```powershell
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --uncapped --1080p --frames=1800 --pick-profile
```

From TheOne:

```powershell
cmake --build build --config Release --target theone_submission_cpu theone_picking theone_prepared_cache theone_shadow_features theone_runtime_features --parallel 8
ctest --test-dir build -C Release -R '^engine_(submission_cpu|picking|prepared_cache|shadow_submission|texture_residency)$' --output-on-failure
./build/Release/theone_scene_load_profile.exe '../Games 2/games/robots/build/robots-village.one'
```

Evidence: `build/next-focused-tests.log`, `next-focused-build.log`,
`next-consumer-build.log`, `next-load-source.log`, `next-load-cache.log`.
Consumer profiling logs are under Games 2/build. `next-1080-cpu-1.log` overlapped
an unfinished build and is excluded. `next-1080-cpu-2.log` was an intermediate
implementation; all 25 picking results matched, but absolute CPU timings varied
substantially from the initial run. Final paired measurements are reported below.

## Loading results and iteration

Actual Robots scene: 50 unique models, 1,238 image slots, 6,186 primitives,
917,157 source vertices. Source-only loads took 1.251, 1.215 and 1.101 seconds.
The final cache population took 1.980 seconds; subsequent loads took **0.736
and 0.832 seconds** (50/50 cache hits), roughly **34% lower** than the mean
source load. All source/cache runs produced fingerprint 109327585889636832.
These are scene-import times; collision build/upload are separate stages.

The first cache prototype was rejected after warm loads measured 1.23-1.54 s.
Removing redundant payload bytes and replacing byte-serial checksum work made
warm loading faster. Image interning was retained across both loading paths.
Cache population has a one-time cost; this is not a cold-import speedup claim.

The first final-render attempt exposed an off-camera masked caster whose material
had never been prepared. It was excluded from performance results. The dedicated
alpha readiness/binding path and off-camera regression fixture were added before
repeating final measurements. This also prevents loading unused surface textures
just to draw a shadow.

## Final 1080p paired results

Final order: before / after / after / before, each 1,800 frames, AboveNormal
child-process priority, no concurrent builds or tests. The saved baseline is
`Games 2/build/Release/runtime_1080_before.exe`. Values below average both run
means per implementation; CPU is preparation + recording + memory accounting.
It excludes scene/game work, fence/acquire waits and submit/present.

| Metric | Before | Final |
| --- | ---: | ---: |
| Renderer CPU | **3.633 ms** | **3.130 ms** |
| Visibility | 0.920 ms | 0.572 ms |
| Shadow planning (nested in recording) | 0.834 ms | 0.552 ms |
| Shadow recording including planning | 0.891 ms | 0.724 ms |
| Picking, 25 rays | 20.345 ms | 0.852 ms |
| Frame wall time | 6.373 ms | 5.888 ms |
| Completed GPU total | 0.972 ms | 1.014 ms |
| Hierarchy node tests/frame | 3,058.74 | 1,483.89 |
| Main logical draws/frame | 2,230.68 | 2,230.68 |
| Shadow logical draws/frame | 10,921.9 | 10,921.9 |
| Shadow triangles/frame | 552,609 | 552,609 |
| End-of-run GPU texture allocation | 8,003,072 B | 8,241,152 B |

Renderer CPU mean fell **13.9%**, visibility **37.8%**, and shadow planning
**33.8%**. Picking fell **95.8%**; the accelerated rays also matched the baked
reference individually in both final runs (checksum 550). Within those same
processes the baked-reference mean was 21.465 ms versus 0.852 ms accelerated.
All four measured runs had zero pending draws throughout their sampled frames.

Before CPU means were 3.237 / 4.030 ms; final means were 2.977 / 3.283 ms.
P95s were before 4.466 / 7.805 ms and final 4.580 / 5.940 ms. Machine/driver
variation remains significant; these are bounded measurements, not a guaranteed
FPS or tail-latency improvement. GPU time rose about 0.042 ms in the mean while
adding alpha-correct shadow sampling. The earlier intermediate pair did not show
a clear net CPU gain; that result drove contained-region acceptance, LOD-decision
reuse and masked state-binding reuse before this final sequence.

The extra **238,080 B (0.238 MB)** of resident alpha textures supports off-camera
casters. A fifth same-resolution/run-length control with `--no-streaming` used
**17,522,176 B**. Final residency therefore keeps a **53.0% GPU texture saving**
against full texture residency in this workload. Full-detail textures are not
forced for alpha casters. These are end-of-run allocations, not peak-memory
claims. Shadow command-buffer capacity varied with cold upload packing: final
543,360 / 750,280 B, versus baseline 794,640 / 543,960 B.

Final runtime logs: `Games 2/build/next-complete-0-before.log` through
`next-complete-4-full-textures.log`; all matching stderr files are empty.
Final scoped verification logs: `build/next-focused-tests.log`,
`next-cache-tests.log`, `next-offscreen-tests.log`, `next-query-tests.log`, and
`next-cutout-tests.log`. Only affected checks were repeated after later changes.
`next-paired-*` records the intermediate pair before the final query/recording
optimizations; `next-final-1-after.err` records the off-camera readiness failure
that prompted the regression fixture and fix.
