# GPU geometry performance evidence

Measured on 2026-11-01, Windows x64 Release, NVIDIA GeForce RTX 3080. These are CPU preparation and transfer measurements; they do not establish an FPS improvement or planet-scale performance.

## Implementation

- Immutable indexed vertex/index data resides in device-local Vulkan buffers, shared by all instances of a primitive. Uploads for newly encountered primitives use one batched transfer submission.
- Vertex shaders apply mesh transforms and four-influence skeletal skinning in the color and shadow passes. Inverse-transpose normal palettes preserve nonuniform-scale behavior. CPU clip sampling, crossfades and hierarchy resolution produce per-node/joint matrices without traversing geometry.
- Palette storage is persistently mapped and resized only after the previous frame fence. Bounds are checked against the device storage-buffer limit.
- CPU hierarchy preparation uses an entity-ID map rather than repeated linear ancestor searches. Transparent draw centroids are computed once per frame; skinned centroids use cached weighted joint sums instead of rescanning mesh vertices during sorting.
- Procedural vertices use an explicit revision cache; unchanged terrain requires zero vertex uploads. Particles retain the small dynamic CPU vertex path. Editor picking bakes CPU triangles on demand.
- Mesh caches retain model ownership, survive resize, and are released with `clearAssets()` or renderer teardown. Geometry mutation requires explicit invalidation.

## Measurements

The original Robots Release executable was measured before rebuilding it with the new engine. Its existing 120-frame validation smoke test reported **11.357 ms** average CPU scene/effect baking. The GPU-path run reported **0.620395 ms** average CPU pose/effect preparation: approximately **18.3x faster for that CPU stage**. After removing redundant pipeline/descriptor/buffer bindings, the final run measured **0.562687 ms** (20.18x versus the baseline); another run measured 0.502894 ms. Sandbox was already small: 0.05499 ms before versus 0.06107 ms after; bookkeeping dominates that case.

The engine's 256-instance animated fixture contains 55,296 expanded vertices. One Release run measured **2.8494 ms CPU baking versus 0.289941 ms GPU pose preparation** (9.83x). Per-frame upload fell from **2,654,208 bytes of CPU-baked vertices to 110,592 bytes of palettes** (24x less); repeated mesh upload was zero. These synthetic timings vary with system load and are printed, not enforced as brittle test thresholds.

GPU readback comparisons matched the CPU reference exactly in most cases. The largest observed mean difference in the initial five-case run was 0.00001194 intensity levels per RGB channel (8-bit channels); no channel differed by more than 8. The test allows a small rasterization tolerance across hardware. It includes animated and static content, normal transforms, selection, shadows and particles.

Planet validation reused resident terrain for 17 of its 20 frames. The asynchronous edit/rebuild test also verified zero vertex uploads on unchanged revisions. Terrain generation and cell collision remain CPU operations.

## Validation and limits

Engine content/import tests and GPU image-parity/validation tests passed. Consumer checks passed for Sandbox/Robots validation (including resize, minimize/restore, scene and game reload), external glTF compatibility, game/engine separation, game integration, planet CPU state, distant terrain, asynchronous rebuilds and planet Vulkan validation.

A pre-existing planet header/implementation signature mismatch was aligned to unblock the rebuild. The planet smoke harness now excludes live input/UI changes so automated camera and picking actions are deterministic; it also asserts that unchanged mesh revisions are not uploaded again.

Repeated pipeline, geometry-descriptor and vertex-buffer bindings are skipped when their state is unchanged. First-use indexing and texture loading remain synchronous. Reload-heavy validation smoke wall times were not faster in the initial comparison (Robots roughly 19 s before and 33 s after, with other build activity during the latter run; the final isolated comparison was 27.74 s after versus 19.16 s before); CPU preparation results must not be interpreted as whole-application speedups. At the time of these initial measurements, GPU frame-time queries, frustum/occlusion culling, automatic cache eviction, GPU animation-graph evaluation, GPU terrain generation and multiple frames in flight were not implemented. See the later frame-development follow-up below for the current state.

Commands:

```powershell
.\run.ps1 -Configuration Release -Target theone_gpu_tests -TestRegex '^engine_gpu_validation$'
ctest --test-dir build -C Release -R engine_gpu -V
```

Raw local logs are under `build/gpu-*.log` and `build/Testing/Temporary/LastTest.log`; consumer evidence is under the separate games project's `build/gpu-*.log`.

## Follow-up: loading regression fixed

The reload-heavy slowdown above was subsequently isolated to thousands of small GPU allocations and per-texture synchronous submissions. Shared mesh allocations and batched texture transfers reduced the isolated Robots reload path from 5.65 s to 2.39 s. See [loading profile and verification](LOADING_PERFORMANCE.md). Those new measurements replace the earlier loading limitation; overall gameplay FPS is still not claimed.


## Follow-up: frame development features

GPU timestamp/CPU stage profiling, static camera/light frustum culling, cached scene transforms/palettes, selective model/image reload with deferred GPU retirement and two frames in flight are now implemented. The fixed Robots view reduced main draw submissions from 6,628 to 1,318. See [frame measurements and focused verification](FRAME_PERFORMANCE.md) for the one/two-frame comparison, limitations and module ownership. No FPS gain is claimed from scheduling alone; FIFO presentation dominated the measured loop.
