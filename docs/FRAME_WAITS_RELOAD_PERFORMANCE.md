# Frame waits, reload reuse and bounded asset preparation

The subsequent [FIFO pacing investigation](FIFO_PACING_PERFORMANCE.md) traces and
addresses the remaining presentation backlog described below.

Measured locally on 2026-11-02 using the Robots visible gameplay/editor replay.
All visible runs contain 1,440 frames, including editor dragging, undo/redo,
asset reload and scene reload. Defaults remain FIFO presentation with two frame
slots. The uncapped and one-slot settings are diagnostic options only.

## Findings and changes

| Workstream | Implementation and result |
| --- | --- |
| Remaining waits | Correlate the waited submission ID with completed GPU timestamps, submission age, fence state and optional waiting-thread CPU time. The baseline's 43.64 ms fence wait corresponded to 3.97 ms of GPU work and no measurable thread CPU work. Uncapped maximum fence wait was 0.006 ms; one-slot FIFO still reached 46.98 ms. This implicates presentation/scheduling behavior, without identifying an exact driver or compositor cause. |
| Scene reload | Validated source/dependency fingerprints retain unchanged model objects and their resident GPU resources. The visible reload reused all 50 models; geometry/texture transfer after the reload trigger fell from 30,866,966 bytes to zero. Physics collider owners are updated without recooking identical geometry. |
| Upload staging | Fence-complete buffers, persistent mappings and command buffers enter a bounded pool. Average upload cleanup fell from 0.621 ms to 0.001 ms per frame. The replay recorded 3,100 reuses and 56 allocations. |
| Draw/shadow submission | Reuse transparent ordering and rigid-depth calculations where valid; update changed shadow indirect commands and copy their arrays only when a frame slot needs a newer revision. An isolated uncapped comparison reduced idle draw-list time from 0.295 to 0.169 ms and shadow planning from 0.479 to 0.349 ms. |
| Asset preparation | Load unique assets with two workers and budgeted admission, then publish in scene order only after all tasks succeed. Three warm runs averaged 319 ms serially and 199 ms with two workers, approximately 38% faster. |

The FIFO replay did **not** improve total frame time: average wall time was
16.69 ms before and 16.74 ms after; worst frames were 50.71 and 53.15 ms.
Maximum fence waits were 43.64 and 45.50 ms. Whole-renderer timings varied across
phases, so the isolated path improvements are not a claim of an overall FPS gain.
GetThreadTimes accounting is coarse, and these captures do not include calibrated
presentation timestamps or an ETW trace. Those are the next useful evidence for
the remaining FIFO spikes.

The submission comparison used the same new executable with
`THEONE_REFERENCE_SUBMISSION_CACHE=1` versus the default cache, both uncapped.
Idle renderer CPU time was 2.264 versus 1.819 ms; other phases varied. Reference
and cached paths produced identical checksums for all 40 focused shadow captures.

## Memory and ownership

- Available staging is limited to 4 MiB and 64 entries. Only completed uploads
  become reusable; allocation capacity is checked independently of Vulkan memory
  allocation size. Entries unused for over 120 frames are retired through the
  existing paced cleanup. The measured pool peaked at 2,654,210 bytes and ended
  at zero.
- Peak retired GPU bytes fell from 32,696,800 to 2,097,150; both runs finished
  with no retired allocations. The new peak includes staging trim callbacks.
- Texture residency budgets and detail policy remain in place. Peak texture
  bytes were 8,819,200 before and 8,816,640 after. Final texture bytes were
  8,250,880 and 8,347,140: retaining the existing resident set across reload costs
  an additional 96,260 bytes (about 94 KiB), rather than clearing and rebuilding it.
- Asset loading admits an estimated 128 MiB of concurrent working storage, not
  an allocator-enforced process memory cap. Cached archive size informs the
  estimate; uncached imports run alone because compressed size is unreliable.
  Observed peak admission was 108,899,337 bytes with two workers. Peak process
  working set was 270,753,792 bytes serially and 272,351,232 bytes in parallel,
  an increase of about 1.52 MiB.
