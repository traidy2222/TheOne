# Gameplay and reload cache correction

This follows the regressions recorded in `RELOAD_CACHE_PERFORMANCE.md`. Changes
are limited to geometry cache lifetime/invalidation, shadow command preparation,
and the focused profiling replay. Texture budgets, aliasing and staging limits
are unchanged. Concurrent editor/history changes were preserved.

## Profiling and decisions

The first four 1080p FIFO gameplay captures used one instrumented executable:
`cache-correction-before`, `cache-correction-plan-off-1`,
`cache-correction-plan-off-2`, and `cache-correction-plan-on-2` under `build/`.
Every gameplay frame was focused. Switching plan reuse off produced overlapping
results: renderer CPU varied from 3.94 to 6.06 ms across runs. That variation is
too large to attribute the historical 0.38 ms regression to dependency checks.
Checks averaged only 0.014-0.020 ms in the enabled runs; command rebuilds dominated.

An alternating snapshot stress probe confirmed that recording a complete
dependency snapshot adds substantial cost on its recording frames. This probe
deliberately prevents reuse, so it records more keys than the normal retry policy
and must not be interpreted as the normal average cache overhead. Snapshot keys
now omit bindings/matrices for entries that cannot emit a command in that cascade.
Candidate/visibility transitions still invalidate plans before submission.

The rebuild loop also fetched immutable LOD counts from MeshGPU vectors even
though those counts already exist in its command templates. It now uses the
templates directly. The diagnostic `THEONE_REFERENCE_SHADOW_BINDINGS` switch
retains the old reads for a same-executable comparison. Alternating the paths
within a run, then reversing their odd/even frame assignment, gave:

| Focused moving gameplay | Old reads | Template counts | Reduction |
| --- | ---: | ---: | ---: |
| Shadow planning, first alternating run | 0.657 ms | 0.466 ms | 0.191 ms |
| Shadow planning, reversed assignment | 0.890 ms | 0.667 ms | 0.223 ms |
| Shadow stage cycles, first run | 1.673M | 1.302M | 22.2% |
| Shadow stage cycles, reversed assignment | 2.270M | 1.829M | 19.4% |

Each row has 120 frames per path. Excluding the first 60 frames gave planning
savings of 0.202 and 0.212 ms, with the same mean number of recorded keys per path.
Whole-renderer means fell by 0.166 and 0.107 ms in those comparisons. The focused
shadow-image check passes with both paths. This supports a specific CPU reduction;
it does not prove the entire historical regression is recovered on every run.

Artifacts: `cache-correction-binding-alternating.csv` and
`cache-correction-binding-reversed.csv`, with their metadata/logs. `alternate`
selects old reads on odd renderer frames; `alternate-even` reverses that mapping.

## Invalidation without scene-wide cache walks

`geometry_lifetime.hpp` provides one small residency token per resident model.
References and per-draw shadow templates keep the token alive, but it owns no
Model or asset payload. Removing the model flips the token before extracting
mesh/binding nodes. Cached raw pointers cannot be dereferenced after that flip.
Re-admission gets a fresh token; it cannot revive an old reference.

Bounds and transparent-depth snapshots compare a monotonically increasing mesh
residency generation, so reusing the same primitive address cannot reuse stale
results. Their entries are refreshed during ordinary use, without a clearing
walk at reload. Shadow plans are invalidated immediately. The model's existing
primitive lists provide direct keys for removing preparation, geometry and
shared shadow-template entries. GPU allocation user counts replace the former
full mesh-map scan for live buffers. A partial model removal keeps any shared
allocation alive until its last mesh is removed.

An intermediate end-to-end trace exposed an extra per-frame MeshGPU read while
checking whether a reference already held the right lifetime token: geometry
preparation rose from 0.048 ms to 0.165-0.198 ms. Token ownership is now assigned
only when the mesh reference is rebound. Stable frames avoid that random metadata
read; invalidated references still reset before resolving the replacement.

`THEONE_REFERENCE_INVALIDATION_SCAN=1` enables an eager clearing fallback for
measurement. Both paths share the same new allocation accounting. Three reloads
per run, at replay steps 720/780/840, produced:

