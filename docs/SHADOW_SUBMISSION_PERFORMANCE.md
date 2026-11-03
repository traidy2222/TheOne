# Shadow submission and remaining renderer work

2026-11-02. Windows Release, RTX 3080 / i7-12700F. This follows the
[renderer CPU work](RENDERER_CPU_PERFORMANCE.md). All five requested changes
are implemented, with texture residency policy unchanged.

## Profile first

The instrumented baseline preserved the previous renderer implementation and
added uncapped presentation before saving `runtime_shadow_before.exe` in the
consumer build directory. The initial run showed 9,639 shadow draw commands,
9,225 vertex/index rebinding pairs, 16,938 opaque candidates and 500,999 shadow
triangles per frame. This made submission batching the first target.

Shadow culling/issue sample counters time one in 64 operations and scale the
result. They include clock overhead and are diagnostic estimates, not additive
stage totals. Exact outer shadow-record and renderer CPU timings are used for
comparisons. The final batched path reports an exact planning timer nested in
shadow recording instead of per-draw timing.

## Modular implementation

1. **Shadow submission**: `vulkan/shadow_submission.cpp` and its state header
   build compatible groups per cascade. Each mesh keeps its original packed
   allocation. `indexed_binding.hpp` rebases index/vertex addresses so meshes
   with a common buffer and vertex-stride remainder share bindings. Indirect
   multi-draw is used only when both required device features are supported;
   grouped direct commands provide the fallback. `setShadowBatching(false)`
   preserves the independent ordinary-draw reference path.
2. **Asset synchronization**: `asset_owners.*` caches the owner pointer sequence
   and unique model list. Unchanged draws require a linear pointer comparison,
   with version/last-use work once per unique model. A resident owner registry
   also keeps image/material pointers alive when texture aliases precede mesh
   admission. Eviction scans owners rather than every mesh. Public same-sized
   scene replacement, reload, and removal remain supported.
3. **Spatial culling**: `render/spatial_regions.*` creates a median-split
   hierarchy with small leaves. Current world bounds refit it every frame;
   camera and cascade queries reject whole regions and preserve original draw
   indices/order. Invalid/unknown bounds remain visible. Bounds and skin poses
   still update before culling. Topology rebuilds when draw count changes;
   arbitrary movement/reordering remains correct through refitting, though
   extensive rearrangement can reduce hierarchy efficiency.
4. **Shadow LOD**: `vulkan/shadow_lod.cpp` selects from existing mesh LODs using
   each cascade's texel scale and a conservative transform scale. The 0.5-texel
   target uses the existing 0.8/1.2 hysteresis (up to 0.6 texel retention).
   Cascade density and unchanged mesh/transform decisions are cached.
   Skinned, masked, and merged-instance draws retain full detail. Main-view LOD,
   texture demand bounds and texture budgets are unchanged.
5. **Uncapped profiling**: `vulkan/presentation.hpp` chooses immediate, then
   mailbox, then FIFO fallback. FIFO remains the application default.
   `Renderer::setVsync(false)` requests uncapped presentation; `vsyncEnabled()`
   and the startup log report the actual selected mode. The profile harness
   supports `--uncapped` and individual feature-disable switches. Its unrelated
   collision-edit probe now requires `--collision-edit`.

Shadow commands occupy persistently mapped coherent buffers owned by each
frame slot. CPU writes/growth occur only after that slot's fence. Indirect
first-instance values preserve geometry palette indices, including skinning.
Old asset buffers retain the existing submission-serial retirement rules.
Invalidation clears cached submission groups before handles can be reused;
clear/shutdown release command buffers. Command-buffer capacity is reported
separately as `AssetMemoryStats::shadowCommandBytes`.

## Final results

Final before/after/after/before sequence, two 128-frame runs per implementation,
with AboveNormal child-process priority and no concurrent builds/tests:

| Mean metric | Before | Final |
| --- | ---: | ---: |
| Renderer CPU work | **4.036 ms** | **2.972 ms** |
| Preparation + recording | 3.858 ms | 2.812 ms |
| Shadow recording, including planning | 1.415 ms | 0.667 ms |
| Shadow planning, nested in recording | n/a | 0.621 ms |
| Asset synchronization | 0.455 ms | 0.035 ms |
| Shadow API draw commands | 9,638.85 | 238.00 |
| Shadow geometry rebinding pairs | 9,225.27 | 238.00 |
| Logical shadow draws | 9,638.85 | 9,638.85 |
| Main draws | 1,878.06 | 1,878.06 |
| Shadow triangles | 500,999 | 484,978 |
| Texture allocation | **5,524,480 B** | **5,524,480 B** |
| Shadow command-buffer capacity, both slots | 0 | **557,520 B** |

Measured renderer CPU work fell **26.4% (1.064 ms/frame)** and shadow recording
fell **52.9%**. Shadow API command count fell **97.5%**, with logical draw counts
preserved. Cascade LOD removed 16,021 triangles/frame (3.2%) on Robots; its gain
is deliberately conservative. Asset synchronization checks 50 unique owners.
Spatial queries skipped about 10,621 individual bound tests per frame across
the camera/cascades. Pending draws were zero in all four final samples.

