# Asset reload and renderer cache work

Follow-up measurements and fixes are recorded in [cache correction](CACHE_CORRECTION_PERFORMANCE.md).

## Scope and ownership

This pass starts with an instrumented 1,440-frame Robots replay at 1920x1080,
FIFO, on the RTX 3080. The replay includes moving gameplay, editor idle, drag,
undo/redo, one asset reload and one unchanged scene reload. Existing concurrent
editor/history work was preserved. Only the changed renderer paths were checked.

Five changes are implemented:

1. `asset_cache_invalidation.*` clears bounds, resolved geometry, draw-depth and
   shadow templates belonging to the removed model. Unrelated entries and
   texture alias representatives survive. Entries store model identity, so
   invalidation does not dereference obsolete primitive pointers. Shared shadow
   binding templates are indexed by geometry, separate from per-instance LOD
   history. Mesh and binding hash nodes are extracted and destroyed by the
   existing release worker after GPU retirement; they are not copied into a
   relocatable vector of mesh values.
2. `asset_memory_snapshot.*` caches unique texture allocations and immutable
   prepared image payload sizes by mutation revision. Residency pressure uses
   resident allocations, while accounting also includes replacements. Public
   mutable RGBA capacities remain observed every frame. `asset_memory_geometry`
   remains responsible for immutable mesh accounting.
3. `shadow_plan_cache.*` and `shadow_plan_key.hpp` retain command plans only while
   their exact dependencies agree. Visibility, material mask/blend state,
   geometry, palette, instances, rigid transform and LOD density are checked.
   Procedural and masked commands retain indices into the current draw list,
   not old Draw pointers. A failed reuse attempt backs off for seven frames;
   changing scenes rebuild normally without recording disposable snapshots
   every frame. Stable scenes subsequently resume reuse. GPU command revision
   and frame-fence rules remain intact.
4. `draw_scratch.hpp` reuses the frame draw buffer, with a 2 MiB retained-capacity
   limit; draw ordering retains at most 1 MiB of blend scratch. Owners are
   released at frame end. Larger working sets are supported but their excess
   capacity is released, so those workloads can still allocate each frame.
   Ordinary Draw copies preserve ownership and future fields automatically.
5. `backend.cpp` owns construction/destruction of opaque memory and shadow
   state. Presentation pacing keeps its existing value API while hiding its
   completion worker behind an opaque pointer. Public memory-stat types live
   in `asset_memory_stats.hpp`, independent of the Renderer API. MSVC dependency
   records show the completion worker header reaches two renderer translation
   units, memory snapshot state five, and shadow submission state six.

GPU texture residency limits, alias sharing, upload budgets and the 4 MiB
staging-pool cap are retained. The new CPU caches and bounded scratch are a
CPU-memory tradeoff; this pass does not claim lower total process memory.

## Measurement evidence

The initial baseline is `build/reload-invalidation-before.csv` with its metadata,
stdout and stderr. Each refinement has a distinct capture; none overwrote that
baseline. Intermediate captures showed regressions, leading to shared geometry
bindings, deferred node destruction and adaptive shadow-plan recording. The
first `after` capture has stale-build provenance and is not performance proof.

Final capture: `build/reload-invalidation-verified.csv`; comparison:
`build/reload-invalidation-verified-comparison.json`. All 1,440 frames completed,
source and executable identities stayed stable, no source was newer than the
binary, and the comparison reported zero provenance warnings. The final working
source identity was also checked against the capture.

Whole-capture aggregates mix focus states and are descriptive only:

| Metric | Baseline | Final |
| --- | ---: | ---: |
| Renderer CPU mean | 2.7258 ms | 2.4037 ms |
| Memory accounting mean | 0.2300 ms | 0.0647 ms |
| Draw copies mean | 0.1483 ms | 0.1464 ms |
| Shadow plan mean | 0.5085 ms | 0.3145 ms |
| Renderer CPU p99 | 5.6328 ms | 4.4998 ms |
| Frame wall p99 | 21.1467 ms | 19.5231 ms |
| Bounds entries invalidated on asset reload | 6,629 | 540 |
| Shadow templates invalidated on asset reload | 19,887 | 902 |
| Asset invalidation call | 1.2671 ms | 1.4629 ms |

The invalidation call itself did **not** improve. It preserves substantially more
cache state, but still scans the scene caches. The single reload observations
also have different focus states; they are not a reload latency distribution.
The renderer on those reload frames took 6.8306 and 6.1156 ms respectively, which
is descriptive evidence rather than a controlled reload speedup claim.

The context-matched comparison exposes a remaining regression:

| Matched context | Frames before/after | Renderer mean before/after |
| --- | ---: | ---: |
| Focused moving gameplay | 240 / 240 | 2.8490 / 3.2309 ms |
| Focused editor idle | 8 / 240 | 2.9570 / 2.3620 ms |
| Unfocused scene-reload phase | 540 / 233 | 2.7649 / 2.2807 ms |

Moving gameplay CPU cycles also increased from 6.19M to 7.09M per frame; its
shadow planning increased from 0.5242 to 0.6730 ms. This is not explained away as
wall-clock scheduling noise. The editor sample has only eight matching baseline
frames and the scene-reload groups cover different frame subsets. Consequently,
the lower whole-capture mean is **not** an across-the-board renderer improvement.
The remaining moving-gameplay cost and selective invalidation scan need further
controlled profiling before claiming those paths are faster.

All three cascade plans reused on 1,117 measured frames. No draw-scratch capacity
growth occurred during measured frames (startup had already warmed the buffer).
Final texture residency stayed at 8,347,136 bytes in both captures. Final staging
pool, retired GPU bytes and pending draws were zero; final peak pooled staging
was 2,752,512 bytes, below the existing 4 MiB cap. The unchanged scene reload reused
all 50 models and transferred zero bytes from frame 900 onward. FIFO frame mean
remained about 16.7 ms; no FPS gain is claimed.

## Focused verification

- `engine_cache_updates`: selective invalidation and shared binding survival;
  shadow dependency changes and bounded retry; shared CPU/GPU accounting,
  replacement pressure and mutable RGBA capacity; draw scratch ownership/cap.
- `engine_streaming_memory`: invalidation, deferred retirement and memory reuse.
- `engine_texture_residency`: texture transitions and shared-allocation limits.
- `engine_draw_order`: opaque/blended ordering parity.
- `engine_shadow_submission`: image parity across batching/LOD, moving/scaled,
  skinned, procedural, masked and invalidated geometry; stable cascade reuse.
- `engine_presentation` and `engine_presentation_pacing`: the opaque worker
  boundary preserves presentation behavior and lifecycle.

All seven passed when their paths changed. Subsequent iterations repeated only
cache/shadow checks, plus streaming-memory checks for the retirement change.
Build and check logs are `build/reload-*-build.log` and
`build/reload-*-checks.log`. No full engine or game suite was run.

Capture command, from the engine root:

```powershell
python tools/profile/capture.py --exe '../Games 2/build/bin/robots/Release/robots.exe' --scene '../Games 2/games/robots/build/robots-village.one' --cwd '../Games 2' --output build/reload-invalidation-verified.csv --frames 1440
```

Use a new output name for subsequent captures. Metadata records source/executable
identity and freshness; `tools/profile/compare.py` exposes incompatible contexts.