| Asset invalidation call | Eager scan fallback | Default lifetime/generation path |
| --- | ---: | ---: |
| Reload 1 | 1.3577 ms | 0.4741 ms |
| Reload 2 | 0.6036 ms | 0.4693 ms |
| Reload 3 | 1.1534 ms | 0.3074 ms |
| Median | 1.1534 ms | 0.4693 ms |

All six events were unfocused. The fallback examined 39,773 cache entries per
reload; the default examined none of those arrays and visited only the removed
model's 540 primitives. The median call fell about 59%. Three events per path
are a small sample, not a p99 latency claim. Material/texture dependency scans
and allocation/group cleanup remain; this is not a claim that all invalidation
work is constant time. The earlier original-path capture measured 1.2576 ms,
including 0.5162 ms in cache clearing and 0.5749 ms in geometry removal.

Artifacts: `cache-correction-reloads.csv` and
`cache-correction-reloads-scan.csv`. `THEONE_PROFILE_RELOAD_REPEAT=1` selects the
three-reload replay and its six expected collision requests. The capture tool
records this flag in metadata. Normal replay retains one reload/four requests.
Replay camera dt is fixed at 1/60, matching its existing fixed simulation step;
normal interactive timing is unchanged.

## Correctness, memory and limits

Focused checks passed:

- `engine_cache_updates`: retired references, re-admission, same-address bounds
  generations, unrelated binding survival, shadow key/culling transitions and
  existing accounting/scratch checks.
- `engine_streaming_memory`: repeated/in-flight removal, bounded retirement and
  partial removal from a shared geometry allocation while survivors still draw.
- `engine_shadow_submission`: shadow/image parity, moving/scaled/skinned/masked
  geometry, explicit invalidation and reset. Also passed with both diagnostic
  reference paths enabled.

Only cache/shadow checks were repeated after the shadow-only change, and only
streaming-memory/shadow checks after the final reference-binding change. No full
engine or game suite was run. Build/check evidence is in
`build/cache-correction-*.log`.

The three-reload capture finished at 8,347,136 texture bytes, zero pooled staging,
zero retired GPU bytes and zero pending draws. The unchanged scene reload reused
all 50 models. Tokens add small CPU metadata and per-entry references, but do not
retain removed model/image/mesh payloads. Total process-memory reduction is not
claimed. Presentation/FIFO behavior was not changed.

## Final build evidence

`cache-correction-verified.csv` captured all 1,440 default-replay frames with
unchanged source/executable identities and no source newer than the binary.
It retained 8,347,136 texture bytes, ended with zero staging/retired/pending work,
peaked at 4,087,808 pooled staging bytes (below 4 MiB), and reused 50 models with
zero transfers after the unchanged scene reload. Its invalidation call was
0.7256 ms, visiting 540 primitives and scanning zero cache-array entries.

That full replay was unfocused throughout, whereas this pass's baseline was
focused throughout. The comparison correctly reports zero matching contexts.
Its renderer mean was 6.8724 ms with substantial timing variation; this cannot
be used to claim an overall gain or regression against the focused baseline.
See `cache-correction-verified-comparison.json` for the rejected comparison.

A final 240-frame same-run alternating check on that exact build,
`cache-correction-verified-paired.csv`, was focused for all frames:

| Final build, 120 frames per path | Old reads | Template counts |
| --- | ---: | ---: |
| Renderer CPU mean | 4.0544 ms | 3.8313 ms |
| Shadow planning mean | 0.8646 ms | 0.5736 ms |
| Shadow stage cycles | 2.187M | 1.622M |
| Geometry preparation mean | 0.0538 ms | 0.0552 ms |
| Draw copies including geometry preparation | 0.1903 ms | 0.1895 ms |

This confirms a 0.291 ms shadow-planning reduction and 0.223 ms whole-renderer
reduction in the paired sample. Geometry preparation is back near the original
instrumented baseline's 0.048 ms after moving lifetime-token reads to rebinding.
The paired capture also has stable source/executable provenance. The complete
historical 0.38 ms regression is not claimed recovered: these are specific,
controlled improvements, and longer matched full-renderer captures remain needed
for that broader conclusion.