CPU-work run means were before 3.131/4.940 ms and final 2.708/3.236 ms. There is
substantial run-to-run timing variation; the table includes every final run.
An earlier paired sequence also showed about 27% CPU reduction. Command-group
counts and buffer capacity can vary with cold upload allocation grouping.
The final renderer retains the texture saving and adds approximately **0.56 MB**
of separately reported command-buffer capacity; it does not retain additional
texture detail or duplicate geometry payloads.

Visibility timing now includes camera **and three cascade** queries; previously
cascade culling was charged to shadow recording. Its 0.510 -> 0.802 ms increase
is therefore not a comparable isolated regression. The intermediate feature
disable runs measured 0.851 ms visibility without regions versus roughly
0.743 ms with regions. Disabling shadow batches raised CPU work to 3.623 ms
versus 2.854 ms in that intermediate paired baseline. These single ablations
are diagnostic, not independently controlled speedup claims.

The first LOD-disable run exposed extra selection work for a modest triangle
saving. Mesh lookups, cascade factors and unchanged LOD decisions were then
cached without loosening the error target. Later samples remained noisy, so
no independent CPU win is claimed for that cache alone.

Uncapped wall time averaged 6.667 -> 4.607 ms; completed GPU timestamps averaged
1.210 -> 0.582 ms. These are bounded 640x480 workload observations, not a
full-resolution gameplay FPS guarantee. Planning (0.621 ms) is now most of the
remaining shadow-record CPU work; Vulkan command emission itself is much smaller.

## Focused checks

Only these checks were selected:

- `engine_submission_cpu`: hierarchy versus brute-force visibility after
  movement/reordering/resizing, unknown bounds, rebased byte addresses including
  overflow fallback, presentation fallback, and same-sized owner changes.
- `engine_shadow_submission`: exact captured-image parity for ordinary,
  grouped-direct and indirect shadow commands; both frame slots; spatial and
  instanced paths; non-vacuous shadow LOD; moved/scaled geometry; invalidation,
  clear/reload, and switching back to FIFO. The wavy-grid fixture reduced
  shadow triangles from 57,350 to 14,342 with identical captures in that view.
- `engine_streaming_memory`: upload budgets, texture sharing/revisions, model
  pressure eviction and lifetime/cleanup after owner caching changes.
- `engine_runtime_features`: the affected instanced-cascade visibility path and
  retained texture-residency memory/image behavior.

GPU checks use Khronos synchronization validation. The initial new shadow test
attempted capture during initial uploads; its fixture was corrected to settle
before readback, then passed. Later LOD-cache changes rerun only the shadow
check; the owner-lifetime adjustment reran only streaming-memory. No full
engine/game suites ran. Robots and the profile executable were rebuilt.

```powershell
cmake --build build --config Release --target theone_submission_cpu theone_shadow_features theone_streaming_memory theone_runtime_features --parallel 8
ctest --test-dir build -C Release -R '^engine_(submission_cpu|shadow_submission|streaming_memory|runtime_features)$' --output-on-failure
```

## Reproduction

From `Games 2`, after builds have finished:

```powershell
cmake --build build --config Release --target runtime_profile --parallel 8
./build/Release/runtime_shadow_before.exe ./games/robots/build/robots-village.one --uncapped
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --uncapped
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --uncapped --no-spatial-culling
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --uncapped --no-shadow-lod
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --uncapped --no-shadow-batching
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --uncapped --direct-shadow-batches
```

The workload is the same 128-frame Robots player/camera/animation sequence,
640x480, main thread on logical CPU 0. Paired runs use AboveNormal priority for
the child processes only. The actual mode was immediate (Vulkan enum 0).
Uncapped presentation can still incur driver/compositor waits; it does not
remove GPU work or establish full-resolution gameplay FPS. Renderer CPU work
means preparation + recording + memory accounting, excluding scene/game work,
frame-slot maintenance, fence/acquire waits and submit/present.

## Evidence

Engine logs: `build/shadow-focused-tests.log`, `shadow-submission-retest.log`,
`shadow-owner-tests.log`, `shadow-lod-cache-tests.log`, and
`shadow-cached-consumer-build.log` (all under `build/`). Final timing files in
`Games 2/build/`: `shadow-cached-0-before.log`, `shadow-cached-1-after.log`,
`shadow-cached-2-after.log`, `shadow-cached-3-before.log`. Intermediate paired
runs and feature-disable measurements are `shadow-final-0-before.log` through
`shadow-final-6-no-batches.log`; the post-cache LOD-disable sample is
`shadow-cached-4-no-lod.log`. Initial profiling is `shadow-before-uncapped.log`.

Follow-up: [longer 1080p profiles, picking, prepared assets and alpha shadows](CPU_EDITOR_ASSET_PERFORMANCE.md).
