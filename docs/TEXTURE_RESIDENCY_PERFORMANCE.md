# Texture residency CPU follow-up

> The [renderer CPU follow-up](RENDERER_CPU_PERFORMANCE.md) profiles the remaining full-frame work and optimizes geometry accounting, draw ordering and LOD preparation.

2026-11-01, Windows Release, RTX 3080 / i7-12700F. The existing Robots gameplay
profile was used with its 128-frame camera path, 640x480 viewport, main thread
pinned to logical CPU 0, and FIFO presentation. A saved instrumented executable
preserves the implementation before optimization for adjacent comparisons.

## Findings and changes

The first breakdown measured 0.123 ms/frame for demand gathering, 0.078 ms for
transition planning, and 0.031 ms for material creation. The original 0.49 ms
estimate compared total renderer preparation/recording with multiple features
toggled; it was not an isolated texture-residency measurement.

- `vulkan/texture_demand.cpp` owns demand gathering. Material cache entries retain
  stable references to texture cache nodes. Per-update epochs replace temporary
  demand and material maps, with the same maximum projected-size/minimum-mip
  aggregation. One-level textures skip detail work. The epoch advances for every
  update, including attempts that do not submit a frame.
- `vulkan/material_residency.cpp` preserves material uniform buffers on a mip
  transition. Both frame slots become dirty. On first use of a slot after its
  fence, only that slot's descriptor set is replaced: stable uniform/shadow
  bindings are copied and current texture views are written. The old slot set
  is then freed. Other slots remain untouched until their fences complete.
- `vulkan/texture_residency.cpp` retains adoption, pressure and transition policy.
  Content edits/reloads still invalidate dependent materials before erasing
  texture cache nodes; mip replacement preserves node addresses. Existing texture
  retirement protects in-flight GPU use.

No mip threshold, eight-frame stability rule, upload budget, memory budget,
visibility policy or texture format was relaxed. Full-texture mode skips demand
calculation altogether. Frame statistics now separate demand, planning,
adoption and descriptor refresh; the profile also reports material creation.

## Measured result

Adjacent before/final runs under the later session conditions:

| Mean CPU stage | Before | Final |
| --- | ---: | ---: |
| Demand gathering | 0.1471 ms | 0.0821 ms |
| Residency total | 0.2780 ms | 0.2195 ms |
| Material creation | 0.0394 ms | 0.0031 ms |
| Descriptor refresh, outside residency total | included in material creation | 0.0027 ms |
| Combined residency + material work | **0.3173 ms** | **0.2253 ms** |
| GPU texture allocation | **5,524,480 B** | **5,524,480 B** |

The isolated work fell by **0.092 ms/frame (29%)**. Remaining material creation
comes from newly visible materials; the focused fixture verifies zero material
uniform recreation during mip-only changes. Main/shadow counts remained about
1,878/9,639, with at most one pending draw in either measured run. Texture memory
is unchanged, retaining the earlier 80.8% reduction from 28,760,064 bytes.

Total renderer preparation plus recording averaged 5.173 ms before and 5.179 ms
in the final run. Those means do **not** establish an end-to-end CPU gain, nor
that the entire historical 0.49 ms delta is removed. Presentation/fence timings
also changed substantially from the previous session. Earlier intermediate
variants reduced the isolated work further in their samples, but they are not
reported as the final implementation's result. No FPS/GPU speedup is claimed.

## Focused checks

Only `engine_texture_residency` and `engine_streaming_memory` ran. Both passed
with Khronos synchronization validation. The residency-only mode omits the
unrelated instancing portion of its executable and covers:

- Coarse-first loading, near-detail image parity, distance/off-screen demotion,
  pressure behavior, and identical-image sharing.
- Both descriptor frame slots, including off-screen retirement followed by a
  material returning to view, checked against the full-detail captured image.
- No material uniform recreation during mip transitions.
- Image revision/in-flight invalidation, cleanup, and existing upload/sharing
  behavior exercised by the focused streaming-memory check.

Fixture allocations remained 48,640 B coarse, 11,189,760 B near, 7,680 B distant,
and 2,560 B under its intentionally unattainable one-byte budget. Robots,
Sandbox and Planets were rebuilt for integration; no whole-game suites ran.

```powershell
cmake --build build --config Release --target theone_runtime_features theone_streaming_memory --parallel 8
ctest --test-dir build -C Release -R '^engine_(texture_residency|streaming_memory)$' --output-on-failure
```

Evidence: engine `build/residency-focused-tests.log`,
`build/residency-descriptor-tests.log`, and `build/residency-final-build.log`;
consumer `build/residency-before.log`, `build/residency-before-repeat.log`,
`build/residency-final.log`, `build/residency-reference.log`, and
`build/residency-consumers-build.log`. The temporary descriptor-recreation
profiling switch was removed after the comparison.
