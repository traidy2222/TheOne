# CPU spikes, input timing, surface transitions and first-view readiness

Measured locally on 2026-11-02 with Robots at 1920x1080 on the RTX 3080.
FIFO, two frame slots, texture detail policy and memory/upload/retirement limits
remain in place. This iteration implements all five accepted workstreams.

## Profiling evidence

The instrumented baseline's first camera change spent **21.618 ms in material
preparation**, within a 29.054 ms renderer CPU frame. That frame used 52.6 million
render-thread cycles versus a 12.2 million median over the capture; the material
scope alone accounted for 36.4 million. This was a work-heavy spike, not evidence
of a long GPU execution. Material setup repeatedly scanned texture maps for shared
allocations and could create many descriptors/uniform buffers in one frame.

Optional `THEONE_PROFILE_CPU=1` records wall time and raw thread cycles for the
renderer and its preparation/recording stages. Nested scopes overlap and must not
be summed as independent durations. `GetThreadTimes` was quantized to 15.625 ms on
this machine and frequently returned zero for short scopes; those samples do not
prove zero CPU work. Raw thread cycles are not converted to milliseconds, per
[Microsoft's QueryThreadCycleTime guidance](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).

The baseline sampled input before waiting for FIFO presentation/resource reuse.
Average sample-to-submission age was 15.54 ms over the full replay. This is a CPU
input-age proxy, not physical click-to-display latency. The profile disables
incidental gameplay input and follows deterministic motion; SDL performance
counters use QPC in this checked-out Windows implementation, matching the
calibrated submission clock.

The focused surface fixture also found a **30.58 ms same-size recreation** and
32.56–42.41 ms actual size changes. Target/pipeline/UI setup took about 10–12 ms;
device-idle waits were only about 0.03–0.18 ms. That directed the work toward
avoiding rebuilds and retaining compatible render state, rather than adding
asynchronous swapchain lifetime complexity.

## Implemented changes

1. **Material CPU work:** `texture_alias_index.*` indexes prepared image identity,
   mip and color space. Values are source keys, not owning image/resource copies.
   Lookups validate representatives against resident/replacement maps and repair
   stale representatives; invalidation/reset clears the index. `material_admission.hpp`
   gives new material work a cooperative 1 ms frame budget, shared across all
   preparation attempts, so later draw-list retries cannot bypass it. One operation
   can exceed the budget; this is not a preemptive hard deadline.
2. **Input boundary:** `Renderer::waitForFrame()` performs resource/presentation
   waits without starting an ImGui frame. The consumer's `frame_boundary.*` calls
   it before normal event dispatch and input sampling, then refreshes SDL input.
   `beginFrame()` remains compatible and idempotent after that wait. Minimized
   windows avoid the presentation wait and still get event processing.
3. **Surface transitions:** size notifications are distinct from mandatory
   out-of-date/present-mode recreation. Same-size notifications and restores are
   no-ops. `render_surface.cpp` owns extent-dependent attachments/framebuffers;
   `render_targets.cpp` retains compatible render passes, pipelines and UI/font
   resources. Format/image-count/minimum-count changes still rebuild incompatible
   state. Real recreation retains the short device-idle wait for safe destruction.
4. **First-use readiness:** indexed sharing and bounded creation address the
   measured first-camera bottleneck. `material_warmup.cpp` also warms hidden
   material descriptors only when all required textures are already resident and
   ready. It visits at most 64 candidates, creates at most four materials, and
   stops after approximately 0.25 ms, within the shared admission budget. It does
   not start hidden image uploads, decode jobs or detail promotions. Controller/
   animation selection was small (baseline gameplay p99 0.033 ms); scene/pose
   preparation p99 was 0.782 ms, so neither received speculative changes.
5. **Comparable evidence:** `tools/profile/` provides capture identities and
   context-separated comparison. Sidecars include executable/scene/shader hashes,
   relevant source identities before/after, source freshness, flags, GPU identity,
   frame counts and focus/resolution/presentation contexts. Optional PresentMon
   capture uses only the launched PID and a uniquely owned ETW session. The
   comparison tool reports drift/incomplete captures and refuses to pool unmatched
   focus or surface contexts. A 240-frame first-camera probe avoids unrelated
   editor/reload work when isolating input/readiness changes.

## Measurements

The first-view comparison uses the first 240 gameplay frames of the focused
baseline versus the focused 240-frame probe. Both use the same scene, resolution,
FIFO mode and scripted motion. Their executable identities differ as expected.
The comparison reports the different capture command options (full replay versus
probe); these numbers are specifically about their matching gameplay phase.

| First-camera metric | Baseline gameplay phase | New gameplay probe |
| --- | ---: | ---: |
| Maximum material preparation | 21.618 ms | 1.186 ms |
| Maximum renderer CPU | 29.054 ms | 6.827 ms |
| Renderer CPU p99 | 11.488 ms | 5.720 ms |
| First frame with no pending draws | 45 | 22 |
| Elapsed until no pending draws | 786 ms | 383 ms |

Admission spreads work across frames: the first changed-camera frame initially
deferred **1,205 draws versus 375** in the baseline. It then reached full readiness
sooner. This is a visible loading tradeoff, not identical first-frame output or
instant readiness for arbitrary unseen assets. The hidden-material fixture
separately proves a warmed material can become visible with zero new material
creation, zero pending draws and identical captured pixels, without increasing
texture bytes. Seven hidden materials warmed during the final full replay.

An isolated input comparison uses the **same executable**, scene and focused
240-frame probe, toggling only `THEONE_REFERENCE_INPUT_BOUNDARY=1`:

| Metric | Wait after sampling (reference) | Wait before polling (default) |
| --- | ---: | ---: |
| Mean sample-to-submission age | 15.689 ms | 3.484 ms |
| p99 sample-to-submission age | 18.078 ms | 6.275 ms |
| Mean frame wall time | 16.684 ms | 16.676 ms |

The final focused surface check measured actual size-change rebuilds at **8.48–8.87
ms**, with attachment setup at 1.03–1.19 ms. Same-size resize/restore retained the
same swapchain generation. Initial device/window setup remains separate and much
more expensive; these are not startup-time claims.

## Presentation anomaly and comparison limits

The first full after-capture (`cpu-input-after.csv`) averaged **46.59 ms/frame**,
including 42.70 ms of presentation waiting. It had no pacing timeouts and is
retained as an unresolved anomalous capture. Short probes with the default,
reference input boundary and reference surface recreation all returned to about
16.7 ms/frame. The repeated full capture (`cpu-input-final.csv`) averaged
**16.69 ms/frame**, with 0.097 ms maximum resource-fence wait. Neither isolated
change reproduced the stall; there is insufficient evidence to assign its cause
or claim it is fixed.

The baseline full run was focused throughout; the final full run was unfocused
throughout. The comparison tool correctly emitted **zero matching whole-run
contexts**. The latter's 32.66 ms frame p99 and 43.39 ms maximum must not be
compared directly with the baseline's focused frame distribution. Its PresentMon
events were mostly Composed Flip, with partial display-completion coverage. The
focused input probes above provide the controlled input-boundary comparison.
There is no claim of a universal whole-engine FPS gain or physical input latency.

## Memory and focused verification

All three full captures ended with exactly **8,347,136 texture bytes**, zero
available staging bytes and zero retired GPU bytes. Their peak texture bytes were
8,819,200. Available staging stayed within the existing 4 MiB cap. Each reload
reused all 50 models with zero replacement models, zero GPU transfers after the
reload trigger, and no pending draws at completion. The alias index retains no
payload ownership; warmup is deliberately limited to existing resident textures.

The following focused GPU checks passed with Vulkan/synchronization validation:

- `engine_presentation`: early/repeated wait API, FIFO/uncapped, one/two slots,
  same-size no-op, real resize, minimize/restore, reset, image parity and shutdown.
  The final capture checksum remains `13028279735593214689`.
- `engine_streaming_memory`: texture sharing, edited-image splitting, invalidation,
  replacement/retirement, staging limits/trim, plus the resident-only warmup case.
- `engine_texture_residency`: coarse/distance/pressure transitions and in-flight
  descriptor refresh/invalidation.

The three checks took 38.39 s together. Presentation was rechecked against the
final diagnostic build (3.09 s); unchanged residency/streaming checks were not
repeated. The existing unused vertex-input performance warning remained; no
validation errors occurred. Three small Python checks passed for context
separation, malformed/incomplete traces, and build drift. No complete engine or
game suite ran. Other workspace work, including `editor_history.cpp`, was preserved.

## Reproduction and artifacts

Capture/compare commands and reference controls are in
[the profiling tool guide](../tools/profile/README.md). Focused GPU command:

```powershell
ctest --test-dir '../Games 2/build' -C Release --output-on-failure -R '^(engine_presentation|engine_streaming_memory|engine_texture_residency)$'
```

Artifacts in the engine build directory:

- `cpu-input-before.csv`, `cpu-input-after.csv`, `cpu-input-final.csv` and their
  identity/log sidecars; the final capture also has `.pm.csv`/`.pm.log`.
- `probe-default.csv`, `probe-late-input.csv`, `probe-rebuild-surface.csv` with
  sidecars for the isolated 240-frame comparisons.
- `input-boundary-comparison.json`, `first-view-comparison.json`,
  `cpu-input-final-comparison.json`, `cpu-input-result-summary.json`.
- `transitions-before.log`, `cpu-input-focused-checks.log`, and
  `cpu-input-presentation-final.log`.