- Reload reuse requires a nonzero validated source fingerprint and unedited
  images. Image revision changes prevent reuse. Models remain immutable
  snapshots: geometry/material changes must replace the model. Fresh redundant
  CPU models are released through the background retirement path.
- Parallel tasks join before publication; failures and cancellation cannot
  publish a partial world. Entity and diagnostic order remain deterministic.
  Model phase timings accumulate worker time and must not be read as elapsed
  scene time when workers overlap.

## Responsibility boundaries

| Responsibility | Module |
| --- | --- |
| Wait clocks and optional thread accounting | `src/render/vulkan/wait_timing.hpp` |
| Submission IDs, fence timing and GPU timestamp collection | `frame_resources.cpp`, `frame.cpp`, `frame_profile.cpp` |
| Bounded staging ownership and trimming | `src/render/vulkan/staging_pool.*` |
| Incremental transparent ordering | `src/render/draw_order_cache.hpp` |
| Shadow command revisions and per-slot uploads | `src/render/vulkan/shadow_submission.cpp`, `shadow_resources.cpp` |
| Asset scheduling and admission | `src/scene/asset_loading.*` |
| Validated source identity and cache stage timing | `src/assets/gltf/loader.cpp`, prepared-cache reader |
| Prepared collider model ownership | `src/physics/model_owners.cpp` |
| Consumer reload reconciliation | `Games 2/shared/import-host/scene_asset_reuse.*`, `scene_handoff.*` |
| Consumer replay and CSV output | `Games 2/shared/import-host/visible_profile.cpp` |

## Focused verification and reproduction

Production modules, Robots and affected focused targets compiled. Seven checks
passed: scene handoff, parallel loading, draw order, streaming memory, texture
residency, shadow submission and prepared cache. These cover unchanged reloads
without GPU transfers, edited-image replacement, unchanged collider ownership,
serial/parallel archive parity, cancellation/failure, transparent ties/readiness,
fence-safe pool reuse and trimming, and in-flight invalidation. Rendering checks
used Vulkan/synchronization validation; they emitted an unused shader-input
performance warning. No complete engine or game suite was run.

From the engine workspace, with the existing consumer build configured:

```powershell
ctest --test-dir '../Games 2/build' -C Release --output-on-failure -R '^(scene_handoff_focused|engine_parallel_loading|engine_draw_order|engine_streaming_memory|engine_texture_residency|engine_shadow_submission|engine_prepared_cache)$'
```

From `Games 2`, use the existing Robots executable and scene:

```powershell
$env:THEONE_PROFILE_WAITS = '1'
./build/bin/robots/Release/robots.exe --scene ./games/robots/build/robots-village.one --profile-visible ./build/waits-fifo.csv --profile-frames 1440
Remove-Item Env:THEONE_PROFILE_WAITS
```

Add `--profile-uncapped` or `--profile-one-frame` separately for diagnostics.
`THEONE_REFERENCE_SUBMISSION_CACHE=1`, `THEONE_REFERENCE_STAGING_POOL=1` and
`THEONE_REFERENCE_SCENE_RELOAD=1` select the corresponding reference paths.
`THEONE_ASSET_WORKERS=1` selects serial loading; the default is two, with an
explicit override clamped to one through four. Remove overrides after a comparison.

Raw visible captures are in the consumer build directory as
`waits-{fifo,uncapped,one-slot}-before.*`, `waits-fifo-after.*` and
`waits-submission-{reference,cached}.*`. Engine build artifacts include
`waits-summary.json`, `waits-load-{serial,parallel}.log`,
`waits-focused-checks.log`, `waits-focused-details.log` and
`waits-shadow-reference.log`. Warm loading logs each contain three runs, 50 cache
hits per run, and the same scene fingerprint `109327585889636832`.
